/**
 * @file capacitive_moisture.cpp
 * @brief Direct ESP32 ADC input implementation
 */

#include "capacitive_moisture.h"
#include <driver/adc.h>
#include <esp_adc_cal.h>

namespace iwmp {

// ADC1 GPIO pins for different ESP32 variants
#if CONFIG_IDF_TARGET_ESP32
// ESP32-WROOM: ADC1 = GPIO 32-39
static const uint8_t ADC1_PINS[] = {32, 33, 34, 35, 36, 37, 38, 39};
static const uint8_t ADC2_PINS[] = {0, 2, 4, 12, 13, 14, 15, 25, 26, 27};
#elif CONFIG_IDF_TARGET_ESP32C3
// ESP32-C3: ADC1 = GPIO 0-4, ADC2 = GPIO 5
static const uint8_t ADC1_PINS[] = {0, 1, 2, 3, 4};
static const uint8_t ADC2_PINS[] = {5};
#elif CONFIG_IDF_TARGET_ESP32S3
// ESP32-S3: ADC1 = GPIO 1-10, ADC2 = GPIO 11-20
static const uint8_t ADC1_PINS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
static const uint8_t ADC2_PINS[] = {11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
#else
// Default to ESP32-WROOM
static const uint8_t ADC1_PINS[] = {32, 33, 34, 35, 36, 37, 38, 39};
static const uint8_t ADC2_PINS[] = {0, 2, 4, 12, 13, 14, 15, 25, 26, 27};
#endif

static const size_t ADC1_PIN_COUNT = sizeof(ADC1_PINS) / sizeof(ADC1_PINS[0]);
static const size_t ADC2_PIN_COUNT = sizeof(ADC2_PINS) / sizeof(ADC2_PINS[0]);

bool isAdc1GpioPin(uint8_t pin) {
    for (size_t i = 0; i < ADC1_PIN_COUNT; i++) {
        if (ADC1_PINS[i] == pin) {
            return true;
        }
    }
    return false;
}

bool isAdc2GpioPin(uint8_t pin) {
    for (size_t i = 0; i < ADC2_PIN_COUNT; i++) {
        if (ADC2_PINS[i] == pin) {
            return true;
        }
    }
    return false;
}

DirectAdcInput::DirectAdcInput(uint8_t pin)
    : _pin(pin) {
}

void DirectAdcInput::begin() {
    if (_initialized) {
        return;
    }

    // Warn if using ADC2 pin
    if (isAdc2GpioPin(_pin)) {
        Serial.printf("[ADC] WARNING: GPIO %d is on ADC2, may conflict with WiFi!\n", _pin);
    }

    configureAdc();
    _initialized = true;

    Serial.printf("[ADC] Initialized GPIO %d (ADC%d)\n", _pin, isAdc1GpioPin(_pin) ? 1 : 2);
}

uint16_t DirectAdcInput::readRaw() {
    if (!_initialized) {
        return 0;
    }

    return analogRead(_pin);
}

void DirectAdcInput::setAttenuation(uint8_t atten) {
    _attenuation = atten;
    if (_initialized) {
        configureAdc();
    }
}

bool DirectAdcInput::isAdc1Pin() const {
    return isAdc1GpioPin(_pin);
}

uint16_t DirectAdcInput::readMultisampled(uint8_t samples) {
    if (!_initialized || samples == 0) {
        return 0;
    }

    uint32_t sum = 0;
    for (uint8_t i = 0; i < samples; i++) {
        sum += analogRead(_pin);
        if (i < samples - 1) {
            delayMicroseconds(100);  // Small delay between samples
        }
    }

    return sum / samples;
}

void DirectAdcInput::configureAdc() {
    // Set ADC resolution to 12 bits
    analogReadResolution(ESP32_ADC_WIDTH);

    // Set attenuation for full 0-3.3V range
    // ADC_ATTEN_DB_11 gives approximately 0-3.3V input range
    adc_attenuation_t atten;
    switch (_attenuation) {
        case 0: atten = ADC_ATTEN_DB_0; break;    // 0-1.1V
        case 1: atten = ADC_ATTEN_DB_2_5; break;  // 0-1.5V
        case 2: atten = ADC_ATTEN_DB_6; break;    // 0-2.2V
        case 3:
        default: atten = ADC_ATTEN_DB_11; break;  // 0-3.3V
    }

    analogSetPinAttenuation(_pin, atten);
}

} // namespace iwmp
