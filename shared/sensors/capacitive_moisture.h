/**
 * @file capacitive_moisture.h
 * @brief Direct ESP32 ADC input for capacitive moisture sensors
 *
 * Uses the ESP32's built-in 12-bit ADC for reading capacitive
 * moisture sensors. Simple but has limitations:
 * - ADC2 cannot be used when WiFi is active
 * - Non-linear response, especially at extremes
 * - Noise-sensitive
 *
 * Recommended pins (ADC1, safe with WiFi):
 * - ESP32-WROOM: GPIO 32-39
 * - ESP32-C3: GPIO 0-4
 */

#pragma once

#include "sensor_interface.h"

namespace iwmp {

// ESP32 ADC constants
static constexpr uint16_t ESP32_ADC_MAX = 4095;  // 12-bit ADC
static constexpr uint8_t ESP32_ADC_WIDTH = 12;
static constexpr uint8_t ESP32_ADC_ATTEN = 3;    // ADC_ATTEN_DB_11 (0-3.3V range)

/**
 * @brief Direct ESP32 ADC input
 *
 * Reads capacitive moisture sensor directly connected to an ESP32 ADC pin.
 * Uses ADC1 channels which are safe to use with WiFi enabled.
 */
class DirectAdcInput : public ISensorInput {
public:
    /**
     * @brief Construct with GPIO pin
     * @param pin GPIO pin number (must be ADC-capable)
     */
    explicit DirectAdcInput(uint8_t pin);

    /**
     * @brief Initialize ADC
     */
    void begin() override;

    /**
     * @brief Read raw ADC value
     * @return 12-bit value (0-4095)
     */
    uint16_t readRaw() override;

    /**
     * @brief Get maximum ADC value
     * @return 4095
     */
    uint16_t getMaxValue() override { return ESP32_ADC_MAX; }

    /**
     * @brief Check if ready
     */
    bool isReady() const override { return _initialized; }

    /**
     * @brief Get type name
     */
    const char* getTypeName() const override { return "DirectADC"; }

    // ============ DirectADC Specific ============

    /**
     * @brief Get GPIO pin number
     */
    uint8_t getPin() const { return _pin; }

    /**
     * @brief Set ADC attenuation
     * @param atten Attenuation (0=0dB, 1=2.5dB, 2=6dB, 3=11dB)
     */
    void setAttenuation(uint8_t atten);

    /**
     * @brief Check if pin is on ADC1 (WiFi-safe)
     */
    bool isAdc1Pin() const;

    /**
     * @brief Read with multisampling for noise reduction
     * @param samples Number of samples to average
     * @return Averaged ADC value
     */
    uint16_t readMultisampled(uint8_t samples);

private:
    uint8_t _pin;
    uint8_t _attenuation = ESP32_ADC_ATTEN;
    bool _initialized = false;

    /**
     * @brief Configure ADC channel
     */
    void configureAdc();
};

/**
 * @brief Check if GPIO pin is ADC1 (WiFi-safe)
 * @param pin GPIO pin number
 * @return true if pin is on ADC1
 */
bool isAdc1GpioPin(uint8_t pin);

/**
 * @brief Check if GPIO pin is ADC2 (not WiFi-safe)
 * @param pin GPIO pin number
 * @return true if pin is on ADC2
 */
bool isAdc2GpioPin(uint8_t pin);

} // namespace iwmp
