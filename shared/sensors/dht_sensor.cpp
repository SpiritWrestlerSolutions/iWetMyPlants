/**
 * @file dht_sensor.cpp
 * @brief DHT11/DHT22 temperature and humidity sensor implementation
 */

#ifndef IWMP_NO_ENVIRONMENTAL

#include "dht_sensor.h"
#include "logger.h"

namespace iwmp {

static constexpr const char* TAG = "DHT";

// Map EnvSensorType to DHT library type
static uint8_t getDhtType(EnvSensorType type) {
    switch (type) {
        case EnvSensorType::DHT11:
            return DHT11;
        case EnvSensorType::DHT22:
            return DHT22;
        default:
            return DHT22;  // Default to DHT22
    }
}

DhtSensor::DhtSensor(uint8_t pin, EnvSensorType type)
    : _dht(pin, getDhtType(type))
    , _pin(pin)
    , _type(type)
{
}

void DhtSensor::begin() {
    LOG_I(TAG, "Initializing %s on pin %d",
          _type == EnvSensorType::DHT11 ? "DHT11" : "DHT22", _pin);

    _dht.begin();

    // Wait for sensor to stabilize
    delay(100);

    // Try an initial read to verify sensor
    float temp = _dht.readTemperature();
    float humidity = _dht.readHumidity();

    if (!isnan(temp) && !isnan(humidity)) {
        _valid = true;
        _last_temp = temp;
        _last_humidity = humidity;
        _last_read_time = millis();
        LOG_I(TAG, "Sensor ready: %.1f°C, %.1f%%", temp, humidity);
    } else {
        _valid = false;
        LOG_W(TAG, "Sensor not responding");
    }
}

float DhtSensor::readTemperature() {
    // Respect minimum read interval
    if (millis() - _last_read_time < MIN_READ_INTERVAL_MS) {
        return _last_temp;
    }

    float temp = _dht.readTemperature();

    if (!isnan(temp)) {
        _last_temp = temp;
        _last_read_time = millis();
        _valid = true;
    } else {
        _valid = false;
        LOG_W(TAG, "Failed to read temperature");
    }

    return _last_temp;
}

float DhtSensor::readHumidity() {
    // Respect minimum read interval
    if (millis() - _last_read_time < MIN_READ_INTERVAL_MS) {
        return _last_humidity;
    }

    float humidity = _dht.readHumidity();

    if (!isnan(humidity)) {
        _last_humidity = humidity;
        _last_read_time = millis();
        _valid = true;
    } else {
        _valid = false;
        LOG_W(TAG, "Failed to read humidity");
    }

    return _last_humidity;
}

bool DhtSensor::read(float& temp, float& humidity) {
    // Respect minimum read interval
    if (millis() - _last_read_time < MIN_READ_INTERVAL_MS) {
        temp = _last_temp;
        humidity = _last_humidity;
        return _valid;
    }

    // Read both values
    float t = _dht.readTemperature();
    float h = _dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
        _last_temp = t;
        _last_humidity = h;
        _last_read_time = millis();
        _valid = true;

        temp = t;
        humidity = h;

        LOG_D(TAG, "Read: %.1f°C, %.1f%%", t, h);
        return true;
    }

    _valid = false;
    temp = _last_temp;
    humidity = _last_humidity;

    LOG_W(TAG, "Read failed");
    return false;
}

} // namespace iwmp

#endif // IWMP_NO_ENVIRONMENTAL
