/**
 * @file dht_sensor.cpp
 * @brief DHT11/DHT22 temperature and humidity sensor implementation
 */

#include "dht_sensor.h"

namespace iwmp {

DhtSensor::DhtSensor(uint8_t pin, EnvSensorType type)
    : _dht(pin, (type == EnvSensorType::DHT22) ? DHT22 : DHT11)
    , _pin(pin)
    , _type(type) {
}

void DhtSensor::begin() {
    _dht.begin();
}

float DhtSensor::readTemperature() {
    uint32_t now = millis();
    if (now - _last_read_time < MIN_READ_INTERVAL_MS) {
        return _last_temp;
    }
    _last_read_time = now;
    _last_temp = _dht.readTemperature();
    return _last_temp;
}

float DhtSensor::readHumidity() {
    uint32_t now = millis();
    if (now - _last_read_time < MIN_READ_INTERVAL_MS) {
        return _last_humidity;
    }
    _last_read_time = now;
    _last_humidity = _dht.readHumidity();
    return _last_humidity;
}

bool DhtSensor::read(float& temp, float& humidity) {
    uint32_t now = millis();
    if (now - _last_read_time >= MIN_READ_INTERVAL_MS) {
        _last_read_time = now;
        _last_temp     = _dht.readTemperature();
        _last_humidity = _dht.readHumidity();
    }

    temp     = _last_temp;
    humidity = _last_humidity;
    _valid   = !isnan(temp) && !isnan(humidity);
    return _valid;
}

} // namespace iwmp
