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

bool CalibrationManager::begin(MoistureSensor* sensor) {
    if (!sensor) {
        LOG_E(TAG, "Null sensor provided");
        return false;
    }

    if (_state != CalibrationState::IDLE && _state != CalibrationState::COMPLETE &&
        _state != CalibrationState::ERROR) {
        LOG_W(TAG, "Calibration already in progress");
        return false;
    }

    _sensor = sensor;
    _state = CalibrationState::IDLE;
    _sample_count = 0;
    _current_reading = 0;

    // Clear result
    memset(&_result, 0, sizeof(_result));

    LOG_I(TAG, "Calibration started for sensor: %s", sensor->getName());
    return true;
}

void CalibrationManager::cancel() {
    if (_state == CalibrationState::IDLE) {
        return;
    }

    LOG_I(TAG, "Calibration cancelled");
    _state = CalibrationState::IDLE;
    _sensor = nullptr;
    _sample_count = 0;
}

bool CalibrationManager::captureDryPoint() {
    if (!_sensor) {
        LOG_E(TAG, "No sensor set");
        return false;
    }

    if (_state != CalibrationState::IDLE && _state != CalibrationState::ERROR) {
        LOG_W(TAG, "Cannot capture dry point in current state");
        return false;
    }

    LOG_I(TAG, "Capturing dry point...");
    _state = CalibrationState::READING_DRY;
    _sample_count = 0;
    _last_sample_time = millis();

    // Take averaged reading
    _result.dry_value = takeAveragedReading();

    if (_result.dry_value == 0) {
        snprintf(_result.error_message, sizeof(_result.error_message),
                 "Failed to read dry value");
        _state = CalibrationState::ERROR;
        LOG_E(TAG, "Failed to capture dry point");
        return false;
    }

    LOG_I(TAG, "Dry point captured: %d", _result.dry_value);
    _state = CalibrationState::IDLE;  // Ready for wet point
    return true;
}

bool CalibrationManager::captureWetPoint() {
    if (!_sensor) {
        LOG_E(TAG, "No sensor set");
        return false;
    }

    if (_result.dry_value == 0) {
        LOG_E(TAG, "Capture dry point first");
        snprintf(_result.error_message, sizeof(_result.error_message),
                 "Capture dry point first");
        return false;
    }

    LOG_I(TAG, "Capturing wet point...");
    _state = CalibrationState::READING_WET;
    _sample_count = 0;
    _last_sample_time = millis();

    // Take averaged reading
    _result.wet_value = takeAveragedReading();

    if (_result.wet_value == 0) {
        snprintf(_result.error_message, sizeof(_result.error_message),
                 "Failed to read wet value");
        _state = CalibrationState::ERROR;
        LOG_E(TAG, "Failed to capture wet point");
        return false;
    }

    LOG_I(TAG, "Wet point captured: %d", _result.wet_value);

    // Validate calibration
    if (!validateCalibration()) {
        _state = CalibrationState::ERROR;
        return false;
    }

    _state = CalibrationState::COMPLETE;
    _result.valid = true;
    LOG_I(TAG, "Calibration complete: dry=%d, wet=%d", _result.dry_value, _result.wet_value);
    return true;
}

bool CalibrationManager::apply() {
    if (_state != CalibrationState::COMPLETE || !_result.valid) {
        LOG_E(TAG, "No valid calibration to apply");
        return false;
    }

    if (!_sensor) {
        LOG_E(TAG, "No sensor set");
        return false;
    }

    // Apply calibration to sensor
    _sensor->setDryValue(_result.dry_value);
    _sensor->setWetValue(_result.wet_value);

    LOG_I(TAG, "Calibration applied to sensor");
    return true;
}

bool CalibrationManager::save(uint8_t sensor_index) {
    if (_state != CalibrationState::COMPLETE || !_result.valid) {
        LOG_E(TAG, "No valid calibration to save");
        return false;
    }

    // Update config with new calibration values
    MoistureSensorConfig& sensor_cfg = Config.getMoistureSensorMutable(sensor_index);
    sensor_cfg.dry_value = _result.dry_value;
    sensor_cfg.wet_value = _result.wet_value;

    // Save config to NVS
    if (Config.save()) {
        LOG_I(TAG, "Calibration saved for sensor %d", sensor_index);
        return true;
    }

    LOG_E(TAG, "Failed to save calibration");
    return false;
}

void CalibrationManager::update() {
    if (!_sensor) {
        return;
    }

    // Update current reading for display
    if (_state == CalibrationState::READING_DRY ||
        _state == CalibrationState::READING_WET ||
        _state == CalibrationState::IDLE) {

        uint32_t now = millis();
        if ((now - _last_sample_time) >= SAMPLE_INTERVAL_MS) {
            _last_sample_time = now;
            _current_reading = _sensor->readRaw();
        }
    }
}

uint16_t CalibrationManager::takeAveragedReading() {
    if (!_sensor) {
        return 0;
    }

    // Collect samples
    _sample_count = 0;
    uint32_t sum = 0;

    for (uint8_t i = 0; i < CALIBRATION_SAMPLES; i++) {
        uint16_t value = _sensor->readRaw();
        if (value > 0) {
            _samples[_sample_count++] = value;
            sum += value;
        }
        delay(SAMPLE_INTERVAL_MS);
    }

    if (_sample_count == 0) {
        return 0;
    }

    // Calculate average
    uint16_t average = sum / _sample_count;

    // Calculate standard deviation for quality check
    uint32_t variance_sum = 0;
    for (uint8_t i = 0; i < _sample_count; i++) {
        int32_t diff = (int32_t)_samples[i] - (int32_t)average;
        variance_sum += diff * diff;
    }
    uint16_t std_dev = sqrt(variance_sum / _sample_count);

    // Warn if readings are too noisy
    if (std_dev > average / 10) {  // More than 10% variation
        LOG_W(TAG, "High variance in readings: avg=%d, std_dev=%d", average, std_dev);
    }

    LOG_D(TAG, "Averaged reading: %d (from %d samples, std_dev=%d)",
          average, _sample_count, std_dev);

    return average;
}

bool CalibrationManager::validateCalibration() {
    // Check for valid range
    if (_result.dry_value == 0 || _result.wet_value == 0) {
        snprintf(_result.error_message, sizeof(_result.error_message),
                 "Invalid zero reading");
        LOG_E(TAG, "Validation failed: zero reading");
        return false;
    }

    // Dry and wet should be different
    if (_result.dry_value == _result.wet_value) {
        snprintf(_result.error_message, sizeof(_result.error_message),
                 "Dry and wet readings are the same");
        LOG_E(TAG, "Validation failed: identical readings");
        return false;
    }

    // Check for reasonable difference (at least 10% of range)
    int32_t diff = abs((int32_t)_result.dry_value - (int32_t)_result.wet_value);
    uint16_t max_val = max(_result.dry_value, _result.wet_value);

    if (diff < max_val / 10) {
        snprintf(_result.error_message, sizeof(_result.error_message),
                 "Insufficient difference between dry/wet");
        LOG_E(TAG, "Validation failed: insufficient difference (%d)", diff);
        return false;
    }

    // Determine sensor polarity and log
    if (_result.dry_value > _result.wet_value) {
        LOG_I(TAG, "Sensor is high-when-dry (typical capacitive)");
    } else {
        LOG_I(TAG, "Sensor is low-when-dry (resistive type)");
    }

    return true;
}

} // namespace iwmp
