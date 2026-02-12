/**
 * @file main_greenhouse.cpp
 * @brief Greenhouse Manager entry point
 *
 * Environmental control with relay automation, temp/humidity monitoring,
 * and sensor-to-relay bindings for automated watering.
 */

#include <Arduino.h>
#include "config_manager.h"
#include "greenhouse_controller.h"
#include "logger.h"
#include "watchdog.h"

using namespace iwmp;

void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println();
    Serial.println("========================================");
    Serial.println("  iWetMyPlants v2.0 - Greenhouse");
    Serial.println("========================================");

    // Initialize logging
    Log.setLevel(LogLevel::INFO);
    Log.setColors(true);

    LOG_I("Main", "Initializing...");

    // Initialize configuration
    Config.begin(DeviceType::GREENHOUSE);

    // Initialize watchdog
    Watchdog.begin(30000);  // 30 second timeout

    // Initialize greenhouse controller
    Greenhouse.begin();

    LOG_I("Main", "Initialization complete");
}

void loop() {
    // Feed watchdog
    Watchdog.feed();

    // Run greenhouse controller
    Greenhouse.loop();
}
