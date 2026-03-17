/**
 * @file logger.h
 * @brief Unified logging system for iWetMyPlants v1.0
 */

#pragma once

#include <Arduino.h>

namespace iwmp {

enum class LogLevel : uint8_t {
    NONE = 0,
    ERROR = 1,
    WARN = 2,
    INFO = 3,
    DEBUG = 4,
    VERBOSE = 5
};

/**
 * @brief Unified logger with level filtering and formatting
 */
class Logger {
public:
    /**
     * @brief Initialize logger
     * @param level Minimum log level to display
     */
    void begin(LogLevel level = LogLevel::INFO);

    /**
     * @brief Set log level
     * @param level Minimum level to display
     */
    void setLevel(LogLevel level) { _level = level; }

    /**
     * @brief Get current log level
     * @return Current level
     */
    LogLevel getLevel() const { return _level; }

    /**
     * @brief Enable/disable timestamps
     * @param enabled Show timestamps
     */
    void setTimestamps(bool enabled) { _timestamps = enabled; }

    /**
     * @brief Enable/disable color output
     * @param enabled Use ANSI colors
     */
    void setColors(bool enabled) { _colors = enabled; }

    /**
     * @brief Log error message
     */
    void error(const char* tag, const char* format, ...);

    /**
     * @brief Log warning message
     */
    void warn(const char* tag, const char* format, ...);

    /**
     * @brief Log info message
     */
    void info(const char* tag, const char* format, ...);

    /**
     * @brief Log debug message
     */
    void debug(const char* tag, const char* format, ...);

    /**
     * @brief Log verbose message
     */
    void verbose(const char* tag, const char* format, ...);

    /**
     * @brief Log with specific level
     */
    void log(LogLevel level, const char* tag, const char* format, ...);

    /**
     * @brief Log hexdump of data
     */
    void hexdump(LogLevel level, const char* tag, const uint8_t* data, size_t len);

private:
    LogLevel _level = LogLevel::INFO;
    bool _timestamps = true;
    bool _colors = true;

    /**
     * @brief Internal log implementation
     */
    void logImpl(LogLevel level, const char* tag, const char* format, va_list args);

    /**
     * @brief Get level string
     */
    const char* getLevelString(LogLevel level);

    /**
     * @brief Get level color code
     */
    const char* getLevelColor(LogLevel level);
};

// Global logger instance
extern Logger Log;

// Convenience macros
#define LOG_E(tag, fmt, ...) iwmp::Log.error(tag, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) iwmp::Log.warn(tag, fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) iwmp::Log.info(tag, fmt, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) iwmp::Log.debug(tag, fmt, ##__VA_ARGS__)
#define LOG_V(tag, fmt, ...) iwmp::Log.verbose(tag, fmt, ##__VA_ARGS__)

} // namespace iwmp
