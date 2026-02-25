/**
 * @file watchdog.cpp
 * @brief Watchdog timer management implementation
 *
 * Supports both old ESP-IDF 4.x API (uint32_t timeout, bool panic)
 * and new ESP-IDF 5.x API (esp_task_wdt_config_t struct).
 */

#include "watchdog.h"
#include <esp_task_wdt.h>

namespace iwmp {

// Global watchdog instance
WatchdogManager Watchdog;

bool WatchdogManager::begin(uint32_t timeout_sec) {
    _timeout_sec = timeout_sec;

    // Deinit first in case already initialized
    esp_task_wdt_deinit();

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms   = timeout_sec * 1000,
        .idle_core_mask = 0,
        .trigger_panic  = true
    };
    esp_err_t err = esp_task_wdt_init(&wdt_config);
#else
    esp_err_t err = esp_task_wdt_init(timeout_sec, true);
#endif

    if (err != ESP_OK) {
        return false;
    }

    esp_task_wdt_add(NULL);
    _enabled     = true;
    _initialized = true;
    _last_feed_time = millis();
    return true;
}

void WatchdogManager::feed() {
    if (_enabled && _initialized) {
        esp_task_wdt_reset();
        _last_feed_time = millis();
    }
}

void WatchdogManager::disable() {
    if (_initialized) {
        esp_task_wdt_delete(NULL);
        _enabled = false;
    }
}

void WatchdogManager::enable() {
    if (_initialized && !_enabled) {
        esp_task_wdt_add(NULL);
        _enabled = true;
    }
}

void WatchdogManager::setTimeout(uint32_t timeout_sec) {
    _timeout_sec = timeout_sec;
    if (_initialized) {
        // Re-init with new timeout
        esp_task_wdt_delete(NULL);
        esp_task_wdt_deinit();
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        esp_task_wdt_config_t wdt_config = {
            .timeout_ms   = timeout_sec * 1000,
            .idle_core_mask = 0,
            .trigger_panic  = true
        };
        esp_task_wdt_init(&wdt_config);
#else
        esp_task_wdt_init(timeout_sec, true);
#endif
        esp_task_wdt_add(NULL);
    }
}

uint32_t WatchdogManager::timeSinceLastFeed() const {
    return millis() - _last_feed_time;
}

} // namespace iwmp
