/**
 * @file main.cpp
 * @brief Hub device entry point
 *
 * iWetMyPlants v2.0 - Hub Firmware
 * Central coordinator, ESP-NOW gateway, MQTT bridge
 */

#include <Arduino.h>
#include "hub_controller.h"
#include "logger.h"
#include "watchdog.h"

using namespace iwmp;

static const char* TAG = "main";

void setup() {
    Serial.begin(115200);
    delay(100);

    LOG_I(TAG, "========================================");
    LOG_I(TAG, "iWetMyPlants v%s - Hub", IWMP_VERSION);
    LOG_I(TAG, "========================================");

    // Initialize watchdog
    Watchdog.begin(30);

    // Initialize hub controller
    Hub.begin();

    LOG_I(TAG, "Setup complete");
}

void loop() {
    // Feed watchdog
    Watchdog.feed();

    // Process hub logic
    Hub.loop();

    // Small delay to prevent tight loop
    delay(1);
}
