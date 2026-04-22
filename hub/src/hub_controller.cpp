/**
 * @file hub_controller.cpp
 * @brief Hub device main controller implementation
 */

#include "hub_controller.h"
#include "config_manager.h"
#include "logger.h"
#include "api_endpoints.h"
#include "web_server.h"
#include "calibration_manager.h"
#include "sensor_interface.h"
#include "mux_moisture.h"
#include <Wire.h>
#include <Preferences.h>
#include "defaults.h"
#include "watchdog.h"

namespace iwmp {

// Global hub controller instance
HubController Hub;

static constexpr const char* TAG = "Hub";

void HubController::begin() {
    LOG_I(TAG, "Initializing Hub controller");
    enterState(HubState::BOOT);
}

void HubController::loop() {
    switch (_state) {
        case HubState::BOOT:
            handleBootState();
            break;
        case HubState::LOAD_CONFIG:
            handleLoadConfigState();
            break;
        case HubState::WIFI_CONNECT:
            handleWifiConnectState();
            break;
        case HubState::MQTT_CONNECT:
            handleMqttConnectState();
            break;
        case HubState::AP_MODE:
            handleApModeState();
            break;
        case HubState::OPERATIONAL:
            handleOperationalState();
            break;
    }
}

void HubController::enterState(HubState new_state) {
    if (_state == new_state) {
        return;
    }

    LOG_I(TAG, "State: %d -> %d", (int)_state, (int)new_state);
    _state = new_state;
    _state_enter_time = millis();
}

void HubController::handleBootState() {
    LOG_I(TAG, "Boot state - initializing systems");
    enterState(HubState::LOAD_CONFIG);
}

void HubController::handleLoadConfigState() {
    // Initialize device registry
    _registry.begin();

    // Initialize local sensors (optional sensors attached to hub)
    initializeLocalSensors();

    // Initialize ESP-NOW for receiving data from remotes
    const auto& espnow_cfg = Config.getEspNow();
    if (espnow_cfg.enabled) {
        uint8_t channel = espnow_cfg.channel;
        if (channel == 0) {
            channel = WiFiMgr.getCurrentChannel();
        }

        if (EspNow.begin(channel)) {
            LOG_I(TAG, "ESP-NOW initialized on channel %d", channel);

            // Set up receive callback
            EspNow.onMessage([this](const uint8_t* mac, const MessageHeader* header,
                                     const uint8_t* payload, size_t len) {
                onEspNowReceive(mac, (const uint8_t*)header, sizeof(MessageHeader) + len);
            });

            // Add broadcast peer for announcements
            EspNow.addPeer(BROADCAST_MAC);
        } else {
            LOG_E(TAG, "Failed to initialize ESP-NOW");
        }
    }

    // Initialize Improv WiFi Serial provisioning once at boot so the
    // web installer can configure WiFi even if the device reconnects to
    // a saved network and never enters AP mode.
    if (!_improvStarted) {
        _improvStarted = true;
        _improv.setConnectCallback([this](const char* ssid, const char* pwd, String& outUrl) -> bool {
            WiFiMgr.stopCaptivePortal();

            // Keep the WiFi driver alive by switching to AP+STA instead of
            // stopping the AP first.  stopAP() calls softAPdisconnect(true)
            // which runs esp_wifi_stop() — re-starting it with WiFi.mode(STA)
            // can be racy and cause WiFi.begin() to never actually connect.
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(ssid, pwd);

            uint32_t t = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) {
                Watchdog.feed();
                delay(100);
            }

            if (WiFi.status() != WL_CONNECTED) {
                WiFi.mode(WIFI_AP);   // Restore pure AP on failure
                WiFiMgr.startCaptivePortal();
                return false;
            }

            // Connected — now drop the AP cleanly (softAPdisconnect false keeps
            // the WiFi driver running in STA mode)
            WiFi.softAPdisconnect(false);
            WiFi.mode(WIFI_STA);

            // Wait for DHCP to assign an IP so we don't send an invalid URL
            uint32_t t2 = millis();
            while ((uint32_t)WiFi.localIP() == 0 && millis() - t2 < 5000) {
                Watchdog.feed();
                delay(100);
            }

            outUrl = "http://" + WiFi.localIP().toString();
            Config.setWifiCredentials(ssid, pwd);
            Config.save();
            return true;
        });
        _improv.begin(Serial);
        _improv.setDeviceInfo("iWetMyPlants Hub", IWMP_VERSION, "ESP32",
                              Config.getDeviceId());
    }

    // Check if WiFi is configured
    const auto& wifi_cfg = Config.getWifi();
    if (strlen(wifi_cfg.ssid) > 0) {
        enterState(HubState::WIFI_CONNECT);
    } else {
        LOG_I(TAG, "No WiFi configured, entering AP mode");
        enterState(HubState::AP_MODE);
    }
}

void HubController::handleWifiConnectState() {
    // Initialize WiFi if not already done
    static bool wifi_started = false;
    if (!wifi_started) {
        WifiConfig wifi_cfg;
        const auto& cfg = Config.getWifi();
        strlcpy(wifi_cfg.ssid, cfg.ssid, sizeof(wifi_cfg.ssid));
        strlcpy(wifi_cfg.password, cfg.password, sizeof(wifi_cfg.password));
        wifi_cfg.use_static_ip = cfg.use_static_ip;
        wifi_cfg.static_ip = cfg.static_ip;
        wifi_cfg.gateway = cfg.gateway;
        wifi_cfg.subnet = cfg.subnet;
        wifi_cfg.dns = cfg.dns;

        WiFiMgr.begin(wifi_cfg);
        WiFiMgr.connect();
        wifi_started = true;
        LOG_I(TAG, "Connecting to WiFi: %s", wifi_cfg.ssid);
    }

    // Process WiFi manager
    WiFiMgr.loop();

    // Check if connected
    if (WiFiMgr.isConnected()) {
        LOG_I(TAG, "WiFi connected: %s", WiFiMgr.getIP().toString().c_str());
        wifi_started = false;

        // Log ESP-NOW channel info
        if (EspNow.isInitialized()) {
            LOG_I(TAG, "ESP-NOW channel synced to %d", WiFiMgr.getCurrentChannel());
        }

        // Start web server
        if (!Web.isRunning()) {
            Web.begin(Config.getConfig().identity);
            setupWebRoutes();
        }

        // Move to MQTT connect if enabled
        const auto& mqtt_cfg = Config.getMqtt();
        if (mqtt_cfg.enabled && strlen(mqtt_cfg.broker) > 0) {
            enterState(HubState::MQTT_CONNECT);
        } else {
            enterState(HubState::OPERATIONAL);
        }
        return;
    }

    // Keep Improv responsive while awaiting WiFi — ensures the installer
    // can detect the device and (re-)provision even if credentials fail.
    _improv.loop();

    // Check for timeout
    if ((millis() - _state_enter_time) > WIFI_CONNECT_TIMEOUT_MS) {
        LOG_W(TAG, "WiFi connection timeout, entering AP mode");
        wifi_started = false;
        enterState(HubState::AP_MODE);
    }
}

void HubController::handleMqttConnectState() {
    // Initialize MQTT if not done
    static bool mqtt_started = false;
    if (!mqtt_started) {
        const auto& mqtt_cfg = Config.getMqtt();
        const auto& identity = Config.getConfig().identity;

        if (Mqtt.begin(mqtt_cfg, identity)) {
            LOG_I(TAG, "MQTT connecting to %s:%d", mqtt_cfg.broker, mqtt_cfg.port);
            Mqtt.connect();

            // Set up callbacks
            Mqtt.onConnect([this](bool session_present) {
                LOG_I(TAG, "MQTT connected (session: %d)", session_present);
                // Publish discovery after connection
                if (Config.getMqtt().ha_discovery_enabled) {
                    Mqtt.publishDiscovery();
                    for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
                        const auto& scfg = Config.getMoistureSensor(i);
                        if (!scfg.enabled) {
                            Mqtt.removeMoistureDiscovery(i);
                            continue;
                        }
                        char name[32];
                        if (scfg.sensor_name[0] != '\0') {
                            strlcpy(name, scfg.sensor_name, sizeof(name));
                        } else {
                            snprintf(name, sizeof(name), "Plant %d", i + 1);
                        }
                        Mqtt.publishMoistureDiscovery(i, name);
                    }
                    Mqtt.publishTemperatureDiscovery();
                    Mqtt.publishHumidityDiscovery();
                }
                Mqtt.publishAvailability(true);
            });

            Mqtt.onDisconnect([](AsyncMqttClientDisconnectReason reason) {
                LOG_W(TAG, "MQTT disconnected (reason: %d)", (int)reason);
            });

            // Hub-to-Greenhouse relay forwarding via MQTT is a planned feature.
            // The Hub does not currently proxy relay commands to paired Greenhouse
            // devices over ESP-NOW. Enable and implement once device targeting is
            // designed (needs a relay-index → target-MAC mapping in the registry).
            // Mqtt.onRelayCommand([this](uint8_t relay_idx, bool state, uint32_t duration) {
            //     EspNow.sendRelayCommand(target_mac, relay_idx, state, duration);
            // });

            mqtt_started = true;
        } else {
            LOG_E(TAG, "Failed to initialize MQTT");
            mqtt_started = false;
            enterState(HubState::OPERATIONAL);
            return;
        }
    }

    // Process MQTT
    Mqtt.loop();

    // Check if connected
    if (Mqtt.isConnected()) {
        mqtt_started = false;
        enterState(HubState::OPERATIONAL);
        return;
    }

    // Keep Improv responsive during MQTT connection wait
    _improv.loop();

    // Check for timeout
    if ((millis() - _state_enter_time) > MQTT_CONNECT_TIMEOUT_MS) {
        LOG_W(TAG, "MQTT connection timeout, proceeding without MQTT");
        mqtt_started = false;
        enterState(HubState::OPERATIONAL);
    }
}

void HubController::handleApModeState() {
    static bool ap_started = false;

    if (!ap_started) {
        char ap_ssid[32];
        snprintf(ap_ssid, sizeof(ap_ssid), "IWMP-Hub-%s",
                 Config.getDeviceId() + 6);  // Last 6 chars of MAC

        WiFiMgr.begin(WifiConfig{});
        WiFiMgr.startAP(ap_ssid);
        WiFiMgr.startCaptivePortal();

        if (!Web.isRunning()) {
            Web.begin(Config.getConfig().identity);
            setupWebRoutes();
        }

        LOG_I(TAG, "AP mode started: %s @ %s",
              ap_ssid, WiFiMgr.getAPIP().toString().c_str());
        ap_started = true;
    }

    // Improv is already initialized at boot (see handleLoadConfigState)
    // Process WiFi (captive portal DNS) and Improv serial
    WiFiMgr.loop();
    _improv.loop();

    if (Web.isRunning()) {
        Web.update();
    }
    Calibration.update();

    // Improv provisioning succeeded — drain TX then reboot into WIFI_CONNECT
    if (_improv.wasReProvisioned()) {
        LOG_I(TAG, "Improv provisioning complete — rebooting");
        delay(1500);
        ESP.restart();
    }

}

void HubController::handleOperationalState() {
    // Load poll interval from NVS on first entry
    static bool poll_interval_loaded = false;
    if (!poll_interval_loaded) {
        poll_interval_loaded = true;
        loadPollInterval();
        // Kick off an initial poll immediately
        _poll_force = true;
        // Notify the installer browser that the device is already connected
        _improv.broadcastProvisioned("http://" + WiFiMgr.getIP().toString());
    }

    // Process all subsystems
    WiFiMgr.loop();

    // Update web server (OTA reboot, calibration WebSocket)
    if (Web.isRunning()) {
        Web.update();
    }

    if (Mqtt.isInitialized()) {
        Mqtt.loop();
    }

    if (EspNow.isInitialized()) {
        EspNow.update();
    }

    Calibration.update();

    // Background sensor polling (sequential, non-blocking)
    doBackgroundPoll();

    // Check device timeouts periodically
    if ((millis() - _last_device_check_time) > DEVICE_CHECK_INTERVAL_MS) {
        _last_device_check_time = millis();
        checkDeviceTimeouts();
    }

    // Publish aggregated state periodically
    if (Mqtt.isConnected()) {
        const auto& mqtt_cfg = Config.getMqtt();
        uint32_t publish_interval = mqtt_cfg.publish_interval_sec * 1000UL;
        if (publish_interval > 0 && (millis() - _last_publish_time) > publish_interval) {
            _last_publish_time = millis();
            publishAggregatedState();
        }
    }

    // Improv WiFi Serial — allow browser to re-provision even when already connected
    _improv.loop();
    if (_improv.wasReProvisioned()) {
        LOG_I(TAG, "Improv re-provisioning complete — rebooting");
        delay(1500);
        ESP.restart();
    }

    // Check for config button (force AP mode)
    if (isConfigButtonPressed()) {
        LOG_I(TAG, "Config button pressed, entering AP mode");
        enterState(HubState::AP_MODE);
    }
}

void HubController::onDeviceAnnounce(const AnnounceMsg& msg) {
    char mac_str[18];
    formatMac(msg.header.sender_mac, mac_str, sizeof(mac_str));
    LOG_I(TAG, "Device announcement from %s: %s (type=%d)",
          mac_str, msg.device_name, msg.device_type);

    // Add/update device in registry
    _registry.addDevice(msg.header.sender_mac, msg.device_type, msg.device_name);
    _registry.updateLastSeen(msg.header.sender_mac, msg.rssi);

    // If device is new, add as ESP-NOW peer
    if (!EspNow.peerExists(msg.header.sender_mac)) {
        EspNow.addPeer(msg.header.sender_mac);
    }
}

void HubController::onPairRequest(const PairRequestMsg& msg) {
    char mac_str[18];
    formatMac(msg.header.sender_mac, mac_str, sizeof(mac_str));
    LOG_I(TAG, "Pair request from %s: %s", mac_str, msg.device_name);

    // Add device to registry
    if (_registry.addDevice(msg.header.sender_mac, msg.device_type, msg.device_name)) {
        // Add as ESP-NOW peer
        EspNow.addPeer(msg.header.sender_mac);

        // Send pair response
        uint8_t channel = WiFiMgr.getCurrentChannel();
        uint16_t interval = Config.getEspNow().send_interval_sec;
        EspNow.sendPairResponse(msg.header.sender_mac, true, channel, interval);

        // Save registry
        _registry.saveToNVS();

        LOG_I(TAG, "Pairing accepted for %s", msg.device_name);
    } else {
        // Send rejection
        EspNow.sendPairResponse(msg.header.sender_mac, false, 0, 0);
        LOG_W(TAG, "Pairing rejected for %s (registry full or error)", msg.device_name);
    }
}

uint8_t HubController::getConnectedDeviceCount() const {
    return (uint8_t)_registry.getOnlineDeviceCount();
}

void HubController::onMoistureReading(const MoistureReadingMsg& msg) {
    char mac_str[18];
    formatMac(msg.header.sender_mac, mac_str, sizeof(mac_str));

    LOG_D(TAG, "Moisture from %s: sensor=%d, raw=%d, pct=%d%%",
          mac_str, msg.sensor_index, msg.raw_value, msg.moisture_percent);

    // Update registry
    _registry.updateLastSeen(msg.header.sender_mac, msg.rssi);
    _registry.updateReadings(msg.header.sender_mac, msg.moisture_percent);

    // Get device for publishing
    RegisteredDevice* dev = _registry.getDevice(msg.header.sender_mac);
    if (dev && Mqtt.isConnected()) {
        // Publish to MQTT
        // Format topic as: base_topic/device_id/moisture/N
        Mqtt.publishMoistureReading(msg.sensor_index, msg.raw_value, msg.moisture_percent);
    }
}

void HubController::onEnvironmentalReading(const EnvironmentalReadingMsg& msg) {
    float temp = msg.temperature_c_x10 / 10.0f;
    float humidity = msg.humidity_percent_x10 / 10.0f;

    char mac_str[18];
    formatMac(msg.header.sender_mac, mac_str, sizeof(mac_str));

    LOG_D(TAG, "Environmental from %s: temp=%.1fC, humidity=%.1f%%",
          mac_str, temp, humidity);

    // Update registry
    _registry.updateLastSeen(msg.header.sender_mac, 0);

    RegisteredDevice* dev = _registry.getDevice(msg.header.sender_mac);
    if (dev) {
        dev->last_temperature = temp;
        dev->last_humidity = humidity;
    }

    // Publish to MQTT
    if (Mqtt.isConnected()) {
        Mqtt.publishEnvironmentalReading(temp, humidity);
    }
}

void HubController::onBatteryStatus(const BatteryStatusMsg& msg) {
    char mac_str[18];
    formatMac(msg.header.sender_mac, mac_str, sizeof(mac_str));

    LOG_D(TAG, "Battery from %s: %dmV, %d%%, charging=%d",
          mac_str, msg.voltage_mv, msg.percent, msg.charging);

    // Update registry
    _registry.updateLastSeen(msg.header.sender_mac, 0);

    RegisteredDevice* dev = _registry.getDevice(msg.header.sender_mac);
    if (dev) {
        dev->last_battery_percent = msg.percent;
    }

    // Publish to MQTT
    if (Mqtt.isConnected()) {
        Mqtt.publishBatteryStatus(msg.voltage_mv, msg.percent, msg.charging);
    }
}

void HubController::sendRelayCommand(const uint8_t* target_mac, uint8_t relay,
                                      bool state, uint32_t duration) {
    LOG_I(TAG, "Sending relay command: relay=%d, state=%d, dur=%lu",
          relay, state, duration);

    if (EspNow.isInitialized()) {
        EspNow.sendRelayCommand(target_mac, relay, state, duration);
    }
}

void HubController::sendCalibrationCommand(const uint8_t* target_mac,
                                            uint8_t sensor, uint8_t point) {
    LOG_I(TAG, "Sending calibration command: sensor=%d, point=%d", sensor, point);

    // Build and send calibration command message
    CalibrationCommandMsg msg;
    initMessageHeader(msg.header, MessageType::CALIBRATION_COMMAND,
                      EspNow.getMac(), EspNow.getNextSequence());
    msg.sensor_index = sensor;
    msg.calibration_point = point;
    msg.manual_value = 0;  // Use current reading

    EspNow.send(target_mac, (uint8_t*)&msg, sizeof(msg));
}

void HubController::sendWakeCommand(const uint8_t* target_mac) {
    LOG_I(TAG, "Sending wake command");

    MessageHeader msg;
    initMessageHeader(msg, MessageType::WAKE_COMMAND,
                      EspNow.getMac(), EspNow.getNextSequence());

    EspNow.send(target_mac, (uint8_t*)&msg, sizeof(msg));
}

void HubController::checkDeviceTimeouts() {
    _registry.checkTimeouts();
}

void HubController::publishAggregatedState() {
    SensorReadings readings;
    readings.uptime_sec = millis() / 1000;
    readings.rssi = WiFiMgr.getRSSI();
    readings.free_heap = ESP.getFreeHeap();

    // Populate local moisture sensor readings from cache.
    for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
        if (_sensor_cache[i].valid) {
            readings.moisture[readings.moisture_count].valid     = true;
            readings.moisture[readings.moisture_count].index     = i;
            readings.moisture[readings.moisture_count].raw_value = _sensor_cache[i].raw;
            readings.moisture[readings.moisture_count].percent   = _sensor_cache[i].percent;
            readings.moisture_count++;
        }
    }

    // NOTE: Hub has no local environmental sensor cache — temperature/humidity
    // are only available from paired remote devices via the device registry.
    // has_environmental remains false; env data is published per-reading via
    // onEnvironmentalReading() -> publishEnvironmentalReading() as messages arrive.

    Mqtt.publishState(readings);
}

void HubController::onEspNowReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(MessageHeader)) {
        LOG_W(TAG, "Received packet too small: %d bytes", len);
        return;
    }

    const MessageHeader* header = (const MessageHeader*)data;

    // Verify protocol version
    if (header->protocol_version != PROTOCOL_VERSION) {
        LOG_W(TAG, "Protocol version mismatch: %d vs %d",
              header->protocol_version, PROTOCOL_VERSION);
        return;
    }

    // Handle based on message type
    switch (header->type) {
        case MessageType::ANNOUNCE:
            if (len >= (int)sizeof(AnnounceMsg)) {
                onDeviceAnnounce(*(const AnnounceMsg*)data);
            }
            break;

        case MessageType::PAIR_REQUEST:
            if (len >= (int)sizeof(PairRequestMsg)) {
                onPairRequest(*(const PairRequestMsg*)data);
            }
            break;

        case MessageType::MOISTURE_READING:
            if (len >= (int)sizeof(MoistureReadingMsg)) {
                onMoistureReading(*(const MoistureReadingMsg*)data);
            }
            break;

        case MessageType::ENVIRONMENTAL_READING:
            if (len >= (int)sizeof(EnvironmentalReadingMsg)) {
                onEnvironmentalReading(*(const EnvironmentalReadingMsg*)data);
            }
            break;

        case MessageType::BATTERY_STATUS:
            if (len >= (int)sizeof(BatteryStatusMsg)) {
                onBatteryStatus(*(const BatteryStatusMsg*)data);
            }
            break;

        case MessageType::HEARTBEAT:
            // Update last seen
            _registry.updateLastSeen(mac, 0);
            break;

        case MessageType::ACK:
        case MessageType::NACK:
            // Handled internally by EspNowManager
            break;

        default:
            LOG_W(TAG, "Unknown message type: 0x%02X", (uint8_t)header->type);
            break;
    }
}

void HubController::onMqttMessage(const char* topic, const char* payload) {
    LOG_D(TAG, "MQTT message: %s = %s", topic, payload);

    // Parse relay commands
    // Topic format: iwetmyplants/device_id/relay/N/set
    // Payload: ON, OFF, or JSON with duration

    // This is handled by MqttManager's relay callback
}

void HubController::setupWebRoutes() {
    // Set up API endpoint callbacks
    ApiEndpoints::onPairedDevices([this](PairedDeviceInfo* devices, size_t max_count) -> size_t {
        size_t count = 0;
        _registry.forEachDevice([&](RegisteredDevice& dev) {
            if (count < max_count) {
                memcpy(devices[count].mac, dev.mac, 6);
                strlcpy(devices[count].name, dev.device_name, sizeof(devices[count].name));
                devices[count].device_type = dev.device_type;
                devices[count].rssi = dev.last_rssi;
                devices[count].online = dev.online;
                devices[count].last_seen = dev.last_seen;
                devices[count].moisture_percent = dev.last_moisture_percent;
                devices[count].temperature = dev.last_temperature;
                devices[count].humidity = dev.last_humidity;
                devices[count].battery_percent = dev.last_battery_percent;
                count++;
            }
        });
        return count;
    });

    ApiEndpoints::onDeleteDevice([this](uint8_t index) -> bool {
        // Walk the registry to find device at the given list index
        size_t count = 0;
        uint8_t target_mac[6] = {};
        bool found = false;
        _registry.forEachDevice([&](RegisteredDevice& dev) {
            if (count == index) {
                memcpy(target_mac, dev.mac, 6);
                found = true;
            }
            count++;
        });
        if (!found) return false;

        bool removed = _registry.removeDevice(target_mac);
        if (removed) {
            _registry.saveToNVS();
            EspNow.removePeer(target_mac);
        }
        return removed;
    });

    // POST /api/remote/report - receive sensor data from WiFi-connected remotes
    Web.addRouteWithBody("/api/remote/report", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len,
               size_t index, size_t total) {
            if (index + len != total) return;  // Wait for complete body

            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                ApiEndpoints::sendError(request, 400, "Invalid JSON");
                return;
            }

            const char* mac_str = doc["mac"] | "";
            if (strlen(mac_str) == 0) {
                ApiEndpoints::sendError(request, 400, "Missing mac");
                return;
            }

            uint8_t mac[6];
            if (!DeviceRegistry::stringToMac(mac_str, mac)) {
                ApiEndpoints::sendError(request, 400, "Invalid MAC");
                return;
            }

            const char* device_name = doc["device_name"] | "Remote";
            uint8_t device_type = doc["device_type"] | 1;
            uint8_t moisture = doc["moisture_percent"] | 0;
            int8_t rssi = doc["rssi"] | 0;

            _registry.addDevice(mac, device_type, device_name);
            _registry.updateLastSeen(mac, rssi);
            _registry.updateReadings(mac, moisture);

            RegisteredDevice* dev = _registry.getDevice(mac);
            if (dev) {
                const char* fw = doc["firmware_version"] | "";
                if (strlen(fw) > 0) {
                    strlcpy(dev->firmware_version, fw, sizeof(dev->firmware_version));
                }
                dev->paired = true;
            }

            LOG_I(TAG, "Remote report from %s: %d%%", mac_str, moisture);
            ApiEndpoints::sendSuccess(request, "OK");
        }
    );

    ApiEndpoints::onSensorData([this](uint8_t index, uint16_t& raw, uint8_t& percent,
                                       bool& valid, uint32_t& age_sec) -> bool {
        if (index >= IWMP_MAX_SENSORS) return false;
        // Sensor doesn't exist at this index → return false; UI omits the entry.
        if (!_local_sensors[index] && !_sensor_cache[index].valid) return false;

        if (_sensor_cache[index].valid) {
            raw     = _sensor_cache[index].raw;
            percent = _sensor_cache[index].percent;
            valid   = true;
            age_sec = (millis() - _sensor_cache[index].last_read_ms) / 1000UL;
        } else {
            // Sensor object exists but background poll hasn't completed yet.
            valid   = false;
            raw     = 0;
            percent = 0;
            age_sec = 0;
        }
        return true;
    });

    // Sensor status callback for hardware connection info
    ApiEndpoints::onSensorStatus([this](uint8_t index, SensorStatusInfo& status) -> bool {
        if (index >= IWMP_MAX_SENSORS) {
            return false;
        }

        const auto& sensor_cfg = Config.getMoistureSensor(index);

        if (_local_sensors[index]) {
            status.exists = true;
            status.ready = _local_sensors[index]->isReady();
            status.hw_connected = _local_sensors[index]->isHardwareConnected();
            status.input_type = _local_sensors[index]->getInputTypeName();
            status.ads_channel = sensor_cfg.ads_channel;
            status.ads_address = sensor_cfg.ads_i2c_address;
        } else {
            status.exists = false;
            status.ready = false;
            status.hw_connected = false;
            status.input_type = "None";
            status.ads_channel = 0;
            status.ads_address = 0;
        }
        return true;
    });

    // NEW CALIBRATION ENDPOINTS
    Web.addRouteWithBody("/api/calibration/start", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) || !doc["sensor"].is<uint8_t>() || !doc["point"].is<const char*>()) {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }
            uint8_t idx = doc["sensor"];
            const char* point = doc["point"];
            if (idx >= IWMP_MAX_SENSORS || !_local_sensors[idx]) {
                request->send(400, "application/json", "{\"error\":\"Invalid sensor\"}");
                return;
            }
            bool is_wet = (strcmp(point, "wet") == 0);
            if (Calibration.begin(_local_sensors[idx].get(), is_wet)) {
                request->send(200, "application/json", "{\"success\":true}");
            } else {
                request->send(400, "application/json", "{\"error\":\"Calibration busy\"}");
            }
        });

    Web.addRoute("/api/calibration/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"state\":%d,\"progress\":%u,\"value\":%u,\"error\":\"%s\"}",
                 (int)Calibration.getState(), Calibration.getProgress(), Calibration.getResult(),
                 Calibration.getErrorMessage());
        req->send(200, "application/json", buf);
    });

    Web.addRouteWithBody("/api/calibration/save", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) || !doc["sensor"].is<uint8_t>()) {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }
            if (Calibration.applyAndSave(doc["sensor"])) {
                request->send(200, "application/json", "{\"success\":true}");
            } else {
                request->send(400, "application/json", "{\"error\":\"Nothing to save\"}");
            }
        });

    // POST /api/sensors/poll — force an immediate sensor reading cycle
    Web.addRoute("/api/sensors/poll", HTTP_POST, [this](AsyncWebServerRequest* request) {
        _poll_force = true;
        ApiEndpoints::sendSuccess(request, "Poll started");
    });

    // GET /api/sensors/poll_interval — return current interval in seconds
    Web.addRoute("/api/sensors/poll_interval", HTTP_GET, [this](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["interval_sec"] = _poll_interval_ms / 1000UL;
        ApiEndpoints::sendJson(request, doc);
    });

    // POST /api/sensors/poll_interval — set interval {"interval_sec": N}
    Web.addRouteWithBody("/api/sensors/poll_interval", HTTP_POST,
        [this](AsyncWebServerRequest* request) { /* handled in body callback */ },
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err || !doc["interval_sec"].is<uint32_t>()) {
                ApiEndpoints::sendError(request, 400, "Bad request");
                return;
            }
            uint32_t sec = doc["interval_sec"].as<uint32_t>();
            if (sec < 5) sec = 5;  // floor: 5 s
            savePollInterval(sec);
            ApiEndpoints::sendSuccess(request, "Poll interval updated");
        }
    );

    // GET /api/sensors/cache — return cached readings with timestamps
    Web.addRoute("/api/sensors/cache", HTTP_GET, [this](AsyncWebServerRequest* request) {
        uint32_t now = millis();
        JsonDocument doc;
        JsonArray arr = doc["sensors"].to<JsonArray>();
        for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
            if (!_local_sensors[i]) continue;
            JsonObject obj = arr.add<JsonObject>();
            obj["index"]   = i;
            obj["valid"]   = _sensor_cache[i].valid;
            obj["raw"]     = _sensor_cache[i].raw;
            obj["percent"] = _sensor_cache[i].percent;
            obj["age_sec"] = _sensor_cache[i].valid
                                ? (now - _sensor_cache[i].last_read_ms) / 1000UL
                                : 0;
        }
        doc["poll_interval_sec"] = _poll_interval_ms / 1000UL;
        doc["polling"]           = (_poll_phase == PollPhase::SAMPLING);
        ApiEndpoints::sendJson(request, doc);
    });

    // GET /api/espnow/export � export hub config so a Remote can import it in one step
    Web.addRoute("/api/espnow/export", HTTP_GET, [this](AsyncWebServerRequest* request) {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        const auto& wifi_cfg = Config.getWifi();
        JsonDocument doc;
        doc["hub_mac"]       = mac_str;
        doc["channel"]       = (int)WiFiMgr.getCurrentChannel();
        doc["wifi_ssid"]     = wifi_cfg.ssid;
        doc["wifi_password"] = wifi_cfg.password;
        doc["hub_ip"]        = WiFi.localIP().toString();

        String json;
        serializeJson(doc, json);
        AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", json);
        resp->addHeader("Access-Control-Allow-Origin", "*");
        request->send(resp);
    });

    LOG_I(TAG, "Web routes configured");
}

bool HubController::isConfigButtonPressed() {
    return digitalRead(0) == LOW;
}

void HubController::initializeLocalSensors() {
    LOG_I(TAG, "Initializing local sensors");
    _local_sensor_count = 0;

    for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
        const auto& sensor_cfg = Config.getMoistureSensor(i);

        if (!sensor_cfg.enabled) {
            continue;
        }

        // MUX sensors need hub-specific pin assignments — create directly
        // rather than through the shared factory which doesn't know the pins.
        if (sensor_cfg.input_type == SensorInputType::MUX_CD74HC4067) {
            auto mux_input = std::make_unique<MuxInput>(
                hub_pins::MUX_SIG,
                hub_pins::MUX_S0, hub_pins::MUX_S1,
                hub_pins::MUX_S2, hub_pins::MUX_S3,
                sensor_cfg.mux_channel
            );
            _local_sensors[i] = std::make_unique<MoistureSensor>(std::move(mux_input), sensor_cfg);
            _local_sensors[i]->setIndex(i);
        } else {
            _local_sensors[i] = createMoistureSensor(sensor_cfg, i);
        }

        if (_local_sensors[i]) {
            _local_sensors[i]->begin();
            _local_sensor_count++;
            LOG_I(TAG, "Sensor %d initialized: %s", i, _local_sensors[i]->getName());
        } else {
            LOG_W(TAG, "Failed to create sensor %d", i);
        }
    }

    // Re-assert I2C config after Adafruit BusIO's Wire.begin() calls
    // (Adafruit_I2CDevice::begin() calls Wire.begin() without pin args,
    // which can disrupt the peripheral on some ESP32 Arduino core versions)
    Wire.begin(hub_pins::I2C_SDA, hub_pins::I2C_SCL);
    Wire.setTimeOut(50);
    Wire1.begin(hub_pins::I2C1_SDA, hub_pins::I2C1_SCL);
    Wire1.setTimeOut(50);

    LOG_I(TAG, "Initialized %d local sensors", _local_sensor_count);
}


// ============================================================
// Sensor Poll Interval — persisted in NVS independently of packed config struct
// ============================================================

void HubController::loadPollInterval() {
    Preferences prefs;
    prefs.begin("hub", /*readOnly=*/true);
    uint32_t sec = prefs.getUInt("sensor_poll_sec", 60);
    prefs.end();
    _poll_interval_ms = sec * 1000UL;
    LOG_I(TAG, "Sensor poll interval: %lus", sec);
}

void HubController::savePollInterval(uint32_t interval_sec) {
    Preferences prefs;
    prefs.begin("hub", /*readOnly=*/false);
    prefs.putUInt("sensor_poll_sec", interval_sec);
    prefs.end();
    _poll_interval_ms = interval_sec * 1000UL;
    LOG_I(TAG, "Sensor poll interval saved: %lus", interval_sec);
}

// ============================================================
// Background Sequential Sensor Polling (non-blocking state machine)
//
// IDLE  -> When (millis() - _poll_cycle_start_ms) >= _poll_interval_ms
//          or _poll_force == true:
//          advance _poll_idx to first enabled sensor, enter SAMPLING.
//
// SAMPLING -> Every POLL_SAMPLE_INTERVAL_MS, read one raw sample from
//             _local_sensors[_poll_idx] and add to _poll_accumulator.
//             After POLL_SAMPLES samples, store average in _sensor_cache,
//             advance _poll_idx to next enabled sensor.
//             When all sensors done, return to IDLE.
// ============================================================

void HubController::doBackgroundPoll() {
    uint32_t now = millis();

    if (_poll_phase == PollPhase::IDLE) {
        bool start = _poll_force || (now - _poll_cycle_start_ms) >= _poll_interval_ms;
        if (!start) return;

        // Find first enabled sensor
        _poll_idx = 0;
        while (_poll_idx < IWMP_MAX_SENSORS && !(_local_sensors[_poll_idx] && _local_sensors[_poll_idx]->isReady())) {
            _poll_idx++;
        }
        if (_poll_idx >= IWMP_MAX_SENSORS) {
            // No sensors — reset timer
            _poll_cycle_start_ms = now;
            _poll_force = false;
            return;
        }

        _poll_sample_count = 0;
        _poll_accumulator  = 0;
        _poll_last_sample_ms = now - POLL_SAMPLE_INTERVAL_MS; // fire immediately
        _poll_phase = PollPhase::SAMPLING;
        if (_poll_force) {
            LOG_I(TAG, "Sensor poll: forced");
        }
    }

    if (_poll_phase == PollPhase::SAMPLING) {
        if ((now - _poll_last_sample_ms) < POLL_SAMPLE_INTERVAL_MS) return;
        _poll_last_sample_ms = now;

        // Take one raw sample (ADS1115 single-shot: ~9 ms blocking — acceptable)
        if (_local_sensors[_poll_idx] && _local_sensors[_poll_idx]->isReady()) {
            _poll_accumulator += _local_sensors[_poll_idx]->readRaw();
        }
        _poll_sample_count++;

        if (_poll_sample_count >= POLL_SAMPLES) {
            // Compute and cache average for this sensor
            uint16_t avg_raw = static_cast<uint16_t>(_poll_accumulator / POLL_SAMPLES);
            uint8_t  percent = _local_sensors[_poll_idx]->rawToPercent(avg_raw);

            _sensor_cache[_poll_idx].raw          = avg_raw;
            _sensor_cache[_poll_idx].percent      = percent;
            _sensor_cache[_poll_idx].last_read_ms = now;
            _sensor_cache[_poll_idx].valid        = true;

            LOG_I(TAG, "Sensor %d: %d%% (raw=%u)", _poll_idx, percent, avg_raw);

            // Advance to next enabled sensor
            _poll_idx++;
            while (_poll_idx < IWMP_MAX_SENSORS && !(_local_sensors[_poll_idx] && _local_sensors[_poll_idx]->isReady())) {
                _poll_idx++;
            }

            if (_poll_idx >= IWMP_MAX_SENSORS) {
                // Cycle complete
                _poll_cycle_start_ms = now;
                _poll_force = false;
                _poll_phase = PollPhase::IDLE;
                LOG_I(TAG, "Sensor poll cycle complete");
            } else {
                // Next sensor — reset sample counters, fire next sample immediately
                _poll_sample_count = 0;
                _poll_accumulator  = 0;
                _poll_last_sample_ms = now - POLL_SAMPLE_INTERVAL_MS;
            }
        }
    }
}

} // namespace iwmp
