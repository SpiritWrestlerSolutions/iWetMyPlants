/**
 * @file mux_moisture.cpp
 * @brief CD74HC4067 16-channel multiplexer implementation
 */

#include "mux_moisture.h"

namespace iwmp {

// Static MuxManager instances
MuxInput* MuxManager::s_mux_instances[4] = {nullptr, nullptr, nullptr, nullptr};

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

    Serial.printf("[MUX] Initialized on SIG=%d, S0-S3=%d,%d,%d,%d\n",
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

void MuxInput::setChannel(uint8_t channel) {
    _channel = channel % MUX_CHANNELS;
    if (_initialized) {
        applyChannelSelection();
    }
}

void MuxInput::enable() {
    if (_en_pin >= 0) {
        digitalWrite(_en_pin, LOW);  // Active low
    }
    _enabled = true;
}

void MuxInput::disable() {
    if (_en_pin >= 0) {
        digitalWrite(_en_pin, HIGH);  // Active low
    }
    _enabled = false;
}

uint16_t MuxInput::readChannel(uint8_t channel) {
    if (!_initialized) {
        return 0;
    }

    // Switch to channel
    uint8_t prev_channel = _channel;
    setChannel(channel);

    // Wait for settling
    delayMicroseconds(_settle_time_us);

    // Read value
    uint16_t value = readRaw();

    // Restore previous channel if different
    if (channel != prev_channel) {
        setChannel(prev_channel);
    }

    return value;
}

void MuxInput::scanAllChannels(uint16_t* values, uint16_t delay_us) {
    if (!_initialized || !values) {
        return;
    }

    for (uint8_t ch = 0; ch < MUX_CHANNELS; ch++) {
        setChannel(ch);
        delayMicroseconds(delay_us);
        values[ch] = readRaw();
    }
}

void MuxInput::setSettleTime(uint16_t time_us) {
    _settle_time_us = time_us;
}

void MuxInput::applyChannelSelection() {
    // Set S0-S3 based on channel number (binary encoding)
    for (uint8_t i = 0; i < 4; i++) {
        digitalWrite(_select_pins[i], (_channel >> i) & 0x01);
    }

    // Small delay for signal to settle
    delayMicroseconds(_settle_time_us);
}

// ============ MuxManager ============

void MuxManager::registerMux(MuxInput* mux, uint8_t id) {
    if (id < 4) {
        s_mux_instances[id] = mux;
    }
}

MuxInput* MuxManager::getMux(uint8_t id) {
    if (id < 4) {
        return s_mux_instances[id];
    }
    return nullptr;
}

uint16_t MuxManager::read(uint8_t mux_id, uint8_t channel) {
    MuxInput* mux = getMux(mux_id);
    if (mux) {
        return mux->readChannel(channel);
    }
    return 0;
}

} // namespace iwmp
