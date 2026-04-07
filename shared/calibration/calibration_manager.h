/**
 * @file calibration_manager.h
 * @brief Two-point calibration management
 *
 * Handles sensor calibration with dry and wet reference points.
 */

#pragma once

#include <Arduino.h>
#include "sensor_interface.h"

namespace iwmp {

enum class CalibrationState {
    IDLE,
    SAMPLING_DRY,
    SAMPLING_WET,
    COMPLETE,
    ERROR
};

class CalibrationManager {
public:
    bool begin(MoistureSensor* sensor, bool is_wet);
    void cancel();
    void update();
    bool applyAndSave(uint8_t sensor_index);

    CalibrationState getState() const { return _state; }
    uint8_t getProgress() const;
    uint16_t getResult() const { return _result_value; }
    const char* getErrorMessage() const { return _error_message; }

private:
    MoistureSensor* _sensor = nullptr;
    CalibrationState _state = CalibrationState::IDLE;
    bool _is_wet = false;
    uint32_t _sample_sum = 0;
    uint16_t _sample_count = 0;
    uint32_t _start_time = 0;
    uint32_t _last_sample_time = 0;
    uint16_t _result_value = 0;
    char _error_message[64] = {0};

    static constexpr uint32_t CALIBRATION_DURATION_MS = 30000;
    static constexpr uint32_t SAMPLE_INTERVAL_MS = 500;
};

// Global calibration manager
extern CalibrationManager Calibration;

} // namespace iwmp
