# iWetMyPlants v1.0 — System Architecture Reference

**Purpose:** LLM-readable reference describing every subsystem, class, communication path, and data flow in the iWetMyPlants firmware. Written for passing context to AI assistants. Version reflects state after the March 2026 pull (41 commits, 64 files changed).

---

## 1. System Overview

iWetMyPlants is a multi-device ESP32 plant monitoring and automated watering system. Three distinct firmware images run on three device roles:

| Role | Class | Hardware | Build Target |
|------|-------|----------|-------------|
| **Hub** | `HubController Hub` | ESP32-WROOM / ESP32-S3 | `env:hub` |
| **Remote** | `RemoteController Remote` | ESP32-C3 SuperMini | `env:remote` |
| **Greenhouse** | `GreenhouseController Greenhouse` | ESP32-WROOM / ESP32-S3 | `env:greenhouse` |

All three share a common `shared/` library compiled in via `build_src_filter`. The namespace for all firmware code is `iwmp`.

---

## 2. Device Roles

### 2.1 Hub
- **Role:** Central coordinator. Receives sensor data, bridges to MQTT/Home Assistant, hosts the primary web UI.
- **Entry point:** `src/main_hub.cpp` → `Hub.begin()` / `Hub.loop()`
- **Controller:** `hub/src/hub_controller.h` / `.cpp`
- **Key capabilities:**
  - Receives ESP-NOW packets from Greenhouse (and optionally low-power Remotes)
  - Connects to home WiFi as STA; publishes all sensor/relay state to MQTT
  - Hosts `WebServer` (port 80) with full REST API and WebSocket
  - Maintains `DeviceRegistry` of all paired devices
  - Runs up to 16 local moisture sensors (8× direct ADC + up to 8× ADS1115 across two I2C buses)
  - Dual I2C buses: primary (SDA=21, SCL=22) for 0x48, secondary (SDA=16, SCL=17) for 0x49
  - Background polling state machine: 10 samples × 500 ms/sample = 5 s/sensor, then 60 s idle
  - Implements `ImprovSerial` for USB provisioning when no WiFi is configured
- **State machine:** `HubState` — BOOT → LOAD_CONFIG → WIFI_CONNECT → MQTT_CONNECT → OPERATIONAL (or AP_MODE if no credentials)

### 2.2 Remote
- **Role:** Wireless sensor node. Reads one moisture sensor, reports to Hub and/or MQTT.
- **Entry point:** `src/main_remote.cpp` → `Remote.begin()` / `Remote.loop()`
- **Controller:** `remote/src/remote_controller.h` / `.cpp`
- **Three operating modes** (set in `PowerConfig::operating_mode`, `RemoteMode` enum):
  - `WIFI (0)` — STA connection, HTTP POST to Hub every 60 s, MQTT publish every 60 s, local web UI
  - `STANDALONE (1)` — Permanent AP (192.168.4.1), local web UI only, no reporting
  - `LOW_POWER (2)` — Deep sleep + wake on timer/button; ESP-NOW send on wake; no WiFi
- **Override button** (GPIO5): Hold to force Standalone mode from any mode
- **Power subsystem** (`PowerModes`, `power_modes.cpp/.h`): Uses RTC memory (`RTC_DATA_ATTR`) to persist `boot_count`, `last_successful_send`, `consecutive_failures`, `total_sleep_time` across deep sleep cycles. Default deep sleep = 300 s, awake = 5 s.
- **Web subsystem:** `RemoteWeb` (`remote/src/remote_web.h/.cpp`) — lightweight ~18-route server with ~10KB of inline HTML, bound to 0.0.0.0:80. Separate from shared `WebServer` to minimize flash usage on ESP32-C3.
- **State machine:** `RemoteState` — BOOT → CONFIG_MODE / CONNECTING → RUNNING / STANDALONE / LOW_POWER_CYCLE
- **Improv Serial:** Active in CONFIG_MODE for USB provisioning

### 2.3 Greenhouse
- **Role:** Environmental controller. Reads temp/humidity + moisture sensors, controls relay bank for automated watering.
- **Entry point:** `src/main_greenhouse.cpp` → `Greenhouse.begin()` / `Greenhouse.loop()`
- **Controller:** `greenhouse/src/greenhouse_controller.h` / `.cpp`
- **Key capabilities:**
  - Up to 4 moisture sensors + 1 environmental sensor (DHT or SHT)
  - Up to 8 relays (GPIOs 16,17,18,19,23,25,26,27) with safety limits and cooldown
  - `AutomationEngine` evaluates `SensorRelayBinding` rules every check interval
  - Receives `RELAY_COMMAND` via ESP-NOW from Hub
  - Sends environmental readings and moisture readings via ESP-NOW to Hub
  - Connects to WiFi + MQTT independently (not routed through Hub)
  - Hosts `WebServer` for local relay control and config
  - Implements `ImprovSerial` for USB provisioning
- **State machine:** `GreenhouseState` — BOOT → LOAD_CONFIG → WIFI_CONNECT → MQTT_CONNECT → OPERATIONAL (or AP_MODE)

---

## 3. Communication Architecture

### 3.1 ESP-NOW (`shared/communication/espnow_manager.h/.cpp`)

**Class:** `EspNowManager` (singleton, accessed as `EspNow`)

ESP-NOW is a 2.4GHz peer-to-peer protocol (no router required). Used between Hub ↔ Greenhouse and optionally Hub ↔ Remote (LOW_POWER mode).

**Key features:**
- Max 20 peers (`ESPNOW_MAX_PEERS`)
- 3 retries by default, 50 ms between retries
- ACK-based confirmation: sender waits up to 100 ms for `ACK` message
- Deduplication window: 5 s, 32 entries (`ESPNOW_DEDUP_WINDOW_MS`, `ESPNOW_DEDUP_MAX_ENTRIES`)
- Optional PMK/LMK encryption per peer
- Statistics: `EspNowStats` (packets_sent, received, lost, acks_received, delivery_rate)
- Channel must match WiFi router channel for Hub/Greenhouse (both use WiFi + ESP-NOW simultaneously)

**Send methods:**
- `send()` — fire and forget
- `sendWithAck()` — blocks until ACK or timeout
- `sendWithRetry()` — calls sendWithAck up to N times
- `broadcast()` — to all peers (broadcast MAC FF:FF:FF:FF:FF:FF)
- Convenience wrappers: `sendMoistureReading()`, `sendRelayCommand()`, `sendAnnounce()`, `sendPairRequest()`, `sendHeartbeat()`, `sendAck()`, `sendNack()`, etc.

### 3.2 WiFi (`shared/communication/wifi_manager.h/.cpp`)

**Class:** `WifiManager` (singleton, not shown in headers above but referenced throughout)
- Manages STA (station) and AP (access point) modes
- AP mode: serves captive portal for initial provisioning
- STA mode: connects to saved credentials from `WifiConfig`
- Fallback: after 60 s without WiFi in RUNNING mode, Remote falls back to STANDALONE

### 3.3 MQTT (`shared/communication/mqtt_manager.h` — referenced but not shown)

**Class:** `MqttManager` (singleton)
- Hub and Greenhouse both connect independently to MQTT broker
- Remote in WIFI mode also publishes to MQTT
- Base topic: `iwetmyplants` (configurable in `MqttConfig`)
- HA auto-discovery: publishes to `homeassistant/<component>/<device_id>/config`
- Publish interval: 60 s default (`MqttConfig::publish_interval_sec`)
- Hub subscribes to command topics; Greenhouse subscribes to relay command topics

### 3.4 HTTP (Remote → Hub Reporting)

Remote in WIFI mode POSTs sensor readings to Hub via HTTP:
- Endpoint on Hub: registered in `setupWebRoutes()` inside `HubController`
- Interval: 60 s (`HUB_REPORT_INTERVAL_MS = 60000`)
- Status tracked: `_last_hub_report_sec`, `_last_hub_report_ok`
- Announce cycle: every 10th report cycle, Remote also sends an `ANNOUNCE` ESP-NOW broadcast

### 3.5 Improv Serial (`shared/communication/improv_serial.h/.cpp`)

**Class:** `ImprovSerial`
- Implements https://www.improv-wifi.com/serial/ protocol
- Active while device is in AP_MODE / CONFIG_MODE
- `esp-web-tools` in the browser sends WiFi credentials over USB serial
- States: `AUTHORIZED` → `PROVISIONING` → `PROVISIONED`
- `ConnectCb` callback: Hub/Greenhouse/Remote each provide a lambda that calls `WifiManager` to connect
- On success, returns device URL (e.g., `http://192.168.1.100`) to browser

---

## 4. ESP-NOW Message Protocol (`shared/communication/message_types.h`)

All messages share a 14-byte `MessageHeader`:

```
protocol_version (1B) | type (1B) | sender_mac (6B) | sequence_number (1B) | flags (1B) | timestamp (4B)
```

**Flags:** `REQUIRES_ACK (0x01)`, `IS_RETRY (0x02)`, `ENCRYPTED (0x04)`, `FRAGMENTED (0x08)`

**Message types (`MessageType` enum):**

| Range | Type | Direction | Notes |
|-------|------|-----------|-------|
| 0x01 | `MOISTURE_READING` | Remote/GH → Hub | sensor_index, raw_value, moisture_percent, rssi |
| 0x02 | `ENVIRONMENTAL_READING` | GH → Hub | temp×10, humidity×10 |
| 0x03 | `BATTERY_STATUS` | Remote → Hub | voltage_mv, percent, charging flag |
| 0x04 | `MULTI_SENSOR_READING` | Any → Hub | Variable: N moisture + optional env + optional battery |
| 0x10 | `RELAY_COMMAND` | Hub → GH | relay_index, state, duration_sec, override_safety |
| 0x11 | `CALIBRATION_COMMAND` | Hub → Remote/GH | sensor_index, point (0=dry,1=wet,2=cancel) |
| 0x12 | `CONFIG_COMMAND` | Hub → Any | config_section, JSON payload |
| 0x13 | `WAKE_COMMAND` | Hub → Remote | Wakes sleeping low-power remote |
| 0x14 | `REBOOT_COMMAND` | Hub → Any | |
| 0x15 | `OTA_COMMAND` | Hub → Any | OTA notification |
| 0x20 | `ANNOUNCE` | Any → Broadcast | device_type, name, version, capabilities, sensor_count, relay_count |
| 0x21 | `PAIR_REQUEST` | Remote/GH → Hub | Requires ACK |
| 0x22 | `PAIR_RESPONSE` | Hub → Remote/GH | accepted, channel, reporting_interval, hub_mac |
| 0x23 | `HEARTBEAT` | Any → Hub | uptime_sec, status, rssi, free_heap |
| 0x24 | `UNPAIR` | Any → Hub | Remove pairing |
| 0xF0 | `ACK` | Any → Any | acked_sequence, acked_type |
| 0xF1 | `NACK` | Any → Any | nacked_sequence, reason, message |

**Commands requiring ACK by default:** `RELAY_COMMAND`, `CALIBRATION_COMMAND`, `CONFIG_COMMAND`, `PAIR_REQUEST`

**Capability flags** (in `AnnounceMsg`):
`MOISTURE_SENSOR (0x01)`, `ENV_SENSOR (0x02)`, `RELAY_CONTROL (0x04)`, `BATTERY_POWERED (0x08)`, `WIFI_ENABLED (0x10)`, `MQTT_ENABLED (0x20)`

---

## 5. Configuration System

### 5.1 Schema (`shared/config/config_schema.h`)

Single packed struct `DeviceConfig` stored in NVS, validated by magic `0x49574D50` ("IWMP") and CRC32:

```
DeviceConfig {
  magic (4B), config_version (4B)    // Currently version 3
  DeviceIdentity identity            // name, device_id (MAC-derived), device_type, firmware_version
  WifiConfig wifi                    // ssid, password, static_ip options, wifi_channel, hub_address
  MqttConfig mqtt                    // enabled, broker, port, user/pass, base_topic, ha_discovery, interval
  EspNowConfig espnow                // enabled, hub_mac, channel, encryption, PMK/LMK, send_interval
  MoistureSensorConfig[8] sensors    // enabled, input_type, pin/channel/address, dry/wet calibration
  EnvironmentalSensorConfig env      // enabled, type (DHT11/22/SHT30/31/40/41), pin/address, interval
  RelayConfig[4] relays              // enabled, gpio_pin, active_low, name, max_on_time, cooldown
  SensorRelayBinding[4] bindings     // sensor→relay, dry/wet thresholds, max_runtime, hysteresis
  PowerConfig power                  // battery_powered, sleep_duration, awake_ms, wake pins, operating_mode
  crc32 (4B)
}
```

**Build-flag limits:**
- Hub: `IWMP_MAX_SENSORS=16`, `IWMP_MAX_RELAYS=4`, `IWMP_MAX_BINDINGS=4`
- Remote: `IWMP_MAX_SENSORS=1`
- Greenhouse: `IWMP_MAX_SENSORS=4`, `IWMP_MAX_RELAYS=8`, `IWMP_MAX_BINDINGS=8`

**Sensor input types** (`SensorInputType`): `DIRECT_ADC`, `ADS1115`, `MUX_CD74HC4067`

**Environmental sensor types** (`EnvSensorType`): `NONE`, `DHT11`, `DHT22`, `SHT30`, `SHT31`, `SHT40`, `SHT41`

### 5.2 Config Manager (`shared/config/config_manager.h/.cpp`)

**Class:** `ConfigManager Config` (singleton)
- `Config.begin(DeviceType)` — loads NVS, applies defaults if missing/invalid, sets `device_id` from MAC
- Getters/setters for each config section
- `Config.save()` — computes CRC32 and writes to NVS namespace `"iwmp"`

### 5.3 Defaults (`shared/config/defaults.h`)

`initDefaultConfig(DeviceConfig&, DeviceType)` initializes the full config. Notable defaults:
- Moisture dry: 3500 / wet: 1500 (12-bit ADC); 45000 / 18000 (ADS1115 16-bit scaled)
- Relay max-on: 300 s, cooldown: 300 s, min-off: 60 s
- Automation dry-threshold: 30%, wet-threshold: 70%, max-runtime: 120 s
- Deep sleep: 300 s, awake: 5 s
- Hub defaults: ESP-NOW enabled, MQTT enabled, 8 direct ADC + 8 ADS1115 sensors
- Remote defaults: WIFI mode, 1 sensor on GPIO0, ESP-NOW disabled
- Greenhouse defaults: ESP-NOW + MQTT enabled, 4 sensors, 8 relays

---

## 6. Sensor Subsystem

### 6.1 Moisture Sensors

**Base interface:** `MoistureSensor` (`shared/sensors/sensor_interface.h`)
- Virtual methods: `begin()`, `read()` → raw uint16, `readPercent()`, `calibrateDry()`, `calibrateWet()`, `isReady()`, `isHardwareConnected()`, `getInputType()`, `getName()`

**Implementations:**
- `CapacitiveMoisture` (`shared/sensors/capacitive_moisture.h/.cpp`) — direct ESP32 ADC, 12-bit (0–4095), reads `adc_pin`
- `Ads1115Input` (`shared/sensors/ads1115_moisture.h/.cpp`) — ADS1115 16-bit I2C ADC, channels 0–3, address 0x48 or 0x49; values scaled ×2 to fill 16-bit range. Hub uses `Ads1115Input::setWireForAddress()` to map addresses to I2C buses.
- `MuxMoisture` (`shared/sensors/mux_moisture.h/.cpp`) — CD74HC4067 16-channel analog MUX; pins S0–S3 select channel, SIG reads ADC

### 6.2 Environmental Sensors

- `DhtSensor` (`shared/sensors/dht_sensor.h/.cpp`) — DHT11 or DHT22 on a GPIO pin; reads temp+humidity; 2 s minimum read interval enforced
- `ShtSensor` (`shared/sensors/sht_sensor.h/.cpp`) — SHT30/31/40/41 via I2C; selectable address (default 0x44)

### 6.3 Calibration (`shared/calibration/calibration_manager.h/.cpp`)

**Class:** `CalibrationManager Calibration`
- Two-point calibration: `captureDryPoint()` then `captureWetPoint()` then `apply()`
- Takes N samples and averages; result stored in `MoistureSensorConfig::dry_value` / `wet_value`
- State machine: `IDLE → CAPTURING_DRY → DRY_CAPTURED → CAPTURING_WET → COMPLETE / ERROR`

**Rapid Read** (`shared/calibration/rapid_read.h/.cpp`)
- Used during calibration UI: streams rapid readings to `WebServer` WebSocket via `Web.sendRapidReading()`

---

## 7. Relay & Automation (Greenhouse Only)

### 7.1 Relay Manager (`greenhouse/src/relay_manager.h/.cpp`)

**Class:** `RelayManager`
- Owns up to 8 relays; each is a `RelayConfig` + runtime state
- Safety features per relay:
  - `max_on_time_sec` — auto-shutoff timer (default 300 s)
  - `min_off_time_sec` — minimum off time before re-activation (default 60 s)
  - `cooldown_sec` — required gap after deactivation (default 300 s)
  - Daily activation counter (capped by config)
- `setRelay(index, state, duration_sec)` — respects all safety limits; returns `ErrorCode`
- `emergencyStop()` — turns all relays off, ignoring safety limits
- Active-low output: most relay modules require `active_low=true` (write LOW to turn ON)

### 7.2 Automation Engine (`greenhouse/src/automation_engine.h/.cpp`)

**Class:** `AutomationEngine`
- Holds up to `IWMP_MAX_BINDINGS` `SensorRelayBinding` entries
- Each binding maps `sensor_index` → `relay_index` with thresholds:
  - `dry_threshold` (%) — below this, activate relay (start watering)
  - `wet_threshold` (%) — above this, deactivate relay (stop watering)
  - `max_runtime_sec` — safety shutoff for this specific binding
  - `check_interval_sec` — how often to evaluate
  - `hysteresis_enabled` — prevents rapid cycling
- `update(moisture_readings[])` called from `GreenhouseController::handleOperationalState()`
- Can be globally paused: `pause(duration_ms)` / `resume()`
- `GreenhouseController::onMoistureReading()` feeds incoming ESP-NOW moisture data into automation engine too (for cross-device watering triggers)

---

## 8. Web Interface

### 8.1 Shared WebServer (`shared/web/web_server.h/.cpp`)

**Class:** `WebServer Web` (singleton, `ESPAsyncWebServer` on port 80)
- Used by Hub and Greenhouse
- Hosts static HTML UI + REST JSON API + WebSocket `/ws`
- WebSocket used for: real-time sensor updates, calibration live readings, relay state changes
- `Web.sendRapidReading(sensor_index, raw, avg, percent)` — pushes calibration data to WS clients
- Delegates all `/api/*` routes to `ApiEndpoints::registerRoutes()`
- Device-specific behavior injected via callbacks: `onStatus()`, `onSensorRead()`, `onRelayControl()`, `onCalibration()`, `onConfigUpdate()`, `onReboot()`

### 8.2 API Endpoints (`shared/web/api_endpoints.h/.cpp`)

**Class:** `ApiEndpoints` (static methods, no instance)

All routes respond with JSON. Errors return HTTP codes mapped from `ErrorCode` via `getHttpStatus()`.

| Route | Method | Description |
|-------|--------|-------------|
| `/api/status` | GET | Device status, all sensor readings, relay states |
| `/api/system/info` | GET | Firmware version, chip info, free heap |
| `/api/system/reboot` | POST | Reboot device |
| `/api/system/ota` | POST | OTA firmware upload (multipart) |
| `/api/sensors` | GET | All sensors: raw, percent, name, hw_connected |
| `/api/sensors/{n}` | GET | Single sensor |
| `/api/sensors/{n}/calibrate` | POST | `{"action":"dry"}` or `{"action":"wet"}` |
| `/api/config` | GET/POST | Full `DeviceConfig` as JSON |
| `/api/config/{section}` | GET/POST | Section: `wifi`, `mqtt`, `sensors`, `relays`, `espnow`, `power` |
| `/api/relays` | GET | All relay states (Greenhouse) |
| `/api/relays/{n}` | POST | `{"state":true,"duration":60}` (Greenhouse) |
| `/api/devices` | GET | Paired devices list (Hub) |
| `/api/devices/{n}` | DELETE | Unpair device (Hub) |
| `/api/wifi/networks` | GET | WiFi network scan results |
| `/api/wifi/connect` | POST | `{"ssid":"...","password":"..."}` |

### 8.3 RemoteWeb (`remote/src/remote_web.h/.cpp`)

**Class:** `RemoteWeb`
- Lightweight dedicated web server for Remote; does NOT use the shared `WebServer`
- ~18 routes, ~10KB HTML stored in PROGMEM
- Routes: `/` (status), `/settings`, `/api/status`, `/api/config`, `/api/wifi/connect`, `/api/wifi/networks`, `/api/mqtt`, `/api/sensor`, `/api/mode`, `/api/reboot`, `/api/return`, `/api/espnow/import`
- Two HTML pages: `REMOTE_STATUS_HTML` and `REMOTE_SETTINGS_HTML`

### 8.4 OTA Manager (`shared/utils/ota_manager.h/.cpp`)

**Class:** `OtaManager Ota`
- Registered on `/api/system/ota`
- State machine: `IDLE → RECEIVING → VERIFYING → COMPLETE / ERROR`
- Progress pushed via `OtaProgressCallback`
- Schedules reboot (default 1 s delay) after successful flash via `checkPendingReboot()` in loop

---

## 9. Device Registry (Hub Only)

**Class:** `DeviceRegistry` (`hub/src/device_registry.h/.cpp`)
- Stores up to `ESPNOW_MAX_PEERS` (20) paired device records
- Each record: MAC, name, type, capabilities, rssi, last_seen, online status, cached sensor readings (moisture_percent, temperature, humidity, battery_percent)
- `onDeviceAnnounce()` / `onPairRequest()` — add/update device
- `checkTimeouts()` — marks devices offline if no message within timeout window
- Persists paired device list to NVS (`Preferences` namespace `"iwmp_reg"`)

---

## 10. Shared Utilities

### 10.1 Logger (`shared/utils/logger.h/.cpp`)

**Class:** `Logger Log`
- Levels: `DEBUG`, `INFO`, `WARN`, `ERROR`
- Macros: `LOG_D(TAG, fmt, ...)`, `LOG_I`, `LOG_W`, `LOG_E`
- Optional: colors (ANSI), timestamps (`millis()`)
- Hub: DEBUG level; Remote: WARN level; Greenhouse: INFO level

### 10.2 Watchdog (`shared/utils/watchdog.h/.cpp`)

**Class:** `Watchdog`
- `Watchdog.begin(timeout_sec)` — initializes hardware WDT
- `Watchdog.feed()` — called every loop iteration
- Hub/Greenhouse timeout: 60 s (sensor reads + WiFi + MQTT can take ~500 ms per iteration)

### 10.3 Error Codes (`shared/utils/error_codes.h`)

`ErrorCode` enum (uint16):
- General (1–99), Config (100–199), WiFi/Net (200–299), MQTT (300–399), ESP-NOW (400–499), Sensor (500–599), Relay (600–699), OTA (700–799), Web (800–899), Automation (900–999)
- `getErrorMessage(ErrorCode)` — human-readable string
- `getHttpStatus(ErrorCode)` — maps to HTTP 200/400/403/404/409/429/500/501/503

### 10.4 Error Tracker (`shared/utils/error_tracker.h/.cpp`)

**Class:** `ErrorTracker Errors`
- Circular buffer of 16 `ErrorRecord` entries (code, severity, context, line, timestamp)
- `IWMP_ERROR(code)`, `IWMP_WARNING(code)`, `IWMP_CRITICAL(code)` macros
- `hasCriticalErrors()`, `totalErrors()`, `timeSinceLastError()`

---

## 11. Build System (`platformio.ini`)

Three environments share a base `[env]` section:

**Shared libraries (all targets):**
- `ArduinoJson ^7.0.0`
- `ESPAsyncWebServer ^3.3.0` (mathieucarbou fork)
- `AsyncTCP ^3.3.0`
- `AsyncMqttClient ^0.9.0`
- `Adafruit ADS1X15 ^2.4.0`
- `Adafruit Unified Sensor ^1.1.9`
- `DHT sensor library ^1.4.4`
- `ClosedCube SHT31D ^1.5.1`
- `DNSServer`, `Preferences`, `WiFi`, `Wire`

**Build flags (all):**
- `-DIWMP_VERSION="1.0.0"`, `-DASYNCWEBSERVER_REGEX=1`, `-Os`, `-std=gnu++14`
- Include paths: `shared`, `shared/config`, `shared/communication`, `shared/sensors`, `shared/calibration`, `shared/web`, `shared/utils`

**Hub (`env:hub`):**
- `board = esp32dev`, `partitions = huge_app.csv` (3 MB app)
- `-DIWMP_DEVICE_TYPE=0 -DIWMP_MAX_SENSORS=16 -DIWMP_HAS_ESPNOW_RX=1 -DIWMP_HAS_DEVICE_REGISTRY=1`
- `build_src_filter`: all `shared/**/*.cpp` + `hub/src/**/*.cpp` + `main_hub.cpp`

**Remote (`env:remote`):**
- `board = esp32-c3-devkitm-1`, native USB CDC (`cdc_on_boot=1`), `partitions = huge_app.csv`
- `-DIWMP_DEVICE_TYPE=1 -DIWMP_MAX_SENSORS=1 -DIWMP_NO_ENVIRONMENTAL=1`
- Ignores: `DHT sensor library`, `ClosedCube SHT31D` (not needed)
- **Explicit whitelist** (not `shared/**/*.cpp`): only `config_manager.cpp`, `wifi_manager.cpp`, `mqtt_manager.cpp`, `espnow_manager.cpp`, `sensor_interface.cpp`, `ads1115_moisture.cpp`, `capacitive_moisture.cpp`, `logger.cpp`, `error_tracker.cpp` + all `remote/src/**/*.cpp`
- `monitor_rts=0 monitor_dtr=0` (CDC quirk)
- `Serial.setTxTimeoutMs(0)` in main to prevent CDC blocking
- 1500 ms startup delay to let USB stack settle before `softAP()`

**Greenhouse (`env:greenhouse`):**
- `board = esp32dev`, `partitions = huge_app.csv`
- `-DIWMP_DEVICE_TYPE=2 -DIWMP_MAX_SENSORS=4 -DIWMP_MAX_RELAYS=8 -DIWMP_MAX_BINDINGS=8 -DIWMP_HAS_AUTOMATION=1`
- `build_src_filter`: all `shared/**/*.cpp` + `greenhouse/src/**/*.cpp` + `main_greenhouse.cpp`

Debug variants (`env:hub_debug`, `env:remote_debug`, `env:greenhouse_debug`) add `-DCORE_DEBUG_LEVEL=5 -DIWMP_DEBUG=1`.

---

## 12. Data Flow

### 12.1 Moisture Sensor → Home Assistant (WiFi Remote)

```
Remote (ESP32-C3)
  └─ MoistureSensor::readPercent()          every 5 s
  └─ reportToHub() HTTP POST /api/...       every 60 s
       → Hub (ESP32-WROOM)
           └─ HubController::onMoistureReading()
           └─ _sensor_cache[n] updated
           └─ publishAggregatedState() MQTT  every 60 s
                → MQTT Broker
                    └─ Home Assistant auto-discovery
                    └─ HA sensor entity updates
```

### 12.2 Moisture Sensor → Home Assistant (Low-Power Remote via ESP-NOW)

```
Remote (ESP32-C3) [wakes from deep sleep]
  └─ PowerModes::begin() — reads RTC wake reason
  └─ MoistureSensor::readPercent()
  └─ EspNow.sendMoistureReading(hub_mac, ...)
       → Hub (ESP32-WROOM)
           └─ HubController::onEspNowReceive()
           └─ onMoistureReading() → _sensor_cache
           └─ publishAggregatedState() MQTT
  └─ PowerModes::enterDeepSleep(300 s)
```

### 12.3 Automated Watering (Greenhouse)

```
Greenhouse (ESP32-WROOM)
  └─ MoistureSensor::readPercent()           every 10 s
  └─ AutomationEngine::update(readings[])
       └─ SensorRelayBinding check:
           if (moisture < dry_threshold)
               RelayManager::setRelay(relay_idx, true, max_runtime)
           if (moisture > wet_threshold)
               RelayManager::setRelay(relay_idx, false)
  └─ publishState() MQTT                     every publish_interval
       → MQTT → Home Assistant
```

### 12.4 Manual Relay Command (HA → Hub → Greenhouse)

```
Home Assistant
  └─ MQTT publish → iwetmyplants/{device_id}/relay/set
       → Hub::onMqttMessage()
       └─ Hub::sendRelayCommand(greenhouse_mac, relay, state, duration)
           └─ EspNow.sendRelayCommand(mac, ...)   [ACK required]
               → Greenhouse::onEspNowReceive()
               └─ Greenhouse::onRelayCommand()
               └─ RelayManager::setRelay()
               └─ EspNow.sendAck()
```

### 12.5 Device Pairing

```
New Remote/Greenhouse
  └─ EspNow.broadcast(AnnounceMsg)           on boot, broadcast
       → Hub::onEspNowReceive()
       └─ Hub::onDeviceAnnounce()
       └─ DeviceRegistry::addOrUpdate()

  OR explicit pair:
  └─ EspNow.sendPairRequest(hub_mac, ...)    [ACK required]
       → Hub::onPairRequest()
       └─ DeviceRegistry::addDevice()
       └─ EspNow.sendPairResponse(mac, accepted, channel, interval)
```

---

## 13. Pin Assignments Summary

### Hub (ESP32-WROOM)
| Function | GPIO |
|----------|------|
| I2C-0 SDA | 21 |
| I2C-0 SCL | 22 |
| I2C-1 SDA | 16 |
| I2C-1 SCL | 17 |
| Direct ADC | 32,33,34,35,36,37,38,39 |
| MUX SIG | 33 |
| MUX S0–S3 | 14, 27, 26, 25 |
| Status LED | 4 |
| Config Button | 0 (Boot) |
| ADS1115-0 | 0x48 on Wire |
| ADS1115-1 | 0x49 on Wire1 |

### Remote (ESP32-C3 SuperMini)
| Function | GPIO |
|----------|------|
| I2C SDA | 8 |
| I2C SCL | 9 |
| Direct ADC | 0 |
| Battery ADC | 1 |
| Status LED | 7 |
| Wake Button | 5 |
| Power Detect | 6 |

### Greenhouse (ESP32-WROOM)
| Function | GPIO |
|----------|------|
| I2C SDA | 21 |
| I2C SCL | 22 |
| Direct ADC | 32,33,34,35 |
| Relay 0–3 | 16,17,18,19 |
| Relay 4–7 | 23,25,26,27 |
| DHT Sensor | 4 |
| Status LED | 2 |

---

## 14. File Structure

```
iWetMyPlants/
├── src/
│   ├── main_hub.cpp            Hub entry point
│   ├── main_remote.cpp         Remote entry point
│   └── main_greenhouse.cpp     Greenhouse entry point
│
├── hub/src/
│   ├── hub_controller.h/.cpp   HubController, HubState machine
│   └── device_registry.h/.cpp  DeviceRegistry, PairedDeviceInfo
│
├── remote/src/
│   ├── remote_controller.h/.cpp  RemoteController, RemoteState machine
│   ├── remote_web.h/.cpp         RemoteWeb, ~18 routes, PROGMEM HTML
│   └── power_modes.h/.cpp        PowerModes, deep sleep, RTC memory
│
├── greenhouse/src/
│   ├── greenhouse_controller.h/.cpp  GreenhouseController
│   ├── relay_manager.h/.cpp          RelayManager, safety limits
│   └── automation_engine.h/.cpp      AutomationEngine, SensorRelayBinding
│
├── shared/
│   ├── communication/
│   │   ├── message_types.h           All ESP-NOW message structs (wire protocol)
│   │   ├── espnow_manager.h/.cpp     EspNowManager singleton
│   │   ├── wifi_manager.h/.cpp       WifiManager singleton
│   │   ├── mqtt_manager.h/.cpp       MqttManager singleton
│   │   └── improv_serial.h/.cpp      ImprovSerial (USB WiFi provisioning)
│   ├── config/
│   │   ├── config_schema.h           DeviceConfig, all config structs
│   │   ├── config_manager.h/.cpp     ConfigManager Config singleton (NVS)
│   │   └── defaults.h                Default values, initDefaultConfig()
│   ├── sensors/
│   │   ├── sensor_interface.h/.cpp   MoistureSensor base class
│   │   ├── capacitive_moisture.h/.cpp  Direct ADC sensor
│   │   ├── ads1115_moisture.h/.cpp   ADS1115 16-bit I2C ADC sensor
│   │   ├── mux_moisture.h/.cpp       CD74HC4067 MUX sensor
│   │   ├── dht_sensor.h/.cpp         DHT11/22 temperature+humidity
│   │   └── sht_sensor.h/.cpp         SHT30/31/40/41 temperature+humidity
│   ├── calibration/
│   │   ├── calibration_manager.h/.cpp  CalibrationManager, two-point cal
│   │   └── rapid_read.h/.cpp           Rapid reading stream for cal UI
│   ├── web/
│   │   ├── web_server.h/.cpp         WebServer Web singleton (Hub/GH)
│   │   └── api_endpoints.h/.cpp      ApiEndpoints static class, all /api routes
│   └── utils/
│       ├── logger.h/.cpp             Logger Log, LOG_I/W/E/D macros
│       ├── watchdog.h/.cpp           Watchdog, 60 s HW WDT
│       ├── error_codes.h             ErrorCode enum, getErrorMessage/getHttpStatus
│       ├── error_tracker.h/.cpp      ErrorTracker Errors, circular history
│       └── ota_manager.h/.cpp        OtaManager Ota, web-based OTA
│
├── docs/
│   ├── ARCHITECTURE.md           ← this file
│   └── BETA_GUIDE.md             End-user beta testing guide
│
├── reference/
│   └── iwetmyplants_spec.md      Original product specification
│
├── installer/
│   ├── index.html                esp-web-tools installer page
│   ├── version.json              Firmware version manifest
│   └── install/
│       ├── hub.json              Improv manifest for Hub
│       ├── remote.json           Improv manifest for Remote
│       └── greenhouse.json       Improv manifest for Greenhouse
│
├── .github/workflows/
│   └── build-deploy.yml          CI: build all 3 targets, deploy to installer
│
└── platformio.ini                Build configuration (hub, remote, greenhouse + debug variants)
```

---

## 15. Key Design Decisions & Constraints

1. **Remote uses `RemoteWeb` not shared `WebServer`** — The ESP32-C3 has limited flash. The shared WebServer + full API + OTA + WebSocket would overflow. RemoteWeb is a minimal inline-HTML server.

2. **Remote `build_src_filter` is an explicit whitelist** — Using `shared/**/*.cpp` would pull in mux, calibration, OTA, web, etc. and balloon the binary. Only 9 shared files are included.

3. **Hub has dual I2C buses** — To support two ADS1115 devices without address conflict. `Ads1115Input::setWireForAddress(0x48, &Wire)` / `setWireForAddress(0x49, &Wire1)`.

4. **ESP-NOW channel must match WiFi** — When a device runs both WiFi STA and ESP-NOW, both must be on the same channel. Channel is set from the router during WiFi connect and propagated to ESP-NOW.

5. **Watchdog is 60 s not 30 s** — Sensor reads + WiFi + MQTT can take ~500 ms per iteration; 30 s was causing spurious resets under load.

6. **`Serial.setTxTimeoutMs(0)` on Remote** — Without this, CDC writes stall ~100 ms when no USB host is connected. This was starving the DNS server and breaking captive portal detection.

7. **1500 ms startup delay on Remote** — USB CDC on ESP32-C3 generates DMA activity during enumeration. Calling `softAP()` too soon silently prevents AP beacons from transmitting.

8. **Relay active-low default** — Most off-the-shelf relay modules (e.g., 4/8-relay boards) trigger on LOW. `active_low=true` is the default.

9. **ADS1115 values scaled ×2** — Raw ADS1115 reads are 15-bit (sign bit removed for unipolar). Multiplying by 2 gives a full 16-bit range, keeping calibration values consistent with the 16-bit type.

10. **Hub uses `huge_app.csv` partition** — The Hub firmware with full WebServer + MQTT + ESP-NOW + OTA + local sensors hits the 1.5 MB default app partition limit. `huge_app.csv` gives 3 MB.
