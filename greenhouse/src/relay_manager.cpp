/**
 * @file relay_manager.cpp
 * @brief Relay control with safety features
 */

#include "relay_manager.h"
#include "logger.h"

namespace iwmp {

static constexpr const char* TAG = "Relay";

// Static empty config for invalid indices
static const RelayConfig s_empty_config = {};
static const RelayState s_empty_state = {};

void RelayManager::begin(const RelayConfig configs[], uint8_t count) {
    _count = (count > IWMP_MAX_RELAYS) ? IWMP_MAX_RELAYS : count;

    LOG_I(TAG, "Initializing %d relays", _count);

    for (uint8_t i = 0; i < _count; i++) {
        // Copy configuration
        memcpy(&_configs[i], &configs[i], sizeof(RelayConfig));

        // Initialize state
        memset(&_states[i], 0, sizeof(RelayState));
        _states[i].current_state = false;
        _states[i].locked_out = false;
        _daily_limit[i] = 0;

        // Configure GPIO if enabled
        if (_configs[i].enabled && _configs[i].gpio_pin > 0) {
            pinMode(_configs[i].gpio_pin, OUTPUT);
            // Start with relay off
            setRelayPin(i, false);
            LOG_D(TAG, "Relay %d: GPIO %d, active_low=%d, name=%s",
                  i, _configs[i].gpio_pin, _configs[i].active_low, _configs[i].relay_name);
        }
    }

    LOG_I(TAG, "Relay manager initialized");
}

bool RelayManager::turnOn(uint8_t index, uint32_t max_duration_sec) {
    if (index >= _count) {
        LOG_W(TAG, "Invalid relay index: %d", index);
        return false;
    }

    if (!_configs[index].enabled) {
        LOG_W(TAG, "Relay %d not enabled", index);
        return false;
    }

    // Already on?
    if (_states[index].current_state) {
        LOG_D(TAG, "Relay %d already on", index);
        return true;
    }

    // Check safety conditions
    if (!checkSafetyConditions(index)) {
        return false;
    }

    // Determine timeout
    uint32_t timeout = max_duration_sec;
    if (timeout == 0) {
        timeout = _configs[index].max_on_time_sec;
    }

    // Set timed off if timeout specified
    if (timeout > 0) {
        _states[index].timed_off_at = millis() + (timeout * 1000);
        LOG_D(TAG, "Relay %d will auto-off in %lu sec", index, timeout);
    } else {
        _states[index].timed_off_at = 0;
    }

    // Turn on
    setRelayPin(index, true);
    _states[index].current_state = true;
    _states[index].last_on_time = millis();
    _states[index].activation_count++;

    LOG_I(TAG, "Relay %d ON (%s)", index, _configs[index].relay_name);
    return true;
}

bool RelayManager::turnOff(uint8_t index) {
    if (index >= _count) {
        LOG_W(TAG, "Invalid relay index: %d", index);
        return false;
    }

    if (!_configs[index].enabled) {
        LOG_W(TAG, "Relay %d not enabled", index);
        return false;
    }

    // Already off?
    if (!_states[index].current_state) {
        LOG_D(TAG, "Relay %d already off", index);
        return true;
    }

    // Calculate runtime and add to daily total
    uint32_t runtime = (millis() - _states[index].last_on_time) / 1000;
    _states[index].total_on_time_today += runtime;
    _states[index].current_on_duration = 0;

    // Turn off
    setRelayPin(index, false);
    _states[index].current_state = false;
    _states[index].last_off_time = millis();
    _states[index].timed_off_at = 0;

    LOG_I(TAG, "Relay %d OFF (%s), ran for %lu sec", index, _configs[index].relay_name, runtime);
    return true;
}

bool RelayManager::toggle(uint8_t index) {
    if (index >= _count) {
        return false;
    }

    if (_states[index].current_state) {
        return turnOff(index);
    } else {
        return turnOn(index);
    }
}

bool RelayManager::isOn(uint8_t index) const {
    if (index >= _count) {
        return false;
    }
    return _states[index].current_state;
}

const RelayState& RelayManager::getState(uint8_t index) const {
    if (index >= _count) {
        return s_empty_state;
    }
    return _states[index];
}

const RelayConfig& RelayManager::getConfig(uint8_t index) const {
    if (index >= _count) {
        return s_empty_config;
    }
    return _configs[index];
}

void RelayManager::update() {
    uint32_t now = millis();

    for (uint8_t i = 0; i < _count; i++) {
        if (!_configs[i].enabled) {
            continue;
        }

        // Update current on duration
        if (_states[i].current_state) {
            _states[i].current_on_duration = (now - _states[i].last_on_time) / 1000;
        }

        // Check for timed off
        if (_states[i].current_state && _states[i].timed_off_at > 0) {
            if (now >= _states[i].timed_off_at) {
                LOG_I(TAG, "Relay %d auto-off (timer expired)", i);
                turnOff(i);
            }
        }

        // Enforce max on time safety
        enforceTimeout(i);
    }
}

void RelayManager::setMaxOnTime(uint8_t index, uint32_t seconds) {
    if (index >= _count) return;
    _configs[index].max_on_time_sec = seconds;
    LOG_D(TAG, "Relay %d max_on_time set to %lu sec", index, seconds);
}

void RelayManager::setMinOffTime(uint8_t index, uint32_t seconds) {
    if (index >= _count) return;
    _configs[index].min_off_time_sec = seconds;
    LOG_D(TAG, "Relay %d min_off_time set to %lu sec", index, seconds);
}

void RelayManager::setCooldown(uint8_t index, uint32_t seconds) {
    if (index >= _count) return;
    _configs[index].cooldown_sec = seconds;
    LOG_D(TAG, "Relay %d cooldown set to %lu sec", index, seconds);
}

void RelayManager::setDailyLimit(uint8_t index, uint32_t max_runtime_sec) {
    if (index >= _count) return;
    _daily_limit[index] = max_runtime_sec;
    LOG_D(TAG, "Relay %d daily limit set to %lu sec", index, max_runtime_sec);
}

void RelayManager::emergencyStopAll() {
    LOG_W(TAG, "EMERGENCY STOP ALL RELAYS");

    for (uint8_t i = 0; i < _count; i++) {
        if (_configs[i].enabled) {
            setRelayPin(i, false);
            _states[i].current_state = false;
            _states[i].timed_off_at = 0;
            lockout(i, "Emergency stop");
        }
    }
}

void RelayManager::clearLockout(uint8_t index) {
    if (index >= _count) return;

    _states[index].locked_out = false;
    _states[index].lockout_reason[0] = '\0';
    LOG_I(TAG, "Relay %d lockout cleared", index);
}

bool RelayManager::isLockedOut(uint8_t index) const {
    if (index >= _count) return true;  // Invalid index is effectively locked
    return _states[index].locked_out;
}

const char* RelayManager::getLockoutReason(uint8_t index) const {
    if (index >= _count) return "Invalid index";
    if (!_states[index].locked_out) return "";
    return _states[index].lockout_reason;
}

void RelayManager::resetDailyCounters() {
    LOG_I(TAG, "Resetting daily counters");

    for (uint8_t i = 0; i < _count; i++) {
        _states[i].total_on_time_today = 0;
        _states[i].activation_count = 0;

        // Clear daily limit lockouts
        if (_states[i].locked_out &&
            strstr(_states[i].lockout_reason, "daily limit") != nullptr) {
            clearLockout(i);
        }
    }
}

bool RelayManager::checkSafetyConditions(uint8_t index) {
    // Check lockout
    if (_states[index].locked_out) {
        LOG_W(TAG, "Relay %d locked out: %s", index, _states[index].lockout_reason);
        return false;
    }

    uint32_t now = millis();

    // Check minimum off time
    if (_configs[index].min_off_time_sec > 0 && _states[index].last_off_time > 0) {
        uint32_t off_duration = (now - _states[index].last_off_time) / 1000;
        if (off_duration < _configs[index].min_off_time_sec) {
            LOG_W(TAG, "Relay %d min_off_time not met (%lu < %lu sec)",
                  index, off_duration, _configs[index].min_off_time_sec);
            return false;
        }
    }

    // Check cooldown
    if (_configs[index].cooldown_sec > 0 && _states[index].last_off_time > 0) {
        uint32_t since_off = (now - _states[index].last_off_time) / 1000;
        if (since_off < _configs[index].cooldown_sec) {
            LOG_W(TAG, "Relay %d in cooldown (%lu < %lu sec)",
                  index, since_off, _configs[index].cooldown_sec);
            return false;
        }
    }

    // Check daily limit
    if (_daily_limit[index] > 0) {
        if (_states[index].total_on_time_today >= _daily_limit[index]) {
            lockout(index, "Exceeded daily limit");
            LOG_W(TAG, "Relay %d exceeded daily limit (%lu >= %lu sec)",
                  index, _states[index].total_on_time_today, _daily_limit[index]);
            return false;
        }
    }

    return true;
}

void RelayManager::enforceTimeout(uint8_t index) {
    if (!_states[index].current_state) {
        return;
    }

    // Check max on time
    if (_configs[index].max_on_time_sec > 0) {
        if (_states[index].current_on_duration >= _configs[index].max_on_time_sec) {
            LOG_W(TAG, "Relay %d max_on_time exceeded, forcing off", index);
            turnOff(index);
            lockout(index, "Exceeded max on time");
        }
    }
}

void RelayManager::setRelayPin(uint8_t index, bool state) {
    uint8_t pin = _configs[index].gpio_pin;
    bool active_low = _configs[index].active_low;

    // Invert for active-low relays
    bool pin_state = active_low ? !state : state;

    digitalWrite(pin, pin_state ? HIGH : LOW);
    LOG_D(TAG, "Relay %d GPIO %d set to %d (active_low=%d)",
          index, pin, pin_state, active_low);
}

void RelayManager::lockout(uint8_t index, const char* reason) {
    _states[index].locked_out = true;
    strncpy(_states[index].lockout_reason, reason, sizeof(_states[index].lockout_reason) - 1);
    _states[index].lockout_reason[sizeof(_states[index].lockout_reason) - 1] = '\0';
    LOG_W(TAG, "Relay %d locked out: %s", index, reason);
}

} // namespace iwmp
