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
    READING_DRY,
    READING_WET,
    COMPLETE,
    ERROR
};

struct CalibrationResult {
    uint16_t dry_value;
    uint16_t wet_value;
    bool valid;
    char error_message[64];
};

/**
 * @brief Calibration manager for moisture sensors
 */
class CalibrationManager {
public:
    /**
     * @brief Start calibration for a sensor
     * @param sensor Sensor to calibrate
     * @return true if calibration started
     */
    bool begin(MoistureSensor* sensor);

    /**
     * @brief Cancel current calibration
     */
    void cancel();

    /**
     * @brief Capture dry point (sensor in air)
     * @return true if captured successfully
     */
    bool captureDryPoint();

    /**
     * @brief Capture wet point (sensor in water)
     * @return true if captured successfully
     */
    bool captureWetPoint();

    /**
     * @brief Apply calibration to sensor
     * @return true if applied successfully
     */
    bool apply();

    /**
     * @brief Save calibration to NVS
     * @param sensor_index Sensor index in config
     * @return true if saved
     */
    bool save(uint8_t sensor_index);

    /**
     * @brief Get current calibration state
     * @return Calibration state
     */
    CalibrationState getState() const { return _state; }

    /**
     * @brief Get calibration result
     * @return Calibration result structure
     */
    const CalibrationResult& getResult() const { return _result; }

    /**
     * @brief Get current averaged reading
     * @return Current averaged ADC value
     */
    uint16_t getCurrentReading() const { return _current_reading; }

    /**
     * @brief Update calibration (call in loop during calibration)
     */
    void update();

private:
    MoistureSensor* _sensor = nullptr;
    CalibrationState _state = CalibrationState::IDLE;
    CalibrationResult _result;
    uint16_t _current_reading = 0;

    static constexpr uint8_t CALIBRATION_SAMPLES = 20;
    static constexpr uint16_t SAMPLE_INTERVAL_MS = 100;

    uint16_t _samples[CALIBRATION_SAMPLES];
    uint8_t _sample_count = 0;
    uint32_t _last_sample_time = 0;

    /**
     * @brief Take averaged reading
     * @return Averaged value
     */
    uint16_t takeAveragedReading();

    /**
     * @brief Validate calibration values
     * @return true if values are valid
     */
    bool validateCalibration();
};

// Global calibration manager
extern CalibrationManager Calibration;

} // namespace iwmp
