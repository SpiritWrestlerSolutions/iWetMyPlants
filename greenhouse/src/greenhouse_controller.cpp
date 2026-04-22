/**
 * @file greenhouse_controller.cpp
 * @brief Greenhouse Manager device controller implementation
 */

#include "greenhouse_controller.h"
#include "config_manager.h"
#include "logger.h"
#include "watchdog.h"
#include "web_server.h"
#include "api_endpoints.h"

namespace iwmp {

GreenhouseController Greenhouse;

static constexpr const char* TAG = "GH";

static const char* stateName(GreenhouseState s) {
    switch (s) {
        case GreenhouseState::BOOT:         return "BOOT";
        case GreenhouseState::LOAD_CONFIG:  return "LOAD_CONFIG";
        case GreenhouseState::WIFI_CONNECT: return "WIFI_CONNECT";
        case GreenhouseState::MQTT_CONNECT: return "MQTT_CONNECT";
        case GreenhouseState::AP_MODE:      return "AP_MODE";
        case GreenhouseState::OPERATIONAL:  return "OPERATIONAL";
    }
    return "?";
}

void GreenhouseController::begin() {
    LOG_I(TAG, "Initializing Greenhouse controller");

    // Gather enabled relay configs
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

    enterState(GreenhouseState::BOOT);
}

void GreenhouseController::loop() {
    switch (_state) {
        case GreenhouseState::BOOT:         handleBootState();         break;
        case GreenhouseState::LOAD_CONFIG:  handleLoadConfigState();   break;
        case GreenhouseState::WIFI_CONNECT: handleWifiConnectState();  break;
        case GreenhouseState::MQTT_CONNECT: handleMqttConnectState();  break;
        case GreenhouseState::AP_MODE:      handleApModeState();       break;
        case GreenhouseState::OPERATIONAL:  handleOperationalState();  break;
    }
}

void GreenhouseController::enterState(GreenhouseState new_state) {
    if (_state == new_state) return;

    LOG_I(TAG, "State: %s -> %s", stateName(_state), stateName(new_state));
    _state = new_state;
    _state_enter_time = millis();
}

void GreenhouseController::handleBootState() {
    LOG_I(TAG, "Boot state");

    initializeEnvSensor();

    enterState(GreenhouseState::LOAD_CONFIG);
}

void GreenhouseController::handleLoadConfigState() {
    LOG_I(TAG, "Loading configuration");

    // Initialize Improv WiFi Serial provisioning once at boot so the
    // web installer can configure WiFi even if the device reconnects to
    // a saved network and never enters AP mode.
    if (!_improvStarted) {
        _improvStarted = true;
        _improv.setConnectCallback([this](const char* ssid, const char* pwd, String& outUrl) -> bool {
            WiFiMgr.stopCaptivePortal();

            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(ssid, pwd);

            uint32_t t = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) {
                Watchdog.feed();
                delay(100);
            }

            if (WiFi.status() != WL_CONNECTED) {
                char ap_ssid[32];
                snprintf(ap_ssid, sizeof(ap_ssid), "IWMP-GH-%s", Config.getDeviceId() + 6);
                WiFi.mode(WIFI_AP);
                WiFiMgr.startAP(ap_ssid);
                WiFiMgr.startCaptivePortal();
                return false;
            }

            WiFi.softAPdisconnect(false);
            WiFi.mode(WIFI_STA);

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
        _improv.setDeviceInfo("iWetMyPlants Greenhouse", IWMP_VERSION, "ESP32",
                              Config.getDeviceId());
    }

    const auto& wifi_cfg = Config.getWifi();

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

    if (WiFiMgr.isConnected()) {
        LOG_I(TAG, "WiFi connected: %s", WiFiMgr.getIP().toString().c_str());
        wifi_started = false;

        const auto& espnow_cfg = Config.getEspNow();
        if (espnow_cfg.enabled) {
            uint8_t channel = WiFiMgr.getCurrentChannel();
            if (EspNow.begin(channel)) {
                LOG_I(TAG, "ESP-NOW initialized on channel %d", channel);

                EspNow.onReceive([this](const uint8_t* mac, const uint8_t* data, int len) {
                    onEspNowReceive(mac, data, len);
                });

                if (memcmp(espnow_cfg.hub_mac, "\0\0\0\0\0\0", 6) != 0) {
                    EspNow.addPeer(espnow_cfg.hub_mac);
                }
            }
        }

        const auto& mqtt_cfg = Config.getMqtt();
        if (mqtt_cfg.enabled) {
            enterState(GreenhouseState::MQTT_CONNECT);
        } else {
            Web.begin(Config.getConfig().identity);
            setupWebRoutes();
            enterState(GreenhouseState::OPERATIONAL);
        }
        return;
    }

    // Keep Improv responsive while awaiting WiFi
    _improv.loop();

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

            Mqtt.onConnect([this](bool session_present) {
                LOG_I(TAG, "MQTT connected (session=%d)", session_present);
                if (Config.getMqtt().ha_discovery_enabled) {
                    Mqtt.publishDiscovery();

                    // Greenhouse is env + relay only. Clean up any legacy
                    // moisture discovery entities from prior firmware.
                    for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
                        Mqtt.removeMoistureDiscovery(i);
                    }

                    const auto& env_cfg = Config.getEnvSensor();
                    if (env_cfg.sensor_type != EnvSensorType::NONE) {
                        Mqtt.publishTemperatureDiscovery();
                        Mqtt.publishHumidityDiscovery();
                    } else {
                        Mqtt.removeTemperatureDiscovery();
                        Mqtt.removeHumidityDiscovery();
                    }

                    for (uint8_t i = 0; i < IWMP_MAX_RELAYS; i++) {
                        const auto& rcfg = Config.getRelay(i);
                        if (!rcfg.enabled) {
                            Mqtt.removeRelayDiscovery(i);
                            continue;
                        }
                        Mqtt.publishRelayDiscovery(i, rcfg.relay_name);
                    }
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

    Mqtt.loop();

    if (Mqtt.isConnected()) {
        LOG_I(TAG, "MQTT connected");
        mqtt_started = false;

        Web.begin(Config.getConfig().identity);
        setupWebRoutes();

        enterState(GreenhouseState::OPERATIONAL);
        return;
    }

    _improv.loop();

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
    _improv.loop();

    if (Web.isRunning()) {
        Web.update();
    }

    // Life-support: relay timers still run offline
    _relays.update();

    uint32_t now = millis();
    if ((now - _last_sensor_read_time) >= SENSOR_READ_INTERVAL_MS) {
        _last_sensor_read_time = now;
        readEnvSensor();
    }

    if (_improv.wasReProvisioned()) {
        LOG_I(TAG, "Improv provisioning complete -- rebooting");
        delay(1500);
        ESP.restart();
    }
}

void GreenhouseController::handleOperationalState() {
    static bool operational_init = false;

    if (!operational_init) {
        operational_init = true;
        _improv.broadcastProvisioned("http://" + WiFiMgr.getIP().toString());
    }

    uint32_t now = millis();

    WiFiMgr.loop();

    if (Web.isRunning()) {
        Web.update();
    }

    _relays.update();

    if (Mqtt.isInitialized()) {
        Mqtt.loop();

        if (!Mqtt.isConnected() && WiFiMgr.isConnected()) {
            static uint32_t last_reconnect_attempt = 0;
            if ((now - last_reconnect_attempt) > 10000) {
                LOG_I(TAG, "Attempting MQTT reconnect");
                Mqtt.connect();
                last_reconnect_attempt = now;
            }
        }
    }

    if (EspNow.isInitialized()) {
        EspNow.update();
    }

    if ((now - _last_sensor_read_time) >= SENSOR_READ_INTERVAL_MS) {
        _last_sensor_read_time = now;
        readEnvSensor();
    }

    // publish_interval_sec is uint16_t; widen before multiply to avoid overflow
    uint32_t publish_interval_ms = (uint32_t)Config.getMqtt().publish_interval_sec * 1000UL;
    if ((now - _last_publish_time) >= publish_interval_ms) {
        _last_publish_time = now;
        publishState();
    }

    _improv.loop();
    if (_improv.wasReProvisioned()) {
        LOG_I(TAG, "Improv re-provisioning complete -- rebooting");
        delay(1500);
        ESP.restart();
    }
}

void GreenhouseController::onRelayCommand(const RelayCommandMsg& msg) {
    LOG_I(TAG, "Relay command: relay=%d, state=%d, override=%d",
          msg.relay_index, msg.state, msg.override_safety);

    if (msg.override_safety) {
        _relays.clearLockout(msg.relay_index);
    }

    setRelay(msg.relay_index, msg.state, msg.duration_sec);
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
    // SHT40/SHT41 fall through — Phase 6 will add driver support.
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
    }
}

void GreenhouseController::publishState() {
    if (!Mqtt.isConnected()) {
        return;
    }

    if (!isnan(_last_temperature) && !isnan(_last_humidity)) {
        Mqtt.publishEnvironmentalReading(_last_temperature, _last_humidity);
    }

    for (uint8_t i = 0; i < _relays.getCount(); i++) {
        Mqtt.publishRelayState(i, _relays.isOn(i));
    }

    // Build aggregated state payload
    SensorReadings readings;
    readings.moisture_count = 0;

    if (!isnan(_last_temperature) && !isnan(_last_humidity)) {
        readings.has_environmental = true;
        readings.temperature_c = _last_temperature;
        readings.humidity_percent = _last_humidity;
    }

    readings.relay_count = 0;
    for (uint8_t i = 0; i < _relays.getCount() && i < IWMP_MAX_RELAYS; i++) {
        readings.relays[i].valid = true;
        readings.relays[i].index = i;
        readings.relays[i].state = _relays.isOn(i);
        readings.relay_count++;
    }

    Mqtt.publishState(readings);
}

void GreenhouseController::setupWebRoutes() {
    // Relay state callback
    ApiEndpoints::onRelayState([this](uint8_t index, bool& state) -> bool {
        if (index < IWMP_MAX_RELAYS) {
            state = _relays.getState(index).current_state;
            return true;
        }
        return false;
    });

    // Relay control callback
    ApiEndpoints::onRelayControl([this](uint8_t index, bool state) -> bool {
        return setRelay(index, state, 0);
    });

    // Environmental data callback
    ApiEndpoints::onEnvironmentalData([this](float& temperature, float& humidity, const char*& sensor_type) -> bool {
        temperature = _last_temperature;
        humidity = _last_humidity;
        const auto& env_cfg = Config.getEnvSensor();
        switch (env_cfg.sensor_type) {
            case EnvSensorType::DHT11:  sensor_type = "DHT11";  break;
            case EnvSensorType::DHT22:  sensor_type = "DHT22";  break;
            case EnvSensorType::SHT30:  sensor_type = "SHT30";  break;
            case EnvSensorType::SHT31:  sensor_type = "SHT31";  break;
            case EnvSensorType::SHT40:  sensor_type = "SHT40";  break;
            case EnvSensorType::SHT41:  sensor_type = "SHT41";  break;
            default:                    sensor_type = "None";    break;
        }
        return _dht_sensor != nullptr || _sht_sensor != nullptr;
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

        default:
            LOG_D(TAG, "Unhandled message type: %d", (int)header->type);
            break;
    }
}

} // namespace iwmp
