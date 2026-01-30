/**
 * @file mux_moisture.h
 * @brief CD74HC4067 16-channel analog multiplexer input
 *
 * Uses the CD74HC4067 to expand a single ADC channel to 16 inputs.
 * The multiplexer routes one of 16 analog inputs to a single output
 * based on 4 digital select lines (S0-S3).
 *
 * Wiring:
 * - SIG: Connect to ESP32 ADC pin (signal output)
 * - S0-S3: Connect to ESP32 GPIO pins (channel select)
 * - EN: Connect to GND (always enabled) or GPIO for power saving
 * - C0-C15: Connect to moisture sensors
 * - VCC: 3.3V
 * - GND: GND
 */

#pragma once

#include "sensor_interface.h"

namespace iwmp {

// CD74HC4067 constants
static constexpr uint8_t MUX_CHANNELS = 16;
static constexpr uint16_t MUX_SETTLE_TIME_US = 10;  // Settling time after channel switch

/**
 * @brief CD74HC4067 16-channel multiplexer input
 *
 * Reads from one of 16 analog inputs through a single ADC pin.
 * Channel selection is done via 4 digital GPIO pins.
 */
class MuxInput : public ISensorInput {
public:
    /**
     * @brief Construct with pin assignments
     * @param sig_pin ADC pin connected to SIG output
     * @param s0 GPIO pin for S0 (LSB)
     * @param s1 GPIO pin for S1
     * @param s2 GPIO pin for S2
     * @param s3 GPIO pin for S3 (MSB)
     * @param channel Initial channel (0-15)
     * @param en_pin Enable pin (-1 for always enabled)
     */
    MuxInput(uint8_t sig_pin, uint8_t s0, uint8_t s1, uint8_t s2, uint8_t s3,
             uint8_t channel = 0, int8_t en_pin = -1);

    /**
     * @brief Initialize multiplexer
     */
    void begin() override;

    /**
     * @brief Read raw ADC value from current channel
     * @return 12-bit value (0-4095)
     */
    uint16_t readRaw() override;

    /**
     * @brief Get maximum ADC value
     * @return 4095 (uses ESP32 ADC)
     */
    uint16_t getMaxValue() override { return 4095; }

    /**
     * @brief Check if ready
     */
    bool isReady() const override { return _initialized; }

    /**
     * @brief Get type name
     */
    const char* getTypeName() const override { return "MUX_CD74HC4067"; }

    // ============ Multiplexer Specific ============

    /**
     * @brief Set active channel
     * @param channel Channel number (0-15)
     */
    void setChannel(uint8_t channel);

    /**
     * @brief Get current channel
     */
    uint8_t getChannel() const { return _channel; }

    /**
     * @brief Get signal pin
     */
    uint8_t getSignalPin() const { return _sig_pin; }

    /**
     * @brief Enable multiplexer
     */
    void enable();

    /**
     * @brief Disable multiplexer (high impedance output)
     */
    void disable();

    /**
     * @brief Check if enabled
     */
    bool isEnabled() const { return _enabled; }

    /**
     * @brief Read from specific channel
     * @param channel Channel to read (0-15)
     * @return ADC value
     */
    uint16_t readChannel(uint8_t channel);

    /**
     * @brief Scan all channels
     * @param values Array to store values (must be at least 16 elements)
     * @param delay_us Delay between readings in microseconds
     */
    void scanAllChannels(uint16_t* values, uint16_t delay_us = MUX_SETTLE_TIME_US);

    /**
     * @brief Set settling time after channel switch
     * @param time_us Time in microseconds
     */
    void setSettleTime(uint16_t time_us);

private:
    uint8_t _sig_pin;
    uint8_t _select_pins[4];
    int8_t _en_pin;
    uint8_t _channel;
    uint16_t _settle_time_us = MUX_SETTLE_TIME_US;
    bool _enabled = true;
    bool _initialized = false;

    /**
     * @brief Apply channel selection to GPIO pins
     */
    void applyChannelSelection();
};

/**
 * @brief Shared multiplexer manager for multiple sensors on same mux
 *
 * When multiple MoistureSensors use the same physical multiplexer,
 * this class ensures proper channel switching and prevents conflicts.
 */
class MuxManager {
public:
    /**
     * @brief Register a multiplexer
     * @param mux Pointer to MuxInput
     * @param id Unique ID for this mux
     */
    static void registerMux(MuxInput* mux, uint8_t id);

    /**
     * @brief Get registered multiplexer by ID
     */
    static MuxInput* getMux(uint8_t id);

    /**
     * @brief Read from specific mux and channel
     * @param mux_id Multiplexer ID
     * @param channel Channel number
     * @return ADC value
     */
    static uint16_t read(uint8_t mux_id, uint8_t channel);

private:
    static MuxInput* s_mux_instances[4];
};

} // namespace iwmp
