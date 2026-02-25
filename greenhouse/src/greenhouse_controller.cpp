/**
 * @file greenhouse_controller.cpp
 * @brief Greenhouse Manager controller implementation
 */

#include "greenhouse_controller.h"
#include "config_manager.h"
#include "espnow_manager.h"
#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "sensor_interface.h"
#include "logger.h"
#include <WiFi.h>

namespace iwmp {

// Global greenhouse controller instance
GreenhouseController Greenhouse;

static const char* TAG = "GH";

void GreenhouseController::begin() {
    LOG_I(TAG, "Initializing greenhouse controller");
    enterState(GreenhouseState::LOAD_CONFIG);
}

void GreenhouseController::loop() {
    switch (_state) {
        case GreenhouseState::BOOT:         handleBootState();        break;
        case GreenhouseState::LOAD_CONFIG:  handleLoadConfigState();  break;
        case GreenhouseState::WIFI_CONNECT: handleWifiConnectState(); break;
        case GreenhouseState::MQTT_CONNECT: handleMqttConnectState(); break;
        case GreenhouseState::AP_MODE:      handleApModeState();      break;
        case GreenhouseState::OPERATIONAL:  handleOperationalState(); break;
    }
}

void GreenhouseController::enterState(GreenhouseState new_state) {
    _state = new_state;
    _state_enter_time = millis();
    LOG_D(TAG, "State -> %d", (int)new_state);
}

void GreenhouseController::handleBootState() {
    enterState(GreenhouseState::LOAD_CONFIG);
}

void GreenhouseController::handleLoadConfigState() {
    Config.begin(DeviceType::GREENHOUSE);
    Config.load();

    const DeviceConfig& cfg = Config.getConfig();

    initializeSensors();
    initializeEnvSensor();

    _relays.begin(cfg.relays, IWMP_MAX_RELAYS);
    _automation.begin(&_relays);

    for (uint8_t i = 0; i < IWMP_MAX_BINDINGS; i++) {
        if (cfg.bindings[i].enabled) {
            _automation.addBinding(cfg.bindings[i]);
        }
    }

    WiFiMgr.begin(cfg.wifi);

    if (cfg.espnow.enabled) {
        EspNow.begin(cfg.espnow.channel);
        EspNow.onReceive([this](const uint8_t* mac, const uint8_t* data, int len) {
            onEspNowReceive(mac, data, len);
        });
    }

    Web.begin(cfg.identity);
    Web.registerRoutes();
    setupWebRoutes();

    if (strlen(cfg.wifi.ssid) > 0) {
        WiFiMgr.connect();
        enterState(GreenhouseState::WIFI_CONNECT);
    } else {
        char ap_ssid[48];
        snprintf(ap_ssid, sizeof(ap_ssid), "iWetMyPlants-GH-%s", Config.getDeviceId());
        WiFiMgr.startAP(ap_ssid);
        WiFiMgr.startCaptivePortal();
        enterState(GreenhouseState::AP_MODE);
    }
}

void GreenhouseController::handleWifiConnectState() {
    WiFiMgr.loop();

    if (WiFiMgr.isConnected()) {
        const DeviceConfig& cfg = Config.getConfig();
        if (cfg.mqtt.enabled && strlen(cfg.mqtt.broker) > 0) {
            Mqtt.begin(cfg.mqtt, cfg.identity);
            Mqtt.connect();
            enterState(GreenhouseState::MQTT_CONNECT);
        } else {
            enterState(GreenhouseState::OPERATIONAL);
        }
    } else if (millis() - _state_enter_time > WIFI_CONNECT_TIMEOUT_MS) {
        LOG_W(TAG, "WiFi timeout, AP mode");
        char ap_ssid[48];
        snprintf(ap_ssid, sizeof(ap_ssid), "iWetMyPlants-GH-%s", Config.getDeviceId());
        WiFiMgr.startAP(ap_ssid);
        WiFiMgr.startCaptivePortal();
        enterState(GreenhouseState::AP_MODE);
    }
}

void GreenhouseController::handleMqttConnectState() {
    WiFiMgr.loop();
    Mqtt.loop();

    if (Mqtt.isConnected()) {
        Mqtt.publishDiscovery();
        Mqtt.publishAvailability(true);
        enterState(GreenhouseState::OPERATIONAL);
    } else if (millis() - _state_enter_time > MQTT_CONNECT_TIMEOUT_MS) {
        LOG_W(TAG, "MQTT timeout, continuing");
        enterState(GreenhouseState::OPERATIONAL);
    }
}

void GreenhouseController::handleApModeState() {
    WiFiMgr.loop();
}

void GreenhouseController::handleOperationalState() {
    WiFiMgr.loop();
    Mqtt.loop();
    EspNow.update();
    _relays.update();
    _automation.update();

    if (millis() - _last_sensor_read_time > SENSOR_READ_INTERVAL_MS) {
        readSensors();
        readEnvSensor();
        _last_sensor_read_time = millis();
    }

    if (isConfigButtonPressed()) {
        char ap_ssid[48];
        snprintf(ap_ssid, sizeof(ap_ssid), "iWetMyPlants-GH-%s", Config.getDeviceId());
        WiFiMgr.startAP(ap_ssid);
        WiFiMgr.startCaptivePortal();
        enterState(GreenhouseState::AP_MODE);
    }
}

MoistureSensor* GreenhouseController::getMoistureSensor(uint8_t index) {
    if (index >= IWMP_MAX_SENSORS) return nullptr;
    return _moisture_sensors[index].get();
}

void GreenhouseController::onRelayCommand(const RelayCommandMsg& msg) {
    if (msg.relay_index < IWMP_MAX_RELAYS) {
        if (msg.state) {
            _relays.turnOn(msg.relay_index, msg.duration_sec);
        } else {
            _relays.turnOff(msg.relay_index);
        }
    }
}

void GreenhouseController::onMoistureReading(const MoistureReadingMsg& msg) {
    _automation.onMoistureReading(msg.sensor_index, msg.moisture_percent);
}

bool GreenhouseController::setRelay(uint8_t index, bool state, uint32_t duration_sec) {
    return state ? _relays.turnOn(index, duration_sec) : _relays.turnOff(index);
}

void GreenhouseController::emergencyStop() {
    _relays.emergencyStopAll();
}

void GreenhouseController::setAutomationEnabled(bool enabled) {
    _automation.setEnabled(enabled);
}

bool GreenhouseController::isAutomationEnabled() const {
    return _automation.isEnabled();
}

void GreenhouseController::initializeSensors() {
    const DeviceConfig& cfg = Config.getConfig();
    _moisture_sensor_count = 0;

    for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
        const MoistureSensorConfig& sc = cfg.moisture_sensors[i];
        if (!sc.enabled) continue;

        _moisture_sensors[i] = createMoistureSensor(sc, i);
        if (_moisture_sensors[i]) {
            _moisture_sensors[i]->begin();
            _moisture_sensor_count++;
        }
    }
    LOG_I(TAG, "Initialized %d moisture sensors", _moisture_sensor_count);
}

void GreenhouseController::initializeEnvSensor() {
    const DeviceConfig& cfg = Config.getConfig();
    if (!cfg.env_sensor.enabled) return;

    switch (cfg.env_sensor.sensor_type) {
        case EnvSensorType::DHT11:
        case EnvSensorType::DHT22:
            _dht_sensor = std::make_unique<DhtSensor>(
                cfg.env_sensor.pin, cfg.env_sensor.sensor_type);
            if (_dht_sensor) _dht_sensor->begin();
            break;
        case EnvSensorType::SHT30:
        case EnvSensorType::SHT31:
        case EnvSensorType::SHT40:
        case EnvSensorType::SHT41:
            _sht_sensor = std::make_unique<ShtSensor>(
                cfg.env_sensor.sensor_type, cfg.env_sensor.i2c_address);
            if (_sht_sensor) _sht_sensor->begin();
            break;
        default:
            break;
    }
}

void GreenhouseController::readSensors() {
    for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
        if (!_moisture_sensors[i] || !_moisture_sensors[i]->isReady()) continue;

        uint16_t raw = _moisture_sensors[i]->readRawAveraged();
        uint8_t  pct = _moisture_sensors[i]->rawToPercent(raw);

        _automation.onMoistureReading(i, pct);

        if (Mqtt.isConnected()) {
            Mqtt.publishMoistureReading(i, raw, pct);
        }
    }
}

void GreenhouseController::readEnvSensor() {
    if (_dht_sensor) {
        float temp, hum;
        if (_dht_sensor->read(temp, hum)) {
            _last_temperature = temp;
            _last_humidity    = hum;
        }
    } else if (_sht_sensor) {
        float temp, hum;
        if (_sht_sensor->read(temp, hum)) {
            _last_temperature = temp;
            _last_humidity    = hum;
        }
    }

    if (!isnan(_last_temperature) && Mqtt.isConnected()) {
        Mqtt.publishEnvironmentalReading(_last_temperature, _last_humidity);
        _automation.onEnvironmentalReading(_last_temperature, _last_humidity);
    }
}

void GreenhouseController::publishState() {
    if (!Mqtt.isConnected()) return;
    for (uint8_t i = 0; i < IWMP_MAX_RELAYS; i++) {
        if (_relays.getConfig(i).enabled) {
            Mqtt.publishRelayState(i, _relays.isOn(i));
        }
    }
}

void GreenhouseController::setupWebRoutes() {
    Web.onRelayControl([this](uint8_t index, bool state) -> bool {
        return setRelay(index, state);
    });

    Web.onStatus([this](JsonDocument& doc) {
        for (uint8_t i = 0; i < IWMP_MAX_RELAYS; i++) {
            doc["relays"][i] = _relays.isOn(i);
        }
        doc["automation"] = _automation.isEnabled();
        if (!isnan(_last_temperature)) {
            doc["temp"]     = _last_temperature;
            doc["humidity"] = _last_humidity;
        }
    });
}

void GreenhouseController::onEspNowReceive(const uint8_t* mac, const uint8_t* data, int len) {
    (void)mac;
    if (len < (int)sizeof(MessageHeader)) return;
    const MessageHeader* hdr = (const MessageHeader*)data;

    switch (hdr->type) {
        case MessageType::RELAY_COMMAND:
            if (len >= (int)sizeof(RelayCommandMsg))
                onRelayCommand(*(const RelayCommandMsg*)data);
            break;
        case MessageType::MOISTURE_READING:
            if (len >= (int)sizeof(MoistureReadingMsg))
                onMoistureReading(*(const MoistureReadingMsg*)data);
            break;
        default:
            break;
    }
}

void GreenhouseController::onMqttMessage(const char* topic, const char* payload) {
    LOG_D(TAG, "MQTT: %s = %s", topic, payload);
}

bool GreenhouseController::isConfigButtonPressed() {
    static constexpr uint8_t CONFIG_BUTTON_PIN = 0;
    static uint32_t press_start = 0;
    static bool was_pressed = false;

    if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
        if (!was_pressed) {
            was_pressed  = true;
            press_start  = millis();
        } else if (millis() - press_start > 3000) {
            was_pressed = false;
            return true;
        }
    } else {
        was_pressed = false;
    }
    return false;
}

} // namespace iwmp
