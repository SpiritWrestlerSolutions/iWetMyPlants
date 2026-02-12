/**
 * @file error_codes.h
 * @brief Standardized error codes and error handling utilities
 *
 * Provides consistent error codes across the iWetMyPlants system
 * for better debugging and error reporting.
 */

#pragma once

#include <Arduino.h>

namespace iwmp {

/**
 * @brief System-wide error codes
 */
enum class ErrorCode : uint16_t {
    // Success (0)
    OK = 0,

    // General errors (1-99)
    UNKNOWN = 1,
    INVALID_PARAMETER = 2,
    OUT_OF_MEMORY = 3,
    TIMEOUT = 4,
    NOT_INITIALIZED = 5,
    ALREADY_INITIALIZED = 6,
    OPERATION_FAILED = 7,
    NOT_SUPPORTED = 8,
    PERMISSION_DENIED = 9,

    // Configuration errors (100-199)
    CONFIG_LOAD_FAILED = 100,
    CONFIG_SAVE_FAILED = 101,
    CONFIG_INVALID = 102,
    CONFIG_VERSION_MISMATCH = 103,
    CONFIG_CORRUPTED = 104,

    // WiFi/Network errors (200-299)
    NET_NOT_CONNECTED = 200,
    NET_CONNECTION_FAILED = 201,
    NET_INVALID_CREDENTIALS = 202,
    NET_NO_SSID = 203,
    NET_AP_FAILED = 204,
    NET_SCAN_ERROR = 205,

    // MQTT errors (300-399)
    MQTT_NOT_CONNECTED = 300,
    MQTT_CONNECTION_FAILED = 301,
    MQTT_SUBSCRIBE_FAILED = 302,
    MQTT_PUBLISH_FAILED = 303,
    MQTT_INVALID_TOPIC = 304,
    MQTT_MESSAGE_TOO_LARGE = 305,

    // ESP-NOW errors (400-499)
    ESPNOW_INIT_FAILED = 400,
    ESPNOW_ADD_PEER_FAILED = 401,
    ESPNOW_SEND_FAILED = 402,
    ESPNOW_RECEIVE_FAILED = 403,
    ESPNOW_PEER_NOT_FOUND = 404,
    ESPNOW_ENCRYPTION_FAILED = 405,

    // Sensor errors (500-599)
    SENSOR_NOT_FOUND = 500,
    SENSOR_READ_FAILED = 501,
    SENSOR_CALIBRATION_FAILED = 502,
    SENSOR_INVALID_INDEX = 503,
    SENSOR_DISABLED = 504,
    SENSOR_I2C_ERROR = 505,
    SENSOR_ADC_ERROR = 506,

    // Relay errors (600-699)
    RELAY_NOT_FOUND = 600,
    RELAY_DISABLED = 601,
    RELAY_LOCKED_OUT = 602,
    RELAY_SAFETY_TIMEOUT = 603,
    RELAY_COOLDOWN_ACTIVE = 604,
    RELAY_DAILY_LIMIT = 605,
    RELAY_INVALID_INDEX = 606,

    // OTA errors (700-799)
    OTA_ALREADY_IN_PROGRESS = 700,
    OTA_NOT_ENOUGH_SPACE = 701,
    OTA_WRITE_FAILED = 702,
    OTA_VERIFY_FAILED = 703,
    OTA_CANCELLED = 704,
    OTA_INVALID_FIRMWARE = 705,

    // Web server errors (800-899)
    WEB_SERVER_FAILED = 800,
    WEB_ROUTE_NOT_FOUND = 801,
    WEB_INVALID_JSON = 802,
    WEB_MISSING_PARAMETER = 803,
    WEB_WEBSOCKET_ERROR = 804,

    // Automation errors (900-999)
    AUTOMATION_INVALID_BINDING = 900,
    AUTOMATION_SENSOR_UNAVAILABLE = 901,
    AUTOMATION_RELAY_UNAVAILABLE = 902,
    AUTOMATION_THRESHOLD_INVALID = 903,
};

/**
 * @brief Error severity levels
 */
enum class ErrorSeverity : uint8_t {
    INFO = 0,      // Informational, not an error
    WARNING = 1,   // Warning, operation continued
    ERROR = 2,     // Error, operation failed
    CRITICAL = 3,  // Critical, system may be unstable
    FATAL = 4      // Fatal, system should restart
};

/**
 * @brief Error information structure
 */
struct ErrorInfo {
    ErrorCode code;
    ErrorSeverity severity;
    const char* message;
    uint32_t timestamp;  // millis() when error occurred
    uint8_t count;       // Number of occurrences
};

/**
 * @brief Get human-readable error message for error code
 * @param code Error code
 * @return Error message string
 */
inline const char* getErrorMessage(ErrorCode code) {
    switch (code) {
        case ErrorCode::OK:                     return "OK";
        case ErrorCode::UNKNOWN:                return "Unknown error";
        case ErrorCode::INVALID_PARAMETER:      return "Invalid parameter";
        case ErrorCode::OUT_OF_MEMORY:          return "Out of memory";
        case ErrorCode::TIMEOUT:                return "Operation timed out";
        case ErrorCode::NOT_INITIALIZED:        return "Not initialized";
        case ErrorCode::ALREADY_INITIALIZED:    return "Already initialized";
        case ErrorCode::OPERATION_FAILED:       return "Operation failed";
        case ErrorCode::NOT_SUPPORTED:          return "Not supported";
        case ErrorCode::PERMISSION_DENIED:      return "Permission denied";

        case ErrorCode::CONFIG_LOAD_FAILED:     return "Config load failed";
        case ErrorCode::CONFIG_SAVE_FAILED:     return "Config save failed";
        case ErrorCode::CONFIG_INVALID:         return "Invalid configuration";
        case ErrorCode::CONFIG_VERSION_MISMATCH: return "Config version mismatch";
        case ErrorCode::CONFIG_CORRUPTED:       return "Config corrupted";

        case ErrorCode::NET_NOT_CONNECTED:      return "WiFi not connected";
        case ErrorCode::NET_CONNECTION_FAILED:  return "WiFi connection failed";
        case ErrorCode::NET_INVALID_CREDENTIALS: return "Invalid WiFi credentials";
        case ErrorCode::NET_NO_SSID:            return "No SSID configured";
        case ErrorCode::NET_AP_FAILED:          return "AP mode failed";
        case ErrorCode::NET_SCAN_ERROR:         return "WiFi scan failed";

        case ErrorCode::MQTT_NOT_CONNECTED:     return "MQTT not connected";
        case ErrorCode::MQTT_CONNECTION_FAILED: return "MQTT connection failed";
        case ErrorCode::MQTT_SUBSCRIBE_FAILED:  return "MQTT subscribe failed";
        case ErrorCode::MQTT_PUBLISH_FAILED:    return "MQTT publish failed";
        case ErrorCode::MQTT_INVALID_TOPIC:     return "Invalid MQTT topic";
        case ErrorCode::MQTT_MESSAGE_TOO_LARGE: return "MQTT message too large";

        case ErrorCode::ESPNOW_INIT_FAILED:     return "ESP-NOW init failed";
        case ErrorCode::ESPNOW_ADD_PEER_FAILED: return "ESP-NOW add peer failed";
        case ErrorCode::ESPNOW_SEND_FAILED:     return "ESP-NOW send failed";
        case ErrorCode::ESPNOW_RECEIVE_FAILED:  return "ESP-NOW receive failed";
        case ErrorCode::ESPNOW_PEER_NOT_FOUND:  return "ESP-NOW peer not found";
        case ErrorCode::ESPNOW_ENCRYPTION_FAILED: return "ESP-NOW encryption failed";

        case ErrorCode::SENSOR_NOT_FOUND:       return "Sensor not found";
        case ErrorCode::SENSOR_READ_FAILED:     return "Sensor read failed";
        case ErrorCode::SENSOR_CALIBRATION_FAILED: return "Sensor calibration failed";
        case ErrorCode::SENSOR_INVALID_INDEX:   return "Invalid sensor index";
        case ErrorCode::SENSOR_DISABLED:        return "Sensor disabled";
        case ErrorCode::SENSOR_I2C_ERROR:       return "Sensor I2C error";
        case ErrorCode::SENSOR_ADC_ERROR:       return "Sensor ADC error";

        case ErrorCode::RELAY_NOT_FOUND:        return "Relay not found";
        case ErrorCode::RELAY_DISABLED:         return "Relay disabled";
        case ErrorCode::RELAY_LOCKED_OUT:       return "Relay locked out";
        case ErrorCode::RELAY_SAFETY_TIMEOUT:   return "Relay safety timeout";
        case ErrorCode::RELAY_COOLDOWN_ACTIVE:  return "Relay cooldown active";
        case ErrorCode::RELAY_DAILY_LIMIT:      return "Relay daily limit reached";
        case ErrorCode::RELAY_INVALID_INDEX:    return "Invalid relay index";

        case ErrorCode::OTA_ALREADY_IN_PROGRESS: return "OTA already in progress";
        case ErrorCode::OTA_NOT_ENOUGH_SPACE:   return "Not enough space for OTA";
        case ErrorCode::OTA_WRITE_FAILED:       return "OTA write failed";
        case ErrorCode::OTA_VERIFY_FAILED:      return "OTA verification failed";
        case ErrorCode::OTA_CANCELLED:          return "OTA cancelled";
        case ErrorCode::OTA_INVALID_FIRMWARE:   return "Invalid firmware image";

        case ErrorCode::WEB_SERVER_FAILED:      return "Web server failed";
        case ErrorCode::WEB_ROUTE_NOT_FOUND:    return "Route not found";
        case ErrorCode::WEB_INVALID_JSON:       return "Invalid JSON";
        case ErrorCode::WEB_MISSING_PARAMETER:  return "Missing parameter";
        case ErrorCode::WEB_WEBSOCKET_ERROR:    return "WebSocket error";

        case ErrorCode::AUTOMATION_INVALID_BINDING: return "Invalid automation binding";
        case ErrorCode::AUTOMATION_SENSOR_UNAVAILABLE: return "Automation sensor unavailable";
        case ErrorCode::AUTOMATION_RELAY_UNAVAILABLE: return "Automation relay unavailable";
        case ErrorCode::AUTOMATION_THRESHOLD_INVALID: return "Invalid threshold values";

        default: return "Unknown error code";
    }
}

/**
 * @brief Get HTTP status code for error code
 * @param code Error code
 * @return HTTP status code
 */
inline int getHttpStatus(ErrorCode code) {
    switch (code) {
        case ErrorCode::OK:
            return 200;

        case ErrorCode::INVALID_PARAMETER:
        case ErrorCode::CONFIG_INVALID:
        case ErrorCode::WEB_INVALID_JSON:
        case ErrorCode::WEB_MISSING_PARAMETER:
        case ErrorCode::AUTOMATION_THRESHOLD_INVALID:
            return 400;  // Bad Request

        case ErrorCode::PERMISSION_DENIED:
            return 403;  // Forbidden

        case ErrorCode::SENSOR_NOT_FOUND:
        case ErrorCode::RELAY_NOT_FOUND:
        case ErrorCode::ESPNOW_PEER_NOT_FOUND:
        case ErrorCode::WEB_ROUTE_NOT_FOUND:
        case ErrorCode::SENSOR_INVALID_INDEX:
        case ErrorCode::RELAY_INVALID_INDEX:
            return 404;  // Not Found

        case ErrorCode::RELAY_LOCKED_OUT:
        case ErrorCode::RELAY_COOLDOWN_ACTIVE:
        case ErrorCode::OTA_ALREADY_IN_PROGRESS:
            return 409;  // Conflict

        case ErrorCode::RELAY_DAILY_LIMIT:
            return 429;  // Too Many Requests

        case ErrorCode::NOT_SUPPORTED:
        case ErrorCode::SENSOR_DISABLED:
        case ErrorCode::RELAY_DISABLED:
            return 501;  // Not Implemented

        case ErrorCode::NET_NOT_CONNECTED:
        case ErrorCode::MQTT_NOT_CONNECTED:
            return 503;  // Service Unavailable

        default:
            return 500;  // Internal Server Error
    }
}

} // namespace iwmp
