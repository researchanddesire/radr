#include "device.h"

#include "utils/psramTask.h"

#include <services/buzzer.h>

#include "pages/genericPages.h"
#include "services/leds.h"
#include "state/remote.h"

Device *device = nullptr;

portMUX_TYPE Device::lifecycleLock = portMUX_INITIALIZER_UNLOCKED;

namespace {
// The connection procedure needs about 4 KB; the rest is headroom for the
// device's onConnect() and the state machine work it triggers.
constexpr uint32_t kConnectionStackBytes = 10 * configMINIMAL_STACK_SIZE;
constexpr uint32_t kInternalStackWaitMs = 4000;
}  // namespace

Device::Device(const NimBLEAdvertisedDevice *advertisedDevice)
    : advertised(advertisedDevice != nullptr ? *advertisedDevice
                                             : NimBLEAdvertisedDevice()),
      notifyGate(std::make_shared<NotifyGate>()) {}

Device::~Device() {
    // Notification callbacks stay registered on NimBLE's client after this
    // device is gone; the gate makes them no-ops from here on.
    notifyGate->close();

    displayObjects.clear();
    characteristics.clear();
    menu.clear();

    // Clear the client callbacks so that we don't call "this->onDisconnect()"
    if (pClient) {
        pClient->setClientCallbacks(nullptr, false);
        pClient->disconnect();
        pClient = nullptr;
    }

    ESP_LOGD(TAG, "Device destructor completed");
}

bool Device::startConnection() {
    // OSSM-style devices never touch flash while connecting, so their task
    // can live in PSRAM. Internal RAM is nearly full once Wi-Fi and BLE run
    // and fragments over connect/disconnect cycles; the internal stack is
    // kept only for devices that read LittleFS here (Buttplug protocol
    // files), which a PSRAM stack cannot do.
    connectionUsesPsramStack = connectsWithoutFlash() && psramFound();
    portENTER_CRITICAL(&lifecycleLock);
    connectionTaskRunning = true;
    portEXIT_CRITICAL(&lifecycleLock);

    BaseType_t created;
    if (connectionUsesPsramStack) {
        created = createPsramTask(Device::connectionTask, "connectionTask",
                                  kConnectionStackBytes, this, 1,
                                  &connectionTaskHandle, 0);
        // createPsramTask falls back to an internal stack on its own.
        if (created == pdPASS) connectionUsesPsramStack = isPsramTask(connectionTaskHandle);
    } else {
        created = createInternalTaskWithWait(
            Device::connectionTask, "connectionTask", kConnectionStackBytes,
            this, 1, &connectionTaskHandle, 0, kInternalStackWaitMs);
    }
    if (created != pdPASS) {
        portENTER_CRITICAL(&lifecycleLock);
        connectionTaskRunning = false;
        portEXIT_CRITICAL(&lifecycleLock);
        connectionTaskHandle = nullptr;
        updateStatusText("Out of memory: cannot connect.");
        return false;
    }
    return true;
}

void Device::releaseClient() {
    NimBLEClient *client = pClient;
    if (client == nullptr) return;
    client->setClientCallbacks(nullptr, false);
    if (client->isConnected()) {
        client->disconnect();
    } else {
        client->cancelConnect();
    }
}

void Device::destroy(Device *target) {
    if (target == nullptr) return;
    target->aborted = true;

    portENTER_CRITICAL(&lifecycleLock);
    const bool running = target->connectionTaskRunning;
    if (running) target->deleteWhenConnectionEnds = true;
    portEXIT_CRITICAL(&lifecycleLock);

    if (!running) target->onTeardown();
    // Stop the link: a connection task blocked in a BLE read or in connect()
    // returns as soon as it is gone.
    target->releaseClient();
    if (running) {
        ESP_LOGW(TAG, "Connection task still running; it deletes the device");
        return;
    }
    target->pClient = nullptr;
    deleteLater(target);
}

namespace {
// Other tasks (RAD BLE requests, input handlers) read the global `device`
// and may be inside one of its methods when it is cleared. They finish
// within this; the link is already down, so their BLE calls fail fast.
constexpr uint32_t kDeleteGraceMs = 1500;

void deleteDeviceTask(void *param) {
    vTaskDelay(pdMS_TO_TICKS(kDeleteGraceMs));
    delete static_cast<Device *>(param);
    exitPsramTask();
}
}  // namespace

void Device::deleteLater(Device *target) {
    // No flash access in a device destructor, so a PSRAM stack is fine.
    if (createPsramTask(deleteDeviceTask, "deviceDelete", 4096, target, 1,
                        nullptr, tskNO_AFFINITY) != pdPASS) {
        delete target;
    }
}

std::atomic<int> Device::runningConnectionTasks{0};

bool Device::waitForConnectionTasks(uint32_t timeoutMs) {
    const TickType_t start = xTaskGetTickCount();
    while (runningConnectionTasks.load() > 0) {
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeoutMs))
            return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

void Device::connectionTask(void *pvParameter) {
    Device *self = static_cast<Device *>(pvParameter);
    runningConnectionTasks++;
    const bool usesPsramStack = self->connectionUsesPsramStack;

    const bool ready = self->connectSteps();
    if (!self->aborted && stateMachine) {
        // The guard on connected_event drops it if this device is no longer
        // the current one by the time the state machine sees it.
        if (ready) {
            connected_event event;
            event.source = self;
            stateMachine->process_event(event);
        } else {
            stateMachine->process_event(connected_error_event());
        }
    }
    ESP_LOGD(TAG, "Connection task done (stack headroom %u bytes)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));

    portENTER_CRITICAL(&lifecycleLock);
    self->connectionTaskRunning = false;
    self->connectionTaskHandle = nullptr;
    const bool deleteSelf = self->deleteWhenConnectionEnds;
    portEXIT_CRITICAL(&lifecycleLock);
    if (deleteSelf) {
        // destroy() ran while this task was busy; the link may have come up
        // after it asked for a disconnect.
        self->releaseClient();
        self->pClient = nullptr;
        deleteLater(self);
    }
    runningConnectionTasks--;
    if (usesPsramStack) exitPsramTask();
    vTaskDelete(NULL);
}

bool Device::connectSteps() {
    Device *device = this;  // shadows the global on purpose
    const NimBLEAdvertisedDevice *advDevice = advertisedDevice;
    const NimBLEAddress address = advDevice->getAddress();

    updateStatusText("Initializing connection...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (aborted) return false;

    NimBLEClient *client = nullptr;
    for (int attempt = 0; attempt < 2 && client == nullptr; attempt++) {
        if (aborted) return false;
        /** Check if we have a client we should reuse first **/
        if (NimBLEDevice::getCreatedClientCount()) {
            updateStatusText("Checking existing connections...");
            /**
             *  Special case when we already know this device, we send false as
             * the second argument in connect() to prevent refreshing the
             * service database. This saves considerable time and power.
             */
            NimBLEClient *known = NimBLEDevice::getClientByPeerAddress(address);
            if (known) {
                // Re-register callbacks — the previous device cleared them.
                known->setClientCallbacks(device, false);
                pClient = known;
                updateStatusText("Attempting fast reconnect...");
                if (!known->connect(address, false)) {
                    pClient = nullptr;
                    known->setClientCallbacks(nullptr, false);
                    if (aborted) return false;
                    updateStatusText("Reconnection failed, retrying...");
                    ESP_LOGE(TAG,
                             "Reconnect failed, deleting client and trying again.");
                    NimBLEDevice::deleteClient(known);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    continue;
                }
                ESP_LOGI(TAG, "Reconnected client");
                client = known;
                break;
            }
            /**
             *  Reusing disconnected clients for DIFFERENT devices can cause
             *  connection issues. Delete stale clients instead and create a
             *  fresh one for reliable connections.
             */
            NimBLEClient *stale = NimBLEDevice::getDisconnectedClient();
            if (stale) NimBLEDevice::deleteClient(stale);
        }

        /** No client to reuse? Create a new one. */
        updateStatusText("Creating new connection...");
        if (NimBLEDevice::getCreatedClientCount() >= NIMBLE_MAX_CONNECTIONS) {
            updateStatusText("Connection limit reached!");
            ESP_LOGE(TAG, "Max clients reached - no more connections available");
            return false;
        }

        NimBLEClient *created = NimBLEDevice::createClient();
        if (created == nullptr) {
            updateStatusText("Out of memory: cannot connect.");
            return false;
        }
        created->setClientCallbacks(device, false);
        /**
         *  Initial connection parameters: safe for 3 clients. Min/max
         * interval 12 * 1.25 ms = 15 ms, 0 latency, 150 * 10 ms = 1500 ms
         * supervision timeout.
         */
        created->setConnectionParams(12, 12, 0, 150);
        /** How long we are willing to wait for the connection (ms). */
        created->setConnectTimeout(5 * 1000);
        pClient = created;

        ESP_LOGI(TAG, "Connecting to peer (new client): %s",
                 address.toString().c_str());
        updateStatusText(String("Connecting to ") + address.toString().c_str() +
                         "...");
        // exchangeMTU stays false on purpose. NimBLE-Arduino 2.4.0's
        // client read callback copies OS_MBUF_PKTLEN bytes from the first
        // mbuf block only (NimBLERemoteValueAttribute.cpp onReadCB), so
        // with a negotiated MTU any single response larger than one
        // block (~250 B, e.g. the OSSM state JSON) comes back with a
        // garbage tail (measured 2026-09-11: every read truncated at
        // 279 B). At MTU 23 every response is a small blob and long
        // reads are reassembled correctly.
        if (!created->connect(address, true, false, false)) {
            pClient = nullptr;
            created->setClientCallbacks(nullptr, false);
            /** Created a client but failed to connect, don't need to keep
             * it as it has no data */
            NimBLEDevice::deleteClient(created);
            if (!aborted) updateStatusText("Connection failed, please try again.");
            ESP_LOGE(TAG, "Failed to connect, deleted client");
            return false;
        }
        client = created;
    }
    if (client == nullptr || aborted) return false;

    if (!client->isConnected()) {
        updateStatusText("Connection failed, please try again.");
        ESP_LOGE(TAG, "Client reports connected but has no link");
        return false;
    }

    ESP_LOGI(TAG, "Connected to: %s RSSI: %d",
             client->getPeerAddress().toString().c_str(), client->getRssi());
    updateStatusText("Connected! Setting up device...");

    pService = client->getService(getServiceUUID());
    if (pService == nullptr) {
        updateStatusText("Device service not found!");
        ESP_LOGE(TAG, "Service not found");
        return false;
    }

    // for each characteristic in the device characteristics vector, get the
    // characteristic and save it.
    updateStatusText("Discovering device capabilities...");
    for (auto &characteristic : characteristics) {
        if (aborted) return false;
        vTaskDelay(1);
        characteristic.second.pCharacteristic =
            pService->getCharacteristic(characteristic.second.uuid);
        auto *pChr = characteristic.second.pCharacteristic;
        if (pChr == nullptr) {
            ESP_LOGD(TAG, "Characteristic %s doesn't exist",
                     characteristic.first.c_str());
            continue;
        }

        if (characteristic.second.notifyCallback) {
            // The callback stays registered on NimBLE's client after this
            // device is deleted; the gate turns it into a no-op then. RAD
            // BLE replies are indications, so fall back to those.
            auto guarded = NotifyGate::wrap(notifyGate,
                                            characteristic.second.notifyCallback);
            if (pChr->canNotify()) {
                pChr->subscribe(true, guarded);
            } else if (pChr->canIndicate()) {
                pChr->subscribe(false, guarded);
            }
        }
    }
    if (aborted) return false;

    updateStatusText("Initializing device settings...");
    // run the user defined "on connect" method.
    onConnect();

    // The peer can drop the link while onConnect() runs (an OSSM reboot);
    // a device that lost it must not open the control screen.
    if (aborted || !client->isConnected()) {
        if (!aborted) updateStatusText("Connection lost, please try again.");
        return false;
    }

    ESP_LOGI(TAG, "Done with this device!");
    updateStatusText("Device ready! Loading interface...");
    return true;
}

void Device::onConnect(NimBLEClient *pClient) {
    ESP_LOGD(TAG, "Connected to %s", getName());
    playBuzzerPattern(BuzzerPattern::DEVICE_CONNECTED);
    setLed(LEDColors::connected, 255, 1500);
}

void Device::onDisconnect(NimBLEClient *pClient, int reason) {
    playBuzzerPattern(BuzzerPattern::DEVICE_DISCONNECTED);
    ESP_LOGD(TAG, "Disconnected from %s (reason %d)", getName(), reason);

    // Defer state machine event to the FreeRTOS timer task to avoid
    // use-after-free: the disconnect action deletes this device, so we
    // must not be inside a device method when that happens.
    xTimerPendFunctionCall(
        [](void *, uint32_t) {
            if (stateMachine) {
                stateMachine->process_event(disconnected_event());
            }
        },
        nullptr, 0, pdMS_TO_TICKS(10));
}

bool Device::send(const std::string &command, const std::string &value) {
    auto it = characteristics.find(command);
    if (it == characteristics.end()) {
        ESP_LOGW(TAG, "Characteristic '%s' not found for device '%s'",
                 command.c_str(), getName());
        return false;
    }

    auto *pChr = it->second.pCharacteristic;
    if (!pChr) {
        ESP_LOGW(TAG,
                 "Characteristic '%s' exists but pCharacteristic is nullptr "
                 "for device '%s'",
                 command.c_str(), getName());
        return false;
    }

    if (!pChr->canWrite()) {
        ESP_LOGW(TAG, "Characteristic '%s' for device '%s' is not writable",
                 command.c_str(), getName());
        return false;
    }

    ESP_LOGI(TAG, "Writing value '%s' to characteristic '%s' on device '%s'",
             value.c_str(), command.c_str(), getName());
    std::string encodedValue = value;
    if (it->second.encode) {
        encodedValue = it->second.encode(value);
    }
    return pChr->writeValue(encodedValue);
}

std::string Device::readString(const std::string &characteristicName) {
    auto it = characteristics.find(characteristicName);
    if (it == characteristics.end()) {
        ESP_LOGW(TAG, "Characteristic '%s' not found for device '%s'",
                 characteristicName.c_str(), getName());
        return std::string();
    }

    auto *pChr = it->second.pCharacteristic;
    if (!pChr) {
        ESP_LOGW(TAG,
                 "Characteristic '%s' exists but pCharacteristic is nullptr "
                 "for device '%s'",
                 characteristicName.c_str(), getName());
        return std::string();
    }

    if (!pChr->canRead()) {
        ESP_LOGW(TAG, "Characteristic '%s' for device '%s' is not readable",
                 characteristicName.c_str(), getName());
        return std::string();
    }

    ESP_LOGD(TAG, "Reading value from characteristic '%s' on device '%s'",
             characteristicName.c_str(), getName());
    NimBLEAttValue rawValue = pChr->readValue();

    ESP_LOGI(TAG, "Read value from characteristic '%s' on device '%s': %s",
             characteristicName.c_str(), getName(), rawValue.c_str());
    return rawValue.c_str();
}

int Device::readInt(const std::string &characteristicName, int defaultValue) {
    auto value = readString(characteristicName);
    char *endptr = nullptr;
    long result = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str() || *endptr != '\0') {
        // Parsing failed, fallback to defaultValue
        return defaultValue;
    }
    return static_cast<int>(result);
}

std::string Device::readJsonString(const std::string &command) {
    return readString(command);
}

void Device::drawDeviceMenu() {
    activeMenu = &menu;
    activeMenuCount = menu.size();

    drawMenu();
}
