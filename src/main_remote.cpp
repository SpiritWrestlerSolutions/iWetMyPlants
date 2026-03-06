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
#include "wifi_manager.h"

using namespace iwmp;

static const char* TAG = "main";

void setup() {
    Serial.begin(115200);

    // Non-blocking CDC writes: each write returns immediately if the host port
    // is not open, instead of stalling up to 100 ms per call.  Without this,
    // WiFi event callbacks (LOG_I "Client connected") block the Arduino loop
    // for ~700 ms, starving the DNS server and breaking captive-portal detection.
    Serial.setTxTimeoutMs(0);

    // USB CDC on ESP32-C3 generates interrupt/DMA activity while enumerating
    // (no-host case).  softAP() called too soon after Serial.begin() silently
    // prevents AP beacons from transmitting.  1500 ms is sufficient for the
    // USB stack to settle; setTxTimeoutMs(0) keeps writes non-blocking after.
    delay(1500);

    // Initialize logger
    Log.setLevel(LogLevel::INFO);
    Log.setColors(false);
    Log.setTimestamps(true);

    LOG_I(TAG, "========================================");
    LOG_I(TAG, "iWetMyPlants v%s - Remote (t=%lums)", IWMP_VERSION, millis());
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
        LOG_I(TAG, "NVS ok (t=%lums)", millis());
    }

    // Initialize I2C for ADS1115 and other I2C peripherals
    Wire.begin(remote_pins::I2C_SDA, remote_pins::I2C_SCL);
    LOG_I(TAG, "I2C SDA=%d SCL=%d (t=%lums)", remote_pins::I2C_SDA, remote_pins::I2C_SCL, millis());

    // Log MAC address for board identification (read from efuse, no WiFi init needed)
    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        LOG_I(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    // Initialize config manager
    LOG_I(TAG, "Config.begin... (t=%lums)", millis());
    if (Config.begin(DeviceType::REMOTE)) {
        LOG_I(TAG, "Config loaded (t=%lums)", millis());
        LOG_I(TAG, "Device ID: %s", Config.getDeviceId());
    } else {
        LOG_E(TAG, "Config initialization failed!");
    }

    // Initialize remote controller
    // This handles wake reason detection and decides on operating mode
    LOG_I(TAG, "Remote.begin... (t=%lums)", millis());
    Remote.begin();

    LOG_I(TAG, "Setup complete (t=%lums)", millis());
}

void loop() {
    Remote.loop();
    delay(1);

    // Periodic heartbeat so we can attach the serial monitor mid-run
    // and immediately see the device state without a reset.
    static uint32_t s_hb = 0;
    if (millis() - s_hb > 5000) {
        s_hb = millis();
        LOG_I(TAG, "ALIVE t=%lus heap=%u ap=%s",
              millis() / 1000, ESP.getFreeHeap(),
              WiFiMgr.getAPIP().toString().c_str());
    }
}
