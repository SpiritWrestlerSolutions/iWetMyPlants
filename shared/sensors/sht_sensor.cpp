/**
 * @file sht_sensor.cpp
 * @brief SHT3x temperature and humidity sensor implementation
 *
 * Direct I2C implementation for SHT30/SHT31 sensors.
 */

#ifndef IWMP_NO_ENVIRONMENTAL

#include "sht_sensor.h"
#include "logger.h"

namespace iwmp {

static constexpr const char* TAG = "SHT";

// SHT3x Commands
static constexpr uint8_t CMD_MEASURE_HIGH[] = {0x24, 0x00};    // High repeatability
static constexpr uint8_t CMD_MEASURE_MED[] = {0x24, 0x0B};     // Medium repeatability
static constexpr uint8_t CMD_MEASURE_LOW[] = {0x24, 0x16};     // Low repeatability
static constexpr uint8_t CMD_HEATER_ON[] = {0x30, 0x6D};
static constexpr uint8_t CMD_HEATER_OFF[] = {0x30, 0x66};
static constexpr uint8_t CMD_SOFT_RESET[] = {0x30, 0xA2};
static constexpr uint8_t CMD_READ_STATUS[] = {0xF3, 0x2D};

// CRC polynomial for SHT3x
static constexpr uint8_t CRC_POLYNOMIAL = 0x31;
static constexpr uint8_t CRC_INIT = 0xFF;

ShtSensor::ShtSensor(EnvSensorType type, uint8_t i2c_address)
    : _type(type)
    , _address(i2c_address)
{
}

bool ShtSensor::begin() {
    LOG_I(TAG, "Initializing SHT3x at address 0x%02X", _address);

    // Ensure Wire is initialized
    Wire.begin();

    // Check if sensor responds
    if (!isConnected()) {
        LOG_E(TAG, "Sensor not found at address 0x%02X", _address);
        return false;
    }

    // Soft reset
    reset();
    delay(10);

    // Try an initial read
    float temp, humidity;
    if (read(temp, humidity)) {
        LOG_I(TAG, "Sensor ready: %.1f°C, %.1f%%", temp, humidity);
        _initialized = true;
        return true;
    }

    LOG_W(TAG, "Sensor found but initial read failed");
    _initialized = true;  // Still mark as initialized, read might work later
    return true;
}

float ShtSensor::readTemperature() {
    float temp, humidity;
    if (read(temp, humidity)) {
        return temp;
    }
    return _last_temp;
}

float ShtSensor::readHumidity() {
    float temp, humidity;
    if (read(temp, humidity)) {
        return humidity;
    }
    return _last_humidity;
}

bool ShtSensor::read(float& temp, float& humidity) {
    // Send measurement command (high repeatability)
    if (!sendCommand(CMD_MEASURE_HIGH, 2)) {
        LOG_W(TAG, "Failed to send measure command");
        _valid = false;
        temp = _last_temp;
        humidity = _last_humidity;
        return false;
    }

    // Wait for measurement (high repeatability takes ~15ms)
    delay(20);

    // Read 6 bytes: temp_msb, temp_lsb, temp_crc, hum_msb, hum_lsb, hum_crc
    uint8_t data[6];
    if (!readData(data, 6)) {
        LOG_W(TAG, "Failed to read data");
        _valid = false;
        temp = _last_temp;
        humidity = _last_humidity;
        return false;
    }

    // Verify CRCs
    if (calculateCRC(data, 2) != data[2]) {
        LOG_W(TAG, "Temperature CRC mismatch");
        _valid = false;
        temp = _last_temp;
        humidity = _last_humidity;
        return false;
    }

    if (calculateCRC(data + 3, 2) != data[5]) {
        LOG_W(TAG, "Humidity CRC mismatch");
        _valid = false;
        temp = _last_temp;
        humidity = _last_humidity;
        return false;
    }

    // Convert raw values
    uint16_t raw_temp = (data[0] << 8) | data[1];
    uint16_t raw_hum = (data[3] << 8) | data[4];

    // Formula from SHT31 datasheet
    temp = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    humidity = 100.0f * ((float)raw_hum / 65535.0f);

    // Clamp humidity to valid range
    if (humidity > 100.0f) humidity = 100.0f;
    if (humidity < 0.0f) humidity = 0.0f;

    _last_temp = temp;
    _last_humidity = humidity;
    _valid = true;

    LOG_D(TAG, "Read: %.2f°C, %.2f%%", temp, humidity);
    return true;
}

bool ShtSensor::isConnected() {
    Wire.beginTransmission(_address);
    uint8_t error = Wire.endTransmission();
    return (error == 0);
}

void ShtSensor::setHeater(bool enabled) {
    if (enabled) {
        sendCommand(CMD_HEATER_ON, 2);
        LOG_D(TAG, "Heater ON");
    } else {
        sendCommand(CMD_HEATER_OFF, 2);
        LOG_D(TAG, "Heater OFF");
    }
}

void ShtSensor::reset() {
    sendCommand(CMD_SOFT_RESET, 2);
    LOG_D(TAG, "Soft reset sent");
}

bool ShtSensor::sendCommand(const uint8_t* cmd, size_t len) {
    Wire.beginTransmission(_address);
    Wire.write(cmd, len);
    uint8_t error = Wire.endTransmission();
    return (error == 0);
}

bool ShtSensor::readData(uint8_t* data, size_t len) {
    size_t received = Wire.requestFrom(_address, (uint8_t)len);
    if (received != len) {
        LOG_W(TAG, "Expected %d bytes, got %d", len, received);
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        data[i] = Wire.read();
    }

    return true;
}

uint8_t ShtSensor::calculateCRC(const uint8_t* data, size_t len) {
    uint8_t crc = CRC_INIT;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ CRC_POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

} // namespace iwmp

#endif // IWMP_NO_ENVIRONMENTAL
