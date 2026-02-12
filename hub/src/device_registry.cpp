/**
 * @file device_registry.cpp
 * @brief Paired device tracking implementation for Hub
 */

#include "device_registry.h"
#include "logger.h"
#include <Preferences.h>

namespace iwmp {

static constexpr const char* TAG = "Registry";

void DeviceRegistry::begin() {
    LOG_I(TAG, "Initializing device registry");
    loadFromNVS();
    LOG_I(TAG, "Registry loaded with %d device(s)", _devices.size());
}

bool DeviceRegistry::addDevice(const uint8_t* mac, uint8_t type, const char* name) {
    if (!mac) {
        return false;
    }

    // Check if device already exists
    int idx = findDeviceIndex(mac);
    if (idx >= 0) {
        // Update existing device
        RegisteredDevice& dev = _devices[idx];
        dev.device_type = type;
        if (name && strlen(name) > 0) {
            strlcpy(dev.device_name, name, sizeof(dev.device_name));
        }
        dev.last_seen = millis() / 1000;
        dev.online = true;
        LOG_I(TAG, "Updated device %s", macToString(mac).c_str());
        return true;
    }

    // Check capacity
    if (_devices.size() >= MAX_DEVICES) {
        LOG_W(TAG, "Registry full, cannot add device");
        return false;
    }

    // Add new device
    RegisteredDevice dev = {};
    memcpy(dev.mac, mac, 6);
    dev.device_type = type;
    if (name && strlen(name) > 0) {
        strlcpy(dev.device_name, name, sizeof(dev.device_name));
    } else {
        snprintf(dev.device_name, sizeof(dev.device_name), "Device_%02X%02X",
                 mac[4], mac[5]);
    }
    dev.last_seen = millis() / 1000;
    dev.paired = true;
    dev.online = true;
    dev.last_moisture_percent = 0;
    dev.last_temperature = NAN;
    dev.last_humidity = NAN;
    dev.last_battery_percent = 255;

    _devices.push_back(dev);

    LOG_I(TAG, "Added device %s (%s)", dev.device_name, macToString(mac).c_str());
    return true;
}

bool DeviceRegistry::removeDevice(const uint8_t* mac) {
    int idx = findDeviceIndex(mac);
    if (idx < 0) {
        return false;
    }

    LOG_I(TAG, "Removing device %s", macToString(mac).c_str());
    _devices.erase(_devices.begin() + idx);
    return true;
}

RegisteredDevice* DeviceRegistry::getDevice(const uint8_t* mac) {
    int idx = findDeviceIndex(mac);
    if (idx < 0) {
        return nullptr;
    }
    return &_devices[idx];
}

void DeviceRegistry::updateLastSeen(const uint8_t* mac, int8_t rssi) {
    RegisteredDevice* dev = getDevice(mac);
    if (dev) {
        dev->last_seen = millis() / 1000;
        dev->last_rssi = rssi;
        dev->online = true;
    }
}

void DeviceRegistry::updateReadings(const uint8_t* mac, uint8_t moisture,
                                     float temp, float humidity, uint8_t battery) {
    RegisteredDevice* dev = getDevice(mac);
    if (dev) {
        dev->last_moisture_percent = moisture;
        if (!isnan(temp)) {
            dev->last_temperature = temp;
        }
        if (!isnan(humidity)) {
            dev->last_humidity = humidity;
        }
        if (battery != 255) {
            dev->last_battery_percent = battery;
        }
        dev->last_seen = millis() / 1000;
        dev->online = true;
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
        if (dev.online) {
            count++;
        }
    }
    return count;
}

void DeviceRegistry::checkTimeouts() {
    uint32_t now_sec = millis() / 1000;

    for (auto& dev : _devices) {
        if (dev.online) {
            uint32_t elapsed = now_sec - dev.last_seen;
            if (elapsed > _offline_timeout_sec) {
                dev.online = false;
                LOG_W(TAG, "Device %s went offline (timeout: %lu sec)",
                      dev.device_name, elapsed);
            }
        }
    }
}

bool DeviceRegistry::saveToNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        LOG_E(TAG, "Failed to open NVS for writing");
        return false;
    }

    // Save device count
    prefs.putUChar("count", _devices.size());

    // Save each device
    for (size_t i = 0; i < _devices.size(); i++) {
        char key[16];
        snprintf(key, sizeof(key), "dev%d", i);

        // Create a serializable structure (without online state - that's runtime)
        struct StoredDevice {
            uint8_t mac[6];
            uint8_t device_type;
            char device_name[32];
            char firmware_version[16];
            bool paired;
            uint16_t dry_value;
            uint16_t wet_value;
        } __attribute__((packed));

        StoredDevice stored;
        memcpy(stored.mac, _devices[i].mac, 6);
        stored.device_type = _devices[i].device_type;
        strlcpy(stored.device_name, _devices[i].device_name, sizeof(stored.device_name));
        strlcpy(stored.firmware_version, _devices[i].firmware_version, sizeof(stored.firmware_version));
        stored.paired = _devices[i].paired;

        prefs.putBytes(key, &stored, sizeof(stored));
    }

    prefs.end();
    LOG_I(TAG, "Saved %d devices to NVS", _devices.size());
    return true;
}

bool DeviceRegistry::loadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        LOG_W(TAG, "No saved registry found");
        return false;
    }

    _devices.clear();

    uint8_t count = prefs.getUChar("count", 0);
    LOG_D(TAG, "Loading %d devices from NVS", count);

    for (uint8_t i = 0; i < count && i < MAX_DEVICES; i++) {
        char key[16];
        snprintf(key, sizeof(key), "dev%d", i);

        struct StoredDevice {
            uint8_t mac[6];
            uint8_t device_type;
            char device_name[32];
            char firmware_version[16];
            bool paired;
            uint16_t dry_value;
            uint16_t wet_value;
        } __attribute__((packed));

        StoredDevice stored;
        size_t len = prefs.getBytes(key, &stored, sizeof(stored));

        if (len == sizeof(stored)) {
            RegisteredDevice dev = {};
            memcpy(dev.mac, stored.mac, 6);
            dev.device_type = stored.device_type;
            strlcpy(dev.device_name, stored.device_name, sizeof(dev.device_name));
            strlcpy(dev.firmware_version, stored.firmware_version, sizeof(dev.firmware_version));
            dev.paired = stored.paired;
            dev.online = false;  // Will be updated when device checks in
            dev.last_seen = 0;
            dev.last_rssi = 0;
            dev.last_moisture_percent = 0;
            dev.last_temperature = NAN;
            dev.last_humidity = NAN;
            dev.last_battery_percent = 255;

            _devices.push_back(dev);
            LOG_D(TAG, "Loaded device: %s", dev.device_name);
        }
    }

    prefs.end();
    return true;
}

void DeviceRegistry::clear() {
    _devices.clear();

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.clear();
        prefs.end();
    }

    LOG_I(TAG, "Registry cleared");
}

String DeviceRegistry::macToString(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

bool DeviceRegistry::stringToMac(const char* str, uint8_t* mac) {
    if (!str || !mac) {
        return false;
    }

    int values[6];
    if (sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6) {
        return false;
    }

    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)values[i];
    }
    return true;
}

int DeviceRegistry::findDeviceIndex(const uint8_t* mac) {
    for (size_t i = 0; i < _devices.size(); i++) {
        if (memcmp(_devices[i].mac, mac, 6) == 0) {
            return (int)i;
        }
    }
    return -1;
}

} // namespace iwmp
