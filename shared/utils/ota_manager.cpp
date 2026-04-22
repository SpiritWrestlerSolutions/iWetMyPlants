/**
 * @file ota_manager.cpp
 * @brief Over-the-Air (OTA) firmware update management implementation
 */

#include "ota_manager.h"
#include "admin_auth.h"
#include "logger.h"

namespace iwmp {

// Global OTA manager instance
OtaManager Ota;

static constexpr const char* TAG = "OTA";

void OtaManager::begin(AsyncWebServer* server, const char* path) {
    if (!server) {
        LOG_E(TAG, "Null server provided");
        return;
    }

    reset();

    // Set up OTA upload endpoint. Auth is checked on the very first
    // chunk so we don't buffer or write a megabyte of un-auth'd firmware
    // before rejecting it. If auth fails, we mark the upload ERROR and
    // every subsequent chunk no-ops (handleUploadData gates on
    // _progress.state == RECEIVING). The completion handler then
    // reports the failure as a 400.
    server->on(path, HTTP_POST,
        // Request handler (called when upload completes)
        [this](AsyncWebServerRequest* request) {
            AsyncWebServerResponse* response;

            if (_progress.state == OtaState::COMPLETE) {
                response = request->beginResponse(200, "application/json",
                    "{\"success\":true,\"message\":\"Update successful, rebooting...\"}");
            } else {
                String error = "{\"success\":false,\"error\":\"";
                error += _progress.error_message;
                error += "\"}";
                response = request->beginResponse(400, "application/json", error);
            }

            request->send(response);
        },
        // Upload handler (called for each chunk)
        [this](AsyncWebServerRequest* request, String filename, size_t index,
               uint8_t* data, size_t len, bool final) {

            if (index == 0) {
                // Auth gate: require() sends a 401 itself when failing.
                // We poison the OTA state so subsequent chunks are
                // ignored and the completion handler reports an error.
                if (!AdminAuth.require(request)) {
                    setError("Unauthorized");
                    return;
                }
                // First chunk - start update
                size_t total = request->contentLength();
                handleUploadStart(total);
            }

            if (_progress.state == OtaState::RECEIVING) {
                handleUploadData(data, len, index, request->contentLength());
            }

            if (final && _progress.state == OtaState::RECEIVING) {
                handleUploadEnd(true);
            }
        }
    );

    LOG_I(TAG, "OTA endpoint initialized at %s", path);
}

bool OtaManager::isUpdating() const {
    return _progress.state == OtaState::RECEIVING ||
           _progress.state == OtaState::VERIFYING;
}

void OtaManager::cancel() {
    if (!isUpdating()) {
        return;
    }

    LOG_W(TAG, "Update cancelled");
    Update.abort();
    setError("Update cancelled");
}

void OtaManager::scheduleReboot(uint32_t delay_ms) {
    _reboot_scheduled = true;
    _reboot_at = millis() + delay_ms;
    LOG_I(TAG, "Reboot scheduled in %lu ms", delay_ms);
}

void OtaManager::checkPendingReboot() {
    if (_reboot_scheduled && millis() >= _reboot_at) {
        LOG_I(TAG, "Rebooting now...");
        delay(100);  // Allow logs to flush
        ESP.restart();
    }
}

void OtaManager::handleUploadStart(size_t total_size) {
    LOG_I(TAG, "Starting OTA update, size: %u bytes", total_size);

    reset();
    _progress.state = OtaState::RECEIVING;
    _progress.total_size = total_size;
    _progress.received = 0;
    _progress.percent = 0;

    // Determine update type (firmware or SPIFFS)
    int cmd = U_FLASH;

    // Check if there's enough space
    if (!Update.begin(total_size, cmd)) {
        String error = Update.errorString();
        LOG_E(TAG, "Update.begin failed: %s", error.c_str());
        setError(error.c_str());
        return;
    }

    LOG_I(TAG, "Update started successfully");

    if (_progress_callback) {
        _progress_callback(_progress);
    }
}

void OtaManager::handleUploadData(uint8_t* data, size_t len, size_t index, size_t total) {
    if (_progress.state != OtaState::RECEIVING) {
        return;
    }

    // Write data to flash
    size_t written = Update.write(data, len);
    if (written != len) {
        String error = Update.errorString();
        LOG_E(TAG, "Write failed: %s", error.c_str());
        setError(error.c_str());
        Update.abort();
        return;
    }

    _progress.received = index + len;
    _progress.percent = (_progress.total_size > 0)
        ? (uint8_t)((_progress.received * 100) / _progress.total_size)
        : 0;

    // Log progress every 10%
    static uint8_t last_logged = 0;
    if (_progress.percent / 10 > last_logged / 10) {
        last_logged = _progress.percent;
        LOG_I(TAG, "Progress: %u%% (%u / %u bytes)",
              _progress.percent, _progress.received, _progress.total_size);
    }

    if (_progress_callback) {
        _progress_callback(_progress);
    }
}

void OtaManager::handleUploadEnd(bool final_chunk) {
    if (_progress.state != OtaState::RECEIVING) {
        return;
    }

    LOG_I(TAG, "Upload complete, verifying...");
    _progress.state = OtaState::VERIFYING;

    if (_progress_callback) {
        _progress_callback(_progress);
    }

    // Finalize update
    if (!Update.end(true)) {
        String error = Update.errorString();
        LOG_E(TAG, "Update.end failed: %s", error.c_str());
        setError(error.c_str());
        return;
    }

    // Verify MD5
    if (!Update.isFinished()) {
        LOG_E(TAG, "Update not finished properly");
        setError("Update verification failed");
        return;
    }

    _progress.state = OtaState::COMPLETE;
    _progress.percent = 100;
    LOG_I(TAG, "OTA update successful!");

    if (_progress_callback) {
        _progress_callback(_progress);
    }

    if (_complete_callback) {
        _complete_callback(true);
    }

    // Schedule reboot
    scheduleReboot(1000);
}

void OtaManager::reset() {
    _progress.state = OtaState::IDLE;
    _progress.total_size = 0;
    _progress.received = 0;
    _progress.percent = 0;
    _progress.error_message[0] = '\0';
    _reboot_scheduled = false;
    _reboot_at = 0;
}

void OtaManager::setError(const char* message) {
    _progress.state = OtaState::ERROR;
    strncpy(_progress.error_message, message, sizeof(_progress.error_message) - 1);
    _progress.error_message[sizeof(_progress.error_message) - 1] = '\0';

    if (_complete_callback) {
        _complete_callback(false);
    }
}

} // namespace iwmp
