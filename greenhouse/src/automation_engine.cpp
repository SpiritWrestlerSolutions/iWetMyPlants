/**
 * @file automation_engine.cpp
 * @brief Sensor-to-relay binding automation implementation
 */

#include "automation_engine.h"
#include "logger.h"

namespace iwmp {

static constexpr const char* TAG = "Auto";

void AutomationEngine::begin(RelayManager* relays) {
    _relays = relays;
    _binding_count = 0;
    _enabled = true;
    _pause_until = 0;

    // Clear all bindings and states
    memset(_bindings, 0, sizeof(_bindings));
    memset(_binding_states, 0, sizeof(_binding_states));
    memset(_moisture_readings, 0, sizeof(_moisture_readings));
    memset(_moisture_valid, 0, sizeof(_moisture_valid));

    LOG_I(TAG, "Automation engine initialized");
}

int AutomationEngine::addBinding(const SensorRelayBinding& binding) {
    if (_binding_count >= IWMP_MAX_RELAYS) {
        LOG_W(TAG, "Max bindings reached");
        return -1;
    }

    if (!binding.enabled) {
        LOG_D(TAG, "Binding not enabled, skipping");
        return -1;
    }

    // Validate binding
    if (binding.sensor_index >= IWMP_MAX_SENSORS) {
        LOG_W(TAG, "Invalid sensor index: %d", binding.sensor_index);
        return -1;
    }

    if (binding.relay_index >= _relays->getCount()) {
        LOG_W(TAG, "Invalid relay index: %d", binding.relay_index);
        return -1;
    }

    // Copy binding
    uint8_t idx = _binding_count++;
    memcpy(&_bindings[idx], &binding, sizeof(SensorRelayBinding));

    // Initialize state
    memset(&_binding_states[idx], 0, sizeof(BindingState));

    LOG_I(TAG, "Binding %d: sensor %d -> relay %d (dry<%d, wet>%d)",
          idx, binding.sensor_index, binding.relay_index,
          binding.dry_threshold, binding.wet_threshold);

    return idx;
}

bool AutomationEngine::removeBinding(uint8_t index) {
    if (index >= _binding_count) {
        return false;
    }

    // Deactivate if active
    if (_binding_states[index].currently_active) {
        deactivateBinding(index, "Binding removed");
    }

    // Shift remaining bindings down
    for (uint8_t i = index; i < _binding_count - 1; i++) {
        memcpy(&_bindings[i], &_bindings[i + 1], sizeof(SensorRelayBinding));
        memcpy(&_binding_states[i], &_binding_states[i + 1], sizeof(BindingState));
    }

    _binding_count--;
    LOG_I(TAG, "Binding %d removed", index);
    return true;
}

bool AutomationEngine::updateBinding(uint8_t index, const SensorRelayBinding& binding) {
    if (index >= _binding_count) {
        return false;
    }

    // Deactivate if active (will re-evaluate with new settings)
    if (_binding_states[index].currently_active) {
        deactivateBinding(index, "Binding updated");
    }

    memcpy(&_bindings[index], &binding, sizeof(SensorRelayBinding));

    LOG_I(TAG, "Binding %d updated: sensor %d -> relay %d (dry<%d, wet>%d)",
          index, binding.sensor_index, binding.relay_index,
          binding.dry_threshold, binding.wet_threshold);

    return true;
}

const SensorRelayBinding* AutomationEngine::getBinding(uint8_t index) const {
    if (index >= _binding_count) {
        return nullptr;
    }
    return &_bindings[index];
}

const BindingState* AutomationEngine::getBindingState(uint8_t index) const {
    if (index >= _binding_count) {
        return nullptr;
    }
    return &_binding_states[index];
}

void AutomationEngine::onMoistureReading(uint8_t sensor_index, uint8_t percent) {
    if (sensor_index >= IWMP_MAX_SENSORS) {
        return;
    }

    _moisture_readings[sensor_index] = percent;
    _moisture_valid[sensor_index] = true;

    LOG_D(TAG, "Sensor %d moisture: %d%%", sensor_index, percent);

    // Evaluate all bindings that use this sensor
    for (uint8_t i = 0; i < _binding_count; i++) {
        if (_bindings[i].enabled && _bindings[i].sensor_index == sensor_index) {
            evaluateBinding(i, percent);
        }
    }
}

void AutomationEngine::onEnvironmentalReading(float temp_c, float humidity_pct) {
    _temperature = temp_c;
    _humidity = humidity_pct;

    LOG_D(TAG, "Environmental: %.1f°C, %.1f%%", temp_c, humidity_pct);

    // Could add temperature/humidity based automation here
    // For now, just store the values
}

void AutomationEngine::update() {
    if (!_enabled || isPaused() || !_relays) {
        return;
    }

    uint32_t now = millis();

    // Check for max runtime exceeded
    for (uint8_t i = 0; i < _binding_count; i++) {
        if (!_bindings[i].enabled || !_binding_states[i].currently_active) {
            continue;
        }

        // Check max runtime
        if (_bindings[i].max_runtime_sec > 0) {
            uint32_t runtime = (now - _binding_states[i].activation_started) / 1000;
            if (runtime >= _bindings[i].max_runtime_sec) {
                LOG_W(TAG, "Binding %d max runtime reached (%lu sec)", i, runtime);
                deactivateBinding(i, "Max runtime");
            }
        }

        // Periodic re-evaluation based on check_interval
        if (_bindings[i].check_interval_sec > 0) {
            uint32_t since_check = (now - _binding_states[i].last_check) / 1000;
            if (since_check >= _bindings[i].check_interval_sec) {
                uint8_t sensor_idx = _bindings[i].sensor_index;
                if (_moisture_valid[sensor_idx]) {
                    evaluateBinding(i, _moisture_readings[sensor_idx]);
                }
            }
        }
    }
}

void AutomationEngine::pause(uint32_t duration_sec) {
    _pause_until = millis() + (duration_sec * 1000);
    LOG_I(TAG, "Automation paused for %lu sec", duration_sec);

    // Turn off all active bindings
    for (uint8_t i = 0; i < _binding_count; i++) {
        if (_binding_states[i].currently_active) {
            deactivateBinding(i, "Paused");
        }
    }
}

void AutomationEngine::resume() {
    _pause_until = 0;
    LOG_I(TAG, "Automation resumed");
}

bool AutomationEngine::isPaused() const {
    if (_pause_until == 0) {
        return false;
    }
    return millis() < _pause_until;
}

void AutomationEngine::evaluateBinding(uint8_t index, uint8_t moisture_percent) {
    if (!_enabled || isPaused() || !_relays) {
        return;
    }

    if (index >= _binding_count || !_bindings[index].enabled) {
        return;
    }

    BindingState& state = _binding_states[index];
    state.last_check = millis();
    state.last_sensor_value = moisture_percent;

    LOG_D(TAG, "Evaluating binding %d: moisture=%d%%, active=%d",
          index, moisture_percent, state.currently_active);

    if (state.currently_active) {
        // Check if we should deactivate
        if (shouldDeactivate(index, moisture_percent)) {
            deactivateBinding(index, "Threshold reached");
        }
    } else {
        // Check if we should activate
        if (shouldActivate(index, moisture_percent)) {
            activateBinding(index);
        }
    }
}

bool AutomationEngine::shouldActivate(uint8_t index, uint8_t moisture_percent) {
    const SensorRelayBinding& binding = _bindings[index];
    BindingState& state = _binding_states[index];

    // Moisture below dry threshold triggers activation
    if (moisture_percent <= binding.dry_threshold) {
        if (binding.hysteresis_enabled) {
            // Need stable readings before activating
            state.stable_reading_count++;
            if (state.stable_reading_count >= STABLE_READING_THRESHOLD) {
                return true;
            }
            LOG_D(TAG, "Binding %d: waiting for stable readings (%d/%d)",
                  index, state.stable_reading_count, STABLE_READING_THRESHOLD);
            return false;
        }
        return true;
    }

    // Reset stable count if not at threshold
    state.stable_reading_count = 0;
    return false;
}

bool AutomationEngine::shouldDeactivate(uint8_t index, uint8_t moisture_percent) {
    const SensorRelayBinding& binding = _bindings[index];
    BindingState& state = _binding_states[index];

    // Moisture above wet threshold triggers deactivation
    if (moisture_percent >= binding.wet_threshold) {
        if (binding.hysteresis_enabled) {
            state.stable_reading_count++;
            if (state.stable_reading_count >= STABLE_READING_THRESHOLD) {
                return true;
            }
            LOG_D(TAG, "Binding %d: waiting for stable readings (%d/%d)",
                  index, state.stable_reading_count, STABLE_READING_THRESHOLD);
            return false;
        }
        return true;
    }

    // Reset stable count if not at threshold
    state.stable_reading_count = 0;
    return false;
}

void AutomationEngine::activateBinding(uint8_t index) {
    const SensorRelayBinding& binding = _bindings[index];
    BindingState& state = _binding_states[index];

    LOG_I(TAG, "Activating binding %d: relay %d ON (moisture=%d%% < %d%%)",
          index, binding.relay_index, state.last_sensor_value, binding.dry_threshold);

    // Activate relay with max runtime limit
    uint32_t max_duration = binding.max_runtime_sec;
    if (_relays->turnOn(binding.relay_index, max_duration)) {
        state.currently_active = true;
        state.activation_started = millis();
        state.stable_reading_count = 0;
    } else {
        LOG_W(TAG, "Failed to activate relay %d", binding.relay_index);
    }
}

void AutomationEngine::deactivateBinding(uint8_t index, const char* reason) {
    const SensorRelayBinding& binding = _bindings[index];
    BindingState& state = _binding_states[index];

    uint32_t runtime = (millis() - state.activation_started) / 1000;

    LOG_I(TAG, "Deactivating binding %d: relay %d OFF (%s, ran %lu sec)",
          index, binding.relay_index, reason, runtime);

    _relays->turnOff(binding.relay_index);

    state.currently_active = false;
    state.activation_started = 0;
    state.stable_reading_count = 0;
}

} // namespace iwmp
