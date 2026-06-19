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

private:
    bool _enabled = false;
    bool _initialized = false;
    uint32_t _timeout_sec = 30;
    uint32_t _last_feed_time = 0;
};

// Global watchdog instance
extern WatchdogManager Watchdog;

} // namespace iwmp
