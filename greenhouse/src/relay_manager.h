/**
 * @file relay_manager.h
 * @brief Relay control with safety features
 *
 * Manages relay outputs with timeout limits, cooldown periods,
 * and daily runtime tracking.
 */

#pragma once

#include <Arduino.h>
#include "config_schema.h"

namespace iwmp {

/**
 * @brief Runtime state for a relay
 */
struct RelayState {
    bool current_state;             // Current on/off state
    uint32_t last_on_time;          // When relay was last turned on
    uint32_t last_off_time;         // When relay was last turned off
    uint32_t current_on_duration;   // How long currently on (if on)
    uint32_t total_on_time_today;   // Cumulative runtime today
    uint32_t activation_count;      // Times activated today
    bool locked_out;                // Safety lockout active
    char lockout_reason[64];        // Why locked out
    uint32_t timed_off_at;          // When to auto-turn-off (0 = no timer)
};

/**
 * @brief Relay manager with safety features
 */
class RelayManager {
public:
    /**
     * @brief Initialize relay manager
     * @param configs Array of relay configurations
     * @param count Number of relays
     */
    void begin(const RelayConfig configs[], uint8_t count);

    /**
     * @brief Turn relay on
     * @param index Relay index
     * @param max_duration_sec Maximum on time (0 = use config default)
     * @return true if successful
     */
    bool turnOn(uint8_t index, uint32_t max_duration_sec = 0);

    /**
     * @brief Turn relay off
     * @param index Relay index
     * @return true if successful
     */
    bool turnOff(uint8_t index);

    /**
     * @brief Toggle relay state
     * @param index Relay index
     * @return true if successful
     */
    bool toggle(uint8_t index);

    /**
     * @brief Check if relay is on
     * @param index Relay index
     * @return true if on
     */
    bool isOn(uint8_t index) const;

    /**
     * @brief Get relay state
     * @param index Relay index
     * @return Relay state structure
     */
    const RelayState& getState(uint8_t index) const;

    /**
     * @brief Get relay configuration
     * @param index Relay index
     * @return Relay configuration
     */
    const RelayConfig& getConfig(uint8_t index) const;

    /**
     * @brief Get relay count
     * @return Number of configured relays
     */
    uint8_t getCount() const { return _count; }

    /**
     * @brief Update relay states (call in loop)
     * Enforces timeouts and timers
     */
    void update();

    // ============ Safety Features ============

    /**
     * @brief Emergency stop all relays
     */
    void emergencyStopAll();

    /**
     * @brief Clear lockout for relay
     * @param index Relay index
     */
    void clearLockout(uint8_t index);

    /**
     * @brief Check if relay is locked out
     * @param index Relay index
     * @return true if locked out
     */
    bool isLockedOut(uint8_t index) const;

    /**
     * @brief Get lockout reason
     * @param index Relay index
     * @return Lockout reason string
     */
    const char* getLockoutReason(uint8_t index) const;

private:
    RelayConfig _configs[IWMP_MAX_RELAYS];
    RelayState _states[IWMP_MAX_RELAYS];
    uint8_t _count = 0;

    /**
     * @brief Check all safety conditions
     * @param index Relay index
     * @return true if safe to activate
     */
    bool checkSafetyConditions(uint8_t index);

    /**
     * @brief Enforce timeout for relay
     * @param index Relay index
     */
    void enforceTimeout(uint8_t index);

    /**
     * @brief Set physical relay state
     * @param index Relay index
     * @param state Desired state
     */
    void setRelayPin(uint8_t index, bool state);

    /**
     * @brief Lock out relay with reason
     * @param index Relay index
     * @param reason Lockout reason
     */
    void lockout(uint8_t index, const char* reason);
};

} // namespace iwmp
