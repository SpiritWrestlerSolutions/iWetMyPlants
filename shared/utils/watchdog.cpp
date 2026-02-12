/**
 * @file watchdog.cpp
 * @brief Watchdog timer management implementation
 */

#include "watchdog.h"
#include "logger.h"

namespace iwmp {

// Global watchdog instance
WatchdogManager Watchdog;

static constexpr const char* TAG = "WDT";

bool WatchdogManager::begin(uint32_t timeout_sec) {
    _timeout_sec = timeout_sec;

    // Initialize task watchdog timer with specified timeout
    // Using the legacy API for broader compatibility
    esp_err_t err = esp_task_wdt_init(timeout_sec, true);  // true = trigger panic on timeout
    if (err == ESP_ERR_INVALID_STATE) {
        // Already initialized, this is OK
        LOG_I(TAG, "Watchdog already initialized");
    } else if (err != ESP_OK) {
        LOG_E(TAG, "Failed to initialize watchdog: %d", err);
        return false;
    }

    // Add current task to watchdog
    err = esp_task_wdt_add(NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_ARG) {
        // ESP_ERR_INVALID_ARG means task already added, which is fine
        LOG_E(TAG, "Failed to add task to watchdog: %d", err);
        return false;
    }

    _initialized = true;
    _enabled = true;
    _last_feed_time = millis();

    LOG_I(TAG, "Watchdog initialized with %lu sec timeout", timeout_sec);
    return true;
}

void WatchdogManager::feed() {
    if (!_initialized || !_enabled) {
        return;
    }

    esp_task_wdt_reset();
    _last_feed_time = millis();
}

void WatchdogManager::disable() {
    if (!_initialized) {
        return;
    }

    // Remove current task from watchdog
    esp_err_t err = esp_task_wdt_delete(NULL);
    if (err == ESP_OK) {
        _enabled = false;
        LOG_I(TAG, "Watchdog disabled");
    }
}

void WatchdogManager::enable() {
    if (!_initialized) {
        LOG_W(TAG, "Cannot enable - not initialized");
        return;
    }

    if (_enabled) {
        return;  // Already enabled
    }

    // Add current task back to watchdog
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err == ESP_OK || err == ESP_ERR_INVALID_ARG) {
        _enabled = true;
        _last_feed_time = millis();
        LOG_I(TAG, "Watchdog enabled");
    }
}

void WatchdogManager::setTimeout(uint32_t timeout_sec) {
    if (_timeout_sec == timeout_sec) {
        return;  // No change needed
    }

    _timeout_sec = timeout_sec;

    if (!_initialized) {
        return;
    }

    // To change timeout, we need to deinit and reinit
    // First remove our task
    esp_task_wdt_delete(NULL);

    // Deinit the watchdog
    esp_task_wdt_deinit();

    // Reinit with new timeout
    esp_err_t err = esp_task_wdt_init(timeout_sec, true);
    if (err == ESP_OK) {
        // Re-add our task
        esp_task_wdt_add(NULL);
        _last_feed_time = millis();
        LOG_I(TAG, "Watchdog timeout changed to %lu sec", timeout_sec);
    } else {
        LOG_E(TAG, "Failed to change timeout: %d", err);
        _initialized = false;
        _enabled = false;
    }
}

uint32_t WatchdogManager::timeSinceLastFeed() const {
    return millis() - _last_feed_time;
}

} // namespace iwmp
