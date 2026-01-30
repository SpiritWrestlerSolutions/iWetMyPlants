/**
 * @file power_modes.h
 * @brief Deep sleep management for Remote device
 *
 * Handles wake sources, RTC data persistence, and power optimization.
 */

#pragma once

#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include "config_schema.h"

namespace iwmp {

// RTC memory data - persists across deep sleep
extern RTC_DATA_ATTR uint32_t rtc_boot_count;
extern RTC_DATA_ATTR uint32_t rtc_last_successful_send;
extern RTC_DATA_ATTR uint8_t rtc_consecutive_failures;
extern RTC_DATA_ATTR uint32_t rtc_total_sleep_time;

/**
 * @brief Power mode manager for Remote devices
 */
class PowerModes {
public:
    /**
     * @brief Initialize power management
     * @param config Power configuration
     */
    void begin(const PowerConfig& config);

    /**
     * @brief Get wake reason
     * @return ESP sleep wake cause
     */
    esp_sleep_wakeup_cause_t getWakeReason() const { return _wake_reason; }

    /**
     * @brief Get wake reason as string
     * @return Human-readable wake reason
     */
    const char* getWakeReasonString() const;

    /**
     * @brief Check if woke from timer
     * @return true if timer wake
     */
    bool wokeFromTimer() const {
        return _wake_reason == ESP_SLEEP_WAKEUP_TIMER;
    }

    /**
     * @brief Check if woke from button/GPIO
     * @return true if GPIO wake
     */
    bool wokeFromButton() const {
        return _wake_reason == ESP_SLEEP_WAKEUP_GPIO ||
               _wake_reason == ESP_SLEEP_WAKEUP_EXT0 ||
               _wake_reason == ESP_SLEEP_WAKEUP_EXT1;
    }

    /**
     * @brief Check if this is first boot (power on)
     * @return true if first boot
     */
    bool isFirstBoot() const {
        return _wake_reason == ESP_SLEEP_WAKEUP_UNDEFINED;
    }

    /**
     * @brief Enter deep sleep
     * @param sleep_duration_sec Sleep duration in seconds
     */
    void enterDeepSleep(uint32_t sleep_duration_sec);

    /**
     * @brief Check if external power is connected
     * @return true if USB/external power detected
     */
    bool isExternalPowerConnected() const;

    /**
     * @brief Read battery voltage
     * @return Voltage in volts
     */
    float getBatteryVoltage() const;

    /**
     * @brief Get estimated battery percentage
     * @return Percentage (0-100)
     */
    uint8_t getBatteryPercent() const;

    /**
     * @brief Check if battery is low
     * @return true if below threshold
     */
    bool isBatteryLow() const;

    /**
     * @brief Calculate optimal sleep duration
     *
     * Adjusts based on:
     * - Consecutive failures (backoff)
     * - Battery level (longer sleep when low)
     * - External power (shorter sleep when powered)
     *
     * @return Recommended sleep duration in seconds
     */
    uint32_t calculateOptimalSleepDuration() const;

    /**
     * @brief Record successful send (for adaptive timing)
     */
    void recordSuccessfulSend();

    /**
     * @brief Record failed send (for backoff)
     */
    void recordFailedSend();

    /**
     * @brief Get boot count
     * @return Number of boots since power on
     */
    uint32_t getBootCount() const { return rtc_boot_count; }

    /**
     * @brief Get consecutive failures
     * @return Number of consecutive send failures
     */
    uint8_t getConsecutiveFailures() const { return rtc_consecutive_failures; }

    /**
     * @brief Get total sleep time
     * @return Total time spent sleeping in seconds
     */
    uint32_t getTotalSleepTime() const { return rtc_total_sleep_time; }

    /**
     * @brief Prepare for sleep (disable peripherals, etc.)
     */
    void prepareForSleep();

private:
    PowerConfig _config;
    esp_sleep_wakeup_cause_t _wake_reason;
    bool _initialized = false;

    /**
     * @brief Configure wake sources
     */
    void configureWakeSources();

    /**
     * @brief Configure GPIO wake for ESP32-C3
     * @param pin GPIO pin
     * @param level Wake level (true = high, false = low)
     */
    void configureGpioWake(uint8_t pin, bool level);

    /**
     * @brief Convert battery voltage to percentage
     * @param voltage Battery voltage
     * @return Percentage (0-100)
     */
    uint8_t voltageToPercent(float voltage) const;

    // Battery voltage curve (LiPo)
    static constexpr float BATTERY_MAX_V = 4.2f;
    static constexpr float BATTERY_MIN_V = 3.0f;

    // Backoff constants
    static constexpr uint8_t MAX_BACKOFF_MULTIPLIER = 8;
    static constexpr uint32_t MIN_SLEEP_SEC = 60;
    static constexpr uint32_t MAX_SLEEP_SEC = 3600;
};

} // namespace iwmp
