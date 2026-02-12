/**
 * @file remote_controller.cpp
 * @brief Remote device controller — ground-up rewrite
 *
 * Clean state machine with ONE web server created at boot.
 * No shared web server, no create/destroy cycles.
 */

#include "remote_controller.h"
#include "remote_web.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "logger.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_mac.h>

namespace iwmp {

RemoteController Remote;
static constexpr const char* TAG = "Remote";

// ============================================================
// Public
// ============================================================

void RemoteController::begin() {
    LOG_I(TAG, "Initializing Remote controller");

    _power.begin(Config.getPower());
    initSensor();

    // Allocate web server — but DON'T call begin() yet.
    // AsyncWebServer::begin() needs lwIP initialized, which only
    // happens after WiFi.mode() / WiFi.softAP(). State handlers
    // will call startWeb() after WiFi is up.
    _web = new RemoteWeb();

    // Decide initial state
    const auto& wifi = Config.getWifi();
    if (strlen(wifi.ssid) == 0) {
        enterState(RemoteState::CONFIG_MODE);
    } else {
        enterState(RemoteState::CONNECTING);
    }
}

void RemoteController::loop() {
    switch (_state) {
        case RemoteState::BOOT:        handleBoot();        break;
        case RemoteState::CONFIG_MODE: handleConfigMode();   break;
        case RemoteState::CONNECTING:  handleConnecting();   break;
        case RemoteState::RUNNING:     handleRunning();      break;
    }
    checkReboot();
}

// ============================================================
// State Machine
// ============================================================

void RemoteController::enterState(RemoteState new_state) {
    LOG_I(TAG, "State: %d -> %d", (int)_state, (int)new_state);
    _state = new_state;
    _state_enter_time = millis();
    _state_initialized = false;
}

void RemoteController::handleBoot() {
    // Safety: begin() already transitions out of BOOT
    const auto& wifi = Config.getWifi();
    if (strlen(wifi.ssid) == 0) {
        enterState(RemoteState::CONFIG_MODE);
    } else {
        enterState(RemoteState::CONNECTING);
    }
}

void RemoteController::handleConfigMode() {
    if (!_state_initialized) {
        char ap_ssid[32];
        snprintf(ap_ssid, sizeof(ap_ssid), "IWMP-Remote-%s",
                 Config.getDeviceId() + 6);

        WifiConfig empty_cfg = {};
        WiFiMgr.begin(empty_cfg);
        WiFiMgr.startAP(ap_ssid);
        WiFiMgr.startCaptivePortal();

        // Start web server after WiFi/lwIP is up
        startWeb();

        _state_initialized = true;
        LOG_I(TAG, "Config mode: AP=%s, IP=%s, heap=%u",
              ap_ssid, WiFiMgr.getAPIP().toString().c_str(), ESP.getFreeHeap());
    }

    // Process captive portal DNS
    WiFiMgr.loop();

    // Web server is already running on 0.0.0.0:80 — serves /settings
    // User saves WiFi → scheduleReboot() → reboots → enters CONNECTING
}

void RemoteController::handleConnecting() {
    if (!_state_initialized) {
        const auto& wifi = Config.getWifi();
        WiFiMgr.begin(wifi);
        WiFiMgr.connect();

        // Start web server after WiFi/lwIP is up
        startWeb();

        LOG_I(TAG, "Connecting to: %s", wifi.ssid);
        _state_initialized = true;
    }

    WiFiMgr.loop();

    if (WiFiMgr.isConnected()) {
        LOG_I(TAG, "========================================");
        LOG_I(TAG, "WiFi connected!");
        LOG_I(TAG, "  IP:      %s", WiFiMgr.getIP().toString().c_str());
        LOG_I(TAG, "  RSSI:    %d dBm", WiFiMgr.getRSSI());
        LOG_I(TAG, "  Channel: %d", WiFiMgr.getCurrentChannel());
        LOG_I(TAG, "========================================");
        enterState(RemoteState::RUNNING);
        return;
    }

    // Timeout → fall back to config mode
    if ((millis() - _state_enter_time) > WIFI_CONNECT_TIMEOUT_MS) {
        LOG_W(TAG, "WiFi timeout after %us, entering config mode",
              WIFI_CONNECT_TIMEOUT_MS / 1000);
        enterState(RemoteState::CONFIG_MODE);
    }
}

void RemoteController::handleRunning() {
    if (!_state_initialized) {
        _was_connected = true;
        _wifi_lost_time = 0;

        initMqtt();

        _state_initialized = true;
        LOG_I(TAG, "Running — heap: %u", ESP.getFreeHeap());
    }

    WiFiMgr.loop();
    Mqtt.loop();

    // Track WiFi connectivity
    if (WiFiMgr.isConnected()) {
        _was_connected = true;
        _wifi_lost_time = 0;
    } else if (_was_connected) {
        _was_connected = false;
        _wifi_lost_time = millis();
        LOG_W(TAG, "WiFi lost");
    }

    // WiFi lost too long → config mode
    if (_wifi_lost_time > 0 &&
        (millis() - _wifi_lost_time) > WIFI_LOST_FALLBACK_MS) {
        LOG_W(TAG, "WiFi lost >%us, entering config mode",
              WIFI_LOST_FALLBACK_MS / 1000);
        enterState(RemoteState::CONFIG_MODE);
        return;
    }

    // Read sensor
    readSensor();

    // Report to Hub
    if (WiFiMgr.isConnected() &&
        strlen(Config.getWifi().hub_address) > 0 &&
        (millis() - _last_hub_report_time) >= HUB_REPORT_INTERVAL_MS) {
        _last_hub_report_time = millis();
        _last_hub_report_ok = reportToHub();
        _last_hub_report_sec = millis() / 1000;
    }

    // Publish MQTT
    if (WiFiMgr.isConnected() && Config.getMqtt().enabled &&
        (millis() - _last_mqtt_publish_time) >= MQTT_PUBLISH_INTERVAL_MS) {
        _last_mqtt_publish_time = millis();
        publishMqtt();
    }
}

// ============================================================
// Sensor
// ============================================================

void RemoteController::initSensor() {
    const auto& cfg = Config.getMoistureSensor(0);
    if (!cfg.enabled) {
        LOG_W(TAG, "Sensor 0 disabled");
        return;
    }

    _sensor = createMoistureSensor(cfg, 0);
    if (_sensor) {
        _sensor->begin();
        LOG_I(TAG, "Sensor: %s (%s)", _sensor->getName(),
              _sensor->getInputTypeName());
    } else {
        LOG_E(TAG, "Failed to create sensor");
    }
}

void RemoteController::readSensor() {
    if (!_sensor || !_sensor->isReady()) return;
    if ((millis() - _last_sensor_read_time) < SENSOR_READ_INTERVAL_MS) return;

    _last_sensor_read_time = millis();
    _last_raw_value = _sensor->readRawAveraged();
    _last_moisture_percent = _sensor->rawToPercent(_last_raw_value);
}

const char* RemoteController::getSensorTypeName() const {
    return _sensor ? _sensor->getInputTypeName() : "None";
}

// ============================================================
// Hub HTTP POST
// ============================================================

bool RemoteController::reportToHub() {
    const auto& wifi = Config.getWifi();
    if (strlen(wifi.hub_address) == 0) return false;

    char url[80];
    snprintf(url, sizeof(url), "http://%s/api/remote/report", wifi.hub_address);

    // Hub expects colon-separated MAC (DeviceRegistry::stringToMac format)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    JsonDocument doc;
    doc["mac"] = mac_str;
    doc["device_name"] = Config.getDeviceName();
    doc["device_type"] = 1;  // REMOTE
    doc["moisture_percent"] = _last_moisture_percent;
    doc["rssi"] = WiFi.RSSI();
    doc["firmware_version"] = IWMP_VERSION;

    String payload;
    serializeJson(doc, payload);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    int code = http.POST(payload);
    http.end();

    if (code == 200) {
        LOG_I(TAG, "Hub report OK");
        _power.recordSuccessfulSend();
        return true;
    } else {
        LOG_W(TAG, "Hub report failed: HTTP %d", code);
        _power.recordFailedSend();
        return false;
    }
}

// ============================================================
// MQTT
// ============================================================

void RemoteController::initMqtt() {
    const auto& cfg = Config.getMqtt();
    if (!cfg.enabled || strlen(cfg.broker) == 0) {
        LOG_I(TAG, "MQTT disabled or no broker");
        return;
    }

    Mqtt.begin(cfg, Config.getIdentity());
    Mqtt.onConnect([this](bool) {
        const auto& s = Config.getMoistureSensor(0);
        if (s.enabled) {
            Mqtt.publishMoistureDiscovery(0, s.sensor_name);
        }
        Mqtt.publishRssiDiscovery();
    });
    Mqtt.connect();

    LOG_I(TAG, "MQTT: %s:%u", cfg.broker, cfg.port);
}

void RemoteController::publishMqtt() {
    if (!Mqtt.isConnected()) return;

    SensorReadings readings = {};
    readings.moisture_count = 1;
    readings.moisture[0].valid = (_sensor && _sensor->isReady());
    readings.moisture[0].index = 0;
    readings.moisture[0].raw_value = _last_raw_value;
    readings.moisture[0].percent = _last_moisture_percent;
    readings.rssi = WiFi.RSSI();
    readings.uptime_sec = millis() / 1000;
    readings.free_heap = ESP.getFreeHeap();

    Mqtt.publishState(readings);
}

bool RemoteController::isMqttConnected() const {
    return Mqtt.isInitialized() && Mqtt.isConnected();
}

void RemoteController::onMqttConfigChanged() {
    const auto& cfg = Config.getMqtt();
    if (cfg.enabled && strlen(cfg.broker) > 0) {
        if (Mqtt.isInitialized()) {
            Mqtt.updateConfig(cfg);
        } else {
            initMqtt();
        }
    } else if (Mqtt.isInitialized()) {
        Mqtt.end();
    }
}

// ============================================================
// Web Server (deferred start)
// ============================================================

void RemoteController::startWeb() {
    if (_web && !_web->isRunning()) {
        _web->begin(this);
        LOG_I(TAG, "Web server started — heap: %u", ESP.getFreeHeap());
    }
}

// ============================================================
// Callbacks from RemoteWeb
// ============================================================

void RemoteController::onSensorConfigChanged() {
    if (_sensor) {
        _sensor->updateConfig(Config.getMoistureSensor(0));
        LOG_I(TAG, "Sensor config updated");
    }
}

void RemoteController::scheduleReboot(uint32_t delay_ms) {
    _reboot_pending = true;
    _reboot_at = millis() + delay_ms;
    LOG_I(TAG, "Reboot in %lu ms", delay_ms);
}

void RemoteController::checkReboot() {
    if (_reboot_pending && millis() >= _reboot_at) {
        LOG_I(TAG, "Rebooting...");
        Serial.flush();
        delay(100);
        ESP.restart();
    }
}

void RemoteController::enterConfigMode() {
    enterState(RemoteState::CONFIG_MODE);
}

} // namespace iwmp
