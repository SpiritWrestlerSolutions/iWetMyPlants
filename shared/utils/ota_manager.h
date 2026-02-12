/**
 * @file ota_manager.h
 * @brief Over-the-Air (OTA) firmware update management
 *
 * Supports web-based firmware upload and progress tracking.
 */

#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <functional>

namespace iwmp {

enum class OtaState {
    IDLE,
    RECEIVING,
    VERIFYING,
    COMPLETE,
    ERROR
};

struct OtaProgress {
    OtaState state;
    size_t total_size;
    size_t received;
    uint8_t percent;
    char error_message[64];
};

using OtaProgressCallback = std::function<void(const OtaProgress&)>;
using OtaCompleteCallback = std::function<void(bool success)>;

/**
 * @brief OTA update manager
 */
class OtaManager {
public:
    /**
     * @brief Initialize OTA manager
     * @param server AsyncWebServer instance
     * @param path Upload endpoint path (default "/api/system/ota")
     */
    void begin(AsyncWebServer* server, const char* path = "/api/system/ota");

    /**
     * @brief Get current OTA state
     * @return OTA state
     */
    OtaState getState() const { return _progress.state; }

    /**
     * @brief Get current progress
     * @return Progress structure
     */
    const OtaProgress& getProgress() const { return _progress; }

    /**
     * @brief Check if update is in progress
     * @return true if receiving or verifying
     */
    bool isUpdating() const;

    /**
     * @brief Set progress callback
     * @param callback Called during update progress
     */
    void onProgress(OtaProgressCallback callback) { _progress_callback = callback; }

    /**
     * @brief Set completion callback
     * @param callback Called when update completes (success or failure)
     */
    void onComplete(OtaCompleteCallback callback) { _complete_callback = callback; }

    /**
     * @brief Cancel current update
     */
    void cancel();

    /**
     * @brief Schedule reboot after successful update
     * @param delay_ms Delay before reboot in milliseconds
     */
    void scheduleReboot(uint32_t delay_ms = 1000);

    /**
     * @brief Check for pending reboot (call in loop)
     */
    void checkPendingReboot();

private:
    OtaProgress _progress;
    OtaProgressCallback _progress_callback;
    OtaCompleteCallback _complete_callback;

    bool _reboot_scheduled = false;
    uint32_t _reboot_at = 0;

    /**
     * @brief Handle upload start
     */
    void handleUploadStart(size_t total_size);

    /**
     * @brief Handle upload data chunk
     */
    void handleUploadData(uint8_t* data, size_t len, size_t index, size_t total);

    /**
     * @brief Handle upload complete
     */
    void handleUploadEnd(bool final_chunk);

    /**
     * @brief Reset state
     */
    void reset();

    /**
     * @brief Set error state
     */
    void setError(const char* message);
};

// Global OTA manager instance
extern OtaManager Ota;

} // namespace iwmp
