/**
 * @file main.cpp
 * @brief Remote device entry point
 *
 * iWetMyPlants v2.0 - Remote Firmware
 * Single sensor node with deep sleep support
 */

#include <Arduino.h>
#include "remote_controller.h"
#include "logger.h"

using namespace iwmp;

static const char* TAG = "main";

void setup() {
    Serial.begin(115200);

    // Minimal delay for serial - we want fast wake
    delay(10);

    LOG_I(TAG, "========================================");
    LOG_I(TAG, "iWetMyPlants v%s - Remote", IWMP_VERSION);
    LOG_I(TAG, "Boot #%lu", Remote.getBootCount());
    LOG_I(TAG, "========================================");

    // Initialize remote controller
    Remote.begin();
}

void loop() {
    // Process remote logic
    Remote.loop();

    // Note: In battery mode, we never reach here
    // as quickReadCycle() enters deep sleep

    // Small delay for powered mode
    delay(1);
}
