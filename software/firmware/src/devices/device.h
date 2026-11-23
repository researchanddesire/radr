#ifndef DEVICE_H
#define DEVICE_H

#include <Arduino.h>

#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <components/DisplayObject.h>
#include <functional>
#include <memory>
#include <structs/Menus.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include "devices/serviceUUIDs.h"

static const char *TAG = "DEVICE";

// Forward declaration to avoid heavy include and ensure pointer type is known

class Device;

extern Device *device;

struct DeviceCharacteristics {
    NimBLEUUID uuid;
    NimBLERemoteCharacteristic *pCharacteristic = nullptr;

    // Function pointers for custom encode/decode
    std::function<std::string(const std::string &)> encode = nullptr;
    NimBLERemoteCharacteristic::notify_callback notifyCallback = nullptr;
};

// Lets a BLE notification callback run only while its device exists.
// NimBLE keeps the callback on its (reused) client after the device is
// deleted, so every device callback goes through one of these.
struct NotifyGate {
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    bool open = true;

    ~NotifyGate() {
        if (mutex != nullptr) vSemaphoreDelete(mutex);
    }

    // Waits for a running callback, then turns later ones into no-ops.
    void close() {
        if (mutex == nullptr) return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        open = false;
        xSemaphoreGive(mutex);
    }

    static NimBLERemoteCharacteristic::notify_callback wrap(
        const std::shared_ptr<NotifyGate> &gate,
        NimBLERemoteCharacteristic::notify_callback callback) {
        return [gate, callback](NimBLERemoteCharacteristic *characteristic,
                                uint8_t *data, size_t length, bool isNotify) {
            if (gate->mutex == nullptr ||
                xSemaphoreTake(gate->mutex, portMAX_DELAY) != pdTRUE)
                return;
            if (gate->open) callback(characteristic, data, length, isNotify);
            xSemaphoreGive(gate->mutex);
        };
    }
};

struct DeviceDisplayObject {
    std::string name;
    DisplayObject *displayObject;
};

class Device : public NimBLEClientCallbacks {
  public:
    // The advertisement this device was created from. The device keeps its
    // own copy: every new scan frees the scanner's results, and the
    // connection task and Buttplug devices read it long after the scan.
    const NimBLEAdvertisedDevice advertised;
    const NimBLEAdvertisedDevice *const advertisedDevice = &advertised;
    NimBLEClient *pClient = nullptr;
    NimBLERemoteService *pService = nullptr;

    bool isConnected = false;
    bool isPaused = false;
    bool isTested = false;

    std::vector<MenuItem> menu;
    std::vector<MenuItem> settingsMenu;

    std::unordered_map<std::string, DeviceCharacteristics> characteristics;
    std::vector<std::unique_ptr<DisplayObject>> displayObjects;

    explicit Device(const NimBLEAdvertisedDevice *advertisedDevice);

    // Delete a device with destroy(), never with delete: the connection task
    // may still be using it.
    virtual ~Device();

    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;

    // Starts connecting on the connection task. Call once, after the device
    // is fully constructed. False when the task could not be created.
    bool startConnection();

    // Stops the connection attempt and deletes the device: now, or when its
    // connection task finishes if one is still running (it may be blocked in
    // a BLE read). Safe to call from the connection task itself.
    static void destroy(Device *device);

    // True when connecting never touches flash (NVS, LittleFS), so the
    // connection task can run on a PSRAM stack. Devices that load files
    // while connecting keep the default internal-RAM stack.
    virtual bool connectsWithoutFlash() const { return false; }

    // Called once by destroy() before the link is dropped, while it may
    // still be up and no connection task runs: stop outputs here.
    virtual void onTeardown() {}

    // True once destroy() was called; connection steps stop early.
    bool connectionAborted() const { return aborted; }

    // Waits until no connection task is running (a destroyed device's task
    // can outlive it briefly). False on timeout. NimBLE must not be
    // deinitialised while one runs: it deletes the clients the task uses.
    static bool waitForConnectionTasks(uint32_t timeoutMs);

    // Virtual methods that child classes can optionally override
    virtual void onRightBumperClick() {}
    virtual void onLeftBumperClick() {}
    virtual void onRightButtonClick() {}
    virtual void onLeftButtonClick() {}
    virtual void onExit() {}
    virtual void onPause() {}
    virtual void onResume() {}
    virtual void onConnect() {}
    virtual void onDisconnect() {}
    virtual void onWiFiConnected() {}
    virtual void onDeviceMenuItemSelected(int index) {}
    virtual void onRestart() {}
    virtual void onUpdate() {}
    virtual void onPairing() {}
    virtual std::string readRawState() { return ""; }
    virtual void onMenuOpen() {}
    virtual void enterStrokeEngineMode() {}
    virtual void enterSimplePenetrationMode() {}
    virtual void enterStreamingMode() {}
    virtual bool isInSimplePenetrationMode() const { return false; }

    // The control screen's Menu button opens a menu only when this is true.
    // Pressing it otherwise calls onMenuBlocked() so the press is not silent.
    virtual bool canOpenSettings() const { return isPaused; }
    virtual void onMenuBlocked() {}

    // True for devices whose Menu button opens the machine settings pages
    // instead of the main menu.
    virtual bool hasMachineSettings() const { return false; }

    // Optional low-level control surface for a connected peripheral.  Device
    // implementations route writes through their existing command transport;
    // unsupported peripherals keep the default false result.
    virtual bool readControlSetting(const String &name, float &value) const {
        return false;
    }
    virtual bool writeControlSetting(const String &name, int value) {
        return false;
    }

    virtual void onRightEncoderChange(int value) {}
    virtual void onLeftEncoderChange(int value) {}

    // Override this method to enable persistent left encoder monitoring across
    // device states, such as while in a device_menu
    virtual bool needsPersistentLeftEncoderMonitoring() const { return false; }

    // Override this method to provide the current left encoder value for
    // devices with persistent encoder monitoring
    virtual int getCurrentLeftEncoderValue() const { return 0; }

    // Override this method to provide the left encoder parameter name for
    // devices with persistent encoder monitoring
    virtual const char *getLeftEncoderParameterName() const { return "Value"; }

    virtual void drawControls() {}
    virtual void drawDeviceMenu();

    virtual void pullValue() {}
    virtual void pushValue() {}

    virtual NimBLEUUID getServiceUUID() = 0;
    virtual const char *getName() = 0;

    // Clears every display object. Devices that keep raw pointers to display
    // objects drop them in onDisplayObjectsCleared() first.
    void clearDisplayObjects() {
        onDisplayObjectsCleared();
        displayObjects.clear();
    }
    virtual void onDisplayObjectsCleared() {}

    // Display object helpers
    template <typename TDisplayObject, typename... TArgs>
    TDisplayObject *draw(TArgs &&...args) {
        auto uniquePtr =
            std::make_unique<TDisplayObject>(std::forward<TArgs>(args)...);
        TDisplayObject *rawPtr = uniquePtr.get();
        displayObjects.emplace_back(std::move(uniquePtr));
        return rawPtr;
    }

  protected:
    static void connectionTask(void *pvParameter);
    // Runs the connection steps; false when the device must not report
    // itself connected.
    bool connectSteps();

    void onConnect(NimBLEClient *pClient) override;

    void onDisconnect(NimBLEClient *pClient, int reason) override;

    bool send(const std::string &command, const std::string &value);

    std::string readString(const std::string &characteristicName);

    int readInt(const std::string &characteristicName, int defaultValue);

    // Helper method to safely read JSON values
    template <typename T>
    T readJsonValue(const std::string &characteristicName, const char *key,
                    T defaultValue) {
        auto value = readString(characteristicName);
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, value.c_str());

        if (error) {
            ESP_LOGE("DEVICE", "JSON parse failed: %s", error.c_str());
            return defaultValue;
        }

        if (!doc.containsKey(key)) {
            ESP_LOGW("DEVICE", "JSON key '%s' not found", key);
            return defaultValue;
        }

        return doc[key].as<T>();
    }

    template <typename T = JsonObject>
    bool readJson(const std::string &command,
                  std::function<void(const T &)> callback) {
        auto value = readString(command);
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, value.c_str());

        if (error) {
            ESP_LOGE("DEVICE", "JSON parse failed: %s", error.c_str());
            return false;
        }

        callback(doc.as<T>());
        return true;
    }

    // Legacy method - now returns a copy of the JSON as string for safety
    std::string readJsonString(const std::string &command);

    std::shared_ptr<NotifyGate> notifyGate;

  private:
    static std::atomic<int> runningConnectionTasks;
    // Frees `target` after a short grace period (see destroy()).
    static void deleteLater(Device *target);
    // Drops the link and NimBLE's callbacks into this device.
    void releaseClient();
    // Guards connectionTaskRunning and deleteWhenConnectionEnds.
    static portMUX_TYPE lifecycleLock;
    TaskHandle_t connectionTaskHandle = nullptr;
    bool connectionTaskRunning = false;
    bool deleteWhenConnectionEnds = false;
    volatile bool aborted = false;
    bool connectionUsesPsramStack = false;
};

#endif  // DEVICE_H
