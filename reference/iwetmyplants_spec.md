# iWetMyPlants v2.0 - Complete System Specification

## Executive Summary

This document specifies a complete reboot of the iWetMyPlants plant monitoring and automation system. The architecture splits into three distinct firmware deployments (Hub, Remote, Greenhouse Manager) that share a common codebase foundation while serving different roles. All devices use ESP32-platform microcontrollers, communicate via ESP-NOW for local mesh and WiFi/MQTT for cloud connectivity, and integrate with Home Assistant.

---

## Part 1: Architecture Overview

### 1.1 Device Types

| Device | Hardware | Primary Role | Connectivity |
|--------|----------|--------------|--------------|
| **Hub** | ESP32-WROOM or ESP32-S3 | Central coordinator, data aggregator, HA bridge | WiFi + ESP-NOW |
| **Remote** | ESP32-C3 SuperMini | Single sensor node, ultra-low power capable | ESP-NOW (battery) or WiFi (powered) |
| **Greenhouse Manager** | ESP32-WROOM or ESP32-S3 | Environmental control, relay automation | WiFi + ESP-NOW |

### 1.2 Communication Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        HOME ASSISTANT                            │
│                    (MQTT Broker + Integration)                   │
└─────────────────────────────────────────────────────────────────┘
                              │
                         MQTT/WiFi
                              │
┌─────────────────────────────────────────────────────────────────┐
│                           HUB                                    │
│   - Receives ESP-NOW from Remotes                               │
│   - Bridges to MQTT                                             │
│   - Can read local sensors                                      │
│   - Sends commands to Greenhouse Manager                        │
└─────────────────────────────────────────────────────────────────┘
         │                                      │
     ESP-NOW                                ESP-NOW
         │                                      │
┌─────────────────┐                 ┌─────────────────────────────┐
│    REMOTE(s)    │                 │    GREENHOUSE MANAGER       │
│  - Battery/USB  │                 │  - Relay control            │
│  - Deep sleep   │                 │  - Temp/humidity sensing    │
│  - Single sensor│                 │  - Pump/fan/humidifier      │
└─────────────────┘                 └─────────────────────────────┘
```

### 1.3 Operating Modes

Each device can operate in multiple modes:

1. **Standalone Mode**: Direct WiFi + MQTT to Home Assistant (no Hub required)
2. **Networked Mode**: ESP-NOW to Hub, Hub bridges to MQTT
3. **Hybrid Mode**: Both ESP-NOW and direct MQTT (redundancy)
4. **Configuration Mode**: AP + Captive Portal for setup

---

## Part 2: Shared Core Components

### 2.1 Project Structure

```
iwetmyplantsv2/
├── platformio.ini
├── shared/
│   ├── config/
│   │   ├── config_manager.h/.cpp      # NVS-based configuration
│   │   ├── config_schema.h            # Configuration structure definitions
│   │   └── defaults.h                 # Default values
│   ├── communication/
│   │   ├── espnow_manager.h/.cpp      # ESP-NOW handling
│   │   ├── mqtt_manager.h/.cpp        # MQTT + HA discovery
│   │   ├── wifi_manager.h/.cpp        # WiFi connection handling
│   │   └── message_types.h            # Shared message structures
│   ├── sensors/
│   │   ├── sensor_interface.h         # Abstract sensor interface
│   │   ├── capacitive_moisture.h/.cpp # Direct ADC moisture reading
│   │   ├── ads1115_moisture.h/.cpp    # ADS1115-based moisture reading
│   │   ├── mux_moisture.h/.cpp        # Multiplexer-based reading
│   │   ├── dht_sensor.h/.cpp          # DHT11/22 temperature/humidity
│   │   └── sht_sensor.h/.cpp          # SHT3x/4x temperature/humidity
│   ├── calibration/
│   │   ├── calibration_manager.h/.cpp # Two-point calibration
│   │   └── rapid_read.h/.cpp          # Fast sampling for calibration
│   ├── web/
│   │   ├── web_server.h/.cpp          # Async web server base
│   │   ├── captive_portal.h/.cpp      # AP mode captive portal
│   │   ├── api_endpoints.h/.cpp       # REST API handlers
│   │   └── web_ui/                    # HTML/CSS/JS (embedded)
│   │       ├── index.html
│   │       ├── calibration.html
│   │       ├── settings.html
│   │       └── style.css
│   └── utils/
│       ├── logger.h/.cpp              # Unified logging
│       ├── watchdog.h/.cpp            # Watchdog timer management
│       └── power_management.h/.cpp    # Sleep mode helpers
├── hub/
│   ├── src/
│   │   ├── main.cpp
│   │   ├── hub_controller.h/.cpp
│   │   └── device_registry.h/.cpp     # Track paired devices
│   └── platformio_hub.ini
├── remote/
│   ├── src/
│   │   ├── main.cpp
│   │   ├── remote_controller.h/.cpp
│   │   └── power_modes.h/.cpp         # Deep sleep management
│   └── platformio_remote.ini
└── greenhouse/
    ├── src/
    │   ├── main.cpp
    │   ├── greenhouse_controller.h/.cpp
    │   ├── relay_manager.h/.cpp        # Relay control with safety
    │   └── automation_engine.h/.cpp    # Sensor-to-relay binding
    └── platformio_greenhouse.ini
```

### 2.2 Configuration Schema

```cpp
// config_schema.h

#pragma once
#include <Arduino.h>

// ============ DEVICE IDENTITY ============
struct DeviceIdentity {
    char device_name[32];           // User-friendly name
    char device_id[13];             // MAC-based unique ID (auto-generated)
    uint8_t device_type;            // 0=Hub, 1=Remote, 2=Greenhouse
    char firmware_version[16];      // Semantic version
};

// ============ WIFI CONFIGURATION ============
struct WifiConfig {
    char ssid[33];
    char password[65];
    bool use_static_ip;
    uint32_t static_ip;
    uint32_t gateway;
    uint32_t subnet;
    uint32_t dns;
    uint8_t wifi_channel;           // Important for ESP-NOW coexistence
};

// ============ MQTT CONFIGURATION ============
struct MqttConfig {
    bool enabled;
    char broker[65];
    uint16_t port;
    char username[33];
    char password[65];
    char base_topic[65];            // Default: "iwetmyplants"
    bool ha_discovery_enabled;
    char ha_discovery_prefix[33];   // Default: "homeassistant"
    uint16_t publish_interval_sec;  // How often to push to MQTT
};

// ============ ESP-NOW CONFIGURATION ============
struct EspNowConfig {
    bool enabled;
    uint8_t hub_mac[6];             // MAC of Hub to send to (remotes)
    uint8_t channel;                // WiFi channel (must match router if WiFi used)
    bool encryption_enabled;
    uint8_t pmk[16];                // Primary Master Key
    uint8_t lmk[16];                // Local Master Key
    uint16_t send_interval_sec;     // How often to send readings
};

// ============ SENSOR CONFIGURATION ============
enum class SensorInputType : uint8_t {
    DIRECT_ADC = 0,                 // Direct to ESP32 ADC pin
    ADS1115 = 1,                    // External 16-bit ADC
    MUX_CD74HC4067 = 2              // 16-channel analog multiplexer
};

struct MoistureSensorConfig {
    bool enabled;
    SensorInputType input_type;
    uint8_t adc_pin;                // For DIRECT_ADC
    uint8_t ads_channel;            // For ADS1115 (0-3)
    uint8_t mux_channel;            // For MUX (0-15)
    uint8_t ads_i2c_address;        // Default 0x48
    uint16_t dry_value;             // ADC value when dry (calibrated)
    uint16_t wet_value;             // ADC value when wet (calibrated)
    uint8_t reading_samples;        // Number of samples to average
    uint16_t sample_delay_ms;       // Delay between samples
    char sensor_name[32];           // User-friendly name
};

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
    bool enabled;
    EnvSensorType sensor_type;
    uint8_t pin;                    // For DHT sensors
    uint8_t i2c_address;            // For SHT sensors (default 0x44)
    uint16_t read_interval_sec;
};

// ============ RELAY CONFIGURATION ============
struct RelayConfig {
    bool enabled;
    uint8_t gpio_pin;
    bool active_low;                // Most relay modules are active-low
    char relay_name[32];
    uint32_t max_on_time_sec;       // Safety timeout (0 = no limit)
    uint32_t min_off_time_sec;      // Minimum off time between activations
    uint32_t cooldown_sec;          // Required gap after deactivation
};

// ============ AUTOMATION BINDING ============
struct SensorRelayBinding {
    bool enabled;
    uint8_t sensor_index;           // Which moisture sensor triggers this
    uint8_t relay_index;            // Which relay to control
    uint16_t dry_threshold;         // Moisture % below which to activate
    uint16_t wet_threshold;         // Moisture % above which to deactivate
    uint32_t max_runtime_sec;       // Maximum single run time
    uint32_t check_interval_sec;    // How often to check sensor
    bool hysteresis_enabled;        // Prevent rapid cycling
};

// ============ POWER CONFIGURATION (Remote only) ============
struct PowerConfig {
    bool battery_powered;
    uint32_t deep_sleep_duration_sec;
    uint32_t awake_duration_ms;
    bool wake_on_button;
    uint8_t wake_button_pin;
    bool wake_on_power_connect;     // Wake when USB power detected
    uint8_t power_detect_pin;       // Pin to detect USB power
    uint8_t battery_adc_pin;        // For battery voltage monitoring
    float low_battery_voltage;      // Threshold for low battery warning
};

// ============ MASTER CONFIGURATION ============
struct DeviceConfig {
    DeviceIdentity identity;
    WifiConfig wifi;
    MqttConfig mqtt;
    EspNowConfig espnow;
    MoistureSensorConfig moisture_sensors[8];  // Hub supports up to 8
    EnvironmentalSensorConfig env_sensor;
    RelayConfig relays[4];                      // Greenhouse supports up to 4
    SensorRelayBinding bindings[4];
    PowerConfig power;                          // Remote only
    uint32_t config_version;                    // For migration
    uint32_t crc32;                             // Configuration integrity
};
```

### 2.3 ESP-NOW Message Protocol

```cpp
// message_types.h

#pragma once
#include <Arduino.h>

// Message type identifiers
enum class MessageType : uint8_t {
    // Sensor data
    MOISTURE_READING = 0x01,
    ENVIRONMENTAL_READING = 0x02,
    BATTERY_STATUS = 0x03,
    
    // Control commands
    RELAY_COMMAND = 0x10,
    CALIBRATION_COMMAND = 0x11,
    CONFIG_COMMAND = 0x12,
    WAKE_COMMAND = 0x13,
    
    // Discovery & pairing
    ANNOUNCE = 0x20,
    PAIR_REQUEST = 0x21,
    PAIR_RESPONSE = 0x22,
    HEARTBEAT = 0x23,
    
    // Acknowledgments
    ACK = 0xF0,
    NACK = 0xF1
};

// ============ BASE MESSAGE HEADER ============
struct MessageHeader {
    uint8_t protocol_version;       // Currently 1
    MessageType type;
    uint8_t sender_mac[6];
    uint8_t sequence_number;        // For deduplication
    uint8_t flags;                  // Bit 0: requires_ack
    uint32_t timestamp;             // Unix timestamp or millis
} __attribute__((packed));

// ============ SENSOR DATA MESSAGES ============
struct MoistureReadingMsg {
    MessageHeader header;
    uint8_t sensor_index;
    uint16_t raw_value;             // Raw ADC value
    uint8_t moisture_percent;       // Calibrated percentage (0-100)
    int8_t rssi;                    // Signal strength
} __attribute__((packed));

struct EnvironmentalReadingMsg {
    MessageHeader header;
    int16_t temperature_c_x10;      // Temperature * 10 (e.g., 235 = 23.5°C)
    uint16_t humidity_percent_x10;  // Humidity * 10 (e.g., 655 = 65.5%)
} __attribute__((packed));

struct BatteryStatusMsg {
    MessageHeader header;
    uint16_t voltage_mv;            // Battery voltage in millivolts
    uint8_t percent;                // Estimated percentage
    bool charging;                  // Is USB power connected
} __attribute__((packed));

// ============ CONTROL MESSAGES ============
struct RelayCommandMsg {
    MessageHeader header;
    uint8_t relay_index;
    bool state;                     // true = ON, false = OFF
    uint32_t duration_sec;          // 0 = indefinite (use with caution)
} __attribute__((packed));

struct CalibrationCommandMsg {
    MessageHeader header;
    uint8_t sensor_index;
    uint8_t calibration_point;      // 0 = dry, 1 = wet
} __attribute__((packed));

// ============ DISCOVERY MESSAGES ============
struct AnnounceMsg {
    MessageHeader header;
    uint8_t device_type;            // 0=Hub, 1=Remote, 2=Greenhouse
    char device_name[32];
    char firmware_version[16];
    uint8_t capabilities;           // Bit flags for features
} __attribute__((packed));

struct PairRequestMsg {
    MessageHeader header;
    uint8_t device_type;
    char device_name[32];
} __attribute__((packed));

struct PairResponseMsg {
    MessageHeader header;
    bool accepted;
    uint8_t assigned_channel;       // WiFi channel to use
} __attribute__((packed));

// Maximum ESP-NOW payload is 250 bytes
static_assert(sizeof(AnnounceMsg) <= 250, "AnnounceMsg too large for ESP-NOW");
```

### 2.4 Home Assistant MQTT Auto-Discovery

```cpp
// mqtt_manager.h - Key discovery payload generation

/*
 * MQTT Topic Structure:
 * 
 * Discovery:   homeassistant/<component>/<device_id>/<entity_id>/config
 * State:       iwetmyplants/<device_id>/state
 * Command:     iwetmyplants/<device_id>/cmd/<entity>
 * Availability: iwetmyplants/<device_id>/status
 * 
 * Example for a moisture sensor:
 * Discovery topic: homeassistant/sensor/iwmp_aabbccdd/moisture_1/config
 * State topic:     iwetmyplants/aabbccdd/state
 * 
 * Discovery payload (JSON):
 * {
 *   "name": "Plant 1 Moisture",
 *   "unique_id": "iwmp_aabbccdd_moisture_1",
 *   "state_topic": "iwetmyplants/aabbccdd/state",
 *   "value_template": "{{ value_json.moisture_1 }}",
 *   "unit_of_measurement": "%",
 *   "device_class": "moisture",
 *   "state_class": "measurement",
 *   "availability_topic": "iwetmyplants/aabbccdd/status",
 *   "device": {
 *     "identifiers": ["iwmp_aabbccdd"],
 *     "name": "iWetMyPlants Remote 1",
 *     "model": "iWetMyPlants Remote",
 *     "manufacturer": "Spirit Wrestler Woodcraft",
 *     "sw_version": "2.0.0"
 *   }
 * }
 */

class MqttManager {
public:
    // Publish HA discovery config for all entities
    void publishDiscovery();
    
    // Publish current state
    void publishState(const SensorReadings& readings);
    
    // Publish availability (online/offline)
    void publishAvailability(bool online);
    
private:
    // Generate discovery payload for a moisture sensor
    String buildMoistureDiscoveryPayload(uint8_t sensor_index);
    
    // Generate discovery payload for temperature
    String buildTemperatureDiscoveryPayload();
    
    // Generate discovery payload for humidity
    String buildHumidityDiscoveryPayload();
    
    // Generate discovery payload for a switch (relay)
    String buildSwitchDiscoveryPayload(uint8_t relay_index);
    
    // Generate discovery payload for battery sensor (remotes)
    String buildBatteryDiscoveryPayload();
};
```

---

## Part 3: Hub Firmware Specification

### 3.1 Hub Responsibilities

1. **ESP-NOW Gateway**: Receive data from all Remotes and Greenhouse Managers
2. **MQTT Bridge**: Forward all data to Home Assistant via MQTT
3. **Device Registry**: Track paired devices, their status, last seen time
4. **Command Relay**: Forward commands from HA to appropriate devices
5. **Local Sensing**: Can also read its own connected sensors
6. **Web Interface**: Configuration, monitoring, manual control

### 3.2 Hub State Machine

```
                    ┌─────────────┐
                    │   BOOT      │
                    └──────┬──────┘
                           │
                           ▼
                    ┌─────────────┐
              ┌─────│ LOAD_CONFIG │
              │     └──────┬──────┘
              │            │
    No config │            │ Config exists
              │            ▼
              │     ┌─────────────┐
              │     │ WIFI_CONNECT│◄────────┐
              │     └──────┬──────┘         │
              │            │                │
              │   Success  │    Fail (timeout)
              │            ▼                │
              │     ┌─────────────┐         │
              │     │ MQTT_CONNECT│─────────┤
              │     └──────┬──────┘         │
              │            │                │
              │   Success  │    Fail        │
              │            ▼                │
              │     ┌─────────────┐         │
              └────►│ AP_MODE     │─────────┘
                    │(Captive Portal)       After config saved
                    └──────┬──────┘
                           │ (also enters if
                           │  button held on boot)
                           │
                           ▼
                    ┌─────────────┐
                    │ OPERATIONAL │◄─── Normal running state
                    │             │
                    │ - ESP-NOW Rx│
                    │ - MQTT Pub  │
                    │ - Web Server│
                    │ - Local Sens│
                    └─────────────┘
```

### 3.3 Hub-Specific Code Structure

```cpp
// hub_controller.h

class HubController {
public:
    void begin();
    void loop();
    
    // Device management
    void onDeviceAnnounce(const AnnounceMsg& msg);
    void onPairRequest(const PairRequestMsg& msg);
    uint8_t getConnectedDeviceCount();
    
    // Data handling
    void onMoistureReading(const MoistureReadingMsg& msg);
    void onEnvironmentalReading(const EnvironmentalReadingMsg& msg);
    void onBatteryStatus(const BatteryStatusMsg& msg);
    
    // Command forwarding
    void sendRelayCommand(const uint8_t* target_mac, uint8_t relay, bool state, uint32_t duration);
    void sendCalibrationCommand(const uint8_t* target_mac, uint8_t sensor, uint8_t point);
    void sendWakeCommand(const uint8_t* target_mac);
    
private:
    DeviceRegistry _registry;
    EspNowManager _espnow;
    MqttManager _mqtt;
    WebServer _webserver;
    
    // Local sensors (optional)
    MoistureSensor* _local_sensors[8];
    uint8_t _local_sensor_count;
    
    void processLocalSensors();
    void checkDeviceTimeouts();
    void publishAggregatedState();
};

// device_registry.h

struct RegisteredDevice {
    uint8_t mac[6];
    uint8_t device_type;
    char device_name[32];
    uint32_t last_seen;             // Unix timestamp
    int8_t last_rssi;
    bool paired;
    bool online;                    // Based on heartbeat timeout
};

class DeviceRegistry {
public:
    bool addDevice(const uint8_t* mac, uint8_t type, const char* name);
    bool removeDevice(const uint8_t* mac);
    RegisteredDevice* getDevice(const uint8_t* mac);
    void updateLastSeen(const uint8_t* mac, int8_t rssi);
    
    // Iterate all devices
    void forEachDevice(std::function<void(RegisteredDevice&)> callback);
    
    // Persistence
    void saveToNVS();
    void loadFromNVS();
    
private:
    std::vector<RegisteredDevice> _devices;
    static constexpr uint32_t OFFLINE_TIMEOUT_SEC = 300;  // 5 minutes
};
```

---

## Part 4: Remote Firmware Specification

### 4.1 Remote Responsibilities

1. **Single Sensor Reading**: Read one capacitive moisture sensor
2. **Low Power Operation**: Deep sleep with timer/GPIO wake
3. **ESP-NOW Transmission**: Send readings to Hub
4. **Standalone Capability**: Direct MQTT if no Hub available
5. **Configuration Mode**: Enter via button or USB power detection

### 4.2 Power Mode State Machine

```
                    ┌─────────────┐
                    │   BOOT      │
                    └──────┬──────┘
                           │
                           ▼
                    ┌─────────────┐
                    │CHECK_WAKE_  │
                    │REASON       │
                    └──────┬──────┘
                           │
          ┌────────────────┼────────────────┐
          │                │                │
     Timer Wake      Button Wake      Power Connected
          │                │                │
          ▼                ▼                ▼
    ┌───────────┐   ┌───────────┐    ┌───────────┐
    │QUICK_READ │   │CONFIG_MODE│    │POWERED_   │
    │           │   │(AP+Portal)│    │MODE       │
    │-Read sensor│  │           │    │           │
    │-Send ESP-NOW│ │-Full WiFi │    │-WiFi+MQTT │
    │-Check ACK │   │-Web config│    │-Continuous│
    │-Sleep again│  │           │    │-Web UI    │
    └─────┬─────┘   └───────────┘    └───────────┘
          │
          ▼
    ┌───────────┐
    │DEEP_SLEEP │
    │(configurable)
    │Default: 5min│
    └───────────┘
```

### 4.3 Remote Deep Sleep Implementation

```cpp
// power_modes.h

#include <esp_sleep.h>
#include <driver/rtc_io.h>

// Data that persists across deep sleep
RTC_DATA_ATTR uint32_t boot_count = 0;
RTC_DATA_ATTR uint32_t last_successful_send = 0;
RTC_DATA_ATTR uint8_t consecutive_failures = 0;

class PowerManager {
public:
    void begin(const PowerConfig& config);
    
    // Check why we woke up
    esp_sleep_wakeup_cause_t getWakeReason();
    
    // Configure and enter deep sleep
    void enterDeepSleep(uint32_t sleep_duration_sec);
    
    // Check if USB power is connected
    bool isExternalPowerConnected();
    
    // Battery voltage reading
    float getBatteryVoltage();
    uint8_t getBatteryPercent();
    
    // Adaptive sleep duration based on conditions
    uint32_t calculateOptimalSleepDuration();
    
private:
    PowerConfig _config;
    
    void configureWakeSources();
    void prepareForSleep();
    
    // ESP32-C3 specific: GPIO wake configuration
    void configureGpioWake(uint8_t pin, bool level);
};

// Quick read cycle for battery mode
void RemoteController::quickReadCycle() {
    // 1. Initialize ESP-NOW (fast, no full WiFi stack)
    _espnow.begin();
    
    // 2. Read sensor
    uint16_t raw = _sensor.readRaw();
    uint8_t percent = _sensor.rawToPercent(raw);
    
    // 3. Build and send message
    MoistureReadingMsg msg;
    buildMoistureMessage(msg, raw, percent);
    
    bool sent = _espnow.sendWithRetry(
        _config.espnow.hub_mac,
        (uint8_t*)&msg,
        sizeof(msg),
        3,      // max retries
        50      // retry delay ms
    );
    
    // 4. Update RTC tracking
    if (sent) {
        last_successful_send = millis();
        consecutive_failures = 0;
    } else {
        consecutive_failures++;
    }
    
    // 5. Calculate next sleep duration
    uint32_t sleep_sec = _power.calculateOptimalSleepDuration();
    
    // 6. Enter deep sleep
    _power.enterDeepSleep(sleep_sec);
}
```

### 4.4 Remote ADC Configuration Options

```cpp
// sensor_interface.h

class ISensorInput {
public:
    virtual ~ISensorInput() = default;
    virtual void begin() = 0;
    virtual uint16_t readRaw() = 0;
    virtual uint16_t getMaxValue() = 0;  // 4095 for 12-bit, 65535 for 16-bit
};

// Direct ESP32 ADC (simplest, least accurate)
class DirectAdcInput : public ISensorInput {
public:
    DirectAdcInput(uint8_t pin);
    void begin() override;
    uint16_t readRaw() override;
    uint16_t getMaxValue() override { return 4095; }
    
private:
    uint8_t _pin;
};

// ADS1115 external ADC (most accurate, I2C)
class Ads1115Input : public ISensorInput {
public:
    Ads1115Input(uint8_t i2c_address, uint8_t channel);
    void begin() override;
    uint16_t readRaw() override;
    uint16_t getMaxValue() override { return 65535; }
    
    // ADS1115 specific
    void setGain(uint8_t gain);  // ±6.144V, ±4.096V, ±2.048V, etc.
    void setSampleRate(uint16_t sps);  // 8, 16, 32, 64, 128, 250, 475, 860 SPS
    
private:
    uint8_t _address;
    uint8_t _channel;
    Adafruit_ADS1115 _adc;
};

// CD74HC4067 16-channel multiplexer
class MuxInput : public ISensorInput {
public:
    MuxInput(uint8_t sig_pin, uint8_t s0, uint8_t s1, uint8_t s2, uint8_t s3, uint8_t channel);
    void begin() override;
    uint16_t readRaw() override;
    uint16_t getMaxValue() override { return 4095; }
    
    void setChannel(uint8_t channel);
    
private:
    uint8_t _sig_pin;
    uint8_t _select_pins[4];
    uint8_t _channel;
};

// Factory function to create appropriate input based on config
std::unique_ptr<ISensorInput> createSensorInput(const MoistureSensorConfig& config);
```

---

## Part 5: Greenhouse Manager Specification

### 5.1 Greenhouse Manager Responsibilities

1. **Environmental Monitoring**: Temperature and humidity sensing
2. **Relay Control**: Up to 4 relays for pumps, fans, humidifiers
3. **Automation Engine**: Sensor-to-relay bindings with thresholds
4. **Safety Features**: Timeout limits, cooldown periods, watchdog
5. **ESP-NOW Integration**: Receive moisture data from Hub/Remotes
6. **Command Processing**: Accept relay commands from Hub/HA

### 5.2 Relay Safety Implementation

```cpp
// relay_manager.h

struct RelayState {
    bool current_state;
    uint32_t last_on_time;          // When relay was last turned on
    uint32_t last_off_time;         // When relay was last turned off
    uint32_t total_on_time_today;   // Cumulative runtime today
    uint32_t activation_count;      // Times activated today
    bool locked_out;                // Safety lockout active
    char lockout_reason[64];
};

class RelayManager {
public:
    void begin(const RelayConfig configs[], uint8_t count);
    
    // Control with safety checks
    bool turnOn(uint8_t index, uint32_t max_duration_sec = 0);
    bool turnOff(uint8_t index);
    bool toggle(uint8_t index);
    
    // State queries
    bool isOn(uint8_t index);
    RelayState getState(uint8_t index);
    
    // Must be called in loop() for timeout enforcement
    void update();
    
    // Safety features
    void setMaxOnTime(uint8_t index, uint32_t seconds);
    void setMinOffTime(uint8_t index, uint32_t seconds);
    void setCooldown(uint8_t index, uint32_t seconds);
    void setDailyLimit(uint8_t index, uint32_t max_runtime_sec);
    
    // Emergency stop
    void emergencyStopAll();
    void clearLockout(uint8_t index);
    
private:
    RelayConfig _configs[4];
    RelayState _states[4];
    uint8_t _count;
    
    bool checkSafetyConditions(uint8_t index);
    void enforceTimeouts();
    void resetDailyCounters();  // Call at midnight
};

// automation_engine.h

class AutomationEngine {
public:
    void begin(RelayManager* relays);
    
    // Configure bindings
    void addBinding(const SensorRelayBinding& binding);
    void removeBinding(uint8_t index);
    void updateBinding(uint8_t index, const SensorRelayBinding& binding);
    
    // Process incoming sensor data
    void onMoistureReading(uint8_t sensor_index, uint8_t percent);
    void onEnvironmentalReading(float temp_c, float humidity_pct);
    
    // Must be called in loop()
    void update();
    
    // Enable/disable automation (for manual override)
    void setEnabled(bool enabled);
    bool isEnabled();
    
private:
    RelayManager* _relays;
    SensorRelayBinding _bindings[4];
    uint8_t _binding_count;
    bool _enabled;
    
    // State tracking for hysteresis
    struct BindingState {
        bool currently_watering;
        uint32_t watering_started;
        uint32_t last_check;
        uint8_t last_moisture;
    };
    BindingState _binding_states[4];
    
    void evaluateBinding(uint8_t index, uint8_t moisture_percent);
};
```

### 5.3 Greenhouse Control Logic Example

```cpp
void AutomationEngine::evaluateBinding(uint8_t index, uint8_t moisture_percent) {
    auto& binding = _bindings[index];
    auto& state = _binding_states[index];
    
    if (!binding.enabled) return;
    
    // Check interval
    uint32_t now = millis();
    if ((now - state.last_check) < (binding.check_interval_sec * 1000)) {
        return;
    }
    state.last_check = now;
    
    if (state.currently_watering) {
        // Check if we should stop
        bool should_stop = false;
        const char* reason = nullptr;
        
        // Moisture reached target?
        if (moisture_percent >= binding.wet_threshold) {
            should_stop = true;
            reason = "Target moisture reached";
        }
        
        // Max runtime exceeded?
        uint32_t runtime = (now - state.watering_started) / 1000;
        if (runtime >= binding.max_runtime_sec) {
            should_stop = true;
            reason = "Max runtime exceeded";
        }
        
        if (should_stop) {
            _relays->turnOff(binding.relay_index);
            state.currently_watering = false;
            LOG_INFO("Stopped watering: %s (ran for %u sec)", reason, runtime);
        }
    } else {
        // Check if we should start
        if (moisture_percent <= binding.dry_threshold) {
            // Hysteresis check - only start if we haven't recently stopped
            if (binding.hysteresis_enabled && state.last_moisture > binding.dry_threshold) {
                // Don't start yet, wait for consistent low reading
                state.last_moisture = moisture_percent;
                return;
            }
            
            if (_relays->turnOn(binding.relay_index, binding.max_runtime_sec)) {
                state.currently_watering = true;
                state.watering_started = now;
                LOG_INFO("Started watering: moisture at %u%%, threshold %u%%", 
                         moisture_percent, binding.dry_threshold);
            }
        }
    }
    
    state.last_moisture = moisture_percent;
}
```

---

## Part 6: Web Interface Specification

### 6.1 Unified Web UI Structure

All three device types share a common web UI with device-specific sections enabled/disabled.

```
/                       → Dashboard (device-specific)
/settings               → General settings
/settings/wifi          → WiFi configuration  
/settings/mqtt          → MQTT configuration
/settings/espnow        → ESP-NOW configuration
/sensors                → Sensor list and status
/sensors/calibrate      → Calibration interface
/relays                 → Relay control (Greenhouse only)
/automation             → Automation rules (Greenhouse only)
/devices                → Paired device list (Hub only)
/api/...                → REST API endpoints
```

### 6.2 API Endpoints

```
GET  /api/status              → Current device status
GET  /api/sensors             → All sensor readings
GET  /api/sensors/{id}        → Specific sensor reading
POST /api/sensors/{id}/calibrate  → Start calibration
GET  /api/config              → Current configuration
POST /api/config              → Update configuration
POST /api/config/reset        → Factory reset
GET  /api/relays              → All relay states (Greenhouse)
POST /api/relays/{id}         → Control relay
GET  /api/devices             → Paired devices (Hub)
POST /api/devices/{mac}/pair  → Pair device (Hub)
DELETE /api/devices/{mac}     → Unpair device (Hub)
GET  /api/system/info         → System information
POST /api/system/reboot       → Reboot device
POST /api/system/ota          → OTA update endpoint
GET  /api/calibration/rapid   → WebSocket for rapid readings
```

### 6.3 Calibration Interface

```cpp
// rapid_read.h - WebSocket-based rapid sensor reading for calibration

class RapidReadServer {
public:
    void begin(AsyncWebServer* server, uint8_t sensor_index);
    
    // Call frequently during calibration
    void update();
    
    // Set sampling parameters
    void setSampleRate(uint16_t samples_per_second);  // Default 10
    void setAveragingWindow(uint8_t samples);         // Default 5
    
private:
    AsyncWebSocket _ws;
    ISensorInput* _sensor;
    uint16_t _sample_rate;
    uint8_t _averaging_window;
    
    std::deque<uint16_t> _recent_values;
    
    void onWebSocketEvent(AsyncWebSocketClient* client, 
                          AwsEventType type,
                          void* arg, 
                          uint8_t* data, 
                          size_t len);
    
    void broadcastReading(uint16_t raw, uint16_t average, uint8_t percent);
};

/*
 * WebSocket message format (JSON):
 * 
 * Server → Client:
 * {
 *   "type": "reading",
 *   "raw": 2048,
 *   "avg": 2052,
 *   "percent": 45,
 *   "timestamp": 1234567890
 * }
 * 
 * Client → Server:
 * {
 *   "type": "set_dry"    // Capture current average as dry point
 * }
 * {
 *   "type": "set_wet"    // Capture current average as wet point
 * }
 * {
 *   "type": "save"       // Save calibration to NVS
 * }
 */
```

---

## Part 7: Communication Reliability

### 7.1 ESP-NOW Best Practices

Based on research, implement these reliability measures:

```cpp
// espnow_manager.h

class EspNowManager {
public:
    bool begin(uint8_t channel = 1);  // Channel must match WiFi if using both
    
    // Reliable send with acknowledgment
    bool sendWithAck(const uint8_t* peer_mac, 
                     const uint8_t* data, 
                     size_t len,
                     uint32_t timeout_ms = 100);
    
    // Send with automatic retry
    bool sendWithRetry(const uint8_t* peer_mac,
                       const uint8_t* data,
                       size_t len,
                       uint8_t max_retries = 3,
                       uint32_t retry_delay_ms = 50);
    
    // Broadcast (no ack)
    bool broadcast(const uint8_t* data, size_t len);
    
    // Peer management
    bool addPeer(const uint8_t* mac, uint8_t channel = 0, bool encrypt = false);
    bool removePeer(const uint8_t* mac);
    bool peerExists(const uint8_t* mac);
    
    // Callbacks
    void onReceive(std::function<void(const uint8_t* mac, const uint8_t* data, int len)> cb);
    void onSendComplete(std::function<void(const uint8_t* mac, bool success)> cb);
    
    // Statistics
    uint32_t getPacketsSent();
    uint32_t getPacketsReceived();
    uint32_t getPacketsLost();
    float getDeliveryRate();
    
private:
    // Sequence number for deduplication
    uint8_t _sequence_number = 0;
    
    // Pending acknowledgments
    struct PendingAck {
        uint8_t peer_mac[6];
        uint8_t sequence;
        uint32_t sent_time;
        bool received;
    };
    std::vector<PendingAck> _pending_acks;
    
    // Message deduplication
    struct RecentMessage {
        uint8_t sender_mac[6];
        uint8_t sequence;
        uint32_t received_time;
    };
    std::deque<RecentMessage> _recent_messages;
    
    bool isDuplicate(const uint8_t* mac, uint8_t sequence);
    void cleanupOldMessages();
};
```

### 7.2 WiFi/ESP-NOW Coexistence

**Critical**: ESP-NOW and WiFi must use the same channel for reliable operation.

```cpp
// wifi_manager.h

class WifiManager {
public:
    bool begin(const WifiConfig& config);
    
    // Connect to WiFi and set channel for ESP-NOW
    bool connect();
    
    // Get current channel (needed for ESP-NOW)
    uint8_t getCurrentChannel();
    
    // Start AP mode for configuration
    bool startAP(const char* ssid, const char* password = nullptr);
    
    // Captive portal
    void startCaptivePortal();
    void stopCaptivePortal();
    
    // Connection status
    bool isConnected();
    IPAddress getIP();
    int8_t getRSSI();
    
    // Event callbacks
    void onConnect(std::function<void()> cb);
    void onDisconnect(std::function<void(uint8_t reason)> cb);
    
private:
    WifiConfig _config;
    DNSServer _dns_server;  // For captive portal
    
    void handleDNS();
    void syncEspNowChannel();  // Ensure ESP-NOW uses same channel as WiFi
};

// Important: When WiFi connects, update ESP-NOW channel
void WifiManager::syncEspNowChannel() {
    uint8_t primary_channel;
    wifi_second_chan_t secondary;
    esp_wifi_get_channel(&primary_channel, &secondary);
    
    // ESP-NOW will use the same channel automatically when WiFi is connected,
    // but if you're manually configuring peers, ensure they use this channel
    LOG_INFO("WiFi connected on channel %d, ESP-NOW synced", primary_channel);
}
```

### 7.3 MQTT Reliability

```cpp
// mqtt_manager.h

class MqttManager {
public:
    bool begin(const MqttConfig& config);
    
    // Connection management
    bool connect();
    void disconnect();
    bool isConnected();
    
    // Publishing with QoS
    bool publish(const char* topic, const char* payload, 
                 bool retain = false, uint8_t qos = 1);
    
    // Subscribing
    bool subscribe(const char* topic, uint8_t qos = 1);
    void onMessage(std::function<void(const char* topic, const char* payload)> cb);
    
    // Home Assistant discovery
    void publishDiscovery();
    void publishAvailability(bool online);
    
    // Last Will and Testament (automatic offline detection)
    void setLWT(const char* topic, const char* payload);
    
    // Must be called in loop()
    void loop();
    
private:
    AsyncMqttClient _client;
    MqttConfig _config;
    
    // Reconnection handling
    uint32_t _last_reconnect_attempt;
    uint8_t _reconnect_attempts;
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;
    static constexpr uint8_t MAX_RECONNECT_ATTEMPTS = 10;
    
    void onMqttConnect(bool session_present);
    void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
    void attemptReconnect();
    
    // Build standard topic paths
    String buildStateTopic();
    String buildCommandTopic(const char* entity);
    String buildAvailabilityTopic();
};
```

---

## Part 8: Build Configuration

### 8.1 PlatformIO Configuration

```ini
; platformio.ini

[platformio]
default_envs = hub

[env]
platform = espressif32
framework = arduino
monitor_speed = 115200
upload_speed = 921600
lib_deps = 
    bblanchon/ArduinoJson@^7.0.0
    me-no-dev/ESPAsyncWebServer@^1.2.3
    me-no-dev/AsyncTCP@^1.1.1
    marvinroger/AsyncMqttClient@^0.9.0
    adafruit/Adafruit ADS1X15@^2.4.0
    adafruit/Adafruit Unified Sensor@^1.1.9
    adafruit/DHT sensor library@^1.4.4
    closedcube/ClosedCube SHT31D@^1.5.1
build_flags = 
    -DCORE_DEBUG_LEVEL=3
    -DCONFIG_ASYNC_TCP_RUNNING_CORE=0
    -DCONFIG_ASYNC_TCP_USE_WDT=0
    -DIWMP_VERSION=\"2.0.0\"

[env:hub]
board = esp32dev
; Or for ESP32-S3:
; board = esp32-s3-devkitc-1
build_flags = 
    ${env.build_flags}
    -DIWMP_DEVICE_TYPE=0
    -DIWMP_MAX_SENSORS=8
    -DIWMP_HAS_ESPNOW_RX=1
    -DIWMP_HAS_DEVICE_REGISTRY=1
build_src_filter = 
    +<../shared/**>
    +<hub/**>

[env:remote]
board = esp32-c3-devkitm-1
; Or for C3 SuperMini (same chip, different board):
; board = seeed_xiao_esp32c3
build_flags = 
    ${env.build_flags}
    -DIWMP_DEVICE_TYPE=1
    -DIWMP_MAX_SENSORS=1
    -DIWMP_HAS_DEEP_SLEEP=1
    -DIWMP_HAS_BATTERY=1
build_src_filter = 
    +<../shared/**>
    +<remote/**>

[env:greenhouse]
board = esp32dev
build_flags = 
    ${env.build_flags}
    -DIWMP_DEVICE_TYPE=2
    -DIWMP_MAX_SENSORS=4
    -DIWMP_MAX_RELAYS=4
    -DIWMP_HAS_AUTOMATION=1
build_src_filter = 
    +<../shared/**>
    +<greenhouse/**>

; Debug builds
[env:hub_debug]
extends = env:hub
build_type = debug
build_flags = 
    ${env:hub.build_flags}
    -DCORE_DEBUG_LEVEL=5

[env:remote_debug]
extends = env:remote
build_type = debug
build_flags = 
    ${env:remote.build_flags}
    -DCORE_DEBUG_LEVEL=5
```

---

## Part 9: Implementation Priorities

### Phase 1: Foundation (Week 1)
1. Project structure setup
2. Configuration system (NVS-based)
3. WiFiManager integration with custom portal
4. Basic web server with settings pages
5. Direct ADC moisture sensor reading

### Phase 2: Hub Core (Week 2)
1. MQTT connection and HA discovery
2. ESP-NOW receive handling
3. Device registry
4. Hub web dashboard
5. State publishing to HA

### Phase 3: Remote Core (Week 3)
1. Deep sleep implementation
2. ESP-NOW transmission
3. Battery monitoring
4. Wake source detection
5. Power mode switching (battery vs powered)

### Phase 4: Greenhouse Manager (Week 4)
1. Relay control with safety features
2. Environmental sensor support (DHT/SHT)
3. Automation engine
4. Sensor-relay bindings
5. Integration with Hub

### Phase 5: Advanced Features (Week 5)
1. ADS1115 support
2. Multiplexer support
3. Calibration interface with rapid reading
4. OTA updates
5. Statistics and logging

### Phase 6: Polish & Testing (Week 6)
1. Error handling improvements
2. Edge case testing
3. Documentation
4. Home Assistant dashboard examples

---

## Part 10: Testing Checklist

### Hardware Testing
- [ ] Direct ADC reading accuracy vs ADS1115
- [ ] Deep sleep current consumption measurement
- [ ] ESP-NOW range testing
- [ ] Relay switching under load
- [ ] WiFi reconnection after power loss
- [ ] Battery voltage monitoring accuracy

### Communication Testing
- [ ] ESP-NOW packet delivery rate (expect >95%)
- [ ] MQTT reconnection handling
- [ ] WiFi/ESP-NOW channel coexistence
- [ ] Message deduplication
- [ ] ACK timeout handling

### Integration Testing
- [ ] Hub receives data from multiple Remotes
- [ ] HA entities appear via auto-discovery
- [ ] Relay commands from HA work
- [ ] Automation rules trigger correctly
- [ ] Calibration saves and persists

### Safety Testing
- [ ] Relay timeout triggers correctly
- [ ] Cooldown periods enforced
- [ ] Daily runtime limits work
- [ ] Emergency stop works
- [ ] Watchdog resets device on hang

---

## Appendix A: Hardware Reference

### Tested Capacitive Moisture Sensors
- Generic "v1.2" with TLC555I chip (best)
- Generic "v2.0" (quality varies)
- DFRobot SEN0193

### Recommended ESP32 Boards
- **Hub**: ESP32-WROOM-32D DevKit, ESP32-S3-DevKitC
- **Remote**: ESP32-C3 SuperMini, Seeed XIAO ESP32-C3
- **Greenhouse**: Same as Hub

### Pin Recommendations

**Hub (ESP32-WROOM):**
- GPIO 32-39: ADC1 inputs for sensors (safe with WiFi)
- GPIO 21, 22: I2C (SDA, SCL) for ADS1115
- GPIO 4: Status LED

**Remote (ESP32-C3):**
- GPIO 0-4: ADC inputs
- GPIO 5: Wake button
- GPIO 6: Power detect
- GPIO 7: Status LED
- GPIO 8-9: I2C (if using ADS1115)

**Greenhouse (ESP32-WROOM):**
- GPIO 16-19: Relay outputs
- GPIO 32-35: ADC inputs
- GPIO 21, 22: I2C
- GPIO 4: DHT sensor
- GPIO 2: Status LED

### ADC Notes
- **CRITICAL**: ESP32 ADC2 (GPIO 0, 2, 4, 12-15, 25-27) cannot be used when WiFi is active
- Use ADC1 (GPIO 32-39) for sensor readings when WiFi is needed
- ADS1115 is strongly recommended for accurate readings

---

## Appendix B: Home Assistant Configuration Examples

### Manual MQTT Sensor (if auto-discovery fails)
```yaml
mqtt:
  sensor:
    - name: "Plant 1 Moisture"
      state_topic: "iwetmyplants/aabbccdd/state"
      value_template: "{{ value_json.moisture_1 }}"
      unit_of_measurement: "%"
      device_class: moisture
```

### Automation Example
```yaml
automation:
  - alias: "Water Plant 1 when dry"
    trigger:
      - platform: numeric_state
        entity_id: sensor.plant_1_moisture
        below: 30
        for:
          minutes: 5
    action:
      - service: switch.turn_on
        target:
          entity_id: switch.greenhouse_pump_1
      - delay:
          seconds: 30
      - service: switch.turn_off
        target:
          entity_id: switch.greenhouse_pump_1
```

---

*Document Version: 1.0*
*Last Updated: January 2025*
*Author: Claude (with Brendan's requirements)*