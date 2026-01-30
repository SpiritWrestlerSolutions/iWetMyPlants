/**
 * @file watchdog.h
 * @brief Watchdog timer management
 */

#pragma once

#include <Arduino.h>
#include <esp_task_wdt.h>

namespace iwmp {

/**
 * @brief Watchdog timer manager
 */
class WatchdogManager {
public:
    /**
     * @brief Initialize watchdog
     * @param timeout_sec Timeout in seconds
     * @return true if initialized
     */
    bool begin(uint32_t timeout_sec = 30);

    /**
     * @brief Feed/reset the watchdog
     */
    void feed();

    /**
     * @brief Disable watchdog
     */
    void disable();

    /**
     * @brief Enable watchdog
     */
    void enable();

    /**
     * @brief Check if watchdog is enabled
     * @return true if enabled
     */
    bool isEnabled() const { return _enabled; }

    /**
     * @brief Set timeout
     * @param timeout_sec New timeout in seconds
     */
    void setTimeout(uint32_t timeout_sec);

    /**
     * @brief Get time since last feed
     * @return Milliseconds since last feed
     */
    uint32_t timeSinceLastFeed() const;

private:
    bool _enabled = false;
    bool _initialized = false;
    uint32_t _timeout_sec = 30;
    uint32_t _last_feed_time = 0;
};

// Global watchdog instance
extern WatchdogManager Watchdog;

} // namespace iwmp
