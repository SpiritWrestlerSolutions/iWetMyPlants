/**
 * @file hub_controller.cpp
 * @brief Hub device controller implementation
 */

#include "hub_controller.h"
#include "config_manager.h"
#include "espnow_manager.h"
#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "logger.h"
#include <WiFi.h>

namespace iwmp {

// Global hub controller instance
HubController Hub;

static const char* TAG = "Hub";

void HubController::begin() {
    LOG_I(TAG, "Initializing hub controller");
    enterState(HubState::LOAD_CONFIG);
}

void HubController::loop() {
    switch (_state) {
        case HubState::BOOT:         handleBootState();        break;
        case HubState::LOAD_CONFIG:  handleLoadConfigState();  break;
        case HubState::WIFI_CONNECT: handleWifiConnectState(); break;
        case HubState::MQTT_CONNECT: handleMqttConnectState(); break;
        case HubState::AP_MODE:      handleApModeState();      break;
        case HubState::OPERATIONAL:  handleOperationalState(); break;
    }
}

void HubController::enterState(HubState new_state) {
    _state = new_state;
    _state_enter_time = millis();
    LOG_D(TAG, "State -> %d", (int)new_state);
}

void HubController::handleBootState() {
    enterState(HubState::LOAD_CONFIG);
}

void HubController::handleLoadConfigState() {
    Config.begin(DeviceType::HUB);
    Config.load();

    const DeviceConfig& cfg = Config.getConfig();

    // Initialize subsystems
    _registry.begin();
    _registry.loadFromNVS();

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
        enterState(HubState::WIFI_CONNECT);
    } else {
        char ap_ssid[48];
        snprintf(ap_ssid, sizeof(ap_ssid), "iWetMyPlants-%s", Config.getDeviceId());
        WiFiMgr.startAP(ap_ssid);
        WiFiMgr.startCaptivePortal();
        enterState(HubState::AP_MODE);
    }
}

void HubController::handleWifiConnectState() {
    WiFiMgr.loop();

    if (WiFiMgr.isConnected()) {
        LOG_I(TAG, "WiFi connected");
        const DeviceConfig& cfg = Config.getConfig();

        if (cfg.mqtt.enabled && strlen(cfg.mqtt.broker) > 0) {
            Mqtt.begin(cfg.mqtt, cfg.identity);
            Mqtt.connect();
            enterState(HubState::MQTT_CONNECT);
        } else {
            enterState(HubState::OPERATIONAL);
        }
    } else if (millis() - _state_enter_time > WIFI_CONNECT_TIMEOUT_MS) {
        LOG_W(TAG, "WiFi timeout, entering AP mode");
        char ap_ssid[48];
        snprintf(ap_ssid, sizeof(ap_ssid), "iWetMyPlants-%s", Config.getDeviceId());
        WiFiMgr.startAP(ap_ssid);
        WiFiMgr.startCaptivePortal();
        enterState(HubState::AP_MODE);
    }
}

void HubController::handleMqttConnectState() {
    WiFiMgr.loop();
    Mqtt.loop();

    if (Mqtt.isConnected()) {
        LOG_I(TAG, "MQTT connected");
        Mqtt.publishDiscovery();
        Mqtt.publishAvailability(true);
        enterState(HubState::OPERATIONAL);
    } else if (millis() - _state_enter_time > MQTT_CONNECT_TIMEOUT_MS) {
        LOG_W(TAG, "MQTT timeout, continuing without MQTT");
        enterState(HubState::OPERATIONAL);
    }
}

void HubController::handleApModeState() {
    WiFiMgr.loop();
}

void HubController::handleOperationalState() {
    WiFiMgr.loop();
    Mqtt.loop();
    EspNow.update();

    if (millis() - _last_device_check_time > DEVICE_CHECK_INTERVAL_MS) {
        checkDeviceTimeouts();
        _last_device_check_time = millis();
    }

    if (isConfigButtonPressed()) {
        LOG_I(TAG, "Config button held, entering AP mode");
        char ap_ssid[48];
        snprintf(ap_ssid, sizeof(ap_ssid), "iWetMyPlants-%s", Config.getDeviceId());
        WiFiMgr.startAP(ap_ssid);
        WiFiMgr.startCaptivePortal();
        enterState(HubState::AP_MODE);
    }
}

// ============ Device Management ============

void HubController::onDeviceAnnounce(const AnnounceMsg& msg) {
    _registry.addDevice(msg.header.sender_mac, msg.device_type, msg.device_name);
    _registry.updateLastSeen(msg.header.sender_mac, msg.rssi);
    LOG_I(TAG, "Announce from: %s (type=%d)", msg.device_name, msg.device_type);

    if (!EspNow.peerExists(msg.header.sender_mac)) {
        EspNow.addPeer(msg.header.sender_mac);
    }

    const DeviceConfig& cfg = Config.getConfig();
    EspNow.sendPairResponse(msg.header.sender_mac, true,
                            EspNow.getChannel(),
                            cfg.espnow.send_interval_sec);
}

void HubController::onPairRequest(const PairRequestMsg& msg) {
    _registry.addDevice(msg.header.sender_mac, msg.device_type, msg.device_name);
    LOG_I(TAG, "Pair request from: %s", msg.device_name);

    if (!EspNow.peerExists(msg.header.sender_mac)) {
        EspNow.addPeer(msg.header.sender_mac);
    }
    EspNow.sendPairResponse(msg.header.sender_mac, true, EspNow.getChannel(), 60);
}

uint8_t HubController::getConnectedDeviceCount() const {
    return (uint8_t)_registry.getOnlineDeviceCount();
}

// ============ Data Handling ============

void HubController::onMoistureReading(const MoistureReadingMsg& msg) {
    _registry.updateLastSeen(msg.header.sender_mac, msg.rssi);
    _registry.updateReadings(msg.header.sender_mac, msg.moisture_percent);
    LOG_D(TAG, "Moisture: sensor=%d raw=%d pct=%d%%",
          msg.sensor_index, msg.raw_value, msg.moisture_percent);
    if (Mqtt.isConnected()) {
        Mqtt.publishMoistureReading(msg.sensor_index, msg.raw_value, msg.moisture_percent);
    }
}

void HubController::onEnvironmentalReading(const EnvironmentalReadingMsg& msg) {
    float temp = msg.temperature_c_x10 / 10.0f;
    float hum  = msg.humidity_percent_x10 / 10.0f;
    LOG_D(TAG, "Environmental: %.1f°C %.1f%%", temp, hum);
    if (Mqtt.isConnected()) {
        Mqtt.publishEnvironmentalReading(temp, hum);
    }
}

void HubController::onBatteryStatus(const BatteryStatusMsg& msg) {
    _registry.updateLastSeen(msg.header.sender_mac, 0);
    LOG_D(TAG, "Battery: %dmV %d%%", msg.voltage_mv, msg.percent);
    if (Mqtt.isConnected()) {
        Mqtt.publishBatteryStatus(msg.voltage_mv, msg.percent, msg.charging);
    }
}

// ============ Command Forwarding ============

void HubController::sendRelayCommand(const uint8_t* target_mac, uint8_t relay,
                                      bool state, uint32_t duration) {
    EspNow.sendRelayCommand(target_mac, relay, state, duration);
}

void HubController::sendCalibrationCommand(const uint8_t* target_mac,
                                            uint8_t sensor, uint8_t point) {
    CalibrationCommandMsg msg = {};
    msg.header.protocol_version = PROTOCOL_VERSION;
    msg.header.type             = MessageType::CALIBRATION_COMMAND;
    WiFi.macAddress(msg.header.sender_mac);
    msg.header.sequence_number  = EspNow.getNextSequence();
    msg.header.timestamp        = millis();
    msg.sensor_index            = sensor;
    msg.calibration_point       = point;
    EspNow.send(target_mac, (const uint8_t*)&msg, sizeof(msg));
}

void HubController::sendWakeCommand(const uint8_t* target_mac) {
    MessageHeader hdr = {};
    hdr.protocol_version = PROTOCOL_VERSION;
    hdr.type             = MessageType::WAKE_COMMAND;
    WiFi.macAddress(hdr.sender_mac);
    hdr.sequence_number  = EspNow.getNextSequence();
    hdr.timestamp        = millis();
    EspNow.send(target_mac, (const uint8_t*)&hdr, sizeof(hdr));
}

// ============ Internal ============

void HubController::processLocalSensors() {
    for (uint8_t i = 0; i < _local_sensor_count; i++) {
        if (_local_sensors[i]) {
            uint8_t pct = _local_sensors[i]->readPercent();
            if (Mqtt.isConnected()) {
                Mqtt.publishMoistureReading(i, _local_sensors[i]->getLastRaw(), pct);
            }
        }
    }
}

void HubController::checkDeviceTimeouts() {
    _registry.checkTimeouts();
}

void HubController::publishAggregatedState() {
    if (!Mqtt.isConnected()) return;
    // Hub-level state (device count, etc.) published on demand
}

void HubController::onEspNowReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(MessageHeader)) return;
    const MessageHeader* hdr = (const MessageHeader*)data;

    switch (hdr->type) {
        case MessageType::MOISTURE_READING:
            if (len >= (int)sizeof(MoistureReadingMsg))
                onMoistureReading(*(const MoistureReadingMsg*)data);
            break;
        case MessageType::ENVIRONMENTAL_READING:
            if (len >= (int)sizeof(EnvironmentalReadingMsg))
                onEnvironmentalReading(*(const EnvironmentalReadingMsg*)data);
            break;
        case MessageType::BATTERY_STATUS:
            if (len >= (int)sizeof(BatteryStatusMsg))
                onBatteryStatus(*(const BatteryStatusMsg*)data);
            break;
        case MessageType::ANNOUNCE:
            if (len >= (int)sizeof(AnnounceMsg))
                onDeviceAnnounce(*(const AnnounceMsg*)data);
            break;
        case MessageType::PAIR_REQUEST:
            if (len >= (int)sizeof(PairRequestMsg))
                onPairRequest(*(const PairRequestMsg*)data);
            break;
        default:
            LOG_D(TAG, "Unknown msg type: 0x%02X", (uint8_t)hdr->type);
            break;
    }
}

void HubController::onMqttMessage(const char* topic, const char* payload) {
    LOG_D(TAG, "MQTT: %s = %s", topic, payload);
}

void HubController::setupWebRoutes() {
    Web.onStatus([this](JsonDocument& doc) {
        doc["state"]         = (int)_state;
        doc["devices"]       = _registry.getDeviceCount();
        doc["online"]        = _registry.getOnlineDeviceCount();
        doc["wifi_ip"]       = WiFiMgr.getIP().toString();
        doc["mqtt_connected"]= Mqtt.isConnected();
    });

    Web.onRelayControl([](uint8_t index, bool state) -> bool {
        // Hub relays are forwarded to greenhouse via ESP-NOW
        (void)index; (void)state;
        return false;
    });
}

bool HubController::isConfigButtonPressed() {
    static constexpr uint8_t CONFIG_BUTTON_PIN = 0; // BOOT button on most dev boards
    static uint32_t press_start = 0;
    static bool was_pressed = false;

    if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
        if (!was_pressed) {
            was_pressed = true;
            press_start = millis();
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
