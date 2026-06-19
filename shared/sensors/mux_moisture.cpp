/**
 * @file mux_moisture.cpp
 * @brief CD74HC4067 16-channel multiplexer implementation
 */

#include "mux_moisture.h"
#include "logger.h"

namespace iwmp {

static constexpr const char* TAG = "MUX";

MuxInput::MuxInput(uint8_t sig_pin, uint8_t s0, uint8_t s1, uint8_t s2, uint8_t s3,
                   uint8_t channel, int8_t en_pin)
    : _sig_pin(sig_pin)
    , _en_pin(en_pin)
    , _channel(channel % MUX_CHANNELS) {
    _select_pins[0] = s0;
    _select_pins[1] = s1;
    _select_pins[2] = s2;
    _select_pins[3] = s3;
}

void MuxInput::begin() {
    if (_initialized) {
        return;
    }

    // Configure select pins as outputs
    for (uint8_t i = 0; i < 4; i++) {
        pinMode(_select_pins[i], OUTPUT);
        digitalWrite(_select_pins[i], LOW);
    }

    // Configure enable pin if used
    if (_en_pin >= 0) {
        pinMode(_en_pin, OUTPUT);
        digitalWrite(_en_pin, LOW);  // Active low enable
        _enabled = true;
    }

    // Configure signal pin for analog input
    pinMode(_sig_pin, INPUT);

    // Set ADC resolution
    analogReadResolution(12);
    analogSetPinAttenuation(_sig_pin, ADC_11db);

    // Set initial channel
    applyChannelSelection();

    _initialized = true;

    LOG_I(TAG, "Initialized on SIG=%d, S0-S3=%d,%d,%d,%d",
                  _sig_pin, _select_pins[0], _select_pins[1],
                  _select_pins[2], _select_pins[3]);
}

uint16_t MuxInput::readRaw() {
    if (!_initialized || !_enabled) {
        return 0;
    }

    // Re-apply channel selection each read — multiple MuxInput instances
    // can share the same GPIO pins (one per MUX channel), so we must
    // set the select lines to our channel before sampling.
    applyChannelSelection();
    return analogRead(_sig_pin);
}

void MuxInput::applyChannelSelection() {
    // Set S0-S3 based on channel number (binary encoding)
    for (uint8_t i = 0; i < 4; i++) {
        digitalWrite(_select_pins[i], (_channel >> i) & 0x01);
    }

    // Small delay for signal to settle
    delayMicroseconds(_settle_time_us);
}

} // namespace iwmp
