/**
 * @file automation_engine.h
 * @brief Sensor-to-relay binding automation
 *
 * Automatically controls relays based on sensor readings
 * with configurable thresholds and hysteresis.
 */

#pragma once

#include <Arduino.h>
#include "relay_manager.h"
#include "config_schema.h"

namespace iwmp {

/**
 * @brief State tracking for a binding
 */
struct BindingState {
    bool currently_active;          // Is relay currently activated by this binding
    uint32_t activation_started;    // When activation started
    uint32_t last_check;            // Last evaluation time
    uint8_t last_sensor_value;      // Last sensor reading
    uint8_t stable_reading_count;   // Consecutive stable readings
};

/**
 * @brief Automation engine for sensor-to-relay control
 */
class AutomationEngine {
public:
    /**
     * @brief Initialize automation engine
     * @param relays Pointer to relay manager
     */
    void begin(RelayManager* relays);

    /**
     * @brief Add automation binding
     * @param binding Binding configuration
     * @return Binding index or -1 on failure
     */
    int addBinding(const SensorRelayBinding& binding);

    /**
     * @brief Remove binding
     * @param index Binding index
     * @return true if removed
     */
    bool removeBinding(uint8_t index);

    /**
     * @brief Update binding configuration
     * @param index Binding index
     * @param binding New configuration
     * @return true if updated
     */
    bool updateBinding(uint8_t index, const SensorRelayBinding& binding);

    /**
     * @brief Get binding configuration
     * @param index Binding index
     * @return Pointer to binding or nullptr
     */
    const SensorRelayBinding* getBinding(uint8_t index) const;

    /**
     * @brief Get binding state
     * @param index Binding index
     * @return Pointer to state or nullptr
     */
    const BindingState* getBindingState(uint8_t index) const;

    /**
     * @brief Get binding count
     * @return Number of active bindings
     */
    uint8_t getBindingCount() const { return _binding_count; }

    // ============ Sensor Data Input ============

    /**
     * @brief Process moisture reading
     * @param sensor_index Sensor index that reported
     * @param percent Moisture percentage
     */
    void onMoistureReading(uint8_t sensor_index, uint8_t percent);

    /**
     * @brief Process environmental reading
     * @param temp_c Temperature in Celsius
     * @param humidity_pct Relative humidity percentage
     */
    void onEnvironmentalReading(float temp_c, float humidity_pct);

    // ============ Control ============

    /**
     * @brief Update automation (call in loop)
     */
    void update();

    /**
     * @brief Enable/disable automation
     * @param enabled Automation state
     */
    void setEnabled(bool enabled) { _enabled = enabled; }

    /**
     * @brief Check if automation is enabled
     * @return true if enabled
     */
    bool isEnabled() const { return _enabled; }

    /**
     * @brief Pause automation temporarily
     * @param duration_sec Pause duration in seconds
     */
    void pause(uint32_t duration_sec);

    /**
     * @brief Resume automation
     */
    void resume();

    /**
     * @brief Check if paused
     * @return true if paused
     */
    bool isPaused() const;

private:
    RelayManager* _relays = nullptr;
    SensorRelayBinding _bindings[IWMP_MAX_RELAYS];
    BindingState _binding_states[IWMP_MAX_RELAYS];
    uint8_t _binding_count = 0;
    bool _enabled = true;

    uint32_t _pause_until = 0;

    // Cached sensor readings
    uint8_t _moisture_readings[IWMP_MAX_SENSORS] = {0};
    bool _moisture_valid[IWMP_MAX_SENSORS] = {false};
    float _temperature = NAN;
    float _humidity = NAN;

    // Hysteresis settings
    static constexpr uint8_t STABLE_READING_THRESHOLD = 3;

    /**
     * @brief Evaluate single binding
     * @param index Binding index
     * @param moisture_percent Current moisture reading
     */
    void evaluateBinding(uint8_t index, uint8_t moisture_percent);

    /**
     * @brief Check if binding should activate
     * @param index Binding index
     * @param moisture_percent Current moisture
     * @return true if should activate
     */
    bool shouldActivate(uint8_t index, uint8_t moisture_percent);

    /**
     * @brief Check if binding should deactivate
     * @param index Binding index
     * @param moisture_percent Current moisture
     * @return true if should deactivate
     */
    bool shouldDeactivate(uint8_t index, uint8_t moisture_percent);

    /**
     * @brief Activate binding (turn on relay)
     * @param index Binding index
     */
    void activateBinding(uint8_t index);

    /**
     * @brief Deactivate binding (turn off relay)
     * @param index Binding index
     * @param reason Deactivation reason
     */
    void deactivateBinding(uint8_t index, const char* reason);
};

} // namespace iwmp
