/**
 * @file sensor_interface.h
 * @brief Abstract sensor interface and moisture sensor class
 *
 * Provides a unified interface for different ADC input types:
 * - Direct ESP32 ADC (12-bit, simplest)
 * - ADS1115 external ADC (16-bit, most accurate)
 * - CD74HC4067 multiplexer (16 channels through single ADC)
 */

#pragma once

#include <Arduino.h>
#include <memory>
#include "config_schema.h"

namespace iwmp {

/**
 * @brief Abstract interface for ADC inputs
 *
 * All sensor input types implement this interface, allowing
 * the moisture sensor class to work with any input type.
 */
class ISensorInput {
public:
    virtual ~ISensorInput() = default;

    /**
     * @brief Initialize the sensor input
     */
    virtual void begin() = 0;

    /**
     * @brief Read raw ADC value
     * @return Raw ADC value (0 to getMaxValue())
     */
    virtual uint16_t readRaw() = 0;

    /**
     * @brief Get maximum ADC value
     * @return 4095 for 12-bit, 65535 for 16-bit
     */
    virtual uint16_t getMaxValue() = 0;

    /**
     * @brief Check if sensor input is ready
     * @return true if initialized and ready
     */
    virtual bool isReady() const = 0;

    /**
     * @brief Get input type name
     * @return Human-readable type name
     */
    virtual const char* getTypeName() const = 0;
};

/**
 * @brief Moisture sensor with calibration and averaging
 *
 * Wraps an ISensorInput and provides:
 * - Multi-sample averaging
 * - Two-point calibration (dry/wet)
 * - Percentage conversion
 */
class MoistureSensor {
public:
    /**
     * @brief Construct with sensor input
     * @param input Sensor input (takes ownership)
     * @param config Sensor configuration
     */
    MoistureSensor(std::unique_ptr<ISensorInput> input, const MoistureSensorConfig& config);

    /**
     * @brief Default constructor (no input)
     */
    MoistureSensor() = default;

    /**
     * @brief Initialize sensor
     */
    void begin();

    /**
     * @brief Check if sensor is enabled and ready
     */
    bool isReady() const;

    /**
     * @brief Read raw ADC value (single sample)
     */
    uint16_t readRaw();

    /**
     * @brief Read averaged raw value
     * @return Average of multiple samples
     */
    uint16_t readRawAveraged();

    /**
     * @brief Read moisture percentage
     * @return Calibrated percentage (0-100)
     */
    uint8_t readPercent();

    /**
     * @brief Convert raw value to percentage
     * @param raw_value Raw ADC value
     * @return Calibrated percentage (0-100)
     */
    uint8_t rawToPercent(uint16_t raw_value) const;

    /**
     * @brief Get last raw reading
     */
    uint16_t getLastRaw() const { return _last_raw; }

    /**
     * @brief Get last percentage reading
     */
    uint8_t getLastPercent() const { return _last_percent; }

    // ============ Calibration ============

    /**
     * @brief Set dry calibration point
     * @param value Raw ADC value when sensor is dry
     */
    void setDryValue(uint16_t value);

    /**
     * @brief Set wet calibration point
     * @param value Raw ADC value when sensor is wet
     */
    void setWetValue(uint16_t value);

    /**
     * @brief Get dry calibration value
     */
    uint16_t getDryValue() const { return _config.dry_value; }

    /**
     * @brief Get wet calibration value
     */
    uint16_t getWetValue() const { return _config.wet_value; }

    /**
     * @brief Capture current reading as dry point
     */
    void calibrateDry();

    /**
     * @brief Capture current reading as wet point
     */
    void calibrateWet();

    /**
     * @brief Check if calibration is valid
     */
    bool isCalibrated() const;

    // ============ Configuration ============

    /**
     * @brief Get sensor name
     */
    const char* getName() const { return _config.sensor_name; }

    /**
     * @brief Get sensor index
     */
    uint8_t getIndex() const { return _index; }

    /**
     * @brief Set sensor index
     */
    void setIndex(uint8_t index) { _index = index; }

    /**
     * @brief Get configuration
     */
    const MoistureSensorConfig& getConfig() const { return _config; }

    /**
     * @brief Update configuration
     */
    void updateConfig(const MoistureSensorConfig& config);

    /**
     * @brief Set number of samples for averaging
     */
    void setSampleCount(uint8_t samples);

    /**
     * @brief Set delay between samples
     */
    void setSampleDelay(uint16_t delay_ms);

private:
    std::unique_ptr<ISensorInput> _input;
    MoistureSensorConfig _config;
    uint8_t _index = 0;
    uint16_t _last_raw = 0;
    uint8_t _last_percent = 0;
    bool _initialized = false;
};

/**
 * @brief Factory function to create sensor input based on config
 * @param config Moisture sensor configuration
 * @return Unique pointer to sensor input
 */
std::unique_ptr<ISensorInput> createSensorInput(const MoistureSensorConfig& config);

/**
 * @brief Factory function to create complete moisture sensor
 * @param config Moisture sensor configuration
 * @param index Sensor index
 * @return Unique pointer to moisture sensor
 */
std::unique_ptr<MoistureSensor> createMoistureSensor(const MoistureSensorConfig& config, uint8_t index);

} // namespace iwmp
