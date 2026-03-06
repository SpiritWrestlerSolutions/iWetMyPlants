/**
 * @file remote_web.h
 * @brief Lightweight web server for Remote device
 *
 * Single AsyncWebServer with ~18 routes serving ~10KB of HTML.
 * Created once at boot, never destroyed.
 * Binds 0.0.0.0:80 — works on both AP and STA interfaces.
 */

#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

namespace iwmp {

class RemoteController;

class RemoteWeb {
public:
    bool begin(RemoteController* controller);
    bool isRunning() const { return _running; }

private:
    AsyncWebServer* _server = nullptr;
    RemoteController* _ctrl = nullptr;
    bool _running = false;

    void registerRoutes();

    // API handlers
    void handleGetStatus(AsyncWebServerRequest* request);
    void handleGetConfig(AsyncWebServerRequest* request);
    void handlePostWifiConnect(AsyncWebServerRequest* request,
                               uint8_t* data, size_t len,
                               size_t index, size_t total);
    void handleGetWifiNetworks(AsyncWebServerRequest* request);
    void handlePostMqttConfig(AsyncWebServerRequest* request,
                               uint8_t* data, size_t len,
                               size_t index, size_t total);
    void handlePostSensorConfig(AsyncWebServerRequest* request,
                                 uint8_t* data, size_t len,
                                 size_t index, size_t total);
    void handlePostModeConfig(AsyncWebServerRequest* request,
                               uint8_t* data, size_t len,
                               size_t index, size_t total);
    void handlePostReboot(AsyncWebServerRequest* request);
    void handlePostReturnMode(AsyncWebServerRequest* request);
    void handlePostEspNowImport(AsyncWebServerRequest* request, uint8_t* data, size_t len);
};

// HTML pages (PROGMEM, defined in remote_web.cpp)
extern const char REMOTE_STATUS_HTML[] PROGMEM;
extern const char REMOTE_SETTINGS_HTML[] PROGMEM;

} // namespace iwmp
