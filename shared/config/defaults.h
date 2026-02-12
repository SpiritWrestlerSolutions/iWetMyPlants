/**
 * @file defaults.h
 * @brief Default configuration values for iWetMyPlants v2.0
 *
 * These defaults are applied when no configuration exists in NVS
 * or when a factory reset is performed.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "config_schema.h"

namespace iwmp {

// ============ DEFAULT PIN ASSIGNMENTS ============
// These vary by device type and board

// Hub (ESP32-WROOM) defaults
namespace hub_pins {
    constexpr uint8_t ADC_PINS[] = {32, 33, 34, 35, 36, 37, 38, 39}; // ADC1 only (WiFi safe)
    constexpr uint8_t I2C_SDA = 21;
    constexpr uint8_t I2C_SCL = 22;
    constexpr uint8_t STATUS_LED = 4;
    constexpr uint8_t CONFIG_BUTTON = 0;  // Boot button
}

// Remote (ESP32-C3) defaults
namespace remote_pins {
    constexpr uint8_t ADC_PIN = 0;        // GPIO0 (ADC1_CH0)
    constexpr uint8_t I2C_SDA = 8;
    constexpr uint8_t I2C_SCL = 9;
    constexpr uint8_t STATUS_LED = 7;
    constexpr uint8_t WAKE_BUTTON = 5;
    constexpr uint8_t POWER_DETECT = 6;
    constexpr uint8_t BATTERY_ADC = 1;    // GPIO1 (ADC1_CH1)
}

// Greenhouse (ESP32-WROOM) defaults
namespace greenhouse_pins {
    constexpr uint8_t ADC_PINS[] = {32, 33, 34, 35};
    constexpr uint8_t RELAY_PINS[] = {16, 17, 18, 19};
    constexpr uint8_t I2C_SDA = 21;
    constexpr uint8_t I2C_SCL = 22;
    constexpr uint8_t DHT_PIN = 4;
    constexpr uint8_t STATUS_LED = 2;
}

// ============ DEFAULT VALUES ============

namespace defaults {

// WiFi defaults
constexpr uint8_t WIFI_CHANNEL = 1;

// MQTT defaults
constexpr uint16_t MQTT_PORT = 1883;
constexpr const char* MQTT_BASE_TOPIC = "iwetmyplants";
constexpr const char* HA_DISCOVERY_PREFIX = "homeassistant";
constexpr uint16_t MQTT_PUBLISH_INTERVAL_SEC = 60;

// ESP-NOW defaults
constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr uint16_t ESPNOW_SEND_INTERVAL_SEC = 300;  // 5 minutes

// Sensor defaults
constexpr uint8_t ADS1115_DEFAULT_ADDRESS = 0x48;

// Direct ADC calibration (ESP32 12-bit ADC, 0-4095 range)
constexpr uint16_t MOISTURE_DRY_VALUE = 3500;       // Typical dry ADC value (12-bit)
constexpr uint16_t MOISTURE_WET_VALUE = 1500;       // Typical wet ADC value (12-bit)

// ADS1115 calibration (16-bit scaled to 0-65535 range)
// Note: ADS1115 raw values are multiplied by 2 for full 16-bit range
constexpr uint16_t ADS1115_DRY_VALUE = 45000;       // Typical dry ADC value (ADS1115)
constexpr uint16_t ADS1115_WET_VALUE = 18000;       // Typical wet ADC value (ADS1115)

constexpr uint8_t READING_SAMPLES = 10;
constexpr uint16_t SAMPLE_DELAY_MS = 10;
constexpr uint8_t MOISTURE_WARNING_LEVEL = 30;  // Warn below 30% moisture

// Environmental sensor defaults
constexpr uint8_t SHT_DEFAULT_ADDRESS = 0x44;
constexpr uint16_t ENV_READ_INTERVAL_SEC = 60;

// Relay defaults
constexpr bool RELAY_ACTIVE_LOW = true;
constexpr uint32_t RELAY_MAX_ON_TIME_SEC = 300;     // 5 minutes safety limit
constexpr uint32_t RELAY_MIN_OFF_TIME_SEC = 60;     // 1 minute minimum off
constexpr uint32_t RELAY_COOLDOWN_SEC = 300;        // 5 minute cooldown

// Automation defaults
constexpr uint16_t AUTOMATION_DRY_THRESHOLD = 30;   // Start watering at 30%
constexpr uint16_t AUTOMATION_WET_THRESHOLD = 70;   // Stop watering at 70%
constexpr uint32_t AUTOMATION_MAX_RUNTIME_SEC = 120; // 2 minute max water time
constexpr uint32_t AUTOMATION_CHECK_INTERVAL_SEC = 60;

// Power defaults (Remote)
constexpr uint32_t DEEP_SLEEP_DURATION_SEC = 300;   // 5 minutes
constexpr uint32_t AWAKE_DURATION_MS = 5000;        // 5 seconds
constexpr float LOW_BATTERY_VOLTAGE = 3.3f;         // 3.3V threshold

} // namespace defaults

/**
 * @brief Initialize DeviceIdentity with defaults
 */
inline void initDefaultIdentity(DeviceIdentity& identity, DeviceType type) {
    memset(&identity, 0, sizeof(identity));

    switch (type) {
        case DeviceType::HUB:
            strncpy(identity.device_name, "iWetMyPlants Hub", sizeof(identity.device_name) - 1);
            break;
        case DeviceType::REMOTE:
            strncpy(identity.device_name, "iWetMyPlants Remote", sizeof(identity.device_name) - 1);
            break;
        case DeviceType::GREENHOUSE:
            strncpy(identity.device_name, "iWetMyPlants Greenhouse", sizeof(identity.device_name) - 1);
            break;
    }

    identity.device_type = static_cast<uint8_t>(type);
    strncpy(identity.firmware_version, IWMP_VERSION, sizeof(identity.firmware_version) - 1);
    // device_id will be set from MAC address during initialization
}

/**
 * @brief Initialize WifiConfig with defaults
 */
inline void initDefaultWifi(WifiConfig& wifi) {
    memset(&wifi, 0, sizeof(wifi));
    wifi.use_static_ip = false;
    wifi.wifi_channel = defaults::WIFI_CHANNEL;
    wifi.subnet = 0x00FFFFFF;  // 255.255.255.0
}

/**
 * @brief Initialize MqttConfig with defaults
 */
inline void initDefaultMqtt(MqttConfig& mqtt) {
    memset(&mqtt, 0, sizeof(mqtt));
    mqtt.enabled = false;
    mqtt.port = defaults::MQTT_PORT;
    strncpy(mqtt.base_topic, defaults::MQTT_BASE_TOPIC, sizeof(mqtt.base_topic) - 1);
    mqtt.ha_discovery_enabled = true;
    strncpy(mqtt.ha_discovery_prefix, defaults::HA_DISCOVERY_PREFIX, sizeof(mqtt.ha_discovery_prefix) - 1);
    mqtt.publish_interval_sec = defaults::MQTT_PUBLISH_INTERVAL_SEC;
}

/**
 * @brief Initialize EspNowConfig with defaults
 */
inline void initDefaultEspNow(EspNowConfig& espnow) {
    memset(&espnow, 0, sizeof(espnow));
    espnow.enabled = false;
    espnow.channel = defaults::ESPNOW_CHANNEL;
    espnow.encryption_enabled = false;
    espnow.send_interval_sec = defaults::ESPNOW_SEND_INTERVAL_SEC;
}

/**
 * @brief Initialize a single MoistureSensorConfig with defaults
 */
inline void initDefaultMoistureSensor(MoistureSensorConfig& sensor, uint8_t index, uint8_t adc_pin) {
    memset(&sensor, 0, sizeof(sensor));
    sensor.enabled = (index == 0);  // Only first sensor enabled by default
    sensor.input_type = SensorInputType::DIRECT_ADC;
    sensor.adc_pin = adc_pin;
    sensor.ads_channel = 0;
    sensor.mux_channel = 0;
    sensor.ads_i2c_address = defaults::ADS1115_DEFAULT_ADDRESS;
    sensor.dry_value = defaults::MOISTURE_DRY_VALUE;
    sensor.wet_value = defaults::MOISTURE_WET_VALUE;
    sensor.reading_samples = defaults::READING_SAMPLES;
    sensor.sample_delay_ms = defaults::SAMPLE_DELAY_MS;
    snprintf(sensor.sensor_name, sizeof(sensor.sensor_name), "Plant %d", index + 1);
    sensor.warning_level = defaults::MOISTURE_WARNING_LEVEL;
}

/**
 * @brief Initialize EnvironmentalSensorConfig with defaults
 */
inline void initDefaultEnvSensor(EnvironmentalSensorConfig& env, uint8_t dht_pin) {
    memset(&env, 0, sizeof(env));
    env.enabled = false;
    env.sensor_type = EnvSensorType::NONE;
    env.pin = dht_pin;
    env.i2c_address = defaults::SHT_DEFAULT_ADDRESS;
    env.read_interval_sec = defaults::ENV_READ_INTERVAL_SEC;
}

/**
 * @brief Initialize a single RelayConfig with defaults
 */
inline void initDefaultRelay(RelayConfig& relay, uint8_t index, uint8_t gpio_pin) {
    memset(&relay, 0, sizeof(relay));
    relay.enabled = false;
    relay.gpio_pin = gpio_pin;
    relay.active_low = defaults::RELAY_ACTIVE_LOW;
    snprintf(relay.relay_name, sizeof(relay.relay_name), "Relay %d", index + 1);
    relay.max_on_time_sec = defaults::RELAY_MAX_ON_TIME_SEC;
    relay.min_off_time_sec = defaults::RELAY_MIN_OFF_TIME_SEC;
    relay.cooldown_sec = defaults::RELAY_COOLDOWN_SEC;
}

/**
 * @brief Initialize a single SensorRelayBinding with defaults
 */
inline void initDefaultBinding(SensorRelayBinding& binding, uint8_t index) {
    memset(&binding, 0, sizeof(binding));
    binding.enabled = false;
    binding.sensor_index = index;
    binding.relay_index = index;
    binding.dry_threshold = defaults::AUTOMATION_DRY_THRESHOLD;
    binding.wet_threshold = defaults::AUTOMATION_WET_THRESHOLD;
    binding.max_runtime_sec = defaults::AUTOMATION_MAX_RUNTIME_SEC;
    binding.check_interval_sec = defaults::AUTOMATION_CHECK_INTERVAL_SEC;
    binding.hysteresis_enabled = true;
}

/**
 * @brief Initialize PowerConfig with defaults
 */
inline void initDefaultPower(PowerConfig& power) {
    memset(&power, 0, sizeof(power));
    power.battery_powered = false;
    power.deep_sleep_duration_sec = defaults::DEEP_SLEEP_DURATION_SEC;
    power.awake_duration_ms = defaults::AWAKE_DURATION_MS;
    power.wake_on_button = true;
    power.wake_button_pin = remote_pins::WAKE_BUTTON;
    power.wake_on_power_connect = true;
    power.power_detect_pin = remote_pins::POWER_DETECT;
    power.battery_adc_pin = remote_pins::BATTERY_ADC;
    power.low_battery_voltage = defaults::LOW_BATTERY_VOLTAGE;
}

/**
 * @brief Initialize entire DeviceConfig with defaults for specified device type
 */
inline void initDefaultConfig(DeviceConfig& config, DeviceType type) {
    memset(&config, 0, sizeof(config));

    // Set header
    config.magic = CONFIG_MAGIC;
    config.config_version = CONFIG_VERSION;

    // Initialize identity
    initDefaultIdentity(config.identity, type);

    // Initialize communication
    initDefaultWifi(config.wifi);
    initDefaultMqtt(config.mqtt);
    initDefaultEspNow(config.espnow);

    // Initialize sensors based on device type
    switch (type) {
        case DeviceType::HUB:
            for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
                uint8_t pin = (i < 8) ? hub_pins::ADC_PINS[i] : 0;
                initDefaultMoistureSensor(config.moisture_sensors[i], i, pin);
                // Sensors 8+ default to ADS1115 since Hub only has 8 ADC pins
                // 8-11 → ADS 0x48 ch0-3, 12-15 → ADS 0x49 ch0-3
                if (i >= 8) {
                    config.moisture_sensors[i].input_type = SensorInputType::ADS1115;
                    config.moisture_sensors[i].ads_channel = (i - 8) % 4;
                    config.moisture_sensors[i].ads_i2c_address = 0x48 + ((i - 8) / 4);
                    config.moisture_sensors[i].dry_value = defaults::ADS1115_DRY_VALUE;
                    config.moisture_sensors[i].wet_value = defaults::ADS1115_WET_VALUE;
                }
            }
            initDefaultEnvSensor(config.env_sensor, hub_pins::STATUS_LED);  // No DHT by default
            config.espnow.enabled = true;  // Hub receives ESP-NOW
            config.mqtt.enabled = true;    // Hub publishes to MQTT
            break;

        case DeviceType::REMOTE:
            initDefaultMoistureSensor(config.moisture_sensors[0], 0, remote_pins::ADC_PIN);
            initDefaultEnvSensor(config.env_sensor, 0);
            initDefaultPower(config.power);
            config.espnow.enabled = false;  // Remote uses WiFi, not ESP-NOW
            config.power.battery_powered = false;  // Default to powered mode; enable battery in config
            break;

        case DeviceType::GREENHOUSE:
            for (uint8_t i = 0; i < IWMP_MAX_SENSORS && i < 4; i++) {
                initDefaultMoistureSensor(config.moisture_sensors[i], i, greenhouse_pins::ADC_PINS[i]);
            }
            initDefaultEnvSensor(config.env_sensor, greenhouse_pins::DHT_PIN);
            for (uint8_t i = 0; i < IWMP_MAX_RELAYS; i++) {
                initDefaultRelay(config.relays[i], i, greenhouse_pins::RELAY_PINS[i]);
            }
            for (uint8_t i = 0; i < IWMP_MAX_BINDINGS; i++) {
                initDefaultBinding(config.bindings[i], i);
            }
            config.espnow.enabled = true;  // Greenhouse receives commands
            config.mqtt.enabled = true;    // Greenhouse publishes state
            break;
    }

    // CRC will be calculated when saving
    config.crc32 = 0;
}

} // namespace iwmp
