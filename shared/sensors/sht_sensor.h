/**
 * @file sht_sensor.h
 * @brief SHT3x/SHT4x temperature and humidity sensor
 *
 * High-accuracy I2C temperature and humidity sensors.
 */

#pragma once

#ifndef IWMP_NO_ENVIRONMENTAL

#include <Arduino.h>
#include <Wire.h>
#include "config_schema.h"

namespace iwmp {

/**
 * @brief SHT temperature/humidity sensor wrapper
 */
class ShtSensor {
public:
    /**
     * @brief Construct SHT sensor
     * @param type Sensor type (SHT30, SHT31, SHT40, SHT41)
     * @param i2c_address I2C address (default 0x44)
     */
    ShtSensor(EnvSensorType type, uint8_t i2c_address = 0x44);

    /**
     * @brief Initialize sensor
     * @return true if sensor detected
     */
    bool begin();

    /**
     * @brief Read temperature
     * @return Temperature in Celsius (NAN on error)
     */
    float readTemperature();

    /**
     * @brief Read humidity
     * @return Relative humidity percentage (NAN on error)
     */
    float readHumidity();

    /**
     * @brief Read both temperature and humidity
     * @param temp Output temperature
     * @param humidity Output humidity
     * @return true if read successful
     */
    bool read(float& temp, float& humidity);

    /**
     * @brief Check if sensor is connected
     * @return true if sensor responds
     */
    bool isConnected();

    /**
     * @brief Enable/disable heater (SHT3x only)
     * @param enabled Heater state
     */
    void setHeater(bool enabled);

    /**
     * @brief Trigger soft reset
     */
    void reset();

    /**
     * @brief Get sensor type
     * @return Sensor type enum
     */
    EnvSensorType getType() const { return _type; }

    /**
     * @brief Check if last read was valid
     * @return true if valid reading
     */
    bool isValid() const { return _valid; }

private:
    EnvSensorType _type;
    uint8_t _address;
    bool _initialized = false;
    bool _valid = false;
    float _last_temp = NAN;
    float _last_humidity = NAN;

    /**
     * @brief Send command to sensor
     * @param cmd Command bytes
     * @param len Command length
     * @return true if successful
     */
    bool sendCommand(const uint8_t* cmd, size_t len);

    /**
     * @brief Read data from sensor
     * @param data Output buffer
     * @param len Expected length
     * @return true if successful
     */
    bool readData(uint8_t* data, size_t len);

    /**
     * @brief Calculate CRC8 for data validation
     * @param data Data bytes
     * @param len Data length
     * @return CRC8 value
     */
    uint8_t calculateCRC(const uint8_t* data, size_t len);
};

} // namespace iwmp

#endif // IWMP_NO_ENVIRONMENTAL
