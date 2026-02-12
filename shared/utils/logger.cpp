/**
 * @file logger.cpp
 * @brief Unified logging system implementation
 */

#include "logger.h"
#include <cstdarg>
#include <cstdio>

namespace iwmp {

// Global logger instance
Logger Log;

// ANSI color codes
static const char* COLOR_RESET = "\033[0m";
static const char* COLOR_RED = "\033[31m";
static const char* COLOR_YELLOW = "\033[33m";
static const char* COLOR_GREEN = "\033[32m";
static const char* COLOR_CYAN = "\033[36m";
static const char* COLOR_MAGENTA = "\033[35m";

void Logger::begin(LogLevel level) {
    _level = level;
    Serial.begin(115200);

    // Wait for serial to be ready (important for some boards)
    uint32_t start = millis();
    while (!Serial && (millis() - start) < 2000) {
        delay(10);
    }

    info("LOG", "Logger initialized at level %d", static_cast<int>(level));
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

void Logger::logImpl(LogLevel level, const char* tag, const char* format, va_list args) {
    // Buffer for formatted message
    char message[256];
    vsnprintf(message, sizeof(message), format, args);

    // Print color code if enabled
    if (_colors) {
        Serial.print(getLevelColor(level));
    }

    // Print timestamp if enabled
    if (_timestamps) {
        uint32_t ms = millis();
        uint32_t seconds = ms / 1000;
        uint32_t minutes = seconds / 60;
        uint32_t hours = minutes / 60;
        Serial.printf("[%02lu:%02lu:%02lu.%03lu] ",
                      hours % 24,
                      minutes % 60,
                      seconds % 60,
                      ms % 1000);
    }

    // Print level and tag
    Serial.printf("[%s] [%s] ", getLevelString(level), tag);

    // Print message
    Serial.print(message);

    // Reset color if enabled
    if (_colors) {
        Serial.print(COLOR_RESET);
    }

    Serial.println();
}

void Logger::hexdump(LogLevel level, const char* tag, const uint8_t* data, size_t len) {
    if (_level < level) return;

    // Print header
    log(level, tag, "Hexdump (%u bytes):", len);

    // Print hex lines (16 bytes per line)
    for (size_t i = 0; i < len; i += 16) {
        char line[80];
        char* ptr = line;

        // Offset
        ptr += sprintf(ptr, "%04X: ", (unsigned int)i);

        // Hex bytes
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) {
                ptr += sprintf(ptr, "%02X ", data[i + j]);
            } else {
                ptr += sprintf(ptr, "   ");
            }
            if (j == 7) {
                ptr += sprintf(ptr, " ");
            }
        }

        // ASCII representation
        ptr += sprintf(ptr, " |");
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            char c = data[i + j];
            *ptr++ = (c >= 32 && c < 127) ? c : '.';
        }
        *ptr++ = '|';
        *ptr = '\0';

        // Print line without timestamp/level prefix for cleaner output
        if (_colors) {
            Serial.print(getLevelColor(level));
        }
        Serial.print("       ");  // Indent to align with messages
        Serial.print(line);
        if (_colors) {
            Serial.print(COLOR_RESET);
        }
        Serial.println();
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
        case LogLevel::ERROR:   return COLOR_RED;
        case LogLevel::WARN:    return COLOR_YELLOW;
        case LogLevel::INFO:    return COLOR_GREEN;
        case LogLevel::DEBUG:   return COLOR_CYAN;
        case LogLevel::VERBOSE: return COLOR_MAGENTA;
        default:                return COLOR_RESET;
    }
}

} // namespace iwmp
