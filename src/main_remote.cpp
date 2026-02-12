/**
 * @file main_remote.cpp
 * @brief Remote firmware entry point
 *
 * iWetMyPlants v2.0 - Remote Firmware
 * WiFi sensor node with HTTP reporting to Hub
 */

#include <Arduino.h>
#include <Wire.h>
#include <nvs_flash.h>
#include <esp_mac.h>
#include "remote_controller.h"
#include "config_manager.h"
#include "defaults.h"
#include "logger.h"

using namespace iwmp;

static const char* TAG = "main";

void setup() {
    Serial.begin(115200);

    // Wait for USB CDC to enumerate (SuperMini has native USB, no UART bridge).
    // The host needs time to detect the CDC device and open the port.
    // Without this, all early boot messages are lost.
    uint32_t cdc_start = millis();
    while (!Serial && (millis() - cdc_start) < 3000) {
        delay(10);
    }

    // Initialize logger
    Log.setLevel(LogLevel::INFO);
    Log.setColors(false);
    Log.setTimestamps(true);

    LOG_I(TAG, "========================================");
    LOG_I(TAG, "iWetMyPlants v%s - Remote", IWMP_VERSION);
    LOG_I(TAG, "========================================");

    // Initialize NVS (required for WiFi and Preferences)
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOG_W(TAG, "NVS needs erase (err=0x%x), reformatting...", nvs_err);
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) {
        LOG_E(TAG, "NVS init failed: 0x%x", nvs_err);
    } else {
        LOG_I(TAG, "NVS initialized");
    }

    // Initialize I2C for ADS1115 and other I2C peripherals
    Wire.begin(remote_pins::I2C_SDA, remote_pins::I2C_SCL);
    LOG_I(TAG, "I2C initialized on SDA=%d, SCL=%d", remote_pins::I2C_SDA, remote_pins::I2C_SCL);

    // Log MAC address for board identification (read from efuse, no WiFi init needed)
    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        LOG_I(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    // Initialize config manager
    if (Config.begin(DeviceType::REMOTE)) {
        LOG_I(TAG, "Config loaded");
        LOG_I(TAG, "Device ID: %s", Config.getDeviceId());
    } else {
        LOG_E(TAG, "Config initialization failed!");
    }

    // Initialize remote controller
    // This handles wake reason detection and decides on operating mode
    Remote.begin();

    LOG_I(TAG, "Setup complete");
}

void loop() {
    // Process remote logic
    Remote.loop();

    // Small yield for WiFi/system tasks
    delay(1);
}
