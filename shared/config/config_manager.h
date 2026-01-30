/**
 * @file config_manager.h
 * @brief NVS-based configuration management for iWetMyPlants v2.0
 *
 * Handles persistent storage of device configuration using ESP32's
 * Non-Volatile Storage (NVS) system. Provides load, save, and reset
 * functionality with CRC32 integrity checking.
 */

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <functional>
#include "config_schema.h"
#include "defaults.h"

namespace iwmp {

// NVS namespace for configuration storage
static constexpr const char* NVS_NAMESPACE = "iwmp_config";

// NVS keys for individual sections (for partial updates)
static constexpr const char* KEY_CONFIG_BLOB = "config";
static constexpr const char* KEY_WIFI = "wifi";
static constexpr const char* KEY_MQTT = "mqtt";
static constexpr const char* KEY_ESPNOW = "espnow";
static constexpr const char* KEY_SENSORS = "sensors";
static constexpr const char* KEY_RELAYS = "relays";
static constexpr const char* KEY_BINDINGS = "bindings";
static constexpr const char* KEY_POWER = "power";

/**
 * @brief Configuration change callback type
 */
using ConfigChangeCallback = std::function<void(const DeviceConfig& config)>;

/**
 * @brief Configuration manager for NVS storage
 *
 * Singleton class that manages device configuration persistence.
 * Thread-safe for ESP32 FreeRTOS environment.
 */
class ConfigManager {
public:
    /**
     * @brief Get singleton instance
     */
    static ConfigManager& getInstance();

    // Delete copy/move constructors
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    /**
     * @brief Initialize configuration manager
     * @param type Device type (determines default configuration)
     * @return true if successful
     */
    bool begin(DeviceType type);

    /**
     * @brief Check if configuration manager is initialized
     */
    bool isInitialized() const { return _initialized; }

    /**
     * @brief Load configuration from NVS
     * @return true if valid config loaded, false if defaults used
     */
    bool load();

    /**
     * @brief Save current configuration to NVS
     * @return true if successful
     */
    bool save();

    /**
     * @brief Reset configuration to factory defaults
     * @param save_immediately If true, save defaults to NVS immediately
     * @return true if successful
     */
    bool resetToDefaults(bool save_immediately = true);

    /**
     * @brief Erase all configuration from NVS
     * @return true if successful
     */
    bool eraseAll();

    /**
     * @brief Get current configuration (read-only)
     */
    const DeviceConfig& getConfig() const { return _config; }

    /**
     * @brief Get mutable configuration reference
     * @note Call save() after modifications
     */
    DeviceConfig& getConfigMutable() { return _config; }

    // ============ Section Accessors ============

    /**
     * @brief Get device identity
     */
    const DeviceIdentity& getIdentity() const { return _config.identity; }
    DeviceIdentity& getIdentityMutable() { return _config.identity; }

    /**
     * @brief Get WiFi configuration
     */
    const WifiConfig& getWifi() const { return _config.wifi; }
    WifiConfig& getWifiMutable() { return _config.wifi; }

    /**
     * @brief Get MQTT configuration
     */
    const MqttConfig& getMqtt() const { return _config.mqtt; }
    MqttConfig& getMqttMutable() { return _config.mqtt; }

    /**
     * @brief Get ESP-NOW configuration
     */
    const EspNowConfig& getEspNow() const { return _config.espnow; }
    EspNowConfig& getEspNowMutable() { return _config.espnow; }

    /**
     * @brief Get moisture sensor configuration
     * @param index Sensor index (0 to IWMP_MAX_SENSORS-1)
     */
    const MoistureSensorConfig& getMoistureSensor(uint8_t index) const;
    MoistureSensorConfig& getMoistureSensorMutable(uint8_t index);

    /**
     * @brief Get environmental sensor configuration
     */
    const EnvironmentalSensorConfig& getEnvSensor() const { return _config.env_sensor; }
    EnvironmentalSensorConfig& getEnvSensorMutable() { return _config.env_sensor; }

    /**
     * @brief Get relay configuration
     * @param index Relay index (0 to IWMP_MAX_RELAYS-1)
     */
    const RelayConfig& getRelay(uint8_t index) const;
    RelayConfig& getRelayMutable(uint8_t index);

    /**
     * @brief Get sensor-relay binding
     * @param index Binding index (0 to IWMP_MAX_BINDINGS-1)
     */
    const SensorRelayBinding& getBinding(uint8_t index) const;
    SensorRelayBinding& getBindingMutable(uint8_t index);

    /**
     * @brief Get power configuration
     */
    const PowerConfig& getPower() const { return _config.power; }
    PowerConfig& getPowerMutable() { return _config.power; }

    // ============ Convenience Methods ============

    /**
     * @brief Get device name
     */
    const char* getDeviceName() const { return _config.identity.device_name; }

    /**
     * @brief Set device name
     * @param name New device name (max 31 chars)
     */
    void setDeviceName(const char* name);

    /**
     * @brief Get device ID (MAC-based)
     */
    const char* getDeviceId() const { return _config.identity.device_id; }

    /**
     * @brief Get device type
     */
    DeviceType getDeviceType() const {
        return static_cast<DeviceType>(_config.identity.device_type);
    }

    /**
     * @brief Set WiFi credentials
     * @param ssid WiFi SSID
     * @param password WiFi password
     */
    void setWifiCredentials(const char* ssid, const char* password);

    /**
     * @brief Set MQTT broker
     * @param broker Broker hostname/IP
     * @param port Broker port
     * @param username Username (optional)
     * @param password Password (optional)
     */
    void setMqttBroker(const char* broker, uint16_t port = 1883,
                       const char* username = nullptr, const char* password = nullptr);

    /**
     * @brief Set ESP-NOW hub MAC address
     * @param mac Hub MAC address (6 bytes)
     */
    void setHubMac(const uint8_t* mac);

    /**
     * @brief Update calibration values for a moisture sensor
     * @param index Sensor index
     * @param dry_value ADC value when dry
     * @param wet_value ADC value when wet
     */
    void setCalibration(uint8_t index, uint16_t dry_value, uint16_t wet_value);

    // ============ Callbacks ============

    /**
     * @brief Register callback for configuration changes
     * @param callback Function to call when config changes
     */
    void onConfigChange(ConfigChangeCallback callback);

    // ============ Validation ============

    /**
     * @brief Validate current configuration
     * @return true if configuration is valid
     */
    bool validate() const;

    /**
     * @brief Check if configuration needs migration
     * @return true if version mismatch detected
     */
    bool needsMigration() const;

    /**
     * @brief Migrate configuration to current version
     * @return true if migration successful
     */
    bool migrate();

    // ============ Debug ============

    /**
     * @brief Print configuration summary to Serial
     */
    void printConfig() const;

    /**
     * @brief Get configuration size in bytes
     */
    size_t getConfigSize() const { return sizeof(DeviceConfig); }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;

    DeviceConfig _config;
    DeviceType _device_type = DeviceType::HUB;
    bool _initialized = false;
    Preferences _prefs;
    ConfigChangeCallback _change_callback = nullptr;

    /**
     * @brief Generate device ID from MAC address
     */
    void generateDeviceId();

    /**
     * @brief Calculate CRC32 of configuration
     * @return CRC32 value
     */
    uint32_t calculateCrc() const;

    /**
     * @brief Verify CRC32 of configuration
     * @return true if CRC matches
     */
    bool verifyCrc() const;

    /**
     * @brief Update CRC32 in configuration
     */
    void updateCrc();

    /**
     * @brief Notify change callback
     */
    void notifyChange();
};

// Global config manager accessor
extern ConfigManager& Config;

} // namespace iwmp
