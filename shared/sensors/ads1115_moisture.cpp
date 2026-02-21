/**
 * @file ads1115_moisture.cpp
 * @brief ADS1115 16-bit ADC input implementation
 */

#include "ads1115_moisture.h"
#include <Wire.h>

namespace iwmp {

// Static shared ADC instances
Adafruit_ADS1115 Ads1115Input::s_adc_instances[4];
bool Ads1115Input::s_adc_initialized[4] = {false, false, false, false};
TwoWire* Ads1115Input::s_adc_wire[4] = {nullptr, nullptr, nullptr, nullptr};
SemaphoreHandle_t Ads1115Input::s_i2c_mutex[4] = {nullptr, nullptr, nullptr, nullptr};

void Ads1115Input::setWireForAddress(uint8_t address, TwoWire* wire) {
    uint8_t idx = address - 0x48;
    if (idx < 4) {
        s_adc_wire[idx] = wire;
        // Create mutex for this bus if it doesn't exist yet
        if (!s_i2c_mutex[idx]) {
            s_i2c_mutex[idx] = xSemaphoreCreateMutex();
        }
        Serial.printf("[ADS1115] Address 0x%02X assigned to %s\n",
                      address, (wire == &Wire) ? "Wire" : "Wire1");
    }
}

Ads1115Input::Ads1115Input(uint8_t i2c_address, uint8_t channel)
    : _address(i2c_address)
    , _channel(channel % ADS1115_CHANNELS) {
}

void Ads1115Input::begin() {
    if (_initialized) {
        return;
    }

    // Get index for shared instance (0x48 -> 0, 0x49 -> 1, etc.)
    uint8_t instance_idx = _address - 0x48;
    if (instance_idx >= 4) {
        Serial.printf("[ADS1115] Invalid address 0x%02X\n", _address);
        return;
    }

    // Resolve which Wire bus this address uses (default to Wire if not mapped)
    TwoWire* wire = s_adc_wire[instance_idx] ? s_adc_wire[instance_idx] : &Wire;
    _wire = wire;

    // Ensure mutex exists (create if setWireForAddress wasn't called)
    if (!s_i2c_mutex[instance_idx]) {
        s_i2c_mutex[instance_idx] = xSemaphoreCreateMutex();
    }

    // Initialize shared instance if not already done
    if (!s_adc_initialized[instance_idx]) {
        // Manual I2C probe first — catches address conflicts and wiring
        // issues before the Adafruit library tries (and potentially
        // re-calls Wire.begin(), which on ESP32 Arduino 3.x can
        // tear down and reinitialize the I2C peripheral).
        _wire->beginTransmission(_address);
        uint8_t i2c_err = _wire->endTransmission();
        if (i2c_err != 0) {
            Serial.printf("[ADS1115] No device at 0x%02X (I2C err=%d). "
                          "Check wiring and ADDR pin.\n", _address, i2c_err);
            return;
        }
        Serial.printf("[ADS1115] Device detected at 0x%02X, initializing driver...\n", _address);

        // Pass the correct Wire instance for this address
        if (!s_adc_instances[instance_idx].begin(_address, _wire)) {
            Serial.printf("[ADS1115] Adafruit driver init failed at 0x%02X\n", _address);
            return;
        }
        s_adc_initialized[instance_idx] = true;
        Serial.printf("[ADS1115] Initialized ADC at 0x%02X on %s\n",
                      _address, (_wire == &Wire) ? "Wire" : "Wire1");
    }

    _adc = &s_adc_instances[instance_idx];

    // Apply settings
    applyGain();
    applyDataRate();

    _initialized = true;
    Serial.printf("[ADS1115] Channel %d ready on 0x%02X\n", _channel, _address);
}

uint16_t Ads1115Input::readRaw() {
    if (!_initialized || !_adc) {
        return 0;
    }

    // Skip reads if device has gone unresponsive (avoid I2C timeout spam)
    if (_consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
        // Periodic re-probe every ~50 calls (let other code keep running)
        static uint16_t skip_count = 0;
        if (++skip_count < 50) return 0;
        skip_count = 0;

        uint8_t idx = _address - 0x48;
        SemaphoreHandle_t mtx = (idx < 4) ? s_i2c_mutex[idx] : nullptr;
        if (mtx && xSemaphoreTake(mtx, pdMS_TO_TICKS(20)) == pdTRUE) {
            _wire->beginTransmission(_address);
            bool ok = (_wire->endTransmission() == 0);
            xSemaphoreGive(mtx);
            if (!ok) return 0;
        } else {
            return 0;
        }
        Serial.printf("[ADS1115] Device 0x%02X responding again, resuming reads\n", _address);
        _consecutive_errors = 0;
    }

    // Use our safe read (delay-based, not busy-wait)
    int16_t raw = readADCSafe(_channel);

    if (raw < 0) {
        raw = 0;
    }

    // Scale to 16-bit range (ADS1115 is 16-bit but signed, so max positive is 32767)
    // Multiply by 2 to use full 16-bit range for percentage calculation
    return static_cast<uint16_t>(raw) * 2;
}

int16_t Ads1115Input::readADCSafe(uint8_t channel) {
    if (channel > 3 || !_adc) return 0;

    uint8_t idx = _address - 0x48;
    SemaphoreHandle_t mtx = (idx < 4) ? s_i2c_mutex[idx] : nullptr;

    // Acquire mutex — if another task (e.g. AsyncWebServer on core 0)
    // is mid-read, wait up to 50ms then give up rather than corrupt the bus
    if (mtx && xSemaphoreTake(mtx, pdMS_TO_TICKS(50)) != pdTRUE) {
        return 0;  // Couldn't acquire lock, skip this read
    }

    // Start conversion (3 I2C writes)
    _adc->startADCReading(MUX_BY_CHANNEL[channel], /*continuous=*/false);

    // Release mutex during the conversion delay — no I2C traffic needed,
    // and this lets other tasks use different channels on this same ADC
    // (not applicable with one-ADS-per-bus, but good practice)
    if (mtx) xSemaphoreGive(mtx);

    // Wait for conversion (no I2C during this time)
    delay(getConversionDelayUs() / 1000 + 1);  // Use delay() not delayMicroseconds() to yield

    // Re-acquire mutex to read the result
    if (mtx && xSemaphoreTake(mtx, pdMS_TO_TICKS(50)) != pdTRUE) {
        return 0;
    }

    // Check conversion complete + read result (all under mutex)
    int16_t result = 0;
    uint8_t retries = 3;
    while (retries-- > 0 && !_adc->conversionComplete()) {
        delay(1);
    }

    if (_adc->conversionComplete()) {
        result = _adc->getLastConversionResults();
        _consecutive_errors = 0;
    } else {
        _consecutive_errors++;
        if (_consecutive_errors == MAX_CONSECUTIVE_ERRORS) {
            Serial.printf("[ADS1115] 0x%02X ch%d: %d consecutive errors, pausing reads\n",
                          _address, _channel, _consecutive_errors);
        }
    }

    if (mtx) xSemaphoreGive(mtx);
    return result;
}

uint32_t Ads1115Input::getConversionDelayUs() const {
    // Conversion time based on data rate, plus 10% margin
    switch (_sample_rate) {
        case Ads1115SampleRate::SPS_8:   return 137500;  // 125ms + margin
        case Ads1115SampleRate::SPS_16:  return 68750;
        case Ads1115SampleRate::SPS_32:  return 34375;
        case Ads1115SampleRate::SPS_64:  return 17188;
        case Ads1115SampleRate::SPS_128: return 8594;    // ~8.6ms
        case Ads1115SampleRate::SPS_250: return 4400;
        case Ads1115SampleRate::SPS_475: return 2316;
        case Ads1115SampleRate::SPS_860: return 1279;
        default:                         return 8594;
    }
}

void Ads1115Input::setChannel(uint8_t channel) {
    _channel = channel % ADS1115_CHANNELS;
}

void Ads1115Input::setGain(Ads1115Gain gain) {
    _gain = gain;
    if (_initialized) {
        applyGain();
    }
}

void Ads1115Input::setSampleRate(Ads1115SampleRate rate) {
    _sample_rate = rate;
    if (_initialized) {
        applyDataRate();
    }
}

float Ads1115Input::readVoltage() {
    if (!_initialized || !_adc) {
        return 0.0f;
    }

    int16_t raw = readADCSafe(_channel);
    return raw * getVoltageScale();
}

int16_t Ads1115Input::readDifferential(uint8_t pos_channel, uint8_t neg_channel) {
    if (!_initialized || !_adc) {
        return 0;
    }

    // ADS1115 supports differential pairs: 0-1 and 2-3
    if (pos_channel == 0 && neg_channel == 1) {
        return _adc->readADC_Differential_0_1();
    } else if (pos_channel == 2 && neg_channel == 3) {
        return _adc->readADC_Differential_2_3();
    }

    Serial.println("[ADS1115] Invalid differential channel pair");
    return 0;
}

bool Ads1115Input::isConnected() const {
    if (!_adc || !_wire) {
        return false;
    }

    uint8_t idx = _address - 0x48;
    SemaphoreHandle_t mtx = (idx < 4) ? s_i2c_mutex[idx] : nullptr;

    if (mtx && xSemaphoreTake(mtx, pdMS_TO_TICKS(20)) != pdTRUE) {
        return false;
    }

    _wire->beginTransmission(_address);
    bool connected = (_wire->endTransmission() == 0);

    if (mtx) xSemaphoreGive(mtx);
    return connected;
}

Adafruit_ADS1115* Ads1115Input::getSharedAdc(uint8_t address) {
    uint8_t instance_idx = address - 0x48;
    if (instance_idx >= 4) {
        return nullptr;
    }

    if (!s_adc_initialized[instance_idx]) {
        return nullptr;
    }

    return &s_adc_instances[instance_idx];
}

void Ads1115Input::applyGain() {
    if (!_adc) {
        return;
    }

    adsGain_t gain;
    switch (_gain) {
        case Ads1115Gain::GAIN_TWOTHIRDS:
            gain = GAIN_TWOTHIRDS;
            break;
        case Ads1115Gain::GAIN_ONE:
            gain = GAIN_ONE;
            break;
        case Ads1115Gain::GAIN_TWO:
            gain = GAIN_TWO;
            break;
        case Ads1115Gain::GAIN_FOUR:
            gain = GAIN_FOUR;
            break;
        case Ads1115Gain::GAIN_EIGHT:
            gain = GAIN_EIGHT;
            break;
        case Ads1115Gain::GAIN_SIXTEEN:
            gain = GAIN_SIXTEEN;
            break;
        default:
            gain = GAIN_ONE;
    }

    _adc->setGain(gain);
}

void Ads1115Input::applyDataRate() {
    if (!_adc) {
        return;
    }

    // The Adafruit library uses setDataRate() with specific rate values
    uint16_t rate;
    switch (_sample_rate) {
        case Ads1115SampleRate::SPS_8:   rate = RATE_ADS1115_8SPS; break;
        case Ads1115SampleRate::SPS_16:  rate = RATE_ADS1115_16SPS; break;
        case Ads1115SampleRate::SPS_32:  rate = RATE_ADS1115_32SPS; break;
        case Ads1115SampleRate::SPS_64:  rate = RATE_ADS1115_64SPS; break;
        case Ads1115SampleRate::SPS_128: rate = RATE_ADS1115_128SPS; break;
        case Ads1115SampleRate::SPS_250: rate = RATE_ADS1115_250SPS; break;
        case Ads1115SampleRate::SPS_475: rate = RATE_ADS1115_475SPS; break;
        case Ads1115SampleRate::SPS_860: rate = RATE_ADS1115_860SPS; break;
        default: rate = RATE_ADS1115_128SPS;
    }

    _adc->setDataRate(rate);
}

float Ads1115Input::getVoltageScale() const {
    // Returns mV per bit based on gain setting
    switch (_gain) {
        case Ads1115Gain::GAIN_TWOTHIRDS: return 0.1875f;  // +/- 6.144V
        case Ads1115Gain::GAIN_ONE:       return 0.125f;   // +/- 4.096V
        case Ads1115Gain::GAIN_TWO:       return 0.0625f;  // +/- 2.048V
        case Ads1115Gain::GAIN_FOUR:      return 0.03125f; // +/- 1.024V
        case Ads1115Gain::GAIN_EIGHT:     return 0.015625f; // +/- 0.512V
        case Ads1115Gain::GAIN_SIXTEEN:   return 0.0078125f; // +/- 0.256V
        default: return 0.125f;
    }
}

} // namespace iwmp
