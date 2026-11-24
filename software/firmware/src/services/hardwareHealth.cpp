#include "hardwareHealth.h"

#include <Arduino.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "hardwareHealthFormat.h"
#include "state/remote.h"

using namespace sml;

#ifndef FIRMWARE_BUILD_SHA
#define FIRMWARE_BUILD_SHA "unknown"
#endif
#ifndef FIRMWARE_TRACK
#define FIRMWARE_TRACK "development"
#endif

namespace {
uint32_t bootId = 0;
std::atomic<uint32_t> menuProgressMs{0};
uint32_t lastHeartbeatMs = 0;

uint64_t uptimeMs() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

// The console is UART0 in every build, the port the serial identity uses.
// One write per record, so other writers cannot split it.
void emit(const char* record, size_t size) {
    if (size != 0) Serial.write(reinterpret_cast<const uint8_t*>(record), size);
}
}  // namespace

void hardwareHealthBoot() {
    bootId = esp_random();
#if CONFIG_SPIRAM_MODE_OCT
    constexpr const char* variant = "r8";
#elif CONFIG_SPIRAM_MODE_QUAD
    constexpr const char* variant = "r2";
#else
    constexpr const char* variant = "unknown";
#endif
    char deviceId[13];
    hardwareHealth::formatDeviceId(ESP.getEfuseMac(), deviceId);
    // The running image's SHA-256, computed exactly as the Lockbox does.
    uint8_t digest[32] = {};
    const esp_partition_t* running = esp_ota_get_running_partition();
    const bool hashed =
        running != nullptr && esp_partition_get_sha256(running, digest) == ESP_OK;
    char imageSha256[65];
    hardwareHealth::formatSha256(hashed ? digest : nullptr, imageSha256);
    char record[hardwareHealth::RECORD_CAPACITY];
    emit(record, hardwareHealth::formatBoot(
                     record, sizeof(record),
                     {bootId, uptimeMs(), FIRMWARE_BUILD_SHA, FIRMWARE_TRACK,
                      variant, deviceId, imageSha256,
                      static_cast<int>(esp_reset_reason())}));
}

void hardwareHealthWifiConnected(uint32_t ipv4) {
    char record[160];
    emit(record, hardwareHealth::formatWifiConnected(record, sizeof(record),
                                                     bootId, uptimeMs(), ipv4));
}

void hardwareHealthMenuProgress() {
    menuProgressMs.store(millis(), std::memory_order_relaxed);
}

void hardwareHealthHeartbeat() {
    if (!hardwareHealth::heartbeatDue(millis(), lastHeartbeatMs)) return;
    // After boot the RADR rests on the device list; the main menu is the
    // other idle screen. Both draw loops report progress.
    const bool atIdleScreen = stateMachine != nullptr &&
                              (stateMachine->is("main_menu"_s) ||
                               stateMachine->is("device_list"_s));
    // Read progress before the clock: a draw loop on the other core can
    // advance it meanwhile, and the age must not wrap.
    const uint32_t menuMs = menuProgressMs.load(std::memory_order_relaxed);
    const uint32_t sampled = millis();
    const hardwareHealth::HeartbeatRecord heartbeat{
        bootId,
        uptimeMs(),
        atIdleScreen && menuMs != 0,
        sampled - menuMs,
        static_cast<uint32_t>(
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<uint32_t>(heap_caps_get_largest_free_block(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
    };
    char record[224];
    emit(record,
         hardwareHealth::formatHeartbeat(record, sizeof(record), heartbeat));
}
