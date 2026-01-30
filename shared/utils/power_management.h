/**
 * @file power_management.h
 * @brief Sleep mode and power management helpers
 */

#pragma once

#include <Arduino.h>
#include <esp_sleep.h>
#include "config_schema.h"

namespace iwmp {

// RTC memory data that persists across deep sleep
struct RtcData {
    uint32_t boot_count;
    uint32_t last_successful_send;
    uint8_t consecutive_failures;
    uint32_t magic;  // Validation marker
};

/**
 * @brief Power management for deep sleep and battery monitoring
 */
class PowerManager {
public:
    /**
     * @brief Initialize power manager
     * @param config Power configuration
     */
    void begin(const PowerConfig& config);

    /**
     * @brief Get wake reason
     * @return ESP sleep wake cause
     */
    esp_sleep_wakeup_cause_t getWakeReason();

    /**
     * @brief Get wake reason as string
     * @return Human-readable wake reason
     */
    const char* getWakeReasonString();

    /**
     * @brief Enter deep sleep
     * @param sleep_duration_sec Sleep duration in seconds
     */
    void enterDeepSleep(uint32_t sleep_duration_sec);

    /**
     * @brief Enter light sleep
     * @param sleep_duration_ms Sleep duration in milliseconds
     */
    void enterLightSleep(uint32_t sleep_duration_ms);

    /**
     * @brief Check if external power is connected
     * @return true if USB/external power detected
     */
    bool isExternalPowerConnected();

    /**
     * @brief Read battery voltage
     * @return Voltage in volts
     */
    float getBatteryVoltage();

    /**
     * @brief Get estimated battery percentage
     * @return Percentage (0-100)
     */
    uint8_t getBatteryPercent();

    /**
     * @brief Check if battery is low
     * @return true if below threshold
     */
    bool isBatteryLow();

    /**
     * @brief Calculate optimal sleep duration based on conditions
     * @return Recommended sleep duration in seconds
     */
    uint32_t calculateOptimalSleepDuration();

    /**
     * @brief Get boot count (from RTC memory)
     * @return Number of boots
     */
    uint32_t getBootCount() const;

    /**
     * @brief Get consecutive send failures
     * @return Failure count
     */
    uint8_t getConsecutiveFailures() const;

    /**
     * @brief Record successful send
     */
    void recordSuccessfulSend();

    /**
     * @brief Record failed send
     */
    void recordFailedSend();

    /**
     * @brief Prepare for sleep (save state, disable peripherals)
     */
    void prepareForSleep();

private:
    PowerConfig _config;
    bool _initialized = false;

    /**
     * @brief Configure wake sources
     */
    void configureWakeSources();

    /**
     * @brief Configure GPIO wake (ESP32-C3 specific)
     * @param pin GPIO pin
     * @param level Wake level
     */
    void configureGpioWake(uint8_t pin, bool level);

    /**
     * @brief Load RTC data
     */
    void loadRtcData();

    /**
     * @brief Save RTC data
     */
    void saveRtcData();

    /**
     * @brief Convert voltage to percentage
     * @param voltage Battery voltage
     * @return Percentage
     */
    uint8_t voltageToPercent(float voltage);
};

// Global power manager instance
extern PowerManager Power;

} // namespace iwmp
