/**
 * @file main.cpp
 * @brief Greenhouse Manager device entry point
 *
 * iWetMyPlants v2.0 - Greenhouse Firmware
 * Environmental control with relay automation
 */

#include <Arduino.h>
#include "greenhouse_controller.h"
#include "logger.h"
#include "watchdog.h"

using namespace iwmp;

static const char* TAG = "main";

void setup() {
    Serial.begin(115200);
    delay(100);

    LOG_I(TAG, "========================================");
    LOG_I(TAG, "iWetMyPlants v%s - Greenhouse", IWMP_VERSION);
    LOG_I(TAG, "========================================");

    // Initialize watchdog
    Watchdog.begin(30);

    // Initialize greenhouse controller
    Greenhouse.begin();

    LOG_I(TAG, "Setup complete");
}

void loop() {
    // Feed watchdog
    Watchdog.feed();

    // Process greenhouse logic
    Greenhouse.loop();

    // Small delay to prevent tight loop
    delay(1);
}
