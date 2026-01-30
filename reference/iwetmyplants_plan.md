# iWetMyPlants v2.0 - Implementation Guide

## How To Use This Document

This guide consolidates everything needed to build the iWetMyPlants system from scratch. It's structured for use with Claude Code or similar AI-assisted development, with each section designed to be self-contained enough to copy into a focused development session.

**Reference Documents:**
- `iwetmyplants_spec.md` - Full technical specification (message formats, schemas, state machines)
- `iwetmyplants_library_research.md` - Detailed library analysis and code examples

---

## Part 1: Project Setup

### 1.1 Create Project Structure

```bash
mkdir iwetmyplants
cd iwetmyplants

# Create directory structure
mkdir -p shared/{config,communication,sensors,web,utils}
mkdir -p hub/src
mkdir -p remote/src  
mkdir -p greenhouse/src
mkdir -p data        # For web UI files
```

### 1.2 PlatformIO Configuration

Create `platformio.ini`:

```ini
; =============================================================================
; iWetMyPlants v2.0 - PlatformIO Configuration
; =============================================================================

[platformio]
default_envs = hub

; -----------------------------------------------------------------------------
; SHARED SETTINGS
; -----------------------------------------------------------------------------
[common]
framework = arduino
monitor_speed = 115200
upload_speed = 921600
lib_deps = 
    ; --- Communication ---
    gmag11/QuickESPNow@^0.8.1
    dawidchyrzynski/arduino-home-assistant@^2.1.0
    ; --- Configuration ---
    tzapu/WiFiManager@^2.0.17
    bblanchon/ArduinoJson@^7.0.0
    ; --- Web Server ---
    me-no-dev/ESPAsyncWebServer@^1.2.4
    me-no-dev/AsyncTCP@^1.1.1
    ; --- Sensors ---
    adafruit/Adafruit ADS1X15@^2.5.0
    adafruit/DHT sensor library@^1.4.6
    adafruit/Adafruit Unified Sensor@^1.1.14

build_flags = 
    -DCORE_DEBUG_LEVEL=3
    -DIWMP_VERSION=\"2.0.0\"

; -----------------------------------------------------------------------------
; HUB CONFIGURATION
; -----------------------------------------------------------------------------
[env:hub]
platform = espressif32
board = esp32dev
; Alternative: board = esp32-s3-devkitc-1
framework = ${common.framework}
monitor_speed = ${common.monitor_speed}
upload_speed = ${common.upload_speed}
lib_deps = ${common.lib_deps}
build_flags = 
    ${common.build_flags}
    -DIWMP_DEVICE_TYPE=0
    -DIWMP_DEVICE_TYPE_NAME=\"Hub\"
    -DIWMP_MAX_MOISTURE_SENSORS=8
    -DIWMP_MAX_RELAYS=0
    -DIWMP_HAS_ESPNOW_RX=1
    -DIWMP_HAS_ESPNOW_TX=1
    -DIWMP_HAS_WIFI=1
    -DIWMP_HAS_MQTT=1
    -DIWMP_HAS_DEVICE_REGISTRY=1
build_src_filter = +<../shared/*> +<../hub/src/*>

; -----------------------------------------------------------------------------
; REMOTE CONFIGURATION  
; -----------------------------------------------------------------------------
[env:remote]
platform = espressif32
board = esp32-c3-devkitm-1
; Alternative for SuperMini: board = seeed_xiao_esp32c3
framework = ${common.framework}
monitor_speed = ${common.monitor_speed}
upload_speed = ${common.upload_speed}
lib_deps = ${common.lib_deps}
build_flags = 
    ${common.build_flags}
    -DIWMP_DEVICE_TYPE=1
    -DIWMP_DEVICE_TYPE_NAME=\"Remote\"
    -DIWMP_MAX_MOISTURE_SENSORS=1
    -DIWMP_MAX_RELAYS=0
    -DIWMP_HAS_ESPNOW_TX=1
    -DIWMP_HAS_DEEP_SLEEP=1
    -DIWMP_HAS_BATTERY_MONITOR=1
build_src_filter = +<../shared/*> +<../remote/src/*>

; -----------------------------------------------------------------------------
; GREENHOUSE MANAGER CONFIGURATION
; -----------------------------------------------------------------------------
[env:greenhouse]
platform = espressif32
board = esp32dev
framework = ${common.framework}
monitor_speed = ${common.monitor_speed}
upload_speed = ${common.upload_speed}
lib_deps = 
    ${common.lib_deps}
    closedcube/ClosedCube SHT31D@^1.5.1
build_flags = 
    ${common.build_flags}
    -DIWMP_DEVICE_TYPE=2
    -DIWMP_DEVICE_TYPE_NAME=\"Greenhouse\"
    -DIWMP_MAX_MOISTURE_SENSORS=4
    -DIWMP_MAX_RELAYS=4
    -DIWMP_HAS_ESPNOW_RX=1
    -DIWMP_HAS_ESPNOW_TX=1
    -DIWMP_HAS_WIFI=1
    -DIWMP_HAS_MQTT=1
    -DIWMP_HAS_AUTOMATION=1
    -DIWMP_HAS_ENV_SENSOR=1
build_src_filter = +<../shared/*> +<../greenhouse/src/*>
```

---

## Part 2: Implementation Sessions

The project is broken into focused development sessions. Each session should result in working, testable code before moving on.

### Session Overview

| Session | Component | Depends On | Deliverable |
|---------|-----------|------------|-------------|
| 1 | Configuration System | None | NVS storage working, load/save config |
| 2 | WiFi + Captive Portal | Session 1 | Device configurable via browser |
| 3 | MQTT + Home Assistant | Sessions 1-2 | Entities appear in HA |
| 4 | Sensor Abstraction | Session 1 | Read moisture from ADC or ADS1115 |
| 5 | Hub Core | Sessions 1-4 | Hub publishes local sensors to HA |
| 6 | ESP-NOW Communication | Session 5 | Hub receives from test transmitter |
| 7 | Remote Core | Sessions 1, 4, 6 | Remote sends to Hub |
| 8 | Deep Sleep | Session 7 | Remote achieves <10µA sleep |
| 9 | Relay Control | Sessions 1-3 | Switch entities control relays |
| 10 | Automation Engine | Session 9 | Sensor thresholds trigger relays |
| 11 | Calibration UI | Sessions 4, 5 | Web-based sensor calibration |
| 12 | Safety & Polish | All | Timeouts, watchdog, error handling |

---

## Session 1: Configuration System

**Goal:** Create a robust NVS-based configuration system with defaults and validation.

### Files to Create:

**shared/config/config_schema.h**
- Copy structures from spec (DeviceIdentity, WifiConfig, MqttConfig, EspNowConfig, MoistureSensorConfig, etc.)
- Include compile-time flags for conditional compilation

**shared/config/defaults.h**
```cpp
#pragma once
#include "config_schema.h"

namespace IWMP {
namespace Defaults {

constexpr char DEVICE_NAME[] = "iWetMyPlants";
constexpr char MQTT_BASE_TOPIC[] = "iwetmyplants";
constexpr char HA_DISCOVERY_PREFIX[] = "homeassistant";
constexpr uint16_t MQTT_PORT = 1883;
constexpr uint16_t MQTT_PUBLISH_INTERVAL_SEC = 60;
constexpr uint16_t ESPNOW_SEND_INTERVAL_SEC = 60;
constexpr uint32_t DEEP_SLEEP_DURATION_SEC = 300;  // 5 minutes

// Sensor defaults
constexpr uint16_t MOISTURE_DRY_DEFAULT = 3000;   // Typical dry ADC value
constexpr uint16_t MOISTURE_WET_DEFAULT = 1200;   // Typical wet ADC value
constexpr uint8_t MOISTURE_SAMPLES = 10;
constexpr uint16_t MOISTURE_SAMPLE_DELAY_MS = 50;

// Safety defaults
constexpr uint32_t RELAY_MAX_ON_TIME_SEC = 300;      // 5 minutes max
constexpr uint32_t RELAY_MIN_OFF_TIME_SEC = 60;      // 1 minute between runs
constexpr uint32_t RELAY_COOLDOWN_SEC = 300;         // 5 minutes after run

} // namespace Defaults
} // namespace IWMP
```

**shared/config/config_manager.h**
```cpp
#pragma once
#include <Preferences.h>
#include "config_schema.h"

namespace IWMP {

class ConfigManager {
public:
    static ConfigManager& getInstance();
    
    bool begin();
    bool load();
    bool save();
    bool factoryReset();
    
    // Accessors
    DeviceConfig& getConfig() { return _config; }
    const DeviceConfig& getConfig() const { return _config; }
    
    // Individual section saves (for partial updates)
    bool saveWifi();
    bool saveMqtt();
    bool saveEspNow();
    bool saveSensor(uint8_t index);
    bool saveRelay(uint8_t index);
    
    // Validation
    bool isValid() const;
    uint32_t calculateCRC() const;
    
    // Device ID generation
    void generateDeviceId();
    
private:
    ConfigManager() = default;
    DeviceConfig _config;
    Preferences _prefs;
    bool _loaded = false;
    
    void applyDefaults();
    void migrate(uint32_t fromVersion);
};

} // namespace IWMP
```

**shared/config/config_manager.cpp**
```cpp
#include "config_manager.h"
#include "defaults.h"
#include <WiFi.h>
#include <esp_crc.h>

namespace IWMP {

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::begin() {
    return _prefs.begin("iwmp", false);
}

bool ConfigManager::load() {
    _prefs.begin("iwmp", true);  // Read-only
    
    uint32_t storedVersion = _prefs.getUInt("cfg_ver", 0);
    
    if (storedVersion == 0) {
        // First boot or factory reset
        _prefs.end();
        applyDefaults();
        generateDeviceId();
        _loaded = true;
        return save();
    }
    
    // Load identity
    _prefs.getBytes("identity", &_config.identity, sizeof(DeviceIdentity));
    
    // Load WiFi
    _prefs.getBytes("wifi", &_config.wifi, sizeof(WifiConfig));
    
    // Load MQTT
    _prefs.getBytes("mqtt", &_config.mqtt, sizeof(MqttConfig));
    
    // Load ESP-NOW
    _prefs.getBytes("espnow", &_config.espnow, sizeof(EspNowConfig));
    
    // Load sensors
    for (int i = 0; i < IWMP_MAX_MOISTURE_SENSORS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "moisture_%d", i);
        _prefs.getBytes(key, &_config.moisture_sensors[i], sizeof(MoistureSensorConfig));
    }
    
    // Load environmental sensor
    _prefs.getBytes("env", &_config.env_sensor, sizeof(EnvironmentalSensorConfig));
    
    // Load relays
    for (int i = 0; i < IWMP_MAX_RELAYS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "relay_%d", i);
        _prefs.getBytes(key, &_config.relays[i], sizeof(RelayConfig));
    }
    
    // Load automation bindings
    for (int i = 0; i < 4; i++) {
        char key[16];
        snprintf(key, sizeof(key), "bind_%d", i);
        _prefs.getBytes(key, &_config.bindings[i], sizeof(SensorRelayBinding));
    }
    
    // Load power config (Remote only)
    #if IWMP_HAS_DEEP_SLEEP
    _prefs.getBytes("power", &_config.power, sizeof(PowerConfig));
    #endif
    
    _prefs.end();
    
    // Handle migration if needed
    if (storedVersion < CONFIG_VERSION) {
        migrate(storedVersion);
    }
    
    // Verify CRC
    _config.crc32 = calculateCRC();
    _loaded = true;
    
    return true;
}

bool ConfigManager::save() {
    _prefs.begin("iwmp", false);  // Read-write
    
    _config.config_version = CONFIG_VERSION;
    _config.crc32 = calculateCRC();
    
    _prefs.putUInt("cfg_ver", _config.config_version);
    _prefs.putBytes("identity", &_config.identity, sizeof(DeviceIdentity));
    _prefs.putBytes("wifi", &_config.wifi, sizeof(WifiConfig));
    _prefs.putBytes("mqtt", &_config.mqtt, sizeof(MqttConfig));
    _prefs.putBytes("espnow", &_config.espnow, sizeof(EspNowConfig));
    
    for (int i = 0; i < IWMP_MAX_MOISTURE_SENSORS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "moisture_%d", i);
        _prefs.putBytes(key, &_config.moisture_sensors[i], sizeof(MoistureSensorConfig));
    }
    
    _prefs.putBytes("env", &_config.env_sensor, sizeof(EnvironmentalSensorConfig));
    
    for (int i = 0; i < IWMP_MAX_RELAYS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "relay_%d", i);
        _prefs.putBytes(key, &_config.relays[i], sizeof(RelayConfig));
    }
    
    for (int i = 0; i < 4; i++) {
        char key[16];
        snprintf(key, sizeof(key), "bind_%d", i);
        _prefs.putBytes(key, &_config.bindings[i], sizeof(SensorRelayBinding));
    }
    
    #if IWMP_HAS_DEEP_SLEEP
    _prefs.putBytes("power", &_config.power, sizeof(PowerConfig));
    #endif
    
    _prefs.end();
    return true;
}

void ConfigManager::applyDefaults() {
    memset(&_config, 0, sizeof(DeviceConfig));
    
    // Identity
    strlcpy(_config.identity.device_name, Defaults::DEVICE_NAME, sizeof(_config.identity.device_name));
    _config.identity.device_type = IWMP_DEVICE_TYPE;
    strlcpy(_config.identity.firmware_version, IWMP_VERSION, sizeof(_config.identity.firmware_version));
    
    // MQTT
    _config.mqtt.port = Defaults::MQTT_PORT;
    strlcpy(_config.mqtt.base_topic, Defaults::MQTT_BASE_TOPIC, sizeof(_config.mqtt.base_topic));
    strlcpy(_config.mqtt.ha_discovery_prefix, Defaults::HA_DISCOVERY_PREFIX, sizeof(_config.mqtt.ha_discovery_prefix));
    _config.mqtt.publish_interval_sec = Defaults::MQTT_PUBLISH_INTERVAL_SEC;
    _config.mqtt.ha_discovery_enabled = true;
    
    // ESP-NOW
    _config.espnow.send_interval_sec = Defaults::ESPNOW_SEND_INTERVAL_SEC;
    _config.espnow.channel = 1;
    
    // First moisture sensor enabled by default
    _config.moisture_sensors[0].enabled = true;
    _config.moisture_sensors[0].input_type = SensorInputType::DIRECT_ADC;
    _config.moisture_sensors[0].adc_pin = 32;  // GPIO32 (ADC1)
    _config.moisture_sensors[0].dry_value = Defaults::MOISTURE_DRY_DEFAULT;
    _config.moisture_sensors[0].wet_value = Defaults::MOISTURE_WET_DEFAULT;
    _config.moisture_sensors[0].reading_samples = Defaults::MOISTURE_SAMPLES;
    _config.moisture_sensors[0].sample_delay_ms = Defaults::MOISTURE_SAMPLE_DELAY_MS;
    strlcpy(_config.moisture_sensors[0].sensor_name, "Moisture 1", sizeof(_config.moisture_sensors[0].sensor_name));
    
    // Relay defaults (Greenhouse)
    #if IWMP_MAX_RELAYS > 0
    for (int i = 0; i < IWMP_MAX_RELAYS; i++) {
        _config.relays[i].active_low = true;  // Most modules are active-low
        _config.relays[i].max_on_time_sec = Defaults::RELAY_MAX_ON_TIME_SEC;
        _config.relays[i].min_off_time_sec = Defaults::RELAY_MIN_OFF_TIME_SEC;
        _config.relays[i].cooldown_sec = Defaults::RELAY_COOLDOWN_SEC;
    }
    #endif
    
    // Power defaults (Remote)
    #if IWMP_HAS_DEEP_SLEEP
    _config.power.deep_sleep_duration_sec = Defaults::DEEP_SLEEP_DURATION_SEC;
    _config.power.low_battery_voltage = 3.3f;
    #endif
}

void ConfigManager::generateDeviceId() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(_config.identity.device_id, sizeof(_config.identity.device_id),
             "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

uint32_t ConfigManager::calculateCRC() const {
    // Calculate CRC32 over config (excluding the crc32 field itself)
    size_t size = sizeof(DeviceConfig) - sizeof(uint32_t);
    return esp_crc32_le(0, (const uint8_t*)&_config, size);
}

bool ConfigManager::factoryReset() {
    _prefs.begin("iwmp", false);
    _prefs.clear();
    _prefs.end();
    
    applyDefaults();
    generateDeviceId();
    return save();
}

} // namespace IWMP
```

### Test Sketch for Session 1

```cpp
// test_config.cpp
#include <Arduino.h>
#include "shared/config/config_manager.h"

using namespace IWMP;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== Config Manager Test ===\n");
    
    ConfigManager& config = ConfigManager::getInstance();
    
    if (!config.begin()) {
        Serial.println("ERROR: Failed to begin config");
        return;
    }
    
    Serial.println("Loading configuration...");
    if (!config.load()) {
        Serial.println("ERROR: Failed to load config");
        return;
    }
    
    // Print loaded values
    DeviceConfig& cfg = config.getConfig();
    
    Serial.printf("Device Name: %s\n", cfg.identity.device_name);
    Serial.printf("Device ID: %s\n", cfg.identity.device_id);
    Serial.printf("Device Type: %d\n", cfg.identity.device_type);
    Serial.printf("Firmware: %s\n", cfg.identity.firmware_version);
    Serial.printf("MQTT Broker: %s:%d\n", cfg.mqtt.broker, cfg.mqtt.port);
    Serial.printf("Moisture Sensor 0 Enabled: %s\n", cfg.moisture_sensors[0].enabled ? "Yes" : "No");
    Serial.printf("Moisture Dry Value: %d\n", cfg.moisture_sensors[0].dry_value);
    Serial.printf("Moisture Wet Value: %d\n", cfg.moisture_sensors[0].wet_value);
    Serial.printf("Config CRC: 0x%08X\n", cfg.crc32);
    
    // Test save
    strlcpy(cfg.mqtt.broker, "192.168.1.100", sizeof(cfg.mqtt.broker));
    config.save();
    Serial.println("\nSaved new MQTT broker");
    
    Serial.println("\n=== Test Complete ===");
}

void loop() {
    delay(1000);
}
```

---

## Session 2: WiFi + Captive Portal

**Goal:** Implement WiFiManager with custom parameters for MQTT and device settings.

### Key Integration Points

**Using WiFiManager Library:**
```cpp
#include <WiFiManager.h>

class WifiSetup {
public:
    bool begin() {
        WiFiManager wm;
        
        // Custom parameters
        WiFiManagerParameter mqtt_server("mqtt_server", "MQTT Server", 
            config.mqtt.broker, sizeof(config.mqtt.broker));
        WiFiManagerParameter mqtt_port("mqtt_port", "MQTT Port", 
            String(config.mqtt.port).c_str(), 6);
        WiFiManagerParameter device_name("device_name", "Device Name",
            config.identity.device_name, sizeof(config.identity.device_name));
        
        wm.addParameter(&mqtt_server);
        wm.addParameter(&mqtt_port);
        wm.addParameter(&device_name);
        
        // Configure portal
        wm.setConfigPortalTimeout(180);  // 3 minutes
        wm.setAPCallback(configModeCallback);
        wm.setSaveConfigCallback(saveConfigCallback);
        
        // Attempt connection
        String apName = String("iWetMyPlants-") + config.identity.device_id;
        if (!wm.autoConnect(apName.c_str(), "moisture123")) {
            return false;
        }
        
        // Save parameters if changed
        if (_shouldSave) {
            strlcpy(config.mqtt.broker, mqtt_server.getValue(), sizeof(config.mqtt.broker));
            config.mqtt.port = String(mqtt_port.getValue()).toInt();
            strlcpy(config.identity.device_name, device_name.getValue(), 
                    sizeof(config.identity.device_name));
            ConfigManager::getInstance().save();
        }
        
        return true;
    }
    
private:
    static bool _shouldSave;
    
    static void configModeCallback(WiFiManager* wm) {
        Serial.println("Entered config mode");
        Serial.println(WiFi.softAPIP());
    }
    
    static void saveConfigCallback() {
        _shouldSave = true;
    }
};
```

### Files to Create:
- `shared/communication/wifi_setup.h/.cpp` - WiFiManager wrapper
- Extend config_manager to handle WiFi credentials

---

## Session 3: MQTT + Home Assistant

**Goal:** Implement ArduinoHA for automatic Home Assistant discovery.

### Using ArduinoHA Library

**Key Pattern - Hub with Multiple Sensors:**
```cpp
#include <WiFi.h>
#include <ArduinoHA.h>

WiFiClient client;
HADevice device;        // Will be initialized with device ID
HAMqtt mqtt(client, device);

// Entity declarations (Hub example with 4 sensors + 4 relays possible)
HASensorNumber* moistureSensors[IWMP_MAX_MOISTURE_SENSORS];
HASensorNumber* tempSensor = nullptr;
HASensorNumber* humiditySensor = nullptr;
HASensorNumber* batterySensor = nullptr;
HASwitch* relaySwitches[IWMP_MAX_RELAYS];

class MqttManager {
public:
    bool begin(const char* broker, uint16_t port, 
               const char* user, const char* pass) {
        
        // Setup device
        device.setUniqueId((const byte*)config.identity.device_id, 12);
        device.setName(config.identity.device_name);
        device.setManufacturer("Spirit Wrestler Woodcraft");
        device.setModel(IWMP_DEVICE_TYPE_NAME);
        device.setSoftwareVersion(IWMP_VERSION);
        
        // Create moisture sensor entities
        for (int i = 0; i < IWMP_MAX_MOISTURE_SENSORS; i++) {
            if (config.moisture_sensors[i].enabled) {
                String id = String("moisture_") + String(i);
                moistureSensors[i] = new HASensorNumber(id.c_str(), 
                    HASensorNumber::PrecisionP1);
                moistureSensors[i]->setName(config.moisture_sensors[i].sensor_name);
                moistureSensors[i]->setUnitOfMeasurement("%");
                moistureSensors[i]->setDeviceClass("moisture");
                moistureSensors[i]->setIcon("mdi:water-percent");
            }
        }
        
        // Create relay switches (Greenhouse only)
        #if IWMP_MAX_RELAYS > 0
        for (int i = 0; i < IWMP_MAX_RELAYS; i++) {
            if (config.relays[i].enabled) {
                String id = String("relay_") + String(i);
                relaySwitches[i] = new HASwitch(id.c_str());
                relaySwitches[i]->setName(config.relays[i].relay_name);
                relaySwitches[i]->setIcon("mdi:water-pump");
                relaySwitches[i]->onCommand(onRelayCommand);
            }
        }
        #endif
        
        // Connect
        mqtt.begin(broker, port, user, pass);
        
        return true;
    }
    
    void loop() {
        mqtt.loop();
    }
    
    void publishMoisture(uint8_t index, float percent) {
        if (moistureSensors[index]) {
            moistureSensors[index]->setValue(percent);
        }
    }
    
    void publishTemperature(float temp) {
        if (tempSensor) {
            tempSensor->setValue(temp);
        }
    }
    
    void setRelayState(uint8_t index, bool state) {
        if (relaySwitches[index]) {
            relaySwitches[index]->setState(state);
        }
    }
    
private:
    static void onRelayCommand(bool state, HASwitch* sender) {
        // Find which relay this is
        for (int i = 0; i < IWMP_MAX_RELAYS; i++) {
            if (relaySwitches[i] == sender) {
                // Call relay manager to actually switch
                RelayManager::getInstance().setRelay(i, state);
                // Confirm state back to HA
                sender->setState(state);
                break;
            }
        }
    }
};
```

**What ArduinoHA Handles Automatically:**
- Discovery JSON payloads
- Topic structure (homeassistant/sensor/device_id/moisture_0/config)
- State publishing
- Availability (LWT)
- Command subscriptions for switches
- Device grouping in HA UI

---

## Session 4: Sensor Abstraction

**Goal:** Create a unified interface for reading moisture regardless of hardware (ADC, ADS1115, Mux).

### Sensor Interface Pattern

```cpp
// shared/sensors/sensor_interface.h
#pragma once
#include <Arduino.h>

namespace IWMP {

class ISensorInput {
public:
    virtual ~ISensorInput() = default;
    virtual bool begin() = 0;
    virtual uint16_t readRaw() = 0;
    virtual uint16_t getMaxValue() = 0;
    virtual const char* getTypeName() = 0;
};

class MoistureSensor {
public:
    MoistureSensor(ISensorInput* input, uint16_t dryVal, uint16_t wetVal);
    
    bool begin();
    float readPercent();
    uint16_t readRaw();
    uint16_t readAveraged(uint8_t samples, uint16_t delayMs);
    
    void calibrateDry(uint16_t value);
    void calibrateWet(uint16_t value);
    
private:
    ISensorInput* _input;
    uint16_t _dryValue;
    uint16_t _wetValue;
};

// Factory function
ISensorInput* createSensorInput(const MoistureSensorConfig& config);

} // namespace IWMP
```

### Implementations

**Direct ADC:**
```cpp
class DirectAdcInput : public ISensorInput {
public:
    DirectAdcInput(uint8_t pin) : _pin(pin) {}
    
    bool begin() override { 
        analogReadResolution(12);  // 12-bit
        analogSetAttenuation(ADC_11db);  // Full 3.3V range
        return true; 
    }
    
    uint16_t readRaw() override { 
        return analogRead(_pin); 
    }
    
    uint16_t getMaxValue() override { return 4095; }
    const char* getTypeName() override { return "DirectADC"; }
    
private:
    uint8_t _pin;
};
```

**ADS1115 (16-bit external ADC):**
```cpp
#include <Adafruit_ADS1X15.h>

class Ads1115Input : public ISensorInput {
public:
    Ads1115Input(uint8_t channel, uint8_t address = 0x48) 
        : _channel(channel), _address(address) {}
    
    bool begin() override {
        if (!_ads.begin(_address)) {
            return false;
        }
        _ads.setGain(GAIN_ONE);  // ±4.096V
        return true;
    }
    
    uint16_t readRaw() override {
        return _ads.readADC_SingleEnded(_channel);
    }
    
    uint16_t getMaxValue() override { return 32767; }  // 15-bit positive
    const char* getTypeName() override { return "ADS1115"; }
    
private:
    Adafruit_ADS1115 _ads;
    uint8_t _channel;
    uint8_t _address;
};
```

**Factory:**
```cpp
ISensorInput* createSensorInput(const MoistureSensorConfig& config) {
    switch (config.input_type) {
        case SensorInputType::DIRECT_ADC:
            return new DirectAdcInput(config.adc_pin);
        case SensorInputType::ADS1115:
            return new Ads1115Input(config.ads_channel, config.ads_i2c_address);
        case SensorInputType::MUX_CD74HC4067:
            return new MuxInput(config.mux_channel, config.adc_pin);
        default:
            return nullptr;
    }
}
```

---

## Session 5: Hub Core

**Goal:** Create the Hub controller that reads local sensors and publishes to MQTT.

### Hub State Machine

```
BOOT → LOAD_CONFIG → WIFI_CONNECT → MQTT_CONNECT → OPERATIONAL
                ↓                                       ↑
         (no config)                              (normal loop)
                ↓                                       │
          AP_MODE (captive portal) ─────────────────────┘
```

### Hub Controller Structure

```cpp
// hub/src/hub_controller.h
#pragma once

namespace IWMP {

enum class HubState {
    BOOT,
    LOAD_CONFIG,
    WIFI_CONNECTING,
    MQTT_CONNECTING,
    OPERATIONAL,
    AP_MODE,
    ERROR
};

class HubController {
public:
    static HubController& getInstance();
    
    void begin();
    void loop();
    
    HubState getState() const { return _state; }
    
private:
    HubState _state = HubState::BOOT;
    
    ConfigManager& _config;
    WifiSetup _wifi;
    MqttManager _mqtt;
    MoistureSensor* _sensors[IWMP_MAX_MOISTURE_SENSORS];
    DeviceRegistry _registry;  // For tracking Remotes
    
    uint32_t _lastPublish = 0;
    uint32_t _lastSensorRead = 0;
    
    void stateBoot();
    void stateLoadConfig();
    void stateWifiConnecting();
    void stateMqttConnecting();
    void stateOperational();
    void stateApMode();
    
    void readLocalSensors();
    void publishSensorData();
    void checkResetButton();
};

} // namespace IWMP
```

---

## Session 6: ESP-NOW Communication

**Goal:** Hub receives messages from Remotes via ESP-NOW.

### Using QuickESPNow

```cpp
#include <QuickEspNow.h>

class EspNowManager {
public:
    bool begin(uint8_t channel = 1) {
        quickEspNow.begin(channel);
        quickEspNow.onDataRcvd(onDataReceived);
        return true;
    }
    
    // Send with automatic retry and blocking confirmation
    bool sendWithConfirm(const uint8_t* mac, const uint8_t* data, size_t len) {
        return quickEspNow.send(mac, data, len, true) == 0;  // true = blocking
    }
    
    // Broadcast to all
    bool broadcast(const uint8_t* data, size_t len) {
        return quickEspNow.send(ESPNOW_BROADCAST_ADDRESS, data, len) == 0;
    }
    
    void setReceiveCallback(void (*callback)(const uint8_t*, const uint8_t*, size_t, int)) {
        _userCallback = callback;
    }
    
private:
    static void (*_userCallback)(const uint8_t*, const uint8_t*, size_t, int);
    
    static void onDataReceived(uint8_t* senderMac, uint8_t* data, 
                                uint8_t len, signed int rssi) {
        // Parse message header
        if (len < sizeof(MessageHeader)) return;
        
        MessageHeader* header = (MessageHeader*)data;
        
        // Deduplication check
        if (isDuplicate(senderMac, header->sequence_number)) {
            return;
        }
        
        // Forward to user callback
        if (_userCallback) {
            _userCallback(senderMac, data, len, rssi);
        }
    }
    
    static bool isDuplicate(const uint8_t* mac, uint8_t seq);
};
```

### Hub Message Handler

```cpp
void HubController::onEspNowMessage(const uint8_t* mac, const uint8_t* data, 
                                     size_t len, int rssi) {
    MessageHeader* header = (MessageHeader*)data;
    
    // Update device registry
    _registry.updateDevice(mac, rssi);
    
    switch (header->type) {
        case MessageType::MOISTURE_READING: {
            MoistureReadingMsg* msg = (MoistureReadingMsg*)data;
            handleMoistureReading(mac, msg);
            break;
        }
        case MessageType::BATTERY_STATUS: {
            BatteryStatusMsg* msg = (BatteryStatusMsg*)data;
            handleBatteryStatus(mac, msg);
            break;
        }
        case MessageType::ANNOUNCE: {
            AnnounceMsg* msg = (AnnounceMsg*)data;
            handleAnnounce(mac, msg);
            break;
        }
        // ... other message types
    }
}
```

---

## Session 7: Remote Core

**Goal:** Remote reads sensor and transmits via ESP-NOW.

### Remote State Machine (Powered Mode)

```
BOOT → CHECK_WAKE_REASON
       │
       ├─ Timer Wake ───→ QUICK_READ (sensor, send, sleep)
       │
       ├─ Button Wake ──→ CONFIG_MODE (AP + web server)
       │
       └─ USB Power ────→ POWERED_MODE (continuous MQTT + WiFi)
```

### Remote Controller

```cpp
class RemoteController {
public:
    void begin() {
        _config = ConfigManager::getInstance();
        _config.begin();
        _config.load();
        
        // Check wake reason
        esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
        
        switch (wakeReason) {
            case ESP_SLEEP_WAKEUP_TIMER:
                _mode = RemoteMode::QUICK_READ;
                break;
            case ESP_SLEEP_WAKEUP_EXT0:
                _mode = RemoteMode::CONFIG_MODE;
                break;
            default:
                // Cold boot or USB power
                if (detectUsbPower()) {
                    _mode = RemoteMode::POWERED;
                } else {
                    _mode = RemoteMode::QUICK_READ;
                }
        }
    }
    
    void loop() {
        switch (_mode) {
            case RemoteMode::QUICK_READ:
                quickRead();
                enterDeepSleep();
                break;
            case RemoteMode::CONFIG_MODE:
                runConfigMode();
                break;
            case RemoteMode::POWERED:
                runPoweredMode();
                break;
        }
    }
    
private:
    void quickRead() {
        // Initialize ESP-NOW only (faster than full WiFi)
        _espnow.begin(_config.getConfig().espnow.channel);
        
        // Read sensor
        _sensor = createSensorInput(_config.getConfig().moisture_sensors[0]);
        _sensor->begin();
        
        uint16_t raw = readAveraged(10, 50);
        float percent = calculatePercent(raw);
        
        // Build message
        MoistureReadingMsg msg;
        msg.header.protocol_version = 1;
        msg.header.type = MessageType::MOISTURE_READING;
        WiFi.macAddress(msg.header.sender_mac);
        msg.header.sequence_number = _sequenceNum++;
        msg.header.flags = 0x01;  // Requires ACK
        msg.header.timestamp = millis();
        msg.sensor_index = 0;
        msg.raw_value = raw;
        msg.moisture_percent = (uint8_t)percent;
        
        // Send with retry
        bool success = sendWithRetry((uint8_t*)&msg, sizeof(msg), 3);
        
        if (!success) {
            _consecutiveFailures++;
        } else {
            _consecutiveFailures = 0;
        }
        
        // Track in RTC memory
        RTC_DATA_ATTR static uint32_t bootCount;
        bootCount++;
    }
    
    bool sendWithRetry(uint8_t* data, size_t len, int maxAttempts) {
        for (int i = 0; i < maxAttempts; i++) {
            if (_espnow.sendWithConfirm(_config.getConfig().espnow.hub_mac, data, len)) {
                return true;
            }
            delay(50);
        }
        return false;
    }
};
```

---

## Session 8: Deep Sleep

**Goal:** Achieve <10µA sleep current on Remote.

### RTC Memory Usage

```cpp
// Data that persists across deep sleep (uses RTC slow memory)
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR uint32_t lastSuccessfulSend = 0;
RTC_DATA_ATTR uint8_t consecutiveFailures = 0;
RTC_DATA_ATTR uint8_t sequenceNumber = 0;
```

### Deep Sleep Implementation

```cpp
void enterDeepSleep() {
    // Calculate sleep duration
    uint32_t sleepSec = _config.getConfig().power.deep_sleep_duration_sec;
    
    // Adaptive sleep: back off if failing
    if (consecutiveFailures > 3) {
        sleepSec *= 2;  // Double sleep time if connection problems
    }
    
    Serial.printf("Sleeping for %d seconds...\n", sleepSec);
    Serial.flush();
    
    // Configure wake sources
    esp_sleep_enable_timer_wakeup(sleepSec * 1000000ULL);
    
    // Optional: wake on button press (GPIO wakeup)
    #if defined(WAKE_BUTTON_PIN)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_BUTTON_PIN, 0);  // Wake on LOW
    #endif
    
    // Optional: wake on USB power detect
    #if defined(POWER_DETECT_PIN)
    esp_sleep_enable_ext1_wakeup(1ULL << POWER_DETECT_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
    #endif
    
    // Enter deep sleep
    esp_deep_sleep_start();
    
    // Never reaches here
}
```

---

## Session 9: Relay Control

**Goal:** Safe relay control with timeouts and safety limits.

### Relay Manager with Safety

```cpp
class RelayManager {
public:
    static RelayManager& getInstance();
    
    bool begin();
    
    bool turnOn(uint8_t index, uint32_t maxDurationSec = 0);
    bool turnOff(uint8_t index);
    bool isOn(uint8_t index) const;
    
    void loop();  // Call in main loop to enforce timeouts
    void emergencyStopAll();
    
    // Statistics
    uint32_t getTodayRuntime(uint8_t index) const;
    uint32_t getActivationCount(uint8_t index) const;
    
private:
    struct RelayState {
        bool currentState = false;
        uint32_t lastOnTime = 0;
        uint32_t lastOffTime = 0;
        uint32_t totalOnTimeToday = 0;
        uint32_t activationCount = 0;
        uint32_t maxOnExpiry = 0;  // When to auto-shutoff
        bool lockedOut = false;
        char lockoutReason[32] = {0};
    };
    
    RelayState _states[IWMP_MAX_RELAYS];
    
    bool checkSafety(uint8_t index);
    void enforceSafetyLimits();
    void resetDailyCounters();
};

bool RelayManager::turnOn(uint8_t index, uint32_t maxDurationSec) {
    if (index >= IWMP_MAX_RELAYS) return false;
    
    const RelayConfig& cfg = ConfigManager::getInstance().getConfig().relays[index];
    RelayState& state = _states[index];
    
    // Safety checks
    if (!checkSafety(index)) {
        return false;
    }
    
    // Calculate expiry time
    uint32_t maxDuration = maxDurationSec;
    if (maxDuration == 0 || maxDuration > cfg.max_on_time_sec) {
        maxDuration = cfg.max_on_time_sec;
    }
    
    state.maxOnExpiry = millis() + (maxDuration * 1000);
    
    // Activate relay
    digitalWrite(cfg.gpio_pin, cfg.active_low ? LOW : HIGH);
    state.currentState = true;
    state.lastOnTime = millis();
    state.activationCount++;
    
    Serial.printf("Relay %d ON (max %d sec)\n", index, maxDuration);
    
    return true;
}

bool RelayManager::checkSafety(uint8_t index) {
    const RelayConfig& cfg = ConfigManager::getInstance().getConfig().relays[index];
    RelayState& state = _states[index];
    uint32_t now = millis();
    
    // Check lockout
    if (state.lockedOut) {
        Serial.printf("Relay %d locked out: %s\n", index, state.lockoutReason);
        return false;
    }
    
    // Check minimum off time
    if (state.lastOffTime > 0) {
        uint32_t offDuration = (now - state.lastOffTime) / 1000;
        if (offDuration < cfg.min_off_time_sec) {
            Serial.printf("Relay %d still in min-off period (%d/%d sec)\n", 
                          index, offDuration, cfg.min_off_time_sec);
            return false;
        }
    }
    
    // Check cooldown
    if (state.lastOffTime > 0) {
        uint32_t cooldownRemaining = cfg.cooldown_sec - ((now - state.lastOffTime) / 1000);
        if (cooldownRemaining > 0 && cooldownRemaining < cfg.cooldown_sec) {
            Serial.printf("Relay %d in cooldown (%d sec remaining)\n", 
                          index, cooldownRemaining);
            return false;
        }
    }
    
    // Check daily runtime limit (optional - prevent runaway)
    const uint32_t MAX_DAILY_RUNTIME_SEC = 3600;  // 1 hour max per day
    if (state.totalOnTimeToday > MAX_DAILY_RUNTIME_SEC) {
        state.lockedOut = true;
        strlcpy(state.lockoutReason, "Daily limit exceeded", sizeof(state.lockoutReason));
        return false;
    }
    
    return true;
}

void RelayManager::loop() {
    uint32_t now = millis();
    
    for (int i = 0; i < IWMP_MAX_RELAYS; i++) {
        RelayState& state = _states[i];
        const RelayConfig& cfg = ConfigManager::getInstance().getConfig().relays[i];
        
        if (state.currentState) {
            // Update runtime tracking
            // ...
            
            // Check timeout
            if (state.maxOnExpiry > 0 && now >= state.maxOnExpiry) {
                Serial.printf("Relay %d timeout - forcing OFF\n", i);
                turnOff(i);
            }
        }
    }
}
```

---

## Session 10: Automation Engine

**Goal:** Sensor thresholds automatically trigger relays.

### Automation Engine

```cpp
class AutomationEngine {
public:
    void begin();
    void loop();
    
private:
    struct BindingState {
        bool currentlyWatering = false;
        uint32_t wateringStartTime = 0;
        float lastMoistureReading = 0;
        uint32_t lastCheckTime = 0;
    };
    
    BindingState _bindingStates[4];
    
    void evaluateBinding(uint8_t index);
};

void AutomationEngine::loop() {
    const DeviceConfig& cfg = ConfigManager::getInstance().getConfig();
    uint32_t now = millis();
    
    for (int i = 0; i < 4; i++) {
        const SensorRelayBinding& binding = cfg.bindings[i];
        BindingState& state = _bindingStates[i];
        
        if (!binding.enabled) continue;
        
        // Check interval
        if (now - state.lastCheckTime < binding.check_interval_sec * 1000) {
            continue;
        }
        state.lastCheckTime = now;
        
        evaluateBinding(i);
    }
}

void AutomationEngine::evaluateBinding(uint8_t index) {
    const SensorRelayBinding& binding = 
        ConfigManager::getInstance().getConfig().bindings[index];
    BindingState& state = _bindingStates[index];
    
    // Get current moisture
    float moisture = SensorManager::getInstance()
        .getMoisturePercent(binding.sensor_index);
    state.lastMoistureReading = moisture;
    
    RelayManager& relays = RelayManager::getInstance();
    
    if (state.currentlyWatering) {
        // Check if we should stop
        bool shouldStop = false;
        
        // Stop if moisture reached wet threshold
        if (moisture >= binding.wet_threshold) {
            Serial.printf("Binding %d: Moisture reached %0.1f%% (target %d%%) - stopping\n",
                          index, moisture, binding.wet_threshold);
            shouldStop = true;
        }
        
        // Stop if max runtime exceeded
        uint32_t runtime = (millis() - state.wateringStartTime) / 1000;
        if (runtime >= binding.max_runtime_sec) {
            Serial.printf("Binding %d: Max runtime reached (%d sec) - stopping\n",
                          index, runtime);
            shouldStop = true;
        }
        
        if (shouldStop) {
            relays.turnOff(binding.relay_index);
            state.currentlyWatering = false;
        }
        
    } else {
        // Check if we should start watering
        if (moisture <= binding.dry_threshold) {
            // Apply hysteresis - don't start if we just stopped
            // (handled by RelayManager cooldown)
            
            Serial.printf("Binding %d: Moisture at %0.1f%% (threshold %d%%) - starting water\n",
                          index, moisture, binding.dry_threshold);
            
            if (relays.turnOn(binding.relay_index, binding.max_runtime_sec)) {
                state.currentlyWatering = true;
                state.wateringStartTime = millis();
            }
        }
    }
}
```

---

## Session 11: Calibration UI

**Goal:** Web-based real-time calibration interface.

### WebSocket-Based Rapid Reading

```cpp
// In web_server setup
AsyncWebSocket ws("/ws/calibrate");

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        // Start rapid reading mode
        startRapidRead(client->id());
    } else if (type == WS_EVT_DISCONNECT) {
        stopRapidRead();
    } else if (type == WS_EVT_DATA) {
        // Parse command
        StaticJsonDocument<256> doc;
        deserializeJson(doc, data, len);
        
        const char* cmd = doc["type"];
        int sensorIndex = doc["sensor"] | 0;
        
        if (strcmp(cmd, "set_dry") == 0) {
            // Save current reading as dry calibration
            uint16_t raw = getCurrentRawReading(sensorIndex);
            config.moisture_sensors[sensorIndex].dry_value = raw;
            ConfigManager::getInstance().saveSensor(sensorIndex);
        } else if (strcmp(cmd, "set_wet") == 0) {
            uint16_t raw = getCurrentRawReading(sensorIndex);
            config.moisture_sensors[sensorIndex].wet_value = raw;
            ConfigManager::getInstance().saveSensor(sensorIndex);
        }
    }
}

// Timer task for rapid readings
void rapidReadTask(void* param) {
    while (rapidReadActive) {
        for (int i = 0; i < IWMP_MAX_MOISTURE_SENSORS; i++) {
            if (config.moisture_sensors[i].enabled) {
                uint16_t raw = sensors[i]->readRaw();
                float percent = calculatePercent(i, raw);
                
                StaticJsonDocument<128> doc;
                doc["type"] = "reading";
                doc["sensor"] = i;
                doc["raw"] = raw;
                doc["percent"] = percent;
                doc["dry_cal"] = config.moisture_sensors[i].dry_value;
                doc["wet_cal"] = config.moisture_sensors[i].wet_value;
                
                String json;
                serializeJson(doc, json);
                ws.textAll(json);
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);  // 10 Hz update rate
    }
}
```

### Calibration HTML (embedded)

```html
<!DOCTYPE html>
<html>
<head>
    <title>Sensor Calibration</title>
    <style>
        .reading { font-size: 48px; font-weight: bold; }
        .bar { height: 30px; background: linear-gradient(to right, brown, green); }
        .marker { width: 4px; background: red; height: 40px; position: relative; }
        button { padding: 20px 40px; margin: 10px; font-size: 18px; }
    </style>
</head>
<body>
    <h1>Moisture Sensor Calibration</h1>
    
    <div id="sensor-0">
        <h2>Sensor 1</h2>
        <div class="reading"><span id="percent-0">--</span>%</div>
        <div>Raw: <span id="raw-0">--</span></div>
        <div class="bar">
            <div class="marker" id="marker-0"></div>
        </div>
        <p>
            <button onclick="calibrate(0, 'dry')">Set DRY Point</button>
            <button onclick="calibrate(0, 'wet')">Set WET Point</button>
        </p>
        <div>Dry cal: <span id="dry-0">--</span> | Wet cal: <span id="wet-0">--</span></div>
    </div>
    
    <script>
        const ws = new WebSocket(`ws://${location.host}/ws/calibrate`);
        
        ws.onmessage = (event) => {
            const data = JSON.parse(event.data);
            if (data.type === 'reading') {
                document.getElementById(`percent-${data.sensor}`).textContent = 
                    data.percent.toFixed(1);
                document.getElementById(`raw-${data.sensor}`).textContent = data.raw;
                document.getElementById(`dry-${data.sensor}`).textContent = data.dry_cal;
                document.getElementById(`wet-${data.sensor}`).textContent = data.wet_cal;
                
                // Update marker position
                const marker = document.getElementById(`marker-${data.sensor}`);
                marker.style.left = `${data.percent}%`;
            }
        };
        
        function calibrate(sensor, point) {
            ws.send(JSON.stringify({
                type: `set_${point}`,
                sensor: sensor
            }));
        }
    </script>
</body>
</html>
```

---

## Session 12: Safety & Polish

### Watchdog Implementation

```cpp
#include <esp_task_wdt.h>

void setupWatchdog() {
    esp_task_wdt_init(30, true);  // 30 second timeout, panic on timeout
    esp_task_wdt_add(NULL);       // Add current task
}

void feedWatchdog() {
    esp_task_wdt_reset();
}

// In main loop
void loop() {
    feedWatchdog();
    
    // ... rest of loop
}
```

### Error Handling Pattern

```cpp
enum class SystemError {
    NONE = 0,
    WIFI_CONNECT_FAILED,
    MQTT_CONNECT_FAILED,
    SENSOR_READ_FAILED,
    ESPNOW_SEND_FAILED,
    NVS_WRITE_FAILED,
    RELAY_SAFETY_LOCKOUT
};

class ErrorHandler {
public:
    void reportError(SystemError error, const char* details = nullptr);
    void clearError(SystemError error);
    bool hasError(SystemError error) const;
    SystemError getMostSevereError() const;
    
    // LED indication
    void updateStatusLed();
    
private:
    uint32_t _errorFlags = 0;
    uint32_t _errorCounts[16] = {0};
};

// Blink patterns for status LED
void ErrorHandler::updateStatusLed() {
    static uint32_t lastBlink = 0;
    static bool ledState = false;
    
    uint32_t blinkInterval;
    
    if (_errorFlags == 0) {
        blinkInterval = 2000;  // Slow blink = all OK
    } else if (_errorFlags & (1 << (int)SystemError::RELAY_SAFETY_LOCKOUT)) {
        blinkInterval = 100;   // Fast blink = safety issue
    } else {
        blinkInterval = 500;   // Medium blink = recoverable error
    }
    
    if (millis() - lastBlink >= blinkInterval) {
        ledState = !ledState;
        digitalWrite(STATUS_LED_PIN, ledState);
        lastBlink = millis();
    }
}
```

---

## Part 3: Testing Checklist

### Hardware Tests
- [ ] Direct ADC reads stable values (±5% variance)
- [ ] ADS1115 reads more stable than internal ADC
- [ ] Deep sleep current <10µA (measure with multimeter)
- [ ] ESP-NOW range test: >95% delivery at expected distance
- [ ] Relay switching under load (inductive kick protected)
- [ ] WiFi reconnects after router reboot
- [ ] Battery voltage reading accurate (±0.1V)

### Communication Tests
- [ ] Hub receives ESP-NOW from Remote
- [ ] RSSI values reasonable (-30 to -90 dBm)
- [ ] Message deduplication works (no duplicates processed)
- [ ] ACK/retry mechanism recovers from packet loss
- [ ] MQTT reconnects automatically
- [ ] HA entities appear via discovery
- [ ] Switch commands work both directions

### Safety Tests
- [ ] Relay timeout triggers at configured time
- [ ] Cooldown period enforced
- [ ] Daily runtime limit works
- [ ] Emergency stop disables all relays
- [ ] Watchdog resets hung device
- [ ] Config survives power cycle

### Integration Tests
- [ ] Full cycle: Remote → Hub → MQTT → HA
- [ ] Automation triggers relay at threshold
- [ ] Calibration UI updates values
- [ ] OTA update succeeds
- [ ] Factory reset clears all settings

---

## Part 4: Quick Reference

### Pin Assignments

**Hub (ESP32-WROOM):**
| Function | GPIO | Notes |
|----------|------|-------|
| Moisture ADC 1-4 | 32, 33, 34, 35 | ADC1 only (safe with WiFi) |
| I2C SDA | 21 | For ADS1115 |
| I2C SCL | 22 | For ADS1115 |
| Status LED | 2 | Built-in on most boards |
| Reset Button | 0 | Boot button |

**Remote (ESP32-C3):**
| Function | GPIO | Notes |
|----------|------|-------|
| Moisture ADC | 0-4 | Any ADC pin |
| Wake Button | 5 | RTC capable |
| Power Detect | 6 | Detect USB |
| Status LED | 8 | |
| I2C SDA | 8 | If using ADS1115 |
| I2C SCL | 9 | If using ADS1115 |

**Greenhouse (ESP32-WROOM):**
| Function | GPIO | Notes |
|----------|------|-------|
| Relay 1-4 | 16, 17, 18, 19 | Active-low |
| Moisture ADC | 32, 33, 34, 35 | ADC1 |
| DHT Sensor | 4 | Or SHT via I2C |
| I2C SDA | 21 | |
| I2C SCL | 22 | |
| Status LED | 2 | |

### MQTT Topics (ArduinoHA handles automatically)

```
homeassistant/sensor/DEVICE_ID/moisture_0/config  # Discovery
iwmp/DEVICE_ID/state                               # State JSON
iwmp/DEVICE_ID/cmd/relay_0                         # Commands
iwmp/DEVICE_ID/status                              # Availability
```

### ESP-NOW Message Quick Reference

| Type | Code | Payload Size |
|------|------|--------------|
| MOISTURE_READING | 0x01 | 16 bytes |
| ENVIRONMENTAL | 0x02 | 16 bytes |
| BATTERY_STATUS | 0x03 | 16 bytes |
| RELAY_COMMAND | 0x10 | 18 bytes |
| ANNOUNCE | 0x20 | 64 bytes |
| ACK | 0xF0 | 14 bytes |

---

## Part 5: Troubleshooting

### ESP-NOW Not Working
1. Check both devices on same WiFi channel
2. Verify MAC addresses are correct
3. Ensure WiFi is initialized before ESP-NOW
4. Check RSSI - if too low, devices too far apart

### HA Entities Not Appearing
1. Verify MQTT broker is accessible
2. Check HA has MQTT integration configured
3. Look in HA Developer Tools → MQTT for discovery messages
4. Ensure device ID is unique

### Sensor Readings Unstable
1. Add capacitor (100nF) near sensor power pins
2. Increase sample count and averaging
3. Use ADS1115 instead of internal ADC
4. Check for loose connections

### Deep Sleep Current Too High
1. Disable WiFi before sleep: `WiFi.disconnect(true); WiFi.mode(WIFI_OFF);`
2. Check no GPIO pins sourcing/sinking current
3. Remove/disable LEDs
4. Use `esp_sleep_pd_config()` to power down unused domains

---

*Document Version: 1.0*
*Last Updated: January 2026*