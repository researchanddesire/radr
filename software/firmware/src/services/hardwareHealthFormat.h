#pragma once

// Production health records for the hardware runner: one JSON object per line
// after the "RADR_HEALTH " prefix. Pure and allocation-free, so native tests
// cover every byte the release firmware prints.
#include <cstddef>
#include <cstdint>
#include <cstdio>

// A literal in every format string, so the image itself shows the hardware
// runner which records it prints (for example `RADR_HEALTH {"event":"boot"`).
#define HARDWARE_HEALTH_PREFIX "RADR_HEALTH "

namespace hardwareHealth {

constexpr uint32_t HEARTBEAT_INTERVAL_MS = 5000;
// Holds the longest record (boot, with a 64-character image hash) with room
// to spare. A record that does not fit is dropped, never truncated.
constexpr size_t RECORD_CAPACITY = 384;

// Values are compile-time build identity or generated hex. Refuse anything
// that would need JSON escaping rather than print a damaged record.
inline bool jsonSafe(const char* value) {
    if (value == nullptr) return false;
    for (; *value; ++value) {
        const unsigned char c = static_cast<unsigned char>(*value);
        if (c < 0x20 || c > 0x7e || c == '"' || c == '\\') return false;
    }
    return true;
}

inline size_t finish(int count, size_t capacity) {
    return count > 0 && static_cast<size_t>(count) < capacity
               ? static_cast<size_t>(count)
               : 0;
}

// The serial identity's device ID: the factory MAC from eFuse, first byte
// first, as 12 upper-case hex digits (the value RAD_ID prints).
inline void formatDeviceId(uint64_t efuseMac, char (&output)[13]) {
    snprintf(output, sizeof(output), "%02X%02X%02X%02X%02X%02X",
             unsigned(efuseMac & 255), unsigned((efuseMac >> 8) & 255),
             unsigned((efuseMac >> 16) & 255), unsigned((efuseMac >> 24) & 255),
             unsigned((efuseMac >> 32) & 255), unsigned((efuseMac >> 40) & 255));
}

// Lower-case hex of a SHA-256 digest; an absent digest prints as "".
inline void formatSha256(const uint8_t* digest, char (&output)[65]) {
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    output[0] = '\0';
    if (digest == nullptr) return;
    for (size_t i = 0; i < 32; ++i) {
        output[i * 2] = HEX_DIGITS[digest[i] >> 4];
        output[i * 2 + 1] = HEX_DIGITS[digest[i] & 0x0f];
    }
    output[64] = '\0';
}

// True when a heartbeat is due, and records it as sent.
inline bool heartbeatDue(uint32_t nowMs, uint32_t& lastHeartbeatMs) {
    if (nowMs - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) return false;
    lastHeartbeatMs = nowMs;
    return true;
}

struct BootRecord {
    uint32_t bootId;
    uint64_t uptimeMs;
    const char* buildSha;
    const char* track;
    const char* variant;
    const char* deviceId;
    const char* imageSha256;
    int resetReason;
};

// Each record starts with a newline so it never continues a partial line.
inline size_t formatBoot(char* output, size_t capacity, const BootRecord& boot) {
    if (!jsonSafe(boot.buildSha) || !jsonSafe(boot.track) ||
        !jsonSafe(boot.variant) || !jsonSafe(boot.deviceId) ||
        !jsonSafe(boot.imageSha256)) {
        return 0;
    }
    return finish(
        snprintf(output, capacity,
                 "\n" HARDWARE_HEALTH_PREFIX
                 "{\"event\":\"boot\",\"boot_id\":%lu,\"uptime_ms\":%llu,"
                 "\"build_sha\":\"%s\",\"track\":\"%s\",\"variant\":\"%s\","
                 "\"device_id\":\"%s\",\"image_sha256\":\"%s\","
                 "\"reset_reason\":%d}\n",
                 static_cast<unsigned long>(boot.bootId),
                 static_cast<unsigned long long>(boot.uptimeMs), boot.buildSha,
                 boot.track, boot.variant, boot.deviceId, boot.imageSha256,
                 boot.resetReason),
        capacity);
}

// ipv4 is in lwIP order: the first octet is the lowest byte.
inline size_t formatWifiConnected(char* output, size_t capacity, uint32_t bootId,
                                  uint64_t uptimeMs, uint32_t ipv4) {
    return finish(
        snprintf(output, capacity,
                 "\n" HARDWARE_HEALTH_PREFIX
                 "{\"event\":\"wifi_connected\",\"boot_id\":%lu,"
                 "\"uptime_ms\":%llu,\"ip\":\"%u.%u.%u.%u\"}\n",
                 static_cast<unsigned long>(bootId),
                 static_cast<unsigned long long>(uptimeMs), unsigned(ipv4 & 255),
                 unsigned((ipv4 >> 8) & 255), unsigned((ipv4 >> 16) & 255),
                 unsigned((ipv4 >> 24) & 255)),
        capacity);
}

struct HeartbeatRecord {
    uint32_t bootId;
    uint64_t uptimeMs;
    bool mainMenu;
    uint32_t menuAgeMs;
    uint32_t ramFree;     // internal RAM free bytes
    uint32_t ramLargest;  // largest free internal RAM block
};

inline size_t formatHeartbeat(char* output, size_t capacity,
                              const HeartbeatRecord& heartbeat) {
    return finish(
        snprintf(output, capacity,
                 "\n" HARDWARE_HEALTH_PREFIX
                 "{\"event\":\"heartbeat\",\"boot_id\":%lu,"
                 "\"uptime_ms\":%llu,\"main_menu\":%s,\"menu_age_ms\":%lu,"
                 "\"ram_free\":%lu,\"ram_largest\":%lu}\n",
                 static_cast<unsigned long>(heartbeat.bootId),
                 static_cast<unsigned long long>(heartbeat.uptimeMs),
                 heartbeat.mainMenu ? "true" : "false",
                 static_cast<unsigned long>(heartbeat.menuAgeMs),
                 static_cast<unsigned long>(heartbeat.ramFree),
                 static_cast<unsigned long>(heartbeat.ramLargest)),
        capacity);
}

}  // namespace hardwareHealth
