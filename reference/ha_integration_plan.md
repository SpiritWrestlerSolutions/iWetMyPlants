# iWetMyPlants — Home Assistant Custom Integration Plan

**Status:** Planning
**Target:** HACS-compatible custom component (`custom_components/iwetmyplants`)
**Scope:** Full bidirectional control replacing the need for any embedded web UI

---

## 1. Goals

| Goal | Detail |
|---|---|
| Real-time sensor data | Moisture, temperature, humidity, battery per device |
| Full relay control | On/off, timed auto-off, safety limits |
| Sensor–relay binding | Bind a moisture sensor to a pump relay for automations |
| Calibration | Trigger dry/wet calibration points from HA UI |
| Device management | See all paired Remotes/Greenhouses from Hub |
| OTA updates | Trigger firmware update from HA |
| Zero embedded UI dependency | Everything controllable without visiting the device IP |

---

## 2. Communication Strategy

The firmware supports two channels — use both for their strengths:

```
Home Assistant
    │
    ├─ REST API (HTTP) ──────► Hub at local IP
    │   • Configuration reads/writes          /api/config/*
    │   • Relay control                       POST /api/relays/{n}
    │   • Calibration commands                POST /api/sensors/{n}/calibrate/{dry|wet}
    │   • System actions (reboot, OTA)        POST /api/system/*
    │   • Device registry (Hub only)          GET  /api/devices
    │   • Polled at 30s interval via coordinator
    │
    └─ MQTT (pub/sub) ───────► Broker → Hub → Remotes/Greenhouse
        • Real-time sensor readings           iwmp/{device_id}/moisture/{n}
        • Relay state changes                 iwmp/{device_id}/relay/{n}/state
        • Battery / heartbeat                 iwmp/{device_id}/battery
        • Online/offline LWT                  iwmp/{device_id}/status
        • HA auto-discovery (already in FW)   homeassistant/sensor/.../config
```

**Why both?**
MQTT gives low-latency real-time updates without polling. REST gives reliable config/control with proper HTTP status codes and responses. Calibration in particular needs a synchronous ACK.

---

## 3. Integration File Structure

```
custom_components/iwetmyplants/
├── __init__.py               Hub setup, entry loading, coordinator init
├── manifest.json             Version, dependencies, codeowners
├── const.py                  All constants (domain, endpoints, defaults)
├── config_flow.py            UI setup: mDNS auto-discovery + manual IP
├── coordinator.py            DataUpdateCoordinator — REST polling every 30s
├── hub.py                    Hub API client (thin async HTTP wrapper)
│
├── sensor.py                 Moisture %, temp, humidity, battery, RSSI, uptime
├── binary_sensor.py          Online/offline, low battery, relay active
├── switch.py                 Relay on/off switches
├── button.py                 Reboot, factory reset, calibrate dry, calibrate wet
├── number.py                 Calibration values, relay auto-off duration
├── select.py                 Sensor→relay binding
├── update.py                 Firmware update entity (OTA)
│
├── diagnostics.py            HA diagnostics redaction (strips passwords/keys)
├── strings.json              UI string keys
└── translations/
    └── en.json               English UI strings
```

---

## 4. REST API Mapping (Firmware → Integration)

All endpoints already exist in the firmware. No new endpoints needed for phase 1.

| HTTP | Endpoint | Integration Use |
|---|---|---|
| GET | `/api/status` | Coordinator poll — device name, WiFi RSSI, uptime, sensor count, relay count |
| GET | `/api/system/info` | Device info entity — MAC, firmware version, chip model, free heap |
| GET | `/api/sensors` | All moisture sensors — raw, %, calibration values |
| POST | `/api/sensors/{n}/calibrate/dry` | Button: Set Dry Point |
| POST | `/api/sensors/{n}/calibrate/wet` | Button: Set Wet Point |
| GET | `/api/relays` | All relay states and configs |
| POST | `/api/relays/{n}` `{"state":true}` | Switch: Relay control |
| GET | `/api/config` | Full device config |
| POST | `/api/config/sensors` | Update sensor name, thresholds, interval |
| POST | `/api/config/relays` | Update relay name, safety limits |
| GET | `/api/devices` | Hub only — paired Remote/Greenhouse node list |
| POST | `/api/system/reboot` | Button: Reboot |
| POST | `/api/system/ota` | Update entity: push firmware binary |
| GET | `/api/wifi/networks` | (Optional) WiFi scan for reconfiguration |

### Endpoints to add in firmware (Phase 2)

| HTTP | Endpoint | Purpose |
|---|---|---|
| GET | `/api/sensors/{n}/calibration` | Read current dry/wet values explicitly |
| POST | `/api/relays/{n}` `{"state":true,"duration_sec":300}` | Timed relay (auto-off) |
| GET | `/api/system/ota/status` | Check if OTA is in progress |
| POST | `/api/sensors/{n}/binding` `{"relay_index":0}` | Bind sensor to relay |

---

## 5. MQTT Topic Structure

The firmware already publishes HA auto-discovery. The integration should **consume existing MQTT** rather than requiring config changes.

```
iwmp/{device_id}/                     Device root (from config.identity.device_id)
    status                            "online" / "offline" (LWT)
    moisture/{sensor_index}           JSON: {"raw":2048,"percent":65,"name":"Plant 1"}
    temperature                       JSON: {"value":23.5,"unit":"C"}
    humidity                          JSON: {"value":65.2}
    battery                           JSON: {"voltage_mv":3800,"percent":85,"low":false}
    relay/{relay_index}/state         JSON: {"state":true,"name":"Pump 1"}
    heartbeat                         JSON: {"uptime_sec":3600,"free_heap":180000,"rssi":-52}

homeassistant/sensor/{device_id}_moisture_{n}/config    HA auto-discovery payload
homeassistant/switch/{device_id}_relay_{n}/config       HA auto-discovery payload
```

The integration will **subscribe** to `iwmp/+/#` and update entity states in real time. If MQTT is not configured on the Hub, it falls back to REST polling only.

---

## 6. Entity Catalogue

### 6a. Per Hub Device

| Entity Type | Name | Source | Notes |
|---|---|---|---|
| `sensor` | WiFi Signal | REST `/api/status` | device class: signal_strength, dBm |
| `sensor` | Uptime | REST `/api/status` | device class: duration, seconds |
| `sensor` | Free Heap | REST `/api/system/info` | diagnostic, bytes |
| `sensor` | Firmware Version | REST `/api/system/info` | diagnostic |
| `binary_sensor` | Online | MQTT LWT / REST | device class: connectivity |
| `button` | Reboot | REST POST | confirmation dialog |
| `button` | Factory Reset | REST POST | confirmation + extra confirm |
| `update` | Firmware | REST POST OTA | shows current vs latest version |

### 6b. Per Moisture Sensor

| Entity Type | Name | Source | Notes |
|---|---|---|---|
| `sensor` | Moisture | MQTT / REST | device class: moisture, % (0–100) |
| `sensor` | Raw ADC | REST | diagnostic, integer |
| `number` | Dry Calibration Value | REST config | 0–4095 |
| `number` | Wet Calibration Value | REST config | 0–4095 |
| `button` | Calibrate Dry Point | REST POST | captures current reading as dry |
| `button` | Calibrate Wet Point | REST POST | captures current reading as wet |
| `select` | Bound Relay | REST config | dropdown: None, Relay 0–3 |
| `number` | Sample Interval | REST config | seconds, 60–3600 |

### 6c. Per Relay

| Entity Type | Name | Source | Notes |
|---|---|---|---|
| `switch` | Relay (Pump) | MQTT / REST | on/off, named from config |
| `binary_sensor` | Relay Active | MQTT | device class: running |
| `number` | Auto-off Duration | REST config | seconds (0 = indefinite) |
| `number` | Max On Time | REST config | safety limit, seconds |
| `number` | Cooldown | REST config | seconds between activations |

### 6d. Per Paired Remote Node (via Hub device registry)

| Entity Type | Name | Source | Notes |
|---|---|---|---|
| `sensor` | Moisture | MQTT (forwarded by Hub) | child device linked to Hub |
| `sensor` | Battery | MQTT | device class: battery, % |
| `sensor` | Signal (RSSI) | MQTT | device class: signal_strength |
| `binary_sensor` | Online | MQTT LWT | device class: connectivity |
| `binary_sensor` | Low Battery | MQTT | device class: battery |
| `button` | Wake Device | REST (via Hub ESP-NOW forward) | force wake from deep sleep |

### 6e. Per Greenhouse Node

All sensor and relay entities above, plus:

| Entity Type | Name | Source | Notes |
|---|---|---|---|
| `sensor` | Temperature | MQTT | device class: temperature, °C |
| `sensor` | Humidity | MQTT | device class: humidity, % |
| `switch` | Relay 0–3 | MQTT / REST | individual pump/fan control |
| `select` | Automation Mode | REST config | Off / Moisture-triggered / Schedule |

---

## 7. Config Flow (Setup UI)

### Step 1: Discovery or Manual
```
Option A: Auto-discover via mDNS
  → Scans for _iwmp._tcp.local
  → Shows list of found Hubs
  → User selects one

Option B: Manual entry
  → User enters IP address
  → Integration pings /api/system/info
  → Confirms device name + firmware version
```

### Step 2: Authentication (if enabled)
```
→ Optional API key / basic auth (firmware feature to add in Phase 2)
```

### Step 3: MQTT (Optional)
```
→ Auto-detected if MQTT is configured on Hub (from /api/config/mqtt)
→ User can override broker address/credentials
→ Can be skipped — REST polling only
```

### Step 4: Confirm
```
→ Shows device name, IP, firmware version, sensor/relay count
→ Creates config entry
→ Loads all entities
```

---

## 8. Services (HA Actions)

These are custom services callable from automations and scripts:

```yaml
iwetmyplants.calibrate_sensor:
  fields:
    device_id: Hub device
    sensor_index: 0-7
    point: "dry" | "wet"

iwetmyplants.set_relay:
  fields:
    device_id: Hub or Greenhouse device
    relay_index: 0-3
    state: true | false
    duration_seconds: 0-86400  # 0 = indefinite

iwetmyplants.bind_sensor_to_relay:
  fields:
    device_id: Hub device
    sensor_index: 0-7
    relay_index: 0-3 | null  # null = unbind

iwetmyplants.wake_remote:
  fields:
    device_id: Remote node device

iwetmyplants.update_firmware:
  fields:
    device_id: Hub/Greenhouse device
    # firmware fetched from GitHub releases automatically

iwetmyplants.reboot_device:
  fields:
    device_id: any device
```

---

## 9. Device Registry (HA)

Each physical device appears as a separate HA **Device** with proper relationships:

```
Hub (ESP32)
├── Manufacturer: iWetMyPlants
├── Model: Hub v2.0
├── Firmware: 2.0.0
├── Identifiers: MAC address
├── Connections: [(network, IP address)]
└── Via: (none — this is the root)

Remote Node (ESP32-C3) — discovered via Hub
├── Manufacturer: iWetMyPlants
├── Model: Remote v2.0
├── Identifiers: MAC address
├── Via Device: Hub (shows relationship in HA)
└── Suggested Area: Plant location from device name

Greenhouse Node (ESP32) — discovered via Hub
├── Manufacturer: iWetMyPlants
├── Model: Greenhouse v2.0
├── Via Device: Hub
└── Identifiers: MAC address
```

---

## 10. Firmware Changes Needed

### 10a. Required for Phase 1

1. **`/api/system/info` must include `version` field**
   Currently returns `chip_model`, `mac_address`, `free_heap`, etc. but needs:
   ```json
   { "version": "2.0.0", "device_id": "hub_abc123", "device_type": 0 }
   ```
   Fix: add `config.identity.firmware_version` and `device_id` to `buildSystemInfoJson()`

2. **`/api/status` must include relay states**
   Currently returns sensor count and relay count but not actual states.
   Fix: include relay on/off in status response.

3. **mDNS advertisement**
   Hub should advertise `_iwmp._tcp.local` with TXT records for IP and version.
   Fix: add `MDNS.addService("iwmp", "tcp", 80)` + TXT records in WiFi setup.

### 10b. Required for Phase 2

4. **Timed relay endpoint**
   `POST /api/relays/{n}` should accept optional `duration_sec` for auto-off.

5. **Sensor–relay binding in config schema**
   `config_schema.h` should add `bound_relay_index` (int8_t, -1 = none) to `MoistureSensorConfig`.

6. **OTA status endpoint**
   `GET /api/system/ota/status` → `{"in_progress": false, "last_result": "success"}`

7. **API authentication**
   Optional API key via header `X-API-Key`. Store in config NVS.

---

## 11. Code Issues Found During Review

These should be fixed before or alongside integration work.

### Critical

| File | Line | Issue | Fix |
|---|---|---|---|
| `espnow_manager.cpp` | sendWithAck() | `while(){ delay(1) }` blocks main task — can starve web server | Replace with non-blocking state machine or move to separate FreeRTOS task |
| `espnow_manager.cpp` | sendWithRetry() | `delay(retry_delay_ms)` blocks between retries | Queue retries with millis()-based timer |
| `sensor_interface.cpp` | readRawAveraged() | Blocks 200ms+ (10 samples × 20ms) during averaged reads | Move to background task or reduce sample count |

### Moderate

| File | Line | Issue | Fix |
|---|---|---|---|
| `mqtt_manager.cpp` | onMqttMessage() | `new char[len+1]` in callback → leak if anything throws | Use `std::string payload_str(payload, len)` instead |
| `config_manager.cpp` | getMoistureSensorMutable() | Returns reference to static dummy on out-of-bounds | Return pointer, nullptr on OOB |
| `relay_manager.h` | setRelay() | No bounds check on relay index array access | Add `if (index >= _relay_count) return false` guard |
| `api_endpoints.cpp` | handlePostConfig() | `doc.containsKey()` is deprecated in ArduinoJson v7 | Replace with `doc["key"].is<JsonObject>()` |
| `api_endpoints.cpp` | Multiple handlers | Same `containsKey` deprecation throughout parsers | Batch replace all instances |

### Minor

| File | Issue | Fix |
|---|---|---|
| `greenhouse_controller.h` | `_last_temperature` / `_last_humidity` read/write race | Wrap in `portMUX_TYPE` or `std::atomic` |
| Multiple files | Magic numbers (10000ms, 20 samples, etc.) | Consolidate in `const.h` |
| `hub_controller.h` | `DEVICE_CHECK_INTERVAL_MS = 10000` defined inline | Move to shared constants |
| `web_server.cpp` | `JsonDocument doc` allocated on stack in every handler | Use heap-allocated `JsonDocument` or a shared pool |

---

## 12. Implementation Phases

### Phase 1 — Read-only MVP (2–3 sessions)
- Integration scaffold (`__init__`, `manifest`, `const`, `coordinator`)
- Config flow: manual IP entry + basic mDNS
- Sensor entities: moisture, temperature, humidity, battery, RSSI, uptime
- Binary sensor: online/offline
- Fix 3 firmware items from §10a

**Outcome:** All sensor data visible in HA dashboards and automations

### Phase 2 — Full Control (2–3 sessions)
- Switch entities: relay control
- Button entities: reboot, factory reset, calibrate dry/wet
- Number entities: calibration values, auto-off duration, safety limits
- Select entity: sensor→relay binding
- Custom services (§8)
- Firmware: timed relay, binding endpoint, OTA status

**Outcome:** No need to ever open the device web UI for operation

### Phase 3 — Polish & HACS (1–2 sessions)
- Update entity (OTA via HA)
- Diagnostics redaction
- Full translations
- HACS submission (hacs.json, info.md, icon)
- Fix all firmware code issues from §11
- API authentication

**Outcome:** HACS-publishable, production-ready

---

## 13. Example HA Automation Using Integration

```yaml
# Water Plant 1 when moisture drops below 30% — 5 minute safety limit
automation:
  alias: "Water Plant 1"
  trigger:
    - platform: numeric_state
      entity_id: sensor.plant_1_moisture
      below: 30
  condition:
    - condition: state
      entity_id: binary_sensor.pump_1_active
      state: "off"
  action:
    - service: iwetmyplants.set_relay
      data:
        device_id: "{{ device_id('sensor.plant_1_moisture') }}"
        relay_index: 0
        state: true
        duration_seconds: 300
    - wait_for_trigger:
        platform: numeric_state
        entity_id: sensor.plant_1_moisture
        above: 60
      timeout: "00:10:00"
    - service: iwetmyplants.set_relay
      data:
        device_id: "{{ device_id('sensor.plant_1_moisture') }}"
        relay_index: 0
        state: false
```

---

## 14. Reference Links

- [HA Custom Component Developer Docs](https://developers.home-assistant.io/docs/creating_component_index)
- [DataUpdateCoordinator pattern](https://developers.home-assistant.io/docs/integration_fetching_data)
- [Config Flow](https://developers.home-assistant.io/docs/config_entries_config_flow_handler)
- [Device Registry](https://developers.home-assistant.io/docs/device_registry_index)
- [HACS submission requirements](https://hacs.xyz/docs/publish/start)
- Firmware REST API: `shared/web/api_endpoints.h`
- MQTT topics: `shared/communication/mqtt_manager.cpp`
- Message protocol: `shared/communication/message_types.h`
- Config schema: `shared/config/config_schema.h`
