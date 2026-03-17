# iWetMyPlants v1.0 Integration Test Checklist

## Build Verification

| Environment | Flash | RAM | Status |
|------------|-------|-----|--------|
| Hub (ESP32-WROOM) | 94.8% | 17.2% | ✅ |
| Remote (ESP32-C3) | ~40% | ~15% | ✅ |
| Greenhouse (ESP32-WROOM) | 96.9% | 17.4% | ✅ |

---

## Hub Device Tests

### Boot & Initialization
- [ ] Device boots without crash
- [ ] Serial output shows version banner
- [ ] Config manager loads/creates config
- [ ] Watchdog initializes

### WiFi Connection
- [ ] Connects to configured WiFi network
- [ ] Falls back to AP mode if no WiFi configured
- [ ] AP mode shows captive portal
- [ ] AP SSID format: `IWMP-Hub-XXXXXX`

### Web Interface
- [ ] Dashboard loads at `http://<ip>/`
- [ ] Settings page accessible
- [ ] WiFi settings page works
- [ ] MQTT settings page works
- [ ] Sensors page shows data
- [ ] API endpoints respond correctly

### MQTT (if configured)
- [ ] Connects to MQTT broker
- [ ] Publishes Home Assistant discovery messages
- [ ] Publishes availability (online/offline)
- [ ] Subscribes to relay command topics
- [ ] Reconnects after broker disconnect

### ESP-NOW Reception
- [ ] Receives announce messages from remotes
- [ ] Processes pair requests
- [ ] Receives moisture readings
- [ ] Receives battery status messages
- [ ] Updates device registry

### OTA Updates
- [ ] OTA endpoint accepts firmware upload
- [ ] Progress tracking works via `/api/system/ota/progress`
- [ ] Successful update triggers reboot
- [ ] Failed update reports error

### API Endpoints
- [ ] `GET /api/status` returns device info
- [ ] `GET /api/system/info` returns chip info
- [ ] `GET /api/system/errors` returns error history
- [ ] `POST /api/system/reboot` triggers reboot
- [ ] `GET /api/sensors` returns sensor list
- [ ] `GET /api/config` returns configuration
- [ ] `POST /api/config` updates configuration
- [ ] `GET /api/devices` returns paired devices

---

## Remote Device Tests

### Boot & Initialization
- [ ] Device boots without crash
- [ ] Detects power source (battery vs USB)
- [ ] Config manager loads/creates config

### Battery Mode (Quick Read Cycle)
- [ ] Wakes from deep sleep
- [ ] Reads sensor
- [ ] Sends ESP-NOW message to hub
- [ ] Returns to deep sleep
- [ ] Sleep duration adjusts based on battery level

### Powered Mode
- [ ] Continuous operation when USB powered
- [ ] Sends readings at configured interval
- [ ] Switches to battery mode if power disconnected

### Config Mode
- [ ] Enters config mode on first boot
- [ ] Enters config mode on button press
- [ ] AP mode works for configuration
- [ ] Web interface accessible
- [ ] Timeout returns to normal operation

### ESP-NOW Transmission
- [ ] Sends moisture readings to hub
- [ ] Sends battery status
- [ ] Retries failed transmissions
- [ ] Broadcasts if hub not configured

### Sensor Reading
- [ ] Capacitive moisture sensor reads correctly
- [ ] Calibration values applied correctly
- [ ] Raw-to-percent conversion accurate

---

## Greenhouse Manager Tests

### Boot & Initialization
- [ ] Device boots without crash
- [ ] Config manager loads/creates config
- [ ] Relays initialize to safe (OFF) state
- [ ] Watchdog initializes

### WiFi Connection
- [ ] Same tests as Hub

### Web Interface
- [ ] Dashboard loads
- [ ] Relay control page works
- [ ] Manual relay toggle functions
- [ ] Emergency stop works

### Relay Control
- [ ] Relays turn on/off via web API
- [ ] Relays turn on/off via MQTT
- [ ] Max runtime timeout triggers OFF
- [ ] Cooldown period enforced
- [ ] Daily activation limit works
- [ ] Lockout clears correctly

### Automation Engine
- [ ] Sensor-to-relay bindings execute
- [ ] Hysteresis prevents rapid cycling
- [ ] Max runtime per binding respected
- [ ] Enable/disable toggle works

### Environmental Sensors
- [ ] DHT11/DHT22 reads temperature
- [ ] DHT11/DHT22 reads humidity
- [ ] SHT30/31 reads via I2C
- [ ] Invalid readings handled (NaN check)

### Moisture Sensors
- [ ] Multiple sensors read correctly
- [ ] Individual calibration per sensor
- [ ] Readings published to MQTT

### ESP-NOW Reception
- [ ] Receives relay commands from hub
- [ ] Receives moisture readings (for automation)

---

## Cross-Device Integration Tests

### Hub + Remote Pairing
- [ ] Remote sends pair request
- [ ] Hub accepts and adds peer
- [ ] Subsequent readings routed correctly
- [ ] Device appears in Hub's device list

### Hub + Greenhouse Communication
- [ ] Greenhouse registers with Hub
- [ ] Hub can send relay commands
- [ ] Greenhouse status visible on Hub

### MQTT + Home Assistant
- [ ] Sensors appear in HA automatically
- [ ] Relay controls work from HA
- [ ] Entity names match configuration
- [ ] State updates in real-time

### Calibration Flow
- [ ] WebSocket connects for rapid readings
- [ ] Live readings display correctly
- [ ] Dry point capture works
- [ ] Wet point capture works
- [ ] Calibration saves to NVS
- [ ] New calibration applies immediately

### OTA Update Flow
- [ ] Upload firmware via web interface
- [ ] Progress bar updates
- [ ] Device reboots with new firmware
- [ ] Configuration preserved after update

---

## Error Handling Tests

### Network Errors
- [ ] WiFi disconnect handled gracefully
- [ ] MQTT disconnect triggers reconnect
- [ ] ESP-NOW failures logged

### Sensor Errors
- [ ] Missing sensor detected
- [ ] I2C errors logged
- [ ] ADC errors handled

### Safety Features
- [ ] Relay safety timeout triggers
- [ ] Emergency stop halts all relays
- [ ] Watchdog reset on hang

### Error Tracking
- [ ] Errors logged with severity
- [ ] Error history accessible via API
- [ ] Critical errors flagged

---

## Performance Tests

### Memory
- [ ] No memory leaks over 24h operation
- [ ] Free heap stays above 30KB

### Timing
- [ ] Sensor reads complete in <100ms
- [ ] ESP-NOW send/receive <50ms
- [ ] Web page loads <2s

### Battery Life (Remote)
- [ ] Deep sleep current <10µA
- [ ] Quick read cycle completes <2s
- [ ] Battery lasts expected duration

---

## Notes

_Use this section to document any issues found during testing_

```
Date:
Tester:
Issues Found:
-
-
-
```
