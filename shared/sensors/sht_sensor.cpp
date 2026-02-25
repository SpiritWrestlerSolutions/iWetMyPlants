/**
 * @file sht_sensor.cpp
 * @brief SHT3x/SHT4x temperature and humidity sensor implementation
 *
 * Implements raw I2C communication for both SHT3x (SHT30/SHT31)
 * and SHT4x (SHT40/SHT41) sensor families without external library.
 */

#include "sht_sensor.h"

// SHT3x commands
static const uint8_t SHT3X_CMD_MEASURE_HP[]  = { 0x24, 0x00 };  // High repeatability, no clock stretch
static const uint8_t SHT3X_CMD_RESET[]        = { 0x30, 0xA2 };
static const uint8_t SHT3X_CMD_HEATER_ON[]    = { 0x30, 0x6D };
static const uint8_t SHT3X_CMD_HEATER_OFF[]   = { 0x30, 0x66 };

// SHT4x commands (single byte)
static const uint8_t SHT4X_CMD_MEASURE_HP     = 0xFD;
static const uint8_t SHT4X_CMD_RESET          = 0x94;
static const uint8_t SHT4X_CMD_HEATER_HIGH    = 0x39; // 200mW, 1s pulse
static const uint8_t SHT4X_CMD_HEATER_OFF_READ= 0xFD; // just re-measure after heater off

// Measurement delay
static const uint32_t SHT3X_MEASURE_DELAY_MS = 20;
static const uint32_t SHT4X_MEASURE_DELAY_MS = 10;

namespace iwmp {

ShtSensor::ShtSensor(EnvSensorType type, uint8_t i2c_address)
    : _type(type)
    , _address(i2c_address) {
}

bool ShtSensor::begin() {
    reset();
    delay(2);
    _initialized = isConnected();
    return _initialized;
}

bool ShtSensor::read(float& temp, float& humidity) {
    uint8_t data[6];
    bool ok = false;

    if (_type == EnvSensorType::SHT40 || _type == EnvSensorType::SHT41) {
        // SHT4x: single-byte command
        Wire.beginTransmission(_address);
        Wire.write(SHT4X_CMD_MEASURE_HP);
        if (Wire.endTransmission() != 0) {
            _valid = false;
            temp = humidity = NAN;
            return false;
        }
        delay(SHT4X_MEASURE_DELAY_MS);
        ok = readData(data, 6);
        if (ok) {
            uint16_t rawT  = ((uint16_t)data[0] << 8) | data[1];
            uint16_t rawRH = ((uint16_t)data[3] << 8) | data[4];
            // Validate CRC
            if (calculateCRC(data, 2) != data[2] || calculateCRC(data + 3, 2) != data[5]) {
                _valid = false;
                temp = humidity = NAN;
                return false;
            }
            _last_temp     = -45.0f + 175.0f * (float)rawT  / 65535.0f;
            _last_humidity =  -6.0f + 125.0f * (float)rawRH / 65535.0f;
            _last_humidity = constrain(_last_humidity, 0.0f, 100.0f);
        }
    } else {
        // SHT3x (SHT30 / SHT31)
        ok = sendCommand(SHT3X_CMD_MEASURE_HP, sizeof(SHT3X_CMD_MEASURE_HP));
        if (!ok) {
            _valid = false;
            temp = humidity = NAN;
            return false;
        }
        delay(SHT3X_MEASURE_DELAY_MS);
        ok = readData(data, 6);
        if (ok) {
            uint16_t rawT  = ((uint16_t)data[0] << 8) | data[1];
            uint16_t rawRH = ((uint16_t)data[3] << 8) | data[4];
            if (calculateCRC(data, 2) != data[2] || calculateCRC(data + 3, 2) != data[5]) {
                _valid = false;
                temp = humidity = NAN;
                return false;
            }
            _last_temp     = -45.0f + 175.0f * (float)rawT  / 65535.0f;
            _last_humidity = 100.0f * (float)rawRH / 65535.0f;
        }
    }

    if (ok) {
        temp     = _last_temp;
        humidity = _last_humidity;
        _valid   = true;
    } else {
        temp = humidity = NAN;
        _valid = false;
    }
    return _valid;
}

float ShtSensor::readTemperature() {
    float t, h;
    read(t, h);
    return t;
}

float ShtSensor::readHumidity() {
    float t, h;
    read(t, h);
    return h;
}

bool ShtSensor::isConnected() {
    Wire.beginTransmission(_address);
    return Wire.endTransmission() == 0;
}

void ShtSensor::setHeater(bool enabled) {
    if (_type == EnvSensorType::SHT40 || _type == EnvSensorType::SHT41) {
        // SHT4x: send heater command then re-read
        Wire.beginTransmission(_address);
        Wire.write(enabled ? SHT4X_CMD_HEATER_HIGH : SHT4X_CMD_MEASURE_HP);
        Wire.endTransmission();
    } else {
        // SHT3x
        if (enabled) {
            sendCommand(SHT3X_CMD_HEATER_ON, sizeof(SHT3X_CMD_HEATER_ON));
        } else {
            sendCommand(SHT3X_CMD_HEATER_OFF, sizeof(SHT3X_CMD_HEATER_OFF));
        }
    }
}

void ShtSensor::reset() {
    if (_type == EnvSensorType::SHT40 || _type == EnvSensorType::SHT41) {
        Wire.beginTransmission(_address);
        Wire.write(SHT4X_CMD_RESET);
        Wire.endTransmission();
    } else {
        sendCommand(SHT3X_CMD_RESET, sizeof(SHT3X_CMD_RESET));
    }
}

bool ShtSensor::sendCommand(const uint8_t* cmd, size_t len) {
    Wire.beginTransmission(_address);
    for (size_t i = 0; i < len; i++) {
        Wire.write(cmd[i]);
    }
    return Wire.endTransmission() == 0;
}

bool ShtSensor::readData(uint8_t* data, size_t len) {
    size_t received = Wire.requestFrom((uint8_t)_address, (uint8_t)len);
    if (received != len) return false;
    for (size_t i = 0; i < len; i++) {
        data[i] = Wire.read();
    }
    return true;
}

uint8_t ShtSensor::calculateCRC(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

} // namespace iwmp
