/**
 * @file main_hub.cpp
 * @brief Hub firmware entry point
 *
 * iWetMyPlants v2.0 - Hub Firmware
 * Central coordinator, ESP-NOW gateway, MQTT bridge
 */

#include <Arduino.h>
#include <Wire.h>
#include "hub_controller.h"
#include "config_manager.h"
#include "logger.h"
#include "watchdog.h"
#include "defaults.h"

using namespace iwmp;

static const char* TAG = "main";

void setup() {
    Serial.begin(115200);
    delay(100);

    LOG_I(TAG, "========================================");
    LOG_I(TAG, "iWetMyPlants v%s - Hub", IWMP_VERSION);
    LOG_I(TAG, "========================================");

    // Initialize logger
    Log.setLevel(LogLevel::DEBUG);
    Log.setColors(true);
    Log.setTimestamps(true);

    // Initialize watchdog (30 second timeout)
    if (Watchdog.begin(30)) {
        LOG_I(TAG, "Watchdog initialized");
    } else {
        LOG_W(TAG, "Watchdog initialization failed");
    }

    // Initialize config manager
    if (Config.begin(DeviceType::HUB)) {
        LOG_I(TAG, "Config manager initialized");
        LOG_I(TAG, "Device ID: %s", Config.getDeviceId());
        LOG_I(TAG, "Device Name: %s", Config.getDeviceName());
    } else {
        LOG_E(TAG, "Config manager initialization failed!");
    }

    // Initialize I2C for ADS1115 and other I2C devices
    Wire.begin(hub_pins::I2C_SDA, hub_pins::I2C_SCL);
    LOG_I(TAG, "I2C initialized on SDA=%d, SCL=%d", hub_pins::I2C_SDA, hub_pins::I2C_SCL);

    // Scan I2C bus — helps diagnose ADS1115 address conflicts
    {
        uint8_t found = 0;
        for (uint8_t addr = 0x08; addr < 0x78; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                LOG_I(TAG, "I2C device at 0x%02X", addr);
                found++;
            }
        }
        LOG_I(TAG, "I2C scan: %d device(s) found", found);
    }

    // Initialize Hub controller (handles WiFi, ESP-NOW, MQTT, Web)
    Hub.begin();

    LOG_I(TAG, "========================================");
    LOG_I(TAG, "Setup complete");
    LOG_I(TAG, "========================================");
}

void loop() {
    // Feed watchdog
    Watchdog.feed();

    // Process hub controller state machine
    Hub.loop();

    // Small delay to prevent tight loop
    delay(1);
}
