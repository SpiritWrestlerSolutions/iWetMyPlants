/**
 * @file config_manager.cpp
 * @brief NVS-based configuration management implementation
 */

#include "config_manager.h"
#include <esp_mac.h>
#include <esp_crc.h>
#include "../utils/logger.h"

namespace iwmp {

static constexpr const char* TAG = "Config";

// Global config manager reference
ConfigManager& Config = ConfigManager::getInstance();

// Static instance accessor
ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::begin(DeviceType type) {
    if (_initialized) {
        return true;
    }

    _device_type = type;

    // Initialize with defaults first
    initDefaultConfig(_config, type);

    // Generate device ID from MAC
    generateDeviceId();

    // Try to load from NVS
    if (!load()) {
        // No valid config in NVS, save defaults
        LOG_I(TAG, "No valid config found, using defaults");
        save();
    }

    _initialized = true;
    return true;
}

bool ConfigManager::load() {
    if (!_prefs.begin(NVS_NAMESPACE, true)) {  // Read-only mode
        LOG_E(TAG, "Failed to open NVS namespace");
        return false;
    }

    // Check if config blob exists
    size_t stored_size = _prefs.getBytesLength(KEY_CONFIG_BLOB);
    if (stored_size == 0) {
        LOG_D(TAG, "No config blob found in NVS");
        _prefs.end();
        return false;
    }

    // Check size matches
    if (stored_size != sizeof(DeviceConfig)) {
        LOG_E(TAG, "Size mismatch: stored=%u, expected=%u",
                      stored_size, sizeof(DeviceConfig));
        _prefs.end();
        return false;
    }

    // Load config blob
    DeviceConfig temp_config;
    size_t read_size = _prefs.getBytes(KEY_CONFIG_BLOB, &temp_config, sizeof(DeviceConfig));
    _prefs.end();

    if (read_size != sizeof(DeviceConfig)) {
        LOG_E(TAG, "Failed to read config blob");
        return false;
    }

    // Verify magic number
    if (temp_config.magic != CONFIG_MAGIC) {
        LOG_E(TAG, "Invalid magic: 0x%08X (expected 0x%08X)",
                      temp_config.magic, CONFIG_MAGIC);
        return false;
    }

    // Verify CRC
    uint32_t stored_crc = temp_config.crc32;
    temp_config.crc32 = 0;  // CRC calculated with this field zeroed

    // Calculate CRC of loaded data (excluding CRC field itself)
    uint32_t calculated_crc = esp_crc32_le(0, (const uint8_t*)&temp_config,
                                            sizeof(DeviceConfig) - sizeof(uint32_t));

    if (calculated_crc != stored_crc) {
        LOG_E(TAG, "CRC mismatch: stored=0x%08X, calculated=0x%08X",
                      stored_crc, calculated_crc);
        return false;
    }

    // Restore CRC and copy config
    temp_config.crc32 = stored_crc;
    memcpy(&_config, &temp_config, sizeof(DeviceConfig));

    // Preserve device ID (always regenerate from MAC)
    generateDeviceId();

    // Check for migration
    if (needsMigration()) {
        LOG_I(TAG, "Config migration needed");
        migrate();
    }

    LOG_I(TAG, "Loaded config v%u (%u bytes)",
                  _config.config_version, sizeof(DeviceConfig));
    return true;
}

bool ConfigManager::save() {
    // Update CRC before saving
    updateCrc();

    if (!_prefs.begin(NVS_NAMESPACE, false)) {  // Read-write mode
        LOG_E(TAG, "Failed to open NVS namespace for writing");
        return false;
    }

    // Save config as blob
    size_t written = _prefs.putBytes(KEY_CONFIG_BLOB, &_config, sizeof(DeviceConfig));
    _prefs.end();

    if (written != sizeof(DeviceConfig)) {
        LOG_E(TAG, "Failed to write config: wrote %u of %u bytes",
                      written, sizeof(DeviceConfig));
        return false;
    }

    LOG_I(TAG, "Saved config (%u bytes, CRC=0x%08X)",
                  sizeof(DeviceConfig), _config.crc32);

    // Notify listeners
    notifyChange();

    return true;
}

bool ConfigManager::resetToDefaults(bool save_immediately) {
    LOG_I(TAG, "Resetting to factory defaults");

    // Preserve device ID before reset
    char device_id[13];
    strncpy(device_id, _config.identity.device_id, sizeof(device_id));

    // Initialize with defaults
    initDefaultConfig(_config, _device_type);

    // Restore device ID
    strncpy(_config.identity.device_id, device_id, sizeof(_config.identity.device_id));

    if (save_immediately) {
        return save();
    }

    notifyChange();
    return true;
}

bool ConfigManager::eraseAll() {
    LOG_I(TAG, "Erasing all configuration from NVS");

    if (!_prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    bool result = _prefs.clear();
    _prefs.end();

    if (result) {
        // Reset to defaults in memory
        resetToDefaults(false);
    }

    return result;
}

// ============ Section Accessors ============

const MoistureSensorConfig& ConfigManager::getMoistureSensor(uint8_t index) const {
    static MoistureSensorConfig dummy;
    if (index >= IWMP_MAX_SENSORS) {
        return dummy;
    }
    return _config.moisture_sensors[index];
}

MoistureSensorConfig& ConfigManager::getMoistureSensorMutable(uint8_t index) {
    static MoistureSensorConfig dummy;
    if (index >= IWMP_MAX_SENSORS) {
        return dummy;
    }
    return _config.moisture_sensors[index];
}

const RelayConfig& ConfigManager::getRelay(uint8_t index) const {
    static RelayConfig dummy;
    if (index >= IWMP_MAX_RELAYS) {
        return dummy;
    }
    return _config.relays[index];
}

RelayConfig& ConfigManager::getRelayMutable(uint8_t index) {
    static RelayConfig dummy;
    if (index >= IWMP_MAX_RELAYS) {
        return dummy;
    }
    return _config.relays[index];
}

const SensorRelayBinding& ConfigManager::getBinding(uint8_t index) const {
    static SensorRelayBinding dummy;
    if (index >= IWMP_MAX_BINDINGS) {
        return dummy;
    }
    return _config.bindings[index];
}

SensorRelayBinding& ConfigManager::getBindingMutable(uint8_t index) {
    static SensorRelayBinding dummy;
    if (index >= IWMP_MAX_BINDINGS) {
        return dummy;
    }
    return _config.bindings[index];
}

// ============ Convenience Methods ============

void ConfigManager::setDeviceName(const char* name) {
    if (name) {
        strncpy(_config.identity.device_name, name,
                sizeof(_config.identity.device_name) - 1);
        _config.identity.device_name[sizeof(_config.identity.device_name) - 1] = '\0';
    }
}

void ConfigManager::setWifiCredentials(const char* ssid, const char* password) {
    if (ssid) {
        strncpy(_config.wifi.ssid, ssid, sizeof(_config.wifi.ssid) - 1);
        _config.wifi.ssid[sizeof(_config.wifi.ssid) - 1] = '\0';
    }
    if (password) {
        strncpy(_config.wifi.password, password, sizeof(_config.wifi.password) - 1);
        _config.wifi.password[sizeof(_config.wifi.password) - 1] = '\0';
    }
}

void ConfigManager::setMqttBroker(const char* broker, uint16_t port,
                                   const char* username, const char* password) {
    if (broker) {
        strncpy(_config.mqtt.broker, broker, sizeof(_config.mqtt.broker) - 1);
        _config.mqtt.broker[sizeof(_config.mqtt.broker) - 1] = '\0';
    }
    _config.mqtt.port = port;

    if (username) {
        strncpy(_config.mqtt.username, username, sizeof(_config.mqtt.username) - 1);
        _config.mqtt.username[sizeof(_config.mqtt.username) - 1] = '\0';
    } else {
        _config.mqtt.username[0] = '\0';
    }

    if (password) {
        strncpy(_config.mqtt.password, password, sizeof(_config.mqtt.password) - 1);
        _config.mqtt.password[sizeof(_config.mqtt.password) - 1] = '\0';
    } else {
        _config.mqtt.password[0] = '\0';
    }
}

void ConfigManager::setHubMac(const uint8_t* mac) {
    if (mac) {
        memcpy(_config.espnow.hub_mac, mac, 6);
    }
}

void ConfigManager::setCalibration(uint8_t index, uint16_t dry_value, uint16_t wet_value) {
    if (index < IWMP_MAX_SENSORS) {
        _config.moisture_sensors[index].dry_value = dry_value;
        _config.moisture_sensors[index].wet_value = wet_value;
    }
}

// ============ Callbacks ============

void ConfigManager::onConfigChange(ConfigChangeCallback callback) {
    _change_callback = callback;
}

// ============ Validation ============

bool ConfigManager::validate() const {
    // Check magic
    if (_config.magic != CONFIG_MAGIC) {
        return false;
    }

    // Check version
    if (_config.config_version == 0 || _config.config_version > CONFIG_VERSION) {
        return false;
    }

    // Check device type
    if (_config.identity.device_type > 2) {
        return false;
    }

    // Validate WiFi SSID if set
    if (_config.wifi.ssid[0] != '\0') {
        size_t ssid_len = strlen(_config.wifi.ssid);
        if (ssid_len > 32) {
            return false;
        }
    }

    // Validate MQTT port
    if (_config.mqtt.enabled && _config.mqtt.port == 0) {
        return false;
    }

    // Validate sensor configurations
    for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
        const auto& sensor = _config.moisture_sensors[i];
        if (sensor.enabled) {
            // Check calibration values are sane
            if (sensor.dry_value == sensor.wet_value) {
                return false;
            }
        }
    }

    return true;
}

bool ConfigManager::needsMigration() const {
    return _config.config_version < CONFIG_VERSION;
}

bool ConfigManager::migrate() {
    uint32_t old_version = _config.config_version;

    // Migration from v0 to v1 (example)
    // if (old_version < 1) {
    //     // Add migration logic here
    //     _config.config_version = 1;
    // }

    // Set to current version
    _config.config_version = CONFIG_VERSION;

    LOG_I(TAG, "Migrated from v%u to v%u", old_version, CONFIG_VERSION);
    return true;
}

// ============ Debug ============

void ConfigManager::printConfig() const {
    LOG_D(TAG, "======== Device Configuration ========");
    LOG_D(TAG, "Magic: 0x%08X", _config.magic);
    LOG_D(TAG, "Version: %u", _config.config_version);
    LOG_D(TAG, "Size: %u bytes", sizeof(DeviceConfig));
    LOG_D(TAG, "CRC32: 0x%08X", _config.crc32);
    LOG_D(TAG, "--- Identity ---");
    LOG_D(TAG, "  Name: %s", _config.identity.device_name);
    LOG_D(TAG, "  ID: %s", _config.identity.device_id);
    LOG_D(TAG, "  Type: %u", _config.identity.device_type);
    LOG_D(TAG, "  Firmware: %s", _config.identity.firmware_version);
    LOG_D(TAG, "--- WiFi ---");
    LOG_D(TAG, "  SSID: %s", _config.wifi.ssid[0] ? _config.wifi.ssid : "(not set)");
    LOG_D(TAG, "  Static IP: %s", _config.wifi.use_static_ip ? "yes" : "no");
    LOG_D(TAG, "  Channel: %u", _config.wifi.wifi_channel);
    LOG_D(TAG, "--- MQTT ---");
    LOG_D(TAG, "  Enabled: %s", _config.mqtt.enabled ? "yes" : "no");
    LOG_D(TAG, "  Broker: %s:%u", _config.mqtt.broker, _config.mqtt.port);
    LOG_D(TAG, "  Base Topic: %s", _config.mqtt.base_topic);
    LOG_D(TAG, "  HA Discovery: %s", _config.mqtt.ha_discovery_enabled ? "yes" : "no");
    LOG_D(TAG, "--- ESP-NOW ---");
    LOG_D(TAG, "  Enabled: %s", _config.espnow.enabled ? "yes" : "no");
    LOG_D(TAG, "  Channel: %u", _config.espnow.channel);
    LOG_D(TAG, "  Hub MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                  _config.espnow.hub_mac[0], _config.espnow.hub_mac[1],
                  _config.espnow.hub_mac[2], _config.espnow.hub_mac[3],
                  _config.espnow.hub_mac[4], _config.espnow.hub_mac[5]);
    LOG_D(TAG, "--- Moisture Sensors ---");
    for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
        const auto& sensor = _config.moisture_sensors[i];
        if (sensor.enabled) {
            LOG_D(TAG, "  [%u] %s: pin=%u, dry=%u, wet=%u",
                          i, sensor.sensor_name, sensor.adc_pin,
                          sensor.dry_value, sensor.wet_value);
        }
    }
    if (_config.identity.device_type == static_cast<uint8_t>(DeviceType::GREENHOUSE)) {
        LOG_D(TAG, "--- Relays ---");
        for (uint8_t i = 0; i < IWMP_MAX_RELAYS; i++) {
            const auto& relay = _config.relays[i];
            if (relay.enabled) {
                LOG_D(TAG, "  [%u] %s: pin=%u, active_low=%s",
                              i, relay.relay_name, relay.gpio_pin,
                              relay.active_low ? "yes" : "no");
            }
        }
    }
    if (_config.identity.device_type == static_cast<uint8_t>(DeviceType::REMOTE)) {
        LOG_D(TAG, "--- Power ---");
        LOG_D(TAG, "  Battery Powered: %s", _config.power.battery_powered ? "yes" : "no");
        LOG_D(TAG, "  Sleep Duration: %u sec", _config.power.deep_sleep_duration_sec);
        LOG_D(TAG, "  Low Battery: %.2fV", _config.power.low_battery_voltage);
    }
    LOG_D(TAG, "======================================");
}

// ============ Private Methods ============

void ConfigManager::generateDeviceId() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // Format as 12-char hex string (lowercase)
    snprintf(_config.identity.device_id, sizeof(_config.identity.device_id),
             "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

uint32_t ConfigManager::calculateCrc() const {
    // Calculate CRC over entire config except the CRC field itself
    // The CRC field is at the end of the struct
    return esp_crc32_le(0, (const uint8_t*)&_config,
                        sizeof(DeviceConfig) - sizeof(uint32_t));
}

bool ConfigManager::verifyCrc() const {
    uint32_t calculated = calculateCrc();
    return calculated == _config.crc32;
}

void ConfigManager::updateCrc() {
    _config.crc32 = 0;  // Zero out before calculation
    _config.crc32 = calculateCrc();
}

void ConfigManager::notifyChange() {
    if (_change_callback) {
        _change_callback(_config);
    }
}

} // namespace iwmp
