/**
 * @file main_greenhouse.cpp
 * @brief Greenhouse Manager entry point
 *
 * Environmental sensor reporter (DHT/SHT) + relay executor. Receives
 * relay commands from Hub via ESP-NOW or from Home Assistant via MQTT;
 * publishes temperature/humidity. No local moisture sensing or
 * autonomous automation -- the greenhouse is a "pro add-on", not a
 * standalone controller.
 */

#include <Arduino.h>
#include <Wire.h>
#include "config_manager.h"
#include "defaults.h"
#include "greenhouse_controller.h"
#include "logger.h"
#include "watchdog.h"
#include "version.h"

using namespace iwmp;

static const char* TAG = "main";

void setup() {
    Serial.begin(115200);
    delay(100);

    Log.setLevel(LogLevel::INFO);
    Log.setColors(false);
    Log.setTimestamps(true);

    LOG_I(TAG, "========================================");
    LOG_I(TAG, "iWetMyPlants " IWMP_BUILD_LINE " - Greenhouse (t=%lums)", millis());
    LOG_I(TAG, "========================================");

    // Initialize I2C for ADS1115 and other peripherals
    Wire.begin(greenhouse_pins::I2C_SDA, greenhouse_pins::I2C_SCL);
    Wire.setTimeOut(50);
    LOG_I(TAG, "I2C SDA=%d SCL=%d", greenhouse_pins::I2C_SDA, greenhouse_pins::I2C_SCL);

    // Initialize configuration (loads NVS)
    if (Config.begin(DeviceType::GREENHOUSE)) {
        LOG_I(TAG, "Config loaded. Device ID: %s", Config.getDeviceId());
    } else {
        LOG_E(TAG, "Config initialization failed!");
    }

    // Watchdog: 60-second timeout (parameter is SECONDS)
    Watchdog.begin(60);

    // Initialize greenhouse controller
    Greenhouse.begin();

    LOG_I(TAG, "Setup complete (t=%lums)", millis());
}

void loop() {
    Watchdog.feed();
    Greenhouse.loop();
    delay(1);

    // Periodic heartbeat
    static uint32_t s_hb = 0;
    if (millis() - s_hb > 10000) {
        s_hb = millis();
        LOG_I(TAG, "ALIVE t=%lus heap=%u", millis() / 1000, ESP.getFreeHeap());
    }
}
