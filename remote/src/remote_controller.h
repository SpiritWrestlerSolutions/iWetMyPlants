/**
 * @file remote_controller.h
 * @brief Remote device controller — clean state machine
 *
 * Single AsyncWebServer created at boot. Supports:
 * - WiFi STA connection to user's network
 * - AP mode for initial WiFi setup (captive portal)
 * - HTTP POST reporting to Hub
 * - MQTT publishing to broker (via shared MqttManager)
 * - Web UI: status page + settings page
 */

#pragma once

#include <Arduino.h>
#include "power_modes.h"
#include "sensor_interface.h"

namespace iwmp {

class RemoteWeb;

enum class RemoteState : uint8_t {
    BOOT,
    CONFIG_MODE,     // AP + captive portal, no WiFi SSID configured
    CONNECTING,      // Attempting WiFi STA connection
    RUNNING,         // Normal operation: reads, reports, publishes
};

class RemoteController {
public:
    void begin();
    void loop();

    // State
    RemoteState getState() const { return _state; }

    // Sensor data (used by RemoteWeb)
    uint8_t getLastMoisturePercent() const { return _last_moisture_percent; }
    uint16_t getLastRawValue() const { return _last_raw_value; }
    const char* getSensorTypeName() const;
    MoistureSensor* getSensor() { return _sensor.get(); }

    // Hub report status (used by RemoteWeb status page)
    uint32_t getLastHubReportTime() const { return _last_hub_report_sec; }
    bool getLastHubReportSuccess() const { return _last_hub_report_ok; }

    // MQTT status
    bool isMqttConnected() const;

    // Callbacks from RemoteWeb
    void onMqttConfigChanged();
    void onSensorConfigChanged();
    void scheduleReboot(uint32_t delay_ms);

    // External trigger (e.g. button hold)
    void enterConfigMode();

private:
    RemoteState _state = RemoteState::BOOT;
    uint32_t _state_enter_time = 0;

    // Subsystems
    std::unique_ptr<MoistureSensor> _sensor;
    PowerModes _power;
    RemoteWeb* _web = nullptr;

    // Sensor cache
    uint8_t _last_moisture_percent = 0;
    uint16_t _last_raw_value = 0;
    uint32_t _last_sensor_read_time = 0;

    // Hub reporting
    uint32_t _last_hub_report_time = 0;
    uint32_t _last_hub_report_sec = 0;
    bool _last_hub_report_ok = false;

    // MQTT
    uint32_t _last_mqtt_publish_time = 0;

    // Reboot
    bool _reboot_pending = false;
    uint32_t _reboot_at = 0;

    // WiFi tracking
    bool _was_connected = false;
    uint32_t _wifi_lost_time = 0;

    // One-shot flag per state (reset on state entry)
    bool _state_initialized = false;

    // Timing
    static constexpr uint32_t SENSOR_READ_INTERVAL_MS = 5000;
    static constexpr uint32_t HUB_REPORT_INTERVAL_MS = 60000;
    static constexpr uint32_t MQTT_PUBLISH_INTERVAL_MS = 60000;
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;  // 2x WiFiMgr timeout — let it retry once
    static constexpr uint32_t WIFI_LOST_FALLBACK_MS = 60000;

    // State handlers
    void enterState(RemoteState new_state);
    void handleBoot();
    void handleConfigMode();
    void handleConnecting();
    void handleRunning();

    // Actions
    void initSensor();
    void initMqtt();
    void startWeb();
    void readSensor();
    bool reportToHub();
    void publishMqtt();
    void checkReboot();
};

extern RemoteController Remote;

} // namespace iwmp
