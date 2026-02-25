/**
 * @file remote_controller.cpp
 * @brief Remote device controller implementation
 */

#include "remote_controller.h"
#include "config_manager.h"
#include "espnow_manager.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "sensor_interface.h"
#include "logger.h"
#include <WiFi.h>

namespace iwmp {

// Global remote controller instance
RemoteController Remote;

static const char* TAG = "Remote";

void RemoteController::begin() {
    Config.begin(DeviceType::REMOTE);
    Config.load();

    const DeviceConfig& cfg = Config.getConfig();
    _power.begin(cfg.power);

    initializeSensor();
    determineOperatingMode();

    if (_mode == RemoteMode::BATTERY) {
        enterState(RemoteState::QUICK_READ);
    } else {
        WiFiMgr.begin(cfg.wifi);
        if (shouldEnterConfigMode()) {
            setupConfigWebServer();
            enterState(RemoteState::CONFIG_MODE);
        } else {
            if (strlen(cfg.wifi.ssid) > 0) {
                WiFiMgr.connect();
            }
            enterState(RemoteState::POWERED_MODE);
        }
    }
}

void RemoteController::loop() {
    switch (_state) {
        case RemoteState::BOOT:              handleBootState();             break;
        case RemoteState::CHECK_WAKE_REASON: handleCheckWakeReasonState(); break;
        case RemoteState::QUICK_READ:        handleQuickReadState();        break;
        case RemoteState::CONFIG_MODE:       handleConfigModeState();       break;
        case RemoteState::POWERED_MODE:      handlePoweredModeState();      break;
        case RemoteState::DEEP_SLEEP:        break; // Never reached
    }
}

void RemoteController::enterState(RemoteState new_state) {
    _state = new_state;
    _state_enter_time = millis();
}

void RemoteController::handleBootState() {
    enterState(RemoteState::CHECK_WAKE_REASON);
}

void RemoteController::handleCheckWakeReasonState() {
    if (_mode == RemoteMode::BATTERY) {
        enterState(RemoteState::QUICK_READ);
    } else {
        enterState(RemoteState::POWERED_MODE);
    }
}

void RemoteController::handleQuickReadState() {
    quickReadCycle();
    // Deep sleep entered inside quickReadCycle — never returns here
}

void RemoteController::handleConfigModeState() {
    WiFiMgr.loop();
    if (millis() - _state_enter_time > CONFIG_MODE_TIMEOUT_MS) {
        LOG_I(TAG, "Config mode timeout, sleeping");
        _power.enterDeepSleep(_power.calculateOptimalSleepDuration());
    }
}

void RemoteController::handlePoweredModeState() {
    WiFiMgr.loop();
    EspNow.update();

    if (millis() - _last_send_time > POWERED_SEND_INTERVAL_MS) {
        readAndSend();
        _last_send_time = millis();
    }
}

void RemoteController::quickReadCycle() {
    LOG_I(TAG, "Quick read cycle");

    const DeviceConfig& cfg = Config.getConfig();

    WiFi.mode(WIFI_STA);
    EspNow.begin(cfg.espnow.channel);

    const uint8_t* hub_mac = cfg.espnow.hub_mac;
    bool use_broadcast = (hub_mac[0] == 0 && hub_mac[1] == 0 &&
                          hub_mac[2] == 0 && hub_mac[3] == 0);
    if (use_broadcast) {
        EspNow.addPeer(BROADCAST_MAC);
    } else {
        EspNow.addPeer(hub_mac);
    }

    bool sent = readAndSend();
    if (sent) {
        _power.recordSuccessfulSend();
    } else {
        _power.recordFailedSend();
    }

    uint32_t sleep_sec = _power.calculateOptimalSleepDuration();
    LOG_I(TAG, "Sleeping %lu sec", (unsigned long)sleep_sec);
    delay(100); // Flush serial
    _power.enterDeepSleep(sleep_sec);
}

void RemoteController::initializeSensor() {
    const DeviceConfig& cfg = Config.getConfig();
    if (!cfg.moisture_sensors[0].enabled) {
        LOG_W(TAG, "No sensor configured");
        return;
    }
    _sensor = createMoistureSensor(cfg.moisture_sensors[0], 0);
    if (_sensor) {
        _sensor->begin();
        LOG_I(TAG, "Sensor initialized");
    }
}

bool RemoteController::readAndSend() {
    if (!_sensor || !_sensor->isReady()) return false;

    uint16_t raw = _sensor->readRawAveraged();
    uint8_t  pct = _sensor->rawToPercent(raw);
    _last_raw_value        = raw;
    _last_moisture_percent = pct;

    const DeviceConfig& cfg = Config.getConfig();
    const uint8_t* hub_mac  = cfg.espnow.hub_mac;
    bool use_broadcast = (hub_mac[0] == 0 && hub_mac[1] == 0 &&
                          hub_mac[2] == 0 && hub_mac[3] == 0);

    bool result = EspNow.sendMoistureReading(
        use_broadcast ? BROADCAST_MAC : hub_mac, 0, raw, pct);

    LOG_I(TAG, "Moisture raw=%d pct=%d%% -> %s", raw, pct, result ? "ok" : "fail");

    // Also send battery status if battery-powered
    if (cfg.power.battery_powered) {
        sendBatteryStatus();
    }

    return result;
}

void RemoteController::buildMoistureMessage(MoistureReadingMsg& msg,
                                             uint16_t raw, uint8_t percent) {
    memset(&msg, 0, sizeof(msg));
    msg.header.protocol_version = PROTOCOL_VERSION;
    msg.header.type             = MessageType::MOISTURE_READING;
    WiFi.macAddress(msg.header.sender_mac);
    msg.header.sequence_number  = EspNow.getNextSequence();
    msg.header.timestamp        = millis();
    msg.sensor_index            = 0;
    msg.raw_value               = raw;
    msg.moisture_percent        = percent;
    msg.rssi                    = (int8_t)WiFi.RSSI();
}

void RemoteController::sendBatteryStatus() {
    float    voltage  = _power.getBatteryVoltage();
    uint8_t  pct      = _power.getBatteryPercent();
    bool     charging = _power.isExternalPowerConnected();
    const DeviceConfig& cfg = Config.getConfig();
    EspNow.sendBatteryStatus(cfg.espnow.hub_mac,
                              (uint16_t)(voltage * 1000.0f), pct, charging);
}

bool RemoteController::shouldEnterConfigMode() {
    const DeviceConfig& cfg = Config.getConfig();
    if (cfg.power.wake_on_button && cfg.power.wake_button_pin > 0) {
        return digitalRead(cfg.power.wake_button_pin) == LOW;
    }
    return strlen(cfg.wifi.ssid) == 0;
}

void RemoteController::setupConfigWebServer() {
    const DeviceConfig& cfg = Config.getConfig();
    char ap_ssid[48];
    snprintf(ap_ssid, sizeof(ap_ssid), "iWetMyPlants-%s", Config.getDeviceId());
    WiFiMgr.begin(cfg.wifi);
    WiFiMgr.startAP(ap_ssid);
    WiFiMgr.startCaptivePortal();
    Web.begin(cfg.identity);
    Web.registerRoutes();
}

void RemoteController::determineOperatingMode() {
    const DeviceConfig& cfg = Config.getConfig();
    if (cfg.power.battery_powered && !_power.isExternalPowerConnected()) {
        _mode = RemoteMode::BATTERY;
    } else {
        _mode = RemoteMode::POWERED;
    }
    LOG_I(TAG, "Mode: %s", _mode == RemoteMode::BATTERY ? "BATTERY" : "POWERED");
}

void RemoteController::enterConfigMode() {
    setupConfigWebServer();
    enterState(RemoteState::CONFIG_MODE);
}

} // namespace iwmp
