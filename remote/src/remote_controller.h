/**
 * @file remote_controller.h
 * @brief Remote device controller — three operating modes
 *
 * Supports three operating modes selected in config:
 * - WiFi:       STA connection, HTTP POST to Hub, MQTT, web UI
 * - Standalone:  Permanent AP, local web UI only, no reporting
 * - Low Power:   Deep sleep + ESP-NOW, override button → Standalone
 *
 * Override button (momentary switch) forces Standalone from any mode.
 */

#pragma once

#include <Arduino.h>
#include "power_modes.h"
#include "sensor_interface.h"
#include "config_schema.h"
#include "improv_serial.h"

namespace iwmp {

class RemoteWeb;

enum class RemoteState : uint8_t {
    BOOT,
    CONFIG_MODE,        // AP + captive portal (initial WiFi setup)
    CONNECTING,         // WiFi STA connecting
    RUNNING,            // WiFi mode operational
    STANDALONE,         // Permanent AP mode (or override mode)
    LOW_POWER_CYCLE,    // Read sensor → ESP-NOW → deep sleep
};

class RemoteController {
public:
    void begin();
    void loop();

    // State
    RemoteState getState() const { return _state; }
    RemoteMode getOperatingMode() const;
    bool isOverrideActive() const { return _override_active; }

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
    void returnToConfiguredMode();

    // External trigger (e.g. button hold)
    void enterConfigMode();

    /**
     * @brief Get boot count (number of wakes since power-on)
     * @return Boot count
     */
    uint32_t getBootCount() const { return _power.getBootCount(); }

private:
    RemoteState _state = RemoteState::BOOT;
    uint32_t _state_enter_time = 0;

    // Operating mode
    bool _override_active = false;

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

    // Override button (WiFi/Standalone modes)
    uint32_t _last_button_check = 0;
    bool _button_was_pressed = false;

    // One-shot flag per state (reset on state entry)
    bool _state_initialized = false;

    // Improv WiFi Serial provisioning
    ImprovSerial _improv;
    bool _improvStarted = false;

    // Timing
    static constexpr uint32_t SENSOR_READ_INTERVAL_MS = 5000;
    static constexpr uint32_t HUB_REPORT_INTERVAL_MS = 60000;
    static constexpr uint32_t MQTT_PUBLISH_INTERVAL_MS = 60000;
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
    static constexpr uint32_t WIFI_LOST_FALLBACK_MS = 60000;
    static constexpr uint32_t BUTTON_CHECK_INTERVAL_MS = 100;
    static constexpr uint32_t ANNOUNCE_EVERY_N_CYCLES = 10;

    // State handlers
    void enterState(RemoteState new_state);
    void handleBoot();
    void handleConfigMode();
    void handleConnecting();
    void handleRunning();
    void handleStandalone();
    void handleLowPowerCycle();

    // Actions
    void initSensor();
    void initMqtt();
    void startWeb();
    void readSensor();
    bool reportToHub();
    void publishMqtt();
    void checkReboot();
    void checkOverrideButton();
};

extern RemoteController Remote;

} // namespace iwmp
