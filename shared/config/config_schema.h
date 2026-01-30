/**
 * @file config_schema.h
 * @brief Configuration structure definitions for iWetMyPlants v2.0
 *
 * All configuration data is stored in NVS and managed by ConfigManager.
 * Structures are packed to ensure consistent memory layout across builds.
 */

#pragma once

#include <Arduino.h>

namespace iwmp {

// Configuration version for migration support
static constexpr uint32_t CONFIG_VERSION = 1;

// Device type identifiers
enum class DeviceType : uint8_t {
    HUB = 0,
    REMOTE = 1,
    GREENHOUSE = 2
};

// ============ DEVICE IDENTITY ============
struct DeviceIdentity {
    char device_name[32];           // User-friendly name
    char device_id[13];             // MAC-based unique ID (auto-generated)
    uint8_t device_type;            // 0=Hub, 1=Remote, 2=Greenhouse
    char firmware_version[16];      // Semantic version
} __attribute__((packed));

// ============ WIFI CONFIGURATION ============
struct WifiConfig {
    char ssid[33];                  // WiFi SSID (max 32 chars + null)
    char password[65];              // WiFi password (max 64 chars + null)
    bool use_static_ip;             // Use static IP instead of DHCP
    uint32_t static_ip;             // Static IP address (network byte order)
    uint32_t gateway;               // Gateway address
    uint32_t subnet;                // Subnet mask
    uint32_t dns;                   // DNS server
    uint8_t wifi_channel;           // WiFi channel (important for ESP-NOW coexistence)
} __attribute__((packed));

// ============ MQTT CONFIGURATION ============
struct MqttConfig {
    bool enabled;                   // MQTT enabled
    char broker[65];                // MQTT broker hostname/IP
    uint16_t port;                  // MQTT port (default 1883)
    char username[33];              // MQTT username
    char password[65];              // MQTT password
    char base_topic[65];            // Base topic (default: "iwetmyplants")
    bool ha_discovery_enabled;      // Home Assistant auto-discovery
    char ha_discovery_prefix[33];   // HA discovery prefix (default: "homeassistant")
    uint16_t publish_interval_sec;  // How often to push to MQTT
} __attribute__((packed));

// ============ ESP-NOW CONFIGURATION ============
struct EspNowConfig {
    bool enabled;                   // ESP-NOW enabled
    uint8_t hub_mac[6];             // MAC of Hub to send to (for remotes)
    uint8_t channel;                // WiFi channel (must match router if WiFi used)
    bool encryption_enabled;        // Use encryption
    uint8_t pmk[16];                // Primary Master Key
    uint8_t lmk[16];                // Local Master Key
    uint16_t send_interval_sec;     // How often to send readings
} __attribute__((packed));

// ============ SENSOR CONFIGURATION ============
enum class SensorInputType : uint8_t {
    DIRECT_ADC = 0,                 // Direct to ESP32 ADC pin
    ADS1115 = 1,                    // External 16-bit ADC
    MUX_CD74HC4067 = 2              // 16-channel analog multiplexer
};

struct MoistureSensorConfig {
    bool enabled;                   // Sensor enabled
    SensorInputType input_type;     // Input type
    uint8_t adc_pin;                // GPIO pin for DIRECT_ADC
    uint8_t ads_channel;            // ADS1115 channel (0-3)
    uint8_t mux_channel;            // Multiplexer channel (0-15)
    uint8_t ads_i2c_address;        // ADS1115 I2C address (default 0x48)
    uint16_t dry_value;             // ADC value when dry (calibrated)
    uint16_t wet_value;             // ADC value when wet (calibrated)
    uint8_t reading_samples;        // Number of samples to average
    uint16_t sample_delay_ms;       // Delay between samples
    char sensor_name[32];           // User-friendly name
} __attribute__((packed));

// ============ ENVIRONMENTAL SENSOR CONFIG ============
enum class EnvSensorType : uint8_t {
    NONE = 0,
    DHT11 = 1,
    DHT22 = 2,
    SHT30 = 3,
    SHT31 = 4,
    SHT40 = 5,
    SHT41 = 6
};

struct EnvironmentalSensorConfig {
    bool enabled;                   // Sensor enabled
    EnvSensorType sensor_type;      // Sensor type
    uint8_t pin;                    // GPIO pin for DHT sensors
    uint8_t i2c_address;            // I2C address for SHT sensors (default 0x44)
    uint16_t read_interval_sec;     // Read interval in seconds
} __attribute__((packed));

// ============ RELAY CONFIGURATION ============
struct RelayConfig {
    bool enabled;                   // Relay enabled
    uint8_t gpio_pin;               // GPIO pin
    bool active_low;                // Most relay modules are active-low
    char relay_name[32];            // User-friendly name
    uint32_t max_on_time_sec;       // Safety timeout (0 = no limit)
    uint32_t min_off_time_sec;      // Minimum off time between activations
    uint32_t cooldown_sec;          // Required gap after deactivation
} __attribute__((packed));

// ============ AUTOMATION BINDING ============
struct SensorRelayBinding {
    bool enabled;                   // Binding enabled
    uint8_t sensor_index;           // Which moisture sensor triggers this
    uint8_t relay_index;            // Which relay to control
    uint16_t dry_threshold;         // Moisture % below which to activate
    uint16_t wet_threshold;         // Moisture % above which to deactivate
    uint32_t max_runtime_sec;       // Maximum single run time
    uint32_t check_interval_sec;    // How often to check sensor
    bool hysteresis_enabled;        // Prevent rapid cycling
} __attribute__((packed));

// ============ POWER CONFIGURATION (Remote only) ============
struct PowerConfig {
    bool battery_powered;           // Running on battery
    uint32_t deep_sleep_duration_sec; // Deep sleep duration
    uint32_t awake_duration_ms;     // How long to stay awake
    bool wake_on_button;            // Wake on button press
    uint8_t wake_button_pin;        // Button GPIO pin
    bool wake_on_power_connect;     // Wake when USB power detected
    uint8_t power_detect_pin;       // Pin to detect USB power
    uint8_t battery_adc_pin;        // Pin for battery voltage monitoring
    float low_battery_voltage;      // Threshold for low battery warning
} __attribute__((packed));

// ============ MASTER CONFIGURATION ============
// Maximum array sizes (can be adjusted per device type via build flags)
#ifndef IWMP_MAX_SENSORS
#define IWMP_MAX_SENSORS 8
#endif

#ifndef IWMP_MAX_RELAYS
#define IWMP_MAX_RELAYS 4
#endif

#ifndef IWMP_MAX_BINDINGS
#define IWMP_MAX_BINDINGS 4
#endif

struct DeviceConfig {
    // Header
    uint32_t magic;                 // Magic number for validation (0x49574D50 = "IWMP")
    uint32_t config_version;        // Configuration version for migration

    // Core configuration
    DeviceIdentity identity;
    WifiConfig wifi;
    MqttConfig mqtt;
    EspNowConfig espnow;

    // Sensors
    MoistureSensorConfig moisture_sensors[IWMP_MAX_SENSORS];
    EnvironmentalSensorConfig env_sensor;

    // Relays and automation (Greenhouse only)
    RelayConfig relays[IWMP_MAX_RELAYS];
    SensorRelayBinding bindings[IWMP_MAX_BINDINGS];

    // Power management (Remote only)
    PowerConfig power;

    // Integrity check (must be last)
    uint32_t crc32;
} __attribute__((packed));

// Magic number for config validation
static constexpr uint32_t CONFIG_MAGIC = 0x49574D50; // "IWMP" in ASCII

// Helper to check if config is valid
inline bool isConfigValid(const DeviceConfig& config) {
    return config.magic == CONFIG_MAGIC;
}

} // namespace iwmp
