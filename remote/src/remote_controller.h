/**
 * @file remote_controller.h
 * @brief Remote device main controller
 *
 * Single sensor node with deep sleep support for battery operation.
 */

#pragma once

#include <Arduino.h>
#include "power_modes.h"
#include "espnow_manager.h"
#include "wifi_manager.h"
#include "sensor_interface.h"
#include "message_types.h"

namespace iwmp {

enum class RemoteState {
    BOOT,
    CHECK_WAKE_REASON,
    QUICK_READ,          // Battery mode: read and send, then sleep
    CONFIG_MODE,         // AP + captive portal for setup
    POWERED_MODE,        // USB powered: continuous WiFi + MQTT
    DEEP_SLEEP
};

enum class RemoteMode {
    BATTERY,             // Deep sleep between readings
    POWERED              // Continuous operation with USB power
};

/**
 * @brief Remote device controller
 */
class RemoteController {
public:
    /**
     * @brief Initialize remote
     */
    void begin();

    /**
     * @brief Main loop processing
     */
    void loop();

    /**
     * @brief Get current state
     * @return Remote state
     */
    RemoteState getState() const { return _state; }

    /**
     * @brief Get operating mode
     * @return Remote mode
     */
    RemoteMode getMode() const { return _mode; }

    /**
     * @brief Force enter configuration mode
     */
    void enterConfigMode();

    /**
     * @brief Get moisture sensor
     * @return Pointer to moisture sensor
     */
    MoistureSensor* getSensor() { return _sensor.get(); }

    /**
     * @brief Get last moisture reading
     * @return Moisture percentage
     */
    uint8_t getLastMoisturePercent() const { return _last_moisture_percent; }

    /**
     * @brief Get last raw reading
     * @return Raw ADC value
     */
    uint16_t getLastRawValue() const { return _last_raw_value; }

    /**
     * @brief Get boot count (number of wakes since power-on)
     * @return Boot count
     */
    uint32_t getBootCount() const { return _power.getBootCount(); }

private:
    RemoteState _state = RemoteState::BOOT;
    RemoteMode _mode = RemoteMode::BATTERY;

    std::unique_ptr<MoistureSensor> _sensor;
    PowerModes _power;

    // Last readings
    uint8_t _last_moisture_percent = 0;
    uint16_t _last_raw_value = 0;

    // Timing
    uint32_t _state_enter_time = 0;
    uint32_t _last_send_time = 0;

    static constexpr uint32_t CONFIG_MODE_TIMEOUT_MS = 300000;  // 5 minutes
    static constexpr uint32_t POWERED_SEND_INTERVAL_MS = 60000; // 1 minute

    // ============ State Machine ============

    void enterState(RemoteState new_state);
    void handleBootState();
    void handleCheckWakeReasonState();
    void handleQuickReadState();
    void handleConfigModeState();
    void handlePoweredModeState();

    // ============ Quick Read Cycle ============

    /**
     * @brief Execute quick read cycle (battery mode)
     * Read sensor, send via ESP-NOW, go to sleep
     */
    void quickReadCycle();

    // ============ Internal Methods ============

    /**
     * @brief Initialize sensor from config
     */
    void initializeSensor();

    /**
     * @brief Read and send moisture data
     * @return true if sent successfully
     */
    bool readAndSend();

    /**
     * @brief Build moisture message
     * @param msg Output message
     * @param raw Raw ADC value
     * @param percent Moisture percentage
     */
    void buildMoistureMessage(MoistureReadingMsg& msg, uint16_t raw, uint8_t percent);

    /**
     * @brief Send battery status
     */
    void sendBatteryStatus();

    /**
     * @brief Check if should enter config mode
     * @return true if config mode triggered
     */
    bool shouldEnterConfigMode();

    /**
     * @brief Setup web server for config mode
     */
    void setupConfigWebServer();

    /**
     * @brief Determine operating mode based on power source
     */
    void determineOperatingMode();
};

// Global remote controller instance
extern RemoteController Remote;

} // namespace iwmp
