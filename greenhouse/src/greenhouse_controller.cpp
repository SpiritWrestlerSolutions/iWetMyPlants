/**
 * @file greenhouse_controller.cpp
 * @brief Greenhouse Manager device controller implementation
 */

#include "greenhouse_controller.h"
#include "config_manager.h"
#include "logger.h"
#include "watchdog.h"
#include "web_server.h"
#include "rapid_read.h"
#include "api_endpoints.h"

namespace iwmp {

// Global greenhouse controller instance
GreenhouseController Greenhouse;

static constexpr const char* TAG = "GH";

void GreenhouseController::begin() {
    LOG_I(TAG, "Initializing Greenhouse controller");

    // Initialize relays
    RelayConfig relay_configs[IWMP_MAX_RELAYS];
    uint8_t relay_count = 0;

    for (uint8_t i = 0; i < IWMP_MAX_RELAYS; i++) {
        const RelayConfig& relay = Config.getRelay(i);
        if (relay.enabled) {
            memcpy(&relay_configs[relay_count++], &relay, sizeof(RelayConfig));
        }
    }

    if (relay_count > 0) {
        _relays.begin(relay_configs, relay_count);
        LOG_I(TAG, "Initialized %d relays", relay_count);
    }

    // Initialize automation
    _automation.begin(&_relays);

    // Load automation bindings from config
    for (uint8_t i = 0; i < IWMP_MAX_BINDINGS; i++) {
        const SensorRelayBinding& binding = Config.getBinding(i);
        if (binding.enabled) {
            _automation.addBinding(binding);
        }
    }

    enterState(GreenhouseState::BOOT);
}

void GreenhouseController::loop() {
    switch (_state) {
        case GreenhouseState::BOOT:
            handleBootState();
            break;
        case GreenhouseState::LOAD_CONFIG:
            handleLoadConfigState();
            break;
        case GreenhouseState::WIFI_CONNECT:
            handleWifiConnectState();
            break;
        case GreenhouseState::MQTT_CONNECT:
            handleMqttConnectState();
            break;
        case GreenhouseState::AP_MODE:
            handleApModeState();
            break;
        case GreenhouseState::OPERATIONAL:
            handleOperationalState();
            break;
    }
}

void GreenhouseController::enterState(GreenhouseState new_state) {
    if (_state == new_state) return;

    LOG_I(TAG, "State: %d -> %d", (int)_state, (int)new_state);
    _state = new_state;
    _state_enter_time = millis();
}

void GreenhouseController::handleBootState() {
    LOG_I(TAG, "Boot state");

    // Initialize sensors
    initializeSensors();
    initializeEnvSensor();

    enterState(GreenhouseState::LOAD_CONFIG);
}

void GreenhouseController::handleLoadConfigState() {
    LOG_I(TAG, "Loading configuration");

    const auto& wifi_cfg = Config.getWifi();

    // Check if we have WiFi credentials
    if (strlen(wifi_cfg.ssid) > 0) {
        enterState(GreenhouseState::WIFI_CONNECT);
    } else {
        LOG_W(TAG, "No WiFi configured, entering AP mode");
        enterState(GreenhouseState::AP_MODE);
    }
}

void GreenhouseController::handleWifiConnectState() {
    static bool wifi_started = false;

    if (!wifi_started) {
        const auto& wifi_cfg = Config.getWifi();
        LOG_I(TAG, "Connecting to WiFi: %s", wifi_cfg.ssid);

        WiFiMgr.begin(wifi_cfg);
        WiFiMgr.connect();
        wifi_started = true;
    }

    // Check connection
    if (WiFiMgr.isConnected()) {
        LOG_I(TAG, "WiFi connected: %s", WiFiMgr.getIP().toString().c_str());
        wifi_started = false;

        // Setup ESP-NOW on WiFi channel
        const auto& espnow_cfg = Config.getEspNow();
        if (espnow_cfg.enabled) {
            uint8_t channel = WiFiMgr.getCurrentChannel();
            if (EspNow.begin(channel)) {
                LOG_I(TAG, "ESP-NOW initialized on channel %d", channel);

                // Set up receive callback
                EspNow.onReceive([this](const uint8_t* mac, const uint8_t* data, int len) {
                    onEspNowReceive(mac, data, len);
                });

                // Add hub as peer if configured
                if (memcmp(espnow_cfg.hub_mac, "\0\0\0\0\0\0", 6) != 0) {
                    EspNow.addPeer(espnow_cfg.hub_mac);
                }
            }
        }

        // Check if MQTT is configured
        const auto& mqtt_cfg = Config.getMqtt();
        if (mqtt_cfg.enabled) {
            enterState(GreenhouseState::MQTT_CONNECT);
        } else {
            // Start web server
            Web.begin(Config.getConfig().identity);
            setupWebRoutes();
            enterState(GreenhouseState::OPERATIONAL);
        }
        return;
    }

    // Check timeout
    if ((millis() - _state_enter_time) > WIFI_CONNECT_TIMEOUT_MS) {
        LOG_W(TAG, "WiFi connection timeout");
        wifi_started = false;
        enterState(GreenhouseState::AP_MODE);
    }
}

void GreenhouseController::handleMqttConnectState() {
    static bool mqtt_started = false;

    if (!mqtt_started) {
        const auto& mqtt_cfg = Config.getMqtt();
        const auto& identity = Config.getConfig().identity;

        LOG_I(TAG, "Connecting to MQTT: %s", mqtt_cfg.broker);

        if (Mqtt.begin(mqtt_cfg, identity)) {
            Mqtt.connect();

            // Set up callbacks
            Mqtt.onConnect([this](bool session_present) {
                LOG_I(TAG, "MQTT connected (session=%d)", session_present);
                if (Config.getMqtt().ha_discovery_enabled) {
                    Mqtt.publishDiscovery();
                }
                Mqtt.publishAvailability(true);
            });

            Mqtt.onDisconnect([](AsyncMqttClientDisconnectReason reason) {
                LOG_W(TAG, "MQTT disconnected: %d", (int)reason);
            });

            Mqtt.onRelayCommand([this](uint8_t relay_idx, bool state, uint32_t duration) {
                LOG_I(TAG, "MQTT relay command: relay=%d, state=%d, duration=%lu",
                      relay_idx, state, duration);
                setRelay(relay_idx, state, duration);
            });

            mqtt_started = true;
        } else {
            LOG_E(TAG, "Failed to initialize MQTT");
            mqtt_started = false;
            enterState(GreenhouseState::OPERATIONAL);
            return;
        }
    }

    // Check connection
    Mqtt.loop();

    if (Mqtt.isConnected()) {
        LOG_I(TAG, "MQTT connected");
        mqtt_started = false;

        // Start web server
        Web.begin(Config.getConfig().identity);
        setupWebRoutes();

        enterState(GreenhouseState::OPERATIONAL);
        return;
    }

    // Check timeout
    if ((millis() - _state_enter_time) > MQTT_CONNECT_TIMEOUT_MS) {
        LOG_W(TAG, "MQTT connection timeout, continuing without MQTT");
        mqtt_started = false;

        Web.begin(Config.getConfig().identity);
        setupWebRoutes();

        enterState(GreenhouseState::OPERATIONAL);
    }
}

void GreenhouseController::handleApModeState() {
    static bool ap_started = false;

    if (!ap_started) {
        char ap_ssid[32];
        snprintf(ap_ssid, sizeof(ap_ssid), "IWMP-GH-%s", Config.getDeviceId() + 6);

        WifiConfig empty_cfg = {};
        WiFiMgr.begin(empty_cfg);
        WiFiMgr.startAP(ap_ssid);
        WiFiMgr.startCaptivePortal();

        Web.begin(Config.getConfig().identity);
        setupWebRoutes();

        LOG_I(TAG, "AP mode: %s @ %s", ap_ssid, WiFiMgr.getAPIP().toString().c_str());
        ap_started = true;
    }

    WiFiMgr.loop();

    // Still update relays even in AP mode
    _relays.update();

    // Check if WiFi configured
    const auto& wifi_cfg = Config.getWifi();
    if (strlen(wifi_cfg.ssid) > 0) {
        LOG_I(TAG, "WiFi configured, transitioning...");
        ap_started = false;
        WiFiMgr.stopCaptivePortal();
        WiFiMgr.stopAP();
        enterState(GreenhouseState::WIFI_CONNECT);
    }
}

void GreenhouseController::handleOperationalState() {
    uint32_t now = millis();

    // Update web server (OTA reboot, calibration WebSocket)
    if (Web.isRunning()) {
        Web.update();
    }

    // Update relay manager (handles timeouts)
    _relays.update();

    // Update automation engine
    _automation.update();

    // Update MQTT
    if (Mqtt.isInitialized()) {
        Mqtt.loop();

        // Reconnect if needed
        if (!Mqtt.isConnected() && WiFiMgr.isConnected()) {
            static uint32_t last_reconnect_attempt = 0;
            if ((now - last_reconnect_attempt) > 10000) {
                LOG_I(TAG, "Attempting MQTT reconnect");
                Mqtt.connect();
                last_reconnect_attempt = now;
            }
        }
    }

    // Update ESP-NOW
    if (EspNow.isInitialized()) {
        EspNow.update();
    }

    // Read sensors periodically
    if ((now - _last_sensor_read_time) >= SENSOR_READ_INTERVAL_MS) {
        _last_sensor_read_time = now;
        readSensors();
        readEnvSensor();
    }

    // Publish state periodically
    if ((now - _last_publish_time) >= Config.getMqtt().publish_interval_sec * 1000) {
        _last_publish_time = now;
        publishState();
    }
}

MoistureSensor* GreenhouseController::getMoistureSensor(uint8_t index) {
    if (index >= _moisture_sensor_count) {
        return nullptr;
    }
    return _moisture_sensors[index].get();
}

void GreenhouseController::onRelayCommand(const RelayCommandMsg& msg) {
    LOG_I(TAG, "Relay command: relay=%d, state=%d, override=%d",
          msg.relay_index, msg.state, msg.override_safety);

    if (msg.override_safety) {
        _relays.clearLockout(msg.relay_index);
    }

    setRelay(msg.relay_index, msg.state, msg.duration_sec);
}

void GreenhouseController::onMoistureReading(const MoistureReadingMsg& msg) {
    LOG_D(TAG, "Moisture reading: sensor=%d, value=%d%%",
          msg.sensor_index, msg.moisture_percent);

    // Forward to automation engine
    _automation.onMoistureReading(msg.sensor_index, msg.moisture_percent);
}

bool GreenhouseController::setRelay(uint8_t index, bool state, uint32_t duration_sec) {
    if (state) {
        return _relays.turnOn(index, duration_sec);
    } else {
        return _relays.turnOff(index);
    }
}

void GreenhouseController::emergencyStop() {
    LOG_W(TAG, "EMERGENCY STOP");
    _relays.emergencyStopAll();
    _automation.setEnabled(false);
}

void GreenhouseController::setAutomationEnabled(bool enabled) {
    _automation.setEnabled(enabled);
    LOG_I(TAG, "Automation %s", enabled ? "enabled" : "disabled");
}

bool GreenhouseController::isAutomationEnabled() const {
    return _automation.isEnabled();
}

void GreenhouseController::initializeSensors() {
    LOG_I(TAG, "Initializing moisture sensors");

    _moisture_sensor_count = 0;

    for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
        const auto& sensor_cfg = Config.getMoistureSensor(i);

        if (!sensor_cfg.enabled) {
            continue;
        }

        auto sensor = createMoistureSensor(sensor_cfg, i);
        if (sensor) {
            sensor->begin();
            _moisture_sensors[_moisture_sensor_count++] = std::move(sensor);
            LOG_I(TAG, "Sensor %d initialized: %s", i, _moisture_sensors[_moisture_sensor_count-1]->getName());
        }
    }

    LOG_I(TAG, "Initialized %d moisture sensors", _moisture_sensor_count);
}

void GreenhouseController::initializeEnvSensor() {
    const auto& env_cfg = Config.getEnvSensor();

    if (env_cfg.sensor_type == EnvSensorType::NONE) {
        LOG_I(TAG, "No environmental sensor configured");
        return;
    }

    LOG_I(TAG, "Initializing environmental sensor type %d", (int)env_cfg.sensor_type);

    if (env_cfg.sensor_type == EnvSensorType::DHT11 ||
        env_cfg.sensor_type == EnvSensorType::DHT22) {
        _dht_sensor = std::make_unique<DhtSensor>(env_cfg.pin, env_cfg.sensor_type);
        _dht_sensor->begin();
    } else if (env_cfg.sensor_type == EnvSensorType::SHT30 ||
               env_cfg.sensor_type == EnvSensorType::SHT31) {
        _sht_sensor = std::make_unique<ShtSensor>(env_cfg.sensor_type, env_cfg.i2c_address);
        _sht_sensor->begin();
    }
}

void GreenhouseController::readSensors() {
    for (uint8_t i = 0; i < _moisture_sensor_count; i++) {
        if (_moisture_sensors[i] && _moisture_sensors[i]->isReady()) {
            uint16_t raw = _moisture_sensors[i]->readRawAveraged();
            uint8_t percent = _moisture_sensors[i]->rawToPercent(raw);

            LOG_D(TAG, "Sensor %d: %d%% (raw=%d)", i, percent, raw);

            // Forward to automation
            _automation.onMoistureReading(i, percent);
        }
    }
}

void GreenhouseController::readEnvSensor() {
    float temp = NAN, humidity = NAN;

    if (_dht_sensor) {
        _dht_sensor->read(temp, humidity);
    } else if (_sht_sensor) {
        _sht_sensor->read(temp, humidity);
    }

    if (!isnan(temp) && !isnan(humidity)) {
        _last_temperature = temp;
        _last_humidity = humidity;

        LOG_D(TAG, "Environment: %.1f°C, %.1f%%", temp, humidity);

        // Forward to automation
        _automation.onEnvironmentalReading(temp, humidity);
    }
}

void GreenhouseController::publishState() {
    if (!Mqtt.isConnected()) {
        return;
    }

    // Publish environmental readings
    if (!isnan(_last_temperature) && !isnan(_last_humidity)) {
        Mqtt.publishEnvironmentalReading(_last_temperature, _last_humidity);
    }

    // Publish relay states
    for (uint8_t i = 0; i < _relays.getCount(); i++) {
        Mqtt.publishRelayState(i, _relays.isOn(i));
    }

    // Build and publish sensor readings
    SensorReadings readings;
    readings.moisture_count = 0;

    for (uint8_t i = 0; i < _moisture_sensor_count; i++) {
        if (_moisture_sensors[i] && readings.moisture_count < 8) {
            uint8_t idx = readings.moisture_count;
            readings.moisture[idx].valid = true;
            readings.moisture[idx].index = i;
            readings.moisture[idx].raw_value = _moisture_sensors[i]->readRawAveraged();
            readings.moisture[idx].percent = _moisture_sensors[i]->rawToPercent(readings.moisture[idx].raw_value);
            readings.moisture_count++;
        }
    }

    // Environmental readings
    if (!isnan(_last_temperature) && !isnan(_last_humidity)) {
        readings.has_environmental = true;
        readings.temperature_c = _last_temperature;
        readings.humidity_percent = _last_humidity;
    }

    // Relay states
    readings.relay_count = 0;
    for (uint8_t i = 0; i < _relays.getCount() && i < 4; i++) {
        readings.relays[i].valid = true;
        readings.relays[i].index = i;
        readings.relays[i].state = _relays.isOn(i);
        readings.relay_count++;
    }

    Mqtt.publishState(readings);
}

void GreenhouseController::setupWebRoutes() {
    // Set first available sensor for RapidRead calibration
    for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
        if (_moisture_sensors[i]) {
            RapidRead.setSensor(_moisture_sensors[i].get());
            break;
        }
    }

    // Register sensor data callback for API
    ApiEndpoints::onSensorData([this](uint8_t index, uint16_t& raw, uint8_t& percent) -> bool {
        if (index < IWMP_MAX_SENSORS && _moisture_sensors[index]) {
            raw = _moisture_sensors[index]->readRaw();
            percent = _moisture_sensors[index]->readPercent();
            return true;
        }
        return false;
    });

    // Register calibration callback
    ApiEndpoints::onCalibration([this](uint8_t sensor_idx, const char* action) -> bool {
        if (sensor_idx < IWMP_MAX_SENSORS && _moisture_sensors[sensor_idx]) {
            // Switch RapidRead to this sensor
            RapidRead.setSensor(_moisture_sensors[sensor_idx].get());

            if (strcmp(action, "dry") == 0) {
                _moisture_sensors[sensor_idx]->calibrateDry();
                auto& cfg = Config.getConfigMutable();
                cfg.moisture_sensors[sensor_idx].dry_value = _moisture_sensors[sensor_idx]->getDryValue();
                Config.save();
                LOG_I(TAG, "Sensor %d dry point set: %d", sensor_idx, _moisture_sensors[sensor_idx]->getDryValue());
                return true;
            } else if (strcmp(action, "wet") == 0) {
                _moisture_sensors[sensor_idx]->calibrateWet();
                auto& cfg = Config.getConfigMutable();
                cfg.moisture_sensors[sensor_idx].wet_value = _moisture_sensors[sensor_idx]->getWetValue();
                Config.save();
                LOG_I(TAG, "Sensor %d wet point set: %d", sensor_idx, _moisture_sensors[sensor_idx]->getWetValue());
                return true;
            }
        }
        return false;
    });

    // Register relay state callback
    ApiEndpoints::onRelayState([this](uint8_t index, bool& state) -> bool {
        if (index < IWMP_MAX_RELAYS) {
            state = _relays.getState(index).current_state;
            return true;
        }
        return false;
    });

    // Register relay control callback
    ApiEndpoints::onRelayControl([this](uint8_t index, bool state) -> bool {
        return setRelay(index, state, 0);
    });

    // Relay control endpoint
    Web.addRoute("/relay/{index}", HTTP_POST, [this](AsyncWebServerRequest* req) {
        String indexStr = req->pathArg(0);
        uint8_t index = indexStr.toInt();

        bool state = true;
        uint32_t duration = 0;

        if (req->hasParam("state", true)) {
            state = req->getParam("state", true)->value() == "true" ||
                    req->getParam("state", true)->value() == "1";
        }

        if (req->hasParam("duration", true)) {
            duration = req->getParam("duration", true)->value().toInt();
        }

        if (setRelay(index, state, duration)) {
            req->send(200, "application/json", "{\"success\":true}");
        } else {
            req->send(400, "application/json", "{\"success\":false,\"error\":\"Failed to set relay\"}");
        }
    });

    // Automation control
    Web.addRoute("/automation/enable", HTTP_POST, [this](AsyncWebServerRequest* req) {
        setAutomationEnabled(true);
        req->send(200, "application/json", "{\"enabled\":true}");
    });

    Web.addRoute("/automation/disable", HTTP_POST, [this](AsyncWebServerRequest* req) {
        setAutomationEnabled(false);
        req->send(200, "application/json", "{\"enabled\":false}");
    });

    // Emergency stop
    Web.addRoute("/emergency-stop", HTTP_POST, [this](AsyncWebServerRequest* req) {
        emergencyStop();
        req->send(200, "application/json", "{\"success\":true}");
    });
}

void GreenhouseController::onEspNowReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < sizeof(MessageHeader)) {
        return;
    }

    const MessageHeader* header = reinterpret_cast<const MessageHeader*>(data);

    switch (header->type) {
        case MessageType::RELAY_COMMAND:
            if (len >= sizeof(RelayCommandMsg)) {
                onRelayCommand(*reinterpret_cast<const RelayCommandMsg*>(data));
            }
            break;

        case MessageType::MOISTURE_READING:
            if (len >= sizeof(MoistureReadingMsg)) {
                onMoistureReading(*reinterpret_cast<const MoistureReadingMsg*>(data));
            }
            break;

        default:
            LOG_D(TAG, "Unhandled message type: %d", (int)header->type);
            break;
    }
}

void GreenhouseController::onMqttMessage(const char* topic, const char* payload) {
    LOG_D(TAG, "MQTT message: %s = %s", topic, payload);
    // Relay commands handled by MqttManager callback
}

bool GreenhouseController::isConfigButtonPressed() {
    // Check GPIO0 (common boot button)
    // Could be made configurable
    return digitalRead(0) == LOW;
}

} // namespace iwmp
