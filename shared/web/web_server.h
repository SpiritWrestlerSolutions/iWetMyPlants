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
using CalibrationCallback = std::function<bool(uint8_t sensor_index, const char* action)>;

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
     * @brief Set callback for calibration
     */
    void onCalibration(CalibrationCallback callback) { _calibration_callback = callback; }

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

    // ============ Access from handlers ============

    const DeviceIdentity& getIdentity() const { return _identity; }

private:
    WebServer();
    ~WebServer();

    AsyncWebServer* _server = nullptr;
    AsyncWebSocket* _ws = nullptr;
    DeviceIdentity _identity;
    bool _running = false;

    // Callbacks
    CalibrationCallback _calibration_callback = nullptr;

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
