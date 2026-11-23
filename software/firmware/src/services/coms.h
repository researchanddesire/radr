
#ifndef LOCKBOX_COMS_H
#define LOCKBOX_COMS_H

#include <ArduinoJson.h>
#include <NimBLEClient.h>
#include <NimBLEDevice.h>
#include <devices/device.h>
#include <devices/registry.h>
#include <structs/SettingPercents.h>
#include <vector>

#include "state/remote.h"

// One supported device from the last scan. It holds its own copy of the
// advertisement: the scanner frees its results when a scan starts, and the
// list is shown (and connected from) long after that.
struct DiscoveredDevice {
    NimBLEAdvertisedDevice advertisement;
    const DeviceFactory *factory = nullptr;
    std::string name;
    int rssi = 0;
};

void sendCommand(const String &command);

void initBLE();

// Device list management
// A copy: the scan callback appends to the list from the BLE host task.
std::vector<DiscoveredDevice> getDiscoveredDevices();
size_t getDiscoveredDeviceCount();
void clearDiscoveredDevices();
void connectToDiscoveredDevice(int index);
bool startScanWithTimeout(int timeoutMs, void (*onComplete)());

#endif
