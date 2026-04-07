/**
 * @file calibration_manager.cpp
 * @brief Two-point calibration management implementation
 */

#include "calibration_manager.h"
#include "config_manager.h"
#include "logger.h"

namespace iwmp {

// Global calibration manager instance
CalibrationManager Calibration;

static constexpr const char* TAG = "Cal";

bool CalibrationManager::begin(MoistureSensor* sensor, bool is_wet) {
    if (!sensor) return false;
    if (_state == CalibrationState::SAMPLING_DRY || _state == CalibrationState::SAMPLING_WET) {
        return false; // Busy
    }

    _sensor = sensor;
    _is_wet = is_wet;
    _state = is_wet ? CalibrationState::SAMPLING_WET : CalibrationState::SAMPLING_DRY;
    _sample_sum = 0;
    _sample_count = 0;
    _result_value = 0;
    memset(_error_message, 0, sizeof(_error_message));
    _start_time = millis();
    _last_sample_time = 0;

    LOG_I(TAG, "Started 30s calibration for %s (%s)", sensor->getName(), is_wet ? "WET" : "DRY");
    return true;
}

void CalibrationManager::cancel() {
    _state = CalibrationState::IDLE;
    _sensor = nullptr;
    LOG_I(TAG, "Calibration cancelled");
}

void CalibrationManager::update() {
    if (_state != CalibrationState::SAMPLING_DRY && _state != CalibrationState::SAMPLING_WET) {
        return;
    }

    uint32_t now = millis();
    
    if (now - _start_time >= CALIBRATION_DURATION_MS) {
        if (_sample_count > 0) {
            _result_value = _sample_sum / _sample_count;
            if (_result_value < 10) {
                _state = CalibrationState::ERROR;
                snprintf(_error_message, sizeof(_error_message), "Improbable reading (%d). Check connection.", _result_value);
                LOG_E(TAG, "Calibration failed: %s", _error_message);
            } else {
                _state = CalibrationState::COMPLETE;
                LOG_I(TAG, "Calibration complete. Average: %d", _result_value);
            }
        } else {
            _state = CalibrationState::ERROR;
            snprintf(_error_message, sizeof(_error_message), "No samples collected.");
            LOG_E(TAG, "Calibration failed: %s", _error_message);
        }
        return;
    }
    
    if (now - _last_sample_time >= SAMPLE_INTERVAL_MS) {
        _last_sample_time = now;
        if (_sensor && _sensor->isReady()) {
            uint16_t val = _sensor->readRaw();
            if (val > 0) {
                _sample_sum += val;
                _sample_count++;
            }
        }
    }
}

uint8_t CalibrationManager::getProgress() const {
    if (_state == CalibrationState::IDLE) return 0;
    if (_state == CalibrationState::COMPLETE || _state == CalibrationState::ERROR) return 100;
    uint32_t elapsed = millis() - _start_time;
    if (elapsed >= CALIBRATION_DURATION_MS) return 100;
    return (elapsed * 100) / CALIBRATION_DURATION_MS;
}

bool CalibrationManager::applyAndSave(uint8_t sensor_index) {
    if (_state != CalibrationState::COMPLETE || _result_value == 0 || !_sensor) {
        return false;
    }

    if (_is_wet) {
        _sensor->setWetValue(_result_value);
        Config.getMoistureSensorMutable(sensor_index).wet_value = _result_value;
    } else {
        _sensor->setDryValue(_result_value);
        Config.getMoistureSensorMutable(sensor_index).dry_value = _result_value;
    }

    if (Config.save()) {
        LOG_I(TAG, "Saved %s calibration for sensor %d: %d", _is_wet ? "WET" : "DRY", sensor_index, _result_value);
        _state = CalibrationState::IDLE;
        return true;
    }
    return false;
}

}