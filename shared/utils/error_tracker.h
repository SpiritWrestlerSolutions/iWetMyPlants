/**
 * @file error_tracker.h
 * @brief Error tracking and history for diagnostics
 *
 * Maintains a circular buffer of recent errors for debugging
 * and system health monitoring.
 */

#pragma once

#include <Arduino.h>
#include "error_codes.h"

namespace iwmp {

// Maximum number of errors to track
static constexpr size_t MAX_ERROR_HISTORY = 16;

/**
 * @brief Error record with context
 */
struct ErrorRecord {
    ErrorCode code;
    ErrorSeverity severity;
    uint32_t timestamp;      // millis() when error occurred
    char context[32];        // Brief context (e.g., function name)
    uint16_t line;           // Source line number
};

/**
 * @brief Error tracker for system diagnostics
 */
class ErrorTracker {
public:
    /**
     * @brief Record an error
     * @param code Error code
     * @param severity Error severity
     * @param context Brief context string
     * @param line Source line number
     */
    void record(ErrorCode code, ErrorSeverity severity,
                const char* context = nullptr, uint16_t line = 0);

    /**
     * @brief Record an error with default ERROR severity
     * @param code Error code
     * @param context Brief context string
     */
    void record(ErrorCode code, const char* context = nullptr);

    /**
     * @brief Get number of recorded errors
     * @return Error count
     */
    size_t count() const { return _count; }

    /**
     * @brief Get total errors since boot
     * @return Total error count
     */
    uint32_t totalErrors() const { return _total_errors; }

    /**
     * @brief Get most recent error code
     * @return Last error code or OK if none
     */
    ErrorCode lastError() const;

    /**
     * @brief Get error record by index (0 = oldest)
     * @param index Record index
     * @return Pointer to record or nullptr if invalid
     */
    const ErrorRecord* getRecord(size_t index) const;

    /**
     * @brief Get most recent error record
     * @return Pointer to most recent error or nullptr
     */
    const ErrorRecord* getLastRecord() const;

    /**
     * @brief Clear all recorded errors
     */
    void clear();

    /**
     * @brief Check if any critical/fatal errors recorded
     * @return true if critical errors exist
     */
    bool hasCriticalErrors() const { return _critical_count > 0; }

    /**
     * @brief Get count of errors by severity
     * @param severity Severity to count
     * @return Number of errors at that severity
     */
    size_t countBySeverity(ErrorSeverity severity) const;

    /**
     * @brief Get uptime since last error
     * @return Milliseconds since last error (or since boot if none)
     */
    uint32_t timeSinceLastError() const;

private:
    ErrorRecord _history[MAX_ERROR_HISTORY];
    size_t _head = 0;           // Next write position
    size_t _count = 0;          // Current entries in buffer
    uint32_t _total_errors = 0; // Total errors since boot
    uint32_t _critical_count = 0;
    uint32_t _last_error_time = 0;
};

// Global error tracker instance
extern ErrorTracker Errors;

// Convenience macros for recording errors with context
#define IWMP_ERROR(code) \
    iwmp::Errors.record(code, iwmp::ErrorSeverity::ERROR, __func__, __LINE__)

#define IWMP_ERROR_CTX(code, ctx) \
    iwmp::Errors.record(code, iwmp::ErrorSeverity::ERROR, ctx, __LINE__)

#define IWMP_WARNING(code) \
    iwmp::Errors.record(code, iwmp::ErrorSeverity::WARNING, __func__, __LINE__)

#define IWMP_CRITICAL(code) \
    iwmp::Errors.record(code, iwmp::ErrorSeverity::CRITICAL, __func__, __LINE__)

} // namespace iwmp
