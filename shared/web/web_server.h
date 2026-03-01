/**
 * @file web_server.h
 * @brief Async web server for iWetMyPlants configuration and monitoring
 *
 * Provides a unified web interface for all device types with:
 * - Device-specific dashboard
 * - Configuration pages
 * - REST API endpoints
 * - WebSocket for real-time updates
 */

#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>
#include "config_schema.h"

namespace iwmp {

// Web server constants
static constexpr uint16_t WEB_SERVER_PORT = 80;
static constexpr size_t WEB_JSON_BUFFER_SIZE = 4096;

// Forward declarations
class ApiEndpoints;

/**
 * @brief Callback types for device-specific handlers
 */
using StatusCallback = std::function<void(JsonDocument& doc)>;
using SensorReadCallback = std::function<void(uint8_t index, JsonDocument& doc)>;
using RelayControlCallback = std::function<bool(uint8_t index, bool state)>;
using CalibrationCallback = std::function<bool(uint8_t sensor_index, const char* action)>;
using ConfigUpdateCallback = std::function<bool(const JsonDocument& doc)>;
using RebootCallback = std::function<void()>;

/**
 * @brief Web server manager
 */
class WebServer {
public:
    /**
     * @brief Get singleton instance
     */
    static WebServer& getInstance();

    // Delete copy/move
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    /**
     * @brief Initialize web server
     * @param identity Device identity for UI customization
     * @return true if successful
     */
    bool begin(const DeviceIdentity& identity);

    /**
     * @brief Stop web server
     */
    void end();

    /**
     * @brief Check if server is running
     */
    bool isRunning() const { return _running; }

    /**
     * @brief Periodic update (call from main loop)
     * Handles OTA reboot checks and other periodic tasks
     */
    void update();

    /**
     * @brief Get underlying AsyncWebServer
     */
    AsyncWebServer* getServer() { return _server; }

    // ============ Route Registration ============

    /**
     * @brief Register all standard routes
     */
    void registerRoutes();

    /**
     * @brief Add custom route
     */
    void addRoute(const char* uri, WebRequestMethodComposite method,
                  ArRequestHandlerFunction handler);

    /**
     * @brief Add route with body handler
     */
    void addRouteWithBody(const char* uri, WebRequestMethodComposite method,
                          ArRequestHandlerFunction handler,
                          ArBodyHandlerFunction bodyHandler);

    // ============ Device-Specific Callbacks ============

    /**
     * @brief Set callback for status endpoint
     */
    void onStatus(StatusCallback callback) { _status_callback = callback; }

    /**
     * @brief Set callback for sensor readings
     */
    void onSensorRead(SensorReadCallback callback) { _sensor_callback = callback; }

    /**
     * @brief Set callback for relay control
     */
    void onRelayControl(RelayControlCallback callback) { _relay_callback = callback; }

    /**
     * @brief Set callback for calibration
     */
    void onCalibration(CalibrationCallback callback) { _calibration_callback = callback; }

    /**
     * @brief Set callback for config updates
     */
    void onConfigUpdate(ConfigUpdateCallback callback) { _config_callback = callback; }

    /**
     * @brief Set callback for reboot
     */
    void onReboot(RebootCallback callback) { _reboot_callback = callback; }

    // ============ WebSocket ============

    /**
     * @brief Get WebSocket for real-time updates
     */
    AsyncWebSocket* getWebSocket() { return _ws; }

    /**
     * @brief Broadcast message to all WebSocket clients
     */
    void broadcastWs(const char* message);

    /**
     * @brief Broadcast JSON to all WebSocket clients
     */
    void broadcastWsJson(const JsonDocument& doc);

    /**
     * @brief Send rapid sensor reading to calibration clients
     */
    void sendRapidReading(uint8_t sensor_index, uint16_t raw, uint16_t avg, uint8_t percent);

    // ============ Helpers ============

    /**
     * @brief Send JSON response
     */
    static void sendJson(AsyncWebServerRequest* request, int code, const JsonDocument& doc);

    /**
     * @brief Send error response
     */
    static void sendError(AsyncWebServerRequest* request, int code, const char* message);

    /**
     * @brief Send success response
     */
    static void sendSuccess(AsyncWebServerRequest* request, const char* message = "OK");

    /**
     * @brief Check if request has valid JSON body
     */
    static bool parseJsonBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                              JsonDocument& doc, String& error);

    // ============ Access from handlers ============

    StatusCallback getStatusCallback() { return _status_callback; }
    SensorReadCallback getSensorCallback() { return _sensor_callback; }
    RelayControlCallback getRelayCallback() { return _relay_callback; }
    CalibrationCallback getCalibrationCallback() { return _calibration_callback; }
    ConfigUpdateCallback getConfigCallback() { return _config_callback; }
    RebootCallback getRebootCallback() { return _reboot_callback; }
    const DeviceIdentity& getIdentity() const { return _identity; }

private:
    WebServer();
    ~WebServer();

    AsyncWebServer* _server = nullptr;
    AsyncWebSocket* _ws = nullptr;
    DeviceIdentity _identity;
    bool _running = false;

    // Callbacks
    StatusCallback _status_callback = nullptr;
    SensorReadCallback _sensor_callback = nullptr;
    RelayControlCallback _relay_callback = nullptr;
    CalibrationCallback _calibration_callback = nullptr;
    ConfigUpdateCallback _config_callback = nullptr;
    RebootCallback _reboot_callback = nullptr;

    // WebSocket event handler
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len);

    // Static pages
    void registerStaticRoutes();

    // API endpoints (delegated to ApiEndpoints class)
    void registerApiRoutes();
};

// Global web server accessor
extern WebServer& Web;

} // namespace iwmp
