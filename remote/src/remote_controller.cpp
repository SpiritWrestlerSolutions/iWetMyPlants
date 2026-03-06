/**
 * @file remote_controller.cpp
 * @brief Remote device controller — three operating modes
 *
 * WiFi:      STA + HTTP POST to Hub + MQTT + web UI
 * Standalone: Permanent AP + web UI (no reporting)
 * Low Power:  Deep sleep + ESP-NOW to Hub
 *
 * Override button forces Standalone from any mode.
 */

#include "remote_controller.h"
#include "remote_web.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "espnow_manager.h"
#include "message_types.h"
#include "logger.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_mac.h>
#include <esp_wifi.h>

namespace iwmp {

RemoteController Remote;
static constexpr const char* TAG = "Remote";

// RTC flag for override mode — survives deep sleep
RTC_DATA_ATTR bool rtc_override_active = false;

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

    // Determine operating mode from config
    RemoteMode mode = getOperatingMode();
    LOG_I(TAG, "Operating mode: %d (%s)", (int)mode,
          mode == RemoteMode::WIFI ? "WiFi" :
          mode == RemoteMode::STANDALONE ? "Standalone" : "Low Power");

    // Check override: button wake or RTC override flag
    if (rtc_override_active) {
        LOG_I(TAG, "RTC override flag set — entering Standalone");
        _override_active = true;
        enterState(RemoteState::STANDALONE);
        return;
    }

    if (_power.wokeFromButton()) {
        LOG_I(TAG, "Button wake detected — entering Standalone override");
        _override_active = true;
        rtc_override_active = true;
        enterState(RemoteState::STANDALONE);
        return;
    }

    // Normal boot based on configured mode
    switch (mode) {
        case RemoteMode::LOW_POWER:
            if (_power.wokeFromTimer() || !_power.isFirstBoot()) {
                enterState(RemoteState::LOW_POWER_CYCLE);
            } else {
                // First boot in Low Power mode — enter Standalone for initial setup
                LOG_I(TAG, "First boot in Low Power mode — Standalone for setup");
                enterState(RemoteState::STANDALONE);
                handleStandalone();  // start AP in setup(), not deferred to loop()
            }
            break;

        case RemoteMode::STANDALONE:
            enterState(RemoteState::STANDALONE);
            handleStandalone();  // start AP in setup(), not deferred to loop()
            break;

        case RemoteMode::WIFI:
        default: {
            const auto& wifi = Config.getWifi();
            if (strlen(wifi.ssid) == 0) {
                enterState(RemoteState::CONFIG_MODE);
                handleConfigMode();  // start AP in setup(), not deferred to loop()
            } else {
                enterState(RemoteState::CONNECTING);
            }
            break;
        }
    }
}

void RemoteController::loop() {
    switch (_state) {
        case RemoteState::BOOT:            handleBoot();            break;
        case RemoteState::CONFIG_MODE:     handleConfigMode();      break;
        case RemoteState::CONNECTING:      handleConnecting();      break;
        case RemoteState::RUNNING:         handleRunning();         break;
        case RemoteState::STANDALONE:      handleStandalone();      break;
        case RemoteState::LOW_POWER_CYCLE: handleLowPowerCycle();   break;
    }
    checkReboot();
}

RemoteMode RemoteController::getOperatingMode() const {
    uint8_t mode = Config.getPower().operating_mode;
    if (mode > (uint8_t)RemoteMode::LOW_POWER) {
        return RemoteMode::WIFI;  // Safety default
    }
    return static_cast<RemoteMode>(mode);
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
    enterState(RemoteState::CONFIG_MODE);
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

        startWeb();

        _state_initialized = true;
        LOG_I(TAG, "Config mode: AP=%s, IP=%s, heap=%u",
              ap_ssid, WiFiMgr.getAPIP().toString().c_str(), ESP.getFreeHeap());
    }

    WiFiMgr.loop();
}

void RemoteController::handleConnecting() {
    if (!_state_initialized) {
        const auto& wifi = Config.getWifi();
        WiFiMgr.begin(wifi);
        WiFiMgr.connect();

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

        // Configure override button pin as input (for polling)
        const auto& pwr = Config.getPower();
        if (pwr.wake_on_button && pwr.wake_button_pin > 0) {
            pinMode(pwr.wake_button_pin, INPUT_PULLUP);
        }

        // Start mDNS so the device is reachable at iwmp-remote-XXXXXX.local
        {
            char mdns_name[32];
            snprintf(mdns_name, sizeof(mdns_name), "iwmp-remote-%s",
                     Config.getDeviceId() + 6);
            if (MDNS.begin(mdns_name)) {
                MDNS.addService("http", "tcp", 80);
                LOG_I(TAG, "mDNS: %s.local", mdns_name);
            } else {
                LOG_W(TAG, "mDNS start failed");
            }
        }

        _state_initialized = true;
        LOG_I(TAG, "Running — heap: %u", ESP.getFreeHeap());
    }

    WiFiMgr.loop();
    Mqtt.loop();

    // Check override button
    checkOverrideButton();

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
// STANDALONE — Permanent AP mode
// ============================================================

void RemoteController::handleStandalone() {
    if (!_state_initialized) {
        char ap_ssid[32];
        const char* name = Config.getDeviceName();
        if (strlen(name) > 0) {
            strlcpy(ap_ssid, name, sizeof(ap_ssid));
            // Replace spaces with dashes for SSID
            for (char* p = ap_ssid; *p; p++) {
                if (*p == ' ') *p = '-';
            }
        } else {
            snprintf(ap_ssid, sizeof(ap_ssid), "IWMP-Remote-%s",
                     Config.getDeviceId() + 6);
        }

        WifiConfig empty_cfg = {};
        WiFiMgr.begin(empty_cfg);
        WiFiMgr.startAP(ap_ssid);
        WiFiMgr.startCaptivePortal();

        startWeb();

        _state_initialized = true;

        if (_override_active) {
            LOG_I(TAG, "Standalone (OVERRIDE): AP=%s, IP=%s, heap=%u",
                  ap_ssid, WiFiMgr.getAPIP().toString().c_str(), ESP.getFreeHeap());
        } else {
            LOG_I(TAG, "Standalone: AP=%s, IP=%s, heap=%u",
                  ap_ssid, WiFiMgr.getAPIP().toString().c_str(), ESP.getFreeHeap());
        }
    }

    WiFiMgr.loop();

    // Read sensor at regular intervals
    readSensor();
}

// ============================================================
// LOW POWER CYCLE — Read → ESP-NOW → Deep Sleep
// ============================================================

void RemoteController::handleLowPowerCycle() {
    LOG_I(TAG, "=== Low Power Cycle (boot #%lu) ===", rtc_boot_count);

    // 1. Read sensor (blocking — single averaged read)
    if (_sensor && _sensor->isReady()) {
        _last_raw_value = _sensor->readRawAveraged();
        _last_moisture_percent = _sensor->rawToPercent(_last_raw_value);
        LOG_I(TAG, "Sensor: %u%% (raw=%u)", _last_moisture_percent, _last_raw_value);
    } else {
        LOG_W(TAG, "Sensor not ready, sending with cached values");
    }

    // 2. Init WiFi radio (STA mode, don't connect — just need radio for ESP-NOW)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);

    // 3. Set channel to match Hub's WiFi/ESP-NOW channel
    const auto& espnow_cfg = Config.getEspNow();
    uint8_t channel = espnow_cfg.channel;
    if (channel == 0) channel = 1;

    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    LOG_I(TAG, "Radio on channel %d", channel);

    // 4. Init ESP-NOW
    if (!EspNow.begin(channel)) {
        LOG_E(TAG, "ESP-NOW init failed!");
        _power.recordFailedSend();
        goto sleep;
    }

    // 5. Add Hub as peer
    {
        const uint8_t* hub_mac = espnow_cfg.hub_mac;

        // Validate Hub MAC (not all zeros)
        bool mac_valid = false;
        for (int i = 0; i < 6; i++) {
            if (hub_mac[i] != 0) { mac_valid = true; break; }
        }

        if (!mac_valid) {
            LOG_E(TAG, "Hub MAC not configured!");
            _power.recordFailedSend();
            EspNow.end();
            goto sleep;
        }

        if (!EspNow.addPeer(hub_mac, channel)) {
            LOG_W(TAG, "Failed to add Hub peer (may already exist)");
        }

        LOG_I(TAG, "Hub MAC: %02X:%02X:%02X:%02X:%02X:%02X",
              hub_mac[0], hub_mac[1], hub_mac[2],
              hub_mac[3], hub_mac[4], hub_mac[5]);

        // 6. Send AnnounceMsg every N cycles so Hub auto-discovers
        if (rtc_boot_count % ANNOUNCE_EVERY_N_CYCLES == 0) {
            LOG_I(TAG, "Sending announce (every %lu cycles)", ANNOUNCE_EVERY_N_CYCLES);
            EspNow.sendAnnounce(
                (uint8_t)DeviceType::REMOTE,
                Config.getDeviceName(),
                IWMP_VERSION,
                0x01  // Capability: has moisture sensor
            );
            delay(50);  // Brief gap between messages
        }

        // 7. Send MoistureReadingMsg
        bool ok = EspNow.sendMoistureReading(
            hub_mac,
            0,  // sensor_index
            _last_raw_value,
            _last_moisture_percent
        );

        if (ok) {
            LOG_I(TAG, "ESP-NOW send OK");
            _power.recordSuccessfulSend();
        } else {
            LOG_W(TAG, "ESP-NOW send failed");
            _power.recordFailedSend();
        }

        EspNow.end();
    }

sleep:
    // 8. Calculate sleep duration (with backoff on failure)
    uint32_t sleep_sec = _power.calculateOptimalSleepDuration();
    LOG_I(TAG, "Sleeping for %lu seconds (failures: %u)",
          sleep_sec, _power.getConsecutiveFailures());

    // 9. Deep sleep — does not return
    _power.enterDeepSleep(sleep_sec);

    // Should never reach here
    LOG_E(TAG, "Deep sleep failed! Rebooting...");
    ESP.restart();
}

// ============================================================
// Override Button Check (for WiFi/Standalone modes)
// ============================================================

void RemoteController::checkOverrideButton() {
    const auto& pwr = Config.getPower();
    if (!pwr.wake_on_button || pwr.wake_button_pin == 0) return;

    if ((millis() - _last_button_check) < BUTTON_CHECK_INTERVAL_MS) return;
    _last_button_check = millis();

    bool pressed = (digitalRead(pwr.wake_button_pin) == LOW);

    if (pressed && !_button_was_pressed) {
        // Button just pressed — debounce check: wait and re-read
        delay(50);
        pressed = (digitalRead(pwr.wake_button_pin) == LOW);
        if (pressed) {
            LOG_I(TAG, "Override button pressed — rebooting into Standalone");
            rtc_override_active = true;
            scheduleReboot(500);
        }
    }
    _button_was_pressed = pressed;
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

void RemoteController::returnToConfiguredMode() {
    LOG_I(TAG, "Returning to configured mode (clearing override)");
    _override_active = false;
    rtc_override_active = false;
    scheduleReboot(500);
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
