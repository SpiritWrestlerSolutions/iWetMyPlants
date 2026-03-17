/**
 * @file sensor_interface.cpp
 * @brief Moisture sensor and factory function implementation
 */

#include "sensor_interface.h"
#include "capacitive_moisture.h"
#include "ads1115_moisture.h"
#include "mux_moisture.h"
#include "logger.h"

namespace iwmp {

static constexpr const char* TAG = "Sensor";

// ============ MoistureSensor Implementation ============

MoistureSensor::MoistureSensor(std::unique_ptr<ISensorInput> input, const MoistureSensorConfig& config)
    : _input(std::move(input))
    , _config(config) {
}

void MoistureSensor::begin() {
    if (_initialized || !_input) {
        return;
    }

    _input->begin();
    _initialized = _input->isReady();

    if (_initialized) {
        LOG_I(TAG, "%s initialized using %s",
                      _config.sensor_name, _input->getTypeName());
    }
}

bool MoistureSensor::isReady() const {
    return _initialized && _config.enabled && _input && _input->isReady();
}

uint16_t MoistureSensor::readRaw() {
    if (!isReady()) {
        return 0;
    }

    _last_raw = _input->readRaw();
    return _last_raw;
}

uint16_t MoistureSensor::readRawAveraged() {
    if (!isReady()) {
        return 0;
    }

    uint8_t samples = _config.reading_samples > 0 ? _config.reading_samples : 1;
    uint32_t sum = 0;

    for (uint8_t i = 0; i < samples; i++) {
        sum += _input->readRaw();
        if (i < samples - 1 && _config.sample_delay_ms > 0) {
            delay(_config.sample_delay_ms);
        }
    }

    _last_raw = sum / samples;
    return _last_raw;
}

uint8_t MoistureSensor::readPercent() {
    if (!isReady()) {
        return 0;
    }

    _last_raw = readRawAveraged();
    _last_percent = rawToPercent(_last_raw);
    return _last_percent;
}

uint8_t MoistureSensor::rawToPercent(uint16_t raw_value) const {
    // Handle uncalibrated or invalid calibration
    if (_config.dry_value == _config.wet_value) {
        return 50;  // Default to middle value
    }

    // Capacitive sensors typically read HIGH when dry, LOW when wet
    // So we invert the percentage calculation
    int32_t range = static_cast<int32_t>(_config.dry_value) - static_cast<int32_t>(_config.wet_value);
    int32_t value = static_cast<int32_t>(_config.dry_value) - static_cast<int32_t>(raw_value);

    // Calculate percentage
    int32_t percent;
    if (range > 0) {
        // Normal: dry > wet
        percent = (value * 100) / range;
    } else {
        // Inverted: wet > dry (unusual but possible)
        percent = 100 - ((value * 100) / (-range));
    }

    // Clamp to 0-100
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    return static_cast<uint8_t>(percent);
}

void MoistureSensor::setDryValue(uint16_t value) {
    _config.dry_value = value;
}

void MoistureSensor::setWetValue(uint16_t value) {
    _config.wet_value = value;
}

void MoistureSensor::calibrateDry() {
    if (!isReady()) {
        return;
    }

    _config.dry_value = readRawAveraged();
    LOG_I(TAG, "%s dry calibration: %u", _config.sensor_name, _config.dry_value);
}

void MoistureSensor::calibrateWet() {
    if (!isReady()) {
        return;
    }

    _config.wet_value = readRawAveraged();
    LOG_I(TAG, "%s wet calibration: %u", _config.sensor_name, _config.wet_value);
}

bool MoistureSensor::isCalibrated() const {
    // Calibration is valid if dry and wet values are different
    // and have reasonable separation
    uint16_t diff = (_config.dry_value > _config.wet_value)
                    ? (_config.dry_value - _config.wet_value)
                    : (_config.wet_value - _config.dry_value);

    // Require at least 10% of ADC range difference
    uint16_t min_diff = _input ? _input->getMaxValue() / 10 : 400;
    return diff >= min_diff;
}

void MoistureSensor::updateConfig(const MoistureSensorConfig& config) {
    _config = config;
}

void MoistureSensor::setSampleCount(uint8_t samples) {
    _config.reading_samples = samples;
}

void MoistureSensor::setSampleDelay(uint16_t delay_ms) {
    _config.sample_delay_ms = delay_ms;
}

// ============ Factory Functions ============

std::unique_ptr<ISensorInput> createSensorInput(const MoistureSensorConfig& config) {
    if (!config.enabled) {
        return nullptr;
    }

    switch (config.input_type) {
        case SensorInputType::DIRECT_ADC: {
            auto input = std::make_unique<DirectAdcInput>(config.adc_pin);
            return input;
        }

        case SensorInputType::ADS1115: {
            auto input = std::make_unique<Ads1115Input>(config.ads_i2c_address, config.ads_channel);
            return input;
        }

        case SensorInputType::MUX_CD74HC4067: {
            // For MUX, we need additional configuration not in MoistureSensorConfig
            // This is a simplified version - real implementation would need
            // MUX pin configuration from somewhere
            LOG_W(TAG, "MUX input requires additional pin configuration");
            return nullptr;
        }

        default:
            LOG_E(TAG, "Unknown input type: %d", static_cast<int>(config.input_type));
            return nullptr;
    }
}

std::unique_ptr<MoistureSensor> createMoistureSensor(const MoistureSensorConfig& config, uint8_t index) {
    auto input = createSensorInput(config);
    if (!input) {
        return nullptr;
    }

    auto sensor = std::make_unique<MoistureSensor>(std::move(input), config);
    sensor->setIndex(index);
    return sensor;
}

} // namespace iwmp
