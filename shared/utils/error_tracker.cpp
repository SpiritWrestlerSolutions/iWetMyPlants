/**
 * @file error_tracker.cpp
 * @brief Error tracking implementation
 */

#include "error_tracker.h"
#include "logger.h"

namespace iwmp {

// Global error tracker instance
ErrorTracker Errors;

static constexpr const char* TAG = "Err";

void ErrorTracker::record(ErrorCode code, ErrorSeverity severity,
                          const char* context, uint16_t line) {
    // Don't record OK
    if (code == ErrorCode::OK) {
        return;
    }

    // Create record
    ErrorRecord& record = _history[_head];
    record.code = code;
    record.severity = severity;
    record.timestamp = millis();
    record.line = line;

    if (context) {
        strncpy(record.context, context, sizeof(record.context) - 1);
        record.context[sizeof(record.context) - 1] = '\0';
    } else {
        record.context[0] = '\0';
    }

    // Update tracking
    _head = (_head + 1) % MAX_ERROR_HISTORY;
    if (_count < MAX_ERROR_HISTORY) {
        _count++;
    }
    _total_errors++;
    _last_error_time = record.timestamp;

    if (severity >= ErrorSeverity::CRITICAL) {
        _critical_count++;
    }

    // Log the error
    const char* msg = getErrorMessage(code);
    switch (severity) {
        case ErrorSeverity::WARNING:
            LOG_W(TAG, "[%s:%d] %s (code %d)", context ? context : "?", line, msg, (int)code);
            break;
        case ErrorSeverity::ERROR:
            LOG_E(TAG, "[%s:%d] %s (code %d)", context ? context : "?", line, msg, (int)code);
            break;
        case ErrorSeverity::CRITICAL:
        case ErrorSeverity::FATAL:
            LOG_E(TAG, "CRITICAL [%s:%d] %s (code %d)", context ? context : "?", line, msg, (int)code);
            break;
        default:
            LOG_I(TAG, "[%s:%d] %s (code %d)", context ? context : "?", line, msg, (int)code);
            break;
    }
}

void ErrorTracker::record(ErrorCode code, const char* context) {
    record(code, ErrorSeverity::ERROR, context, 0);
}

ErrorCode ErrorTracker::lastError() const {
    if (_count == 0) {
        return ErrorCode::OK;
    }

    // Get the most recent entry
    size_t last_idx = (_head + MAX_ERROR_HISTORY - 1) % MAX_ERROR_HISTORY;
    return _history[last_idx].code;
}

const ErrorRecord* ErrorTracker::getRecord(size_t index) const {
    if (index >= _count) {
        return nullptr;
    }

    // Calculate actual position in circular buffer
    // index 0 = oldest, index _count-1 = newest
    size_t start = (_head + MAX_ERROR_HISTORY - _count) % MAX_ERROR_HISTORY;
    size_t actual = (start + index) % MAX_ERROR_HISTORY;

    return &_history[actual];
}

const ErrorRecord* ErrorTracker::getLastRecord() const {
    if (_count == 0) {
        return nullptr;
    }
    return getRecord(_count - 1);
}

void ErrorTracker::clear() {
    _head = 0;
    _count = 0;
    // Note: total_errors is not cleared - it's cumulative since boot
}

size_t ErrorTracker::countBySeverity(ErrorSeverity severity) const {
    size_t count = 0;
    for (size_t i = 0; i < _count; i++) {
        const ErrorRecord* rec = getRecord(i);
        if (rec && rec->severity == severity) {
            count++;
        }
    }
    return count;
}

uint32_t ErrorTracker::timeSinceLastError() const {
    if (_last_error_time == 0) {
        return millis();  // No errors, return uptime
    }
    return millis() - _last_error_time;
}

} // namespace iwmp
