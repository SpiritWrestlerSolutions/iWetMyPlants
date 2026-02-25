/**
 * @file device_registry.cpp
 * @brief Paired device registry implementation
 */

#include "device_registry.h"
#include "logger.h"
#include <Preferences.h>

namespace iwmp {

static const char* TAG = "Registry";

void DeviceRegistry::begin() {
    _devices.clear();
    _devices.reserve(MAX_DEVICES);
}

bool DeviceRegistry::addDevice(const uint8_t* mac, uint8_t type, const char* name) {
    int idx = findDeviceIndex(mac);
    if (idx >= 0) {
        RegisteredDevice& dev = _devices[idx];
        dev.device_type = type;
        strncpy(dev.device_name, name, sizeof(dev.device_name) - 1);
        dev.online    = true;
        dev.last_seen = millis() / 1000;
        return true;
    }

    if (_devices.size() >= MAX_DEVICES) {
        LOG_W(TAG, "Device registry full (%d devices)", (int)MAX_DEVICES);
        return false;
    }

    RegisteredDevice dev = {};
    memcpy(dev.mac, mac, 6);
    dev.device_type = type;
    strncpy(dev.device_name, name, sizeof(dev.device_name) - 1);
    dev.paired    = false;
    dev.online    = true;
    dev.last_seen = millis() / 1000;

    _devices.push_back(dev);
    LOG_I(TAG, "Added device: %s", name);
    return true;
}

bool DeviceRegistry::removeDevice(const uint8_t* mac) {
    int idx = findDeviceIndex(mac);
    if (idx < 0) return false;
    _devices.erase(_devices.begin() + idx);
    return true;
}

RegisteredDevice* DeviceRegistry::getDevice(const uint8_t* mac) {
    int idx = findDeviceIndex(mac);
    if (idx < 0) return nullptr;
    return &_devices[idx];
}

void DeviceRegistry::updateLastSeen(const uint8_t* mac, int8_t rssi) {
    int idx = findDeviceIndex(mac);
    if (idx >= 0) {
        _devices[idx].last_seen = millis() / 1000;
        _devices[idx].last_rssi = rssi;
        _devices[idx].online    = true;
    }
}

void DeviceRegistry::updateReadings(const uint8_t* mac, uint8_t moisture,
                                     float temp, float humidity, uint8_t battery) {
    int idx = findDeviceIndex(mac);
    if (idx >= 0) {
        _devices[idx].last_moisture_percent = moisture;
        if (!isnan(temp))     _devices[idx].last_temperature   = temp;
        if (!isnan(humidity)) _devices[idx].last_humidity       = humidity;
        if (battery != 255)   _devices[idx].last_battery_percent = battery;
    }
}

void DeviceRegistry::forEachDevice(DeviceCallback callback) {
    for (auto& dev : _devices) {
        callback(dev);
    }
}

size_t DeviceRegistry::getOnlineDeviceCount() const {
    size_t count = 0;
    for (const auto& dev : _devices) {
        if (dev.online) count++;
    }
    return count;
}

void DeviceRegistry::checkTimeouts() {
    uint32_t now_sec = millis() / 1000;
    for (auto& dev : _devices) {
        if (dev.online && (now_sec - dev.last_seen) > _offline_timeout_sec) {
            dev.online = false;
            LOG_I(TAG, "Device offline: %s", dev.device_name);
        }
    }
}

bool DeviceRegistry::saveToNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return false;

    uint8_t count = (uint8_t)_devices.size();
    prefs.putUChar("count", count);

    for (uint8_t i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "dev%d", i);
        prefs.putBytes(key, &_devices[i], sizeof(RegisteredDevice));
    }

    prefs.end();
    return true;
}

bool DeviceRegistry::loadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return false;

    uint8_t count = prefs.getUChar("count", 0);
    _devices.clear();

    for (uint8_t i = 0; i < count && i < MAX_DEVICES; i++) {
        char key[16];
        snprintf(key, sizeof(key), "dev%d", i);
        RegisteredDevice dev;
        size_t len = prefs.getBytes(key, &dev, sizeof(RegisteredDevice));
        if (len == sizeof(RegisteredDevice)) {
            dev.online = false; // Devices start offline after reboot
            _devices.push_back(dev);
        }
    }

    prefs.end();
    LOG_I(TAG, "Loaded %d devices from NVS", (int)_devices.size());
    return true;
}

void DeviceRegistry::clear() {
    _devices.clear();
}

String DeviceRegistry::macToString(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

bool DeviceRegistry::stringToMac(const char* str, uint8_t* mac) {
    if (!str || strlen(str) < 17) return false;
    int vals[6];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x",
               &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)vals[i];
    return true;
}

int DeviceRegistry::findDeviceIndex(const uint8_t* mac) {
    for (int i = 0; i < (int)_devices.size(); i++) {
        if (memcmp(_devices[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

} // namespace iwmp
