/**
 * @file dht_sensor.h
 * @brief DHT11/DHT22 temperature and humidity sensor
 */

#pragma once

#include <Arduino.h>
#include <DHT.h>
#include "config_schema.h"

namespace iwmp {

/**
 * @brief DHT temperature/humidity sensor wrapper
 */
class DhtSensor {
public:
    /**
     * @brief Construct DHT sensor
     * @param pin Data pin
     * @param type Sensor type (DHT11 or DHT22)
     */
    DhtSensor(uint8_t pin, EnvSensorType type);

    /**
     * @brief Initialize sensor
     */
    void begin();

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
     * @brief Check if last read was successful
     * @return true if valid reading
     */
    bool isValid() const { return _valid; }

    /**
     * @brief Get sensor type
     * @return Sensor type enum
     */
    EnvSensorType getType() const { return _type; }

private:
    DHT _dht;
    uint8_t _pin;
    EnvSensorType _type;
    bool _valid = false;
    float _last_temp = NAN;
    float _last_humidity = NAN;
    uint32_t _last_read_time = 0;
    static constexpr uint32_t MIN_READ_INTERVAL_MS = 2000;  // DHT needs 2s between reads
};

} // namespace iwmp
