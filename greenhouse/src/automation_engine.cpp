/**
 * @file automation_engine.cpp
 * @brief Sensor-to-relay automation implementation
 */

#include "automation_engine.h"
#include "logger.h"

namespace iwmp {

static const char* TAG = "AutoEng";

void AutomationEngine::begin(RelayManager* relays) {
    _relays        = relays;
    _binding_count = 0;
    memset(_bindings,       0, sizeof(_bindings));
    memset(_binding_states, 0, sizeof(_binding_states));
    memset(_moisture_valid, 0, sizeof(_moisture_valid));
}

int AutomationEngine::addBinding(const SensorRelayBinding& binding) {
    if (_binding_count >= IWMP_MAX_RELAYS) return -1;
    int idx = _binding_count++;
    _bindings[idx]       = binding;
    _binding_states[idx] = {};
    return idx;
}

bool AutomationEngine::removeBinding(uint8_t index) {
    if (index >= _binding_count) return false;
    for (uint8_t i = index; i < _binding_count - 1; i++) {
        _bindings[i]       = _bindings[i + 1];
        _binding_states[i] = _binding_states[i + 1];
    }
    _binding_count--;
    return true;
}

bool AutomationEngine::updateBinding(uint8_t index, const SensorRelayBinding& binding) {
    if (index >= _binding_count) return false;
    _bindings[index] = binding;
    return true;
}

const SensorRelayBinding* AutomationEngine::getBinding(uint8_t index) const {
    if (index >= _binding_count) return nullptr;
    return &_bindings[index];
}

const BindingState* AutomationEngine::getBindingState(uint8_t index) const {
    if (index >= _binding_count) return nullptr;
    return &_binding_states[index];
}

void AutomationEngine::onMoistureReading(uint8_t sensor_index, uint8_t percent) {
    if (sensor_index >= IWMP_MAX_SENSORS) return;
    _moisture_readings[sensor_index] = percent;
    _moisture_valid[sensor_index]    = true;

    for (uint8_t i = 0; i < _binding_count; i++) {
        if (_bindings[i].enabled && _bindings[i].sensor_index == sensor_index) {
            evaluateBinding(i, percent);
        }
    }
}

void AutomationEngine::onEnvironmentalReading(float temp_c, float humidity_pct) {
    _temperature = temp_c;
    _humidity    = humidity_pct;
}

void AutomationEngine::update() {
    if (!_enabled || isPaused()) return;

    for (uint8_t i = 0; i < _binding_count; i++) {
        if (!_bindings[i].enabled) continue;

        uint8_t sensor_idx = _bindings[i].sensor_index;
        if (!_moisture_valid[sensor_idx]) continue;

        uint32_t interval_ms = _bindings[i].check_interval_sec * 1000UL;
        if (interval_ms == 0) interval_ms = 60000;

        if (millis() - _binding_states[i].last_check >= interval_ms) {
            evaluateBinding(i, _moisture_readings[sensor_idx]);
        }
    }
}

void AutomationEngine::pause(uint32_t duration_sec) {
    _pause_until = millis() + duration_sec * 1000UL;
}

void AutomationEngine::resume() {
    _pause_until = 0;
}

bool AutomationEngine::isPaused() const {
    return _pause_until > 0 && millis() < _pause_until;
}

void AutomationEngine::evaluateBinding(uint8_t index, uint8_t moisture_percent) {
    _binding_states[index].last_check        = millis();
    _binding_states[index].last_sensor_value = moisture_percent;

    if (_binding_states[index].currently_active) {
        if (shouldDeactivate(index, moisture_percent)) {
            deactivateBinding(index, "wet_threshold");
        }
    } else {
        if (shouldActivate(index, moisture_percent)) {
            activateBinding(index);
        }
    }
}

bool AutomationEngine::shouldActivate(uint8_t index, uint8_t moisture_percent) {
    if (moisture_percent > _bindings[index].dry_threshold) {
        _binding_states[index].stable_reading_count = 0;
        return false;
    }
    if (_bindings[index].hysteresis_enabled) {
        _binding_states[index].stable_reading_count++;
        return _binding_states[index].stable_reading_count >= STABLE_READING_THRESHOLD;
    }
    return true;
}

bool AutomationEngine::shouldDeactivate(uint8_t index, uint8_t moisture_percent) {
    if (moisture_percent >= _bindings[index].wet_threshold) return true;

    if (_bindings[index].max_runtime_sec > 0) {
        uint32_t active_sec = (millis() - _binding_states[index].activation_started) / 1000;
        if (active_sec >= _bindings[index].max_runtime_sec) return true;
    }
    return false;
}

void AutomationEngine::activateBinding(uint8_t index) {
    if (!_relays) return;
    const SensorRelayBinding& b = _bindings[index];

    if (_relays->turnOn(b.relay_index, b.max_runtime_sec)) {
        _binding_states[index].currently_active    = true;
        _binding_states[index].activation_started  = millis();
        _binding_states[index].stable_reading_count = 0;
        LOG_I(TAG, "Binding %d activated relay %d (moisture=%d%%)",
              index, b.relay_index, _binding_states[index].last_sensor_value);
    }
}

void AutomationEngine::deactivateBinding(uint8_t index, const char* reason) {
    if (!_relays) return;
    const SensorRelayBinding& b = _bindings[index];

    _relays->turnOff(b.relay_index);
    _binding_states[index].currently_active     = false;
    _binding_states[index].stable_reading_count  = 0;
    LOG_I(TAG, "Binding %d deactivated relay %d (%s)",
          index, b.relay_index, reason);
}

} // namespace iwmp
