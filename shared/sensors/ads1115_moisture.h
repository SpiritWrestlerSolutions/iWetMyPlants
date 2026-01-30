/**
 * @file ads1115_moisture.h
 * @brief ADS1115 16-bit ADC input for capacitive moisture sensors
 *
 * Uses the Texas Instruments ADS1115 external ADC via I2C.
 * Advantages over ESP32 internal ADC:
 * - 16-bit resolution (65535 vs 4095)
 * - Better linearity
 * - Programmable gain amplifier (PGA)
 * - 4 channels per chip
 * - Not affected by WiFi
 *
 * I2C Addresses (set via ADDR pin):
 * - 0x48: ADDR -> GND
 * - 0x49: ADDR -> VDD
 * - 0x4A: ADDR -> SDA
 * - 0x4B: ADDR -> SCL
 */

#pragma once

#include "sensor_interface.h"
#include <Adafruit_ADS1X15.h>

namespace iwmp {

// ADS1115 constants
static constexpr uint16_t ADS1115_MAX = 65535;
static constexpr uint8_t ADS1115_DEFAULT_ADDRESS = 0x48;
static constexpr uint8_t ADS1115_CHANNELS = 4;

/**
 * @brief ADS1115 gain settings
 */
enum class Ads1115Gain : uint8_t {
    GAIN_TWOTHIRDS = 0,  // +/- 6.144V, 0.1875mV/bit (default)
    GAIN_ONE = 1,        // +/- 4.096V, 0.125mV/bit
    GAIN_TWO = 2,        // +/- 2.048V, 0.0625mV/bit
    GAIN_FOUR = 3,       // +/- 1.024V, 0.03125mV/bit
    GAIN_EIGHT = 4,      // +/- 0.512V, 0.015625mV/bit
    GAIN_SIXTEEN = 5     // +/- 0.256V, 0.0078125mV/bit
};

/**
 * @brief ADS1115 sample rate settings
 */
enum class Ads1115SampleRate : uint8_t {
    SPS_8 = 0,
    SPS_16 = 1,
    SPS_32 = 2,
    SPS_64 = 3,
    SPS_128 = 4,   // Default
    SPS_250 = 5,
    SPS_475 = 6,
    SPS_860 = 7
};

/**
 * @brief ADS1115 16-bit external ADC input
 *
 * Provides high-resolution ADC readings via I2C.
 * Supports 4 single-ended channels per chip.
 */
class Ads1115Input : public ISensorInput {
public:
    /**
     * @brief Construct with I2C address and channel
     * @param i2c_address I2C address (0x48-0x4B)
     * @param channel ADC channel (0-3)
     */
    Ads1115Input(uint8_t i2c_address = ADS1115_DEFAULT_ADDRESS, uint8_t channel = 0);

    /**
     * @brief Initialize ADC
     */
    void begin() override;

    /**
     * @brief Read raw ADC value
     * @return 16-bit value (0-65535)
     */
    uint16_t readRaw() override;

    /**
     * @brief Get maximum ADC value
     * @return 65535
     */
    uint16_t getMaxValue() override { return ADS1115_MAX; }

    /**
     * @brief Check if ready
     */
    bool isReady() const override { return _initialized; }

    /**
     * @brief Get type name
     */
    const char* getTypeName() const override { return "ADS1115"; }

    // ============ ADS1115 Specific ============

    /**
     * @brief Get I2C address
     */
    uint8_t getAddress() const { return _address; }

    /**
     * @brief Get channel
     */
    uint8_t getChannel() const { return _channel; }

    /**
     * @brief Set channel
     */
    void setChannel(uint8_t channel);

    /**
     * @brief Set gain
     * @param gain Gain setting
     */
    void setGain(Ads1115Gain gain);

    /**
     * @brief Get current gain
     */
    Ads1115Gain getGain() const { return _gain; }

    /**
     * @brief Set sample rate
     * @param rate Sample rate setting
     */
    void setSampleRate(Ads1115SampleRate rate);

    /**
     * @brief Get current sample rate
     */
    Ads1115SampleRate getSampleRate() const { return _sample_rate; }

    /**
     * @brief Read voltage in millivolts
     * @return Voltage in mV
     */
    float readVoltage();

    /**
     * @brief Read differential between two channels
     * @param pos_channel Positive channel (0 or 2)
     * @param neg_channel Negative channel (1 or 3)
     * @return Differential ADC value
     */
    int16_t readDifferential(uint8_t pos_channel, uint8_t neg_channel);

    /**
     * @brief Check if ADC chip is responding
     */
    bool isConnected();

    /**
     * @brief Get shared ADS1115 instance for address
     * @param address I2C address
     * @return Pointer to shared Adafruit_ADS1115 instance
     *
     * Multiple Ads1115Input instances can share the same
     * physical ADC chip if they use the same address.
     */
    static Adafruit_ADS1115* getSharedAdc(uint8_t address);

private:
    uint8_t _address;
    uint8_t _channel;
    Ads1115Gain _gain = Ads1115Gain::GAIN_ONE;
    Ads1115SampleRate _sample_rate = Ads1115SampleRate::SPS_128;
    bool _initialized = false;

    Adafruit_ADS1115* _adc = nullptr;

    /**
     * @brief Apply gain setting to ADC
     */
    void applyGain();

    /**
     * @brief Apply sample rate setting to ADC
     */
    void applyDataRate();

    /**
     * @brief Get voltage scale factor for current gain
     */
    float getVoltageScale() const;

    // Shared ADC instances (one per I2C address)
    static Adafruit_ADS1115 s_adc_instances[4];
    static bool s_adc_initialized[4];
};

} // namespace iwmp
