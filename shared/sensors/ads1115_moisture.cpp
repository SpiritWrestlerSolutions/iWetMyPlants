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

    // Initialize shared instance if not already done
    if (!s_adc_initialized[instance_idx]) {
        if (!s_adc_instances[instance_idx].begin(_address)) {
            Serial.printf("[ADS1115] Failed to initialize at 0x%02X\n", _address);
            return;
        }
        s_adc_initialized[instance_idx] = true;
        Serial.printf("[ADS1115] Initialized ADC at 0x%02X\n", _address);
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

    // Read single-ended from channel
    int16_t raw = _adc->readADC_SingleEnded(_channel);

    // ADS1115 returns signed value, convert to unsigned
    // For single-ended measurements, value should be positive
    if (raw < 0) {
        raw = 0;
    }

    // Scale to 16-bit range (ADS1115 is 16-bit but signed, so max positive is 32767)
    // Multiply by 2 to use full 16-bit range for percentage calculation
    return static_cast<uint16_t>(raw) * 2;
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

    int16_t raw = _adc->readADC_SingleEnded(_channel);
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

bool Ads1115Input::isConnected() {
    if (!_adc) {
        return false;
    }

    // Try to read from the ADC
    Wire.beginTransmission(_address);
    return Wire.endTransmission() == 0;
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
