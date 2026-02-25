/**
 * @file relay_manager.cpp
 * @brief Relay control with safety features implementation
 */

#include "relay_manager.h"
#include "logger.h"

namespace iwmp {

static const char* TAG = "RelayMgr";

void RelayManager::begin(const RelayConfig configs[], uint8_t count) {
    _count = (count > IWMP_MAX_RELAYS) ? IWMP_MAX_RELAYS : count;

    for (uint8_t i = 0; i < _count; i++) {
        _configs[i]    = configs[i];
        _states[i]     = {};
        _daily_limit[i] = 0;

        if (_configs[i].enabled && _configs[i].gpio_pin > 0) {
            pinMode(_configs[i].gpio_pin, OUTPUT);
            setRelayPin(i, false);
        }
    }
    LOG_I(TAG, "Initialized %d relays", _count);
}

bool RelayManager::turnOn(uint8_t index, uint32_t max_duration_sec) {
    if (index >= _count)             return false;
    if (!_configs[index].enabled)    return false;
    if (_states[index].current_state) return true;

    if (!checkSafetyConditions(index)) {
        LOG_W(TAG, "Relay %d blocked: %s", index, _states[index].lockout_reason);
        return false;
    }

    uint32_t duration = max_duration_sec > 0 ? max_duration_sec
                                              : _configs[index].max_on_time_sec;

    _states[index].current_state   = true;
    _states[index].last_on_time    = millis();
    _states[index].activation_count++;
    _states[index].timed_off_at    = (duration > 0)
                                     ? millis() + duration * 1000UL
                                     : 0;

    setRelayPin(index, true);
    LOG_I(TAG, "Relay %d ON (dur=%lu s)", index, (unsigned long)duration);
    return true;
}

bool RelayManager::turnOff(uint8_t index) {
    if (index >= _count)              return false;
    if (!_states[index].current_state) return true;

    uint32_t on_sec = (millis() - _states[index].last_on_time) / 1000;
    _states[index].total_on_time_today += on_sec;
    _states[index].current_state        = false;
    _states[index].last_off_time        = millis();
    _states[index].timed_off_at         = 0;

    setRelayPin(index, false);
    LOG_I(TAG, "Relay %d OFF (was on %lu s)", index, (unsigned long)on_sec);
    return true;
}

bool RelayManager::toggle(uint8_t index) {
    return _states[index].current_state ? turnOff(index) : turnOn(index);
}

bool RelayManager::isOn(uint8_t index) const {
    if (index >= _count) return false;
    return _states[index].current_state;
}

const RelayState& RelayManager::getState(uint8_t index) const {
    static RelayState empty = {};
    if (index >= IWMP_MAX_RELAYS) return empty;
    return _states[index];
}

const RelayConfig& RelayManager::getConfig(uint8_t index) const {
    static RelayConfig empty = {};
    if (index >= IWMP_MAX_RELAYS) return empty;
    return _configs[index];
}

void RelayManager::update() {
    for (uint8_t i = 0; i < _count; i++) {
        if (_states[i].current_state) {
            enforceTimeout(i);
        }
    }
}

void RelayManager::setMaxOnTime(uint8_t index, uint32_t seconds) {
    if (index < IWMP_MAX_RELAYS) _configs[index].max_on_time_sec = seconds;
}

void RelayManager::setMinOffTime(uint8_t index, uint32_t seconds) {
    if (index < IWMP_MAX_RELAYS) _configs[index].min_off_time_sec = seconds;
}

void RelayManager::setCooldown(uint8_t index, uint32_t seconds) {
    if (index < IWMP_MAX_RELAYS) _configs[index].cooldown_sec = seconds;
}

void RelayManager::setDailyLimit(uint8_t index, uint32_t max_runtime_sec) {
    if (index < IWMP_MAX_RELAYS) _daily_limit[index] = max_runtime_sec;
}

void RelayManager::emergencyStopAll() {
    for (uint8_t i = 0; i < _count; i++) {
        if (_states[i].current_state) turnOff(i);
    }
    LOG_W(TAG, "Emergency stop activated");
}

void RelayManager::clearLockout(uint8_t index) {
    if (index < IWMP_MAX_RELAYS) {
        _states[index].locked_out = false;
        memset(_states[index].lockout_reason, 0, sizeof(_states[index].lockout_reason));
    }
}

bool RelayManager::isLockedOut(uint8_t index) const {
    if (index >= IWMP_MAX_RELAYS) return true;
    return _states[index].locked_out;
}

const char* RelayManager::getLockoutReason(uint8_t index) const {
    if (index >= IWMP_MAX_RELAYS) return "invalid";
    return _states[index].lockout_reason;
}

void RelayManager::resetDailyCounters() {
    for (uint8_t i = 0; i < _count; i++) {
        _states[i].total_on_time_today = 0;
        _states[i].activation_count    = 0;
        clearLockout(i);
    }
}

bool RelayManager::checkSafetyConditions(uint8_t index) {
    if (_states[index].locked_out) return false;

    // Min off time
    if (_states[index].last_off_time > 0 && _configs[index].min_off_time_sec > 0) {
        uint32_t off_sec = (millis() - _states[index].last_off_time) / 1000;
        if (off_sec < _configs[index].min_off_time_sec) {
            lockout(index, "min_off_time");
            return false;
        }
    }

    // Daily runtime limit
    if (_daily_limit[index] > 0 &&
        _states[index].total_on_time_today >= _daily_limit[index]) {
        lockout(index, "daily_limit");
        return false;
    }

    return true;
}

void RelayManager::enforceTimeout(uint8_t index) {
    if (!_states[index].current_state) return;

    // Timed auto-off
    if (_states[index].timed_off_at > 0 &&
        millis() >= _states[index].timed_off_at) {
        LOG_I(TAG, "Relay %d timed off", index);
        turnOff(index);
        return;
    }

    // Config max on time
    if (_configs[index].max_on_time_sec > 0) {
        uint32_t on_sec = (millis() - _states[index].last_on_time) / 1000;
        if (on_sec >= _configs[index].max_on_time_sec) {
            LOG_W(TAG, "Relay %d max on time exceeded", index);
            turnOff(index);
            lockout(index, "max_on_time");
        }
    }
}

void RelayManager::setRelayPin(uint8_t index, bool state) {
    bool pin_high = _configs[index].active_low ? !state : state;
    digitalWrite(_configs[index].gpio_pin, pin_high ? HIGH : LOW);
}

void RelayManager::lockout(uint8_t index, const char* reason) {
    _states[index].locked_out = true;
    strncpy(_states[index].lockout_reason, reason,
            sizeof(_states[index].lockout_reason) - 1);
    LOG_W(TAG, "Relay %d locked out: %s", index, reason);
}

} // namespace iwmp
