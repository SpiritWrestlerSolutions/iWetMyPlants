/**
 * @file logger.cpp
 * @brief Unified logging system implementation
 */

#include "logger.h"
#include <stdarg.h>

namespace iwmp {

// Global logger instance
Logger Log;

void Logger::begin(LogLevel level) {
    _level = level;
}

void Logger::error(const char* tag, const char* format, ...) {
    if (_level < LogLevel::ERROR) return;
    va_list args;
    va_start(args, format);
    logImpl(LogLevel::ERROR, tag, format, args);
    va_end(args);
}

void Logger::warn(const char* tag, const char* format, ...) {
    if (_level < LogLevel::WARN) return;
    va_list args;
    va_start(args, format);
    logImpl(LogLevel::WARN, tag, format, args);
    va_end(args);
}

void Logger::info(const char* tag, const char* format, ...) {
    if (_level < LogLevel::INFO) return;
    va_list args;
    va_start(args, format);
    logImpl(LogLevel::INFO, tag, format, args);
    va_end(args);
}

void Logger::debug(const char* tag, const char* format, ...) {
    if (_level < LogLevel::DEBUG) return;
    va_list args;
    va_start(args, format);
    logImpl(LogLevel::DEBUG, tag, format, args);
    va_end(args);
}

void Logger::verbose(const char* tag, const char* format, ...) {
    if (_level < LogLevel::VERBOSE) return;
    va_list args;
    va_start(args, format);
    logImpl(LogLevel::VERBOSE, tag, format, args);
    va_end(args);
}

void Logger::log(LogLevel level, const char* tag, const char* format, ...) {
    if (_level < level) return;
    va_list args;
    va_start(args, format);
    logImpl(level, tag, format, args);
    va_end(args);
}

void Logger::hexdump(LogLevel level, const char* tag, const uint8_t* data, size_t len) {
    if (_level < level) return;
    char buf[64];
    for (size_t i = 0; i < len; i += 16) {
        int n = 0;
        for (size_t j = i; j < len && j < i + 16; j++) {
            n += snprintf(buf + n, sizeof(buf) - n, "%02X ", data[j]);
        }
        info(tag, "%04X: %s", (unsigned)i, buf);
    }
}

void Logger::logImpl(LogLevel level, const char* tag, const char* format, va_list args) {
    if (_colors) {
        Serial.print(getLevelColor(level));
    }
    if (_timestamps) {
        Serial.printf("[%8lu] ", (unsigned long)millis());
    }
    Serial.printf("[%s] [%s] ", getLevelString(level), tag);
    char buf[256];
    vsnprintf(buf, sizeof(buf), format, args);
    Serial.println(buf);
    if (_colors) {
        Serial.print("\033[0m");
    }
}

const char* Logger::getLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::ERROR:   return "E";
        case LogLevel::WARN:    return "W";
        case LogLevel::INFO:    return "I";
        case LogLevel::DEBUG:   return "D";
        case LogLevel::VERBOSE: return "V";
        default:                return "?";
    }
}

const char* Logger::getLevelColor(LogLevel level) {
    switch (level) {
        case LogLevel::ERROR:   return "\033[31m";
        case LogLevel::WARN:    return "\033[33m";
        case LogLevel::INFO:    return "\033[32m";
        case LogLevel::DEBUG:   return "\033[36m";
        case LogLevel::VERBOSE: return "\033[37m";
        default:                return "";
    }
}

} // namespace iwmp
