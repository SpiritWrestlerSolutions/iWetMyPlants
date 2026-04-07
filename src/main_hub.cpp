/**
 * @file main_hub.cpp
 * @brief Hub firmware entry point
 *
 * iWetMyPlants v1.0 - Hub Firmware
 * Central coordinator, ESP-NOW gateway, MQTT bridge
 */

#include <Arduino.h>
#include <Wire.h>
#include "hub_controller.h"
#include "config_manager.h"
#include "logger.h"
#include "watchdog.h"
#include "defaults.h"
#include "ads1115_moisture.h"
#include "version.h"

using namespace iwmp;

static const char* TAG = "main";

void setup() {
    Serial.begin(115200);
    delay(100);

    LOG_I(TAG, "========================================");
    LOG_I(TAG, "iWetMyPlants " IWMP_BUILD_LINE " - Hub");
    LOG_I(TAG, "========================================");

    // Initialize logger — DEBUG in dev builds, INFO in production
#ifdef IWMP_DEBUG
    Log.setLevel(LogLevel::DEBUG);
#else
    Log.setLevel(LogLevel::INFO);
#endif
    Log.setColors(true);
    Log.setTimestamps(true);

    // Initialize watchdog (60 second timeout — sensor reads + WiFi + MQTT can take ~500ms
    // per iteration; 30 s was too tight and caused spurious resets under load)
    if (Watchdog.begin(60)) {
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

    // Initialize primary I2C bus (ADS1115 @ 0x48, SHT sensors, etc.)
    Wire.begin(hub_pins::I2C_SDA, hub_pins::I2C_SCL);
    Wire.setTimeOut(50);
    LOG_I(TAG, "I2C bus 0: SDA=%d, SCL=%d", hub_pins::I2C_SDA, hub_pins::I2C_SCL);

    // Initialize secondary I2C bus (ADS1115 @ 0x49)
    Wire1.begin(hub_pins::I2C1_SDA, hub_pins::I2C1_SCL);
    Wire1.setTimeOut(50);
    LOG_I(TAG, "I2C bus 1: SDA=%d, SCL=%d", hub_pins::I2C1_SDA, hub_pins::I2C1_SCL);

    // Map ADS1115 addresses to their I2C buses (one ADS per bus = no shared wiring)
    Ads1115Input::setWireForAddress(0x48, &Wire);
    Ads1115Input::setWireForAddress(0x49, &Wire1);

    // Scan both I2C buses
    for (uint8_t bus = 0; bus < 2; bus++) {
        TwoWire& w = (bus == 0) ? Wire : Wire1;
        uint8_t found = 0;
        for (uint8_t addr = 0x08; addr < 0x78; addr++) {
            w.beginTransmission(addr);
            if (w.endTransmission() == 0) {
                LOG_I(TAG, "I2C%d: device at 0x%02X", bus, addr);
                found++;
            }
        }
        LOG_I(TAG, "I2C%d scan: %d device(s) found", bus, found);
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
