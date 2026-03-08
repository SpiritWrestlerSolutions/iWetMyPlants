/**
 * @file api_endpoints.h
 * @brief REST API endpoint handlers for iWetMyPlants web interface
 *
 * Provides JSON API endpoints for:
 * - Device status and information
 * - Sensor readings and calibration
 * - Configuration management
 * - Relay control (Greenhouse)
 * - Paired device management (Hub)
 * - System operations (reboot, info)
 */

#pragma once

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "../config/config_schema.h"

namespace iwmp {

// Forward declarations
class WebServer;

/**
 * @brief Sensor data provider callback
 *
 * Called when API needs current sensor readings.
 * @param index Sensor index (0-based)
 * @param raw Output: raw ADC value
 * @param percent Output: moisture percentage
 * @return true if sensor exists and reading is valid
 */
using SensorDataCallback = std::function<bool(uint8_t index, uint16_t& raw, uint8_t& percent)>;

/**
 * @brief Extended sensor status info
 */
struct SensorStatusInfo {
    bool exists;            // Sensor object exists
    bool ready;             // Sensor is ready to read
    bool hw_connected;      // Hardware is connected (I2C device responding, etc.)
    const char* input_type; // Input type name ("ADS1115", "DirectADC", etc.)
    uint8_t ads_channel;    // ADS1115 channel (0-3), only valid for ADS1115
    uint8_t ads_address;    // ADS1115 I2C address, only valid for ADS1115
};

/**
 * @brief Sensor status provider callback
 *
 * Called when API needs extended sensor status.
 * @param index Sensor index (0-based)
 * @param status Output: sensor status info
 * @return true if status was populated
 */
using SensorStatusCallback = std::function<bool(uint8_t index, SensorStatusInfo& status)>;

/**
 * @brief Relay state provider callback
 *
 * Called when API needs current relay states.
 * @param index Relay index (0-based)
 * @param state Output: relay on/off state
 * @return true if relay exists
 */
using RelayStateCallback = std::function<bool(uint8_t index, bool& state)>;

/**
 * @brief Relay control callback for API
 *
 * Called when API receives relay control command.
 * @param index Relay index (0-based)
 * @param state Desired state (true = on)
 * @return true if command was executed
 */
using ApiRelayControlCallback = std::function<bool(uint8_t index, bool state)>;

/**
 * @brief Calibration action callback for API
 *
 * Called when API receives calibration command.
 * @param sensor_index Sensor to calibrate
 * @param action "dry" or "wet"
 * @return true if calibration was performed
 */
using ApiCalibrationCallback = std::function<bool(uint8_t sensor_index, const char* action)>;

/**
 * @brief Paired device info for Hub
 */
struct PairedDeviceInfo {
    uint8_t mac[6];
    char name[32];
    uint8_t device_type;
    int8_t rssi;
    uint32_t last_seen;     // millis() of last message
    bool online;
    // Cached sensor readings
    uint8_t moisture_percent;
    float temperature;
    float humidity;
    uint8_t battery_percent;
};

/**
 * @brief Paired devices provider callback (Hub only)
 */
using PairedDevicesCallback = std::function<size_t(PairedDeviceInfo* devices, size_t max_count)>;

/**
 * @brief Delete device callback (Hub only)
 * @param index List index of the device to remove
 * @return true if device was found and removed
 */
using DeleteDeviceCallback = std::function<bool(uint8_t index)>;

/**
 * @brief Environmental data provider callback (Greenhouse only)
 */
using EnvironmentalDataCallback = std::function<bool(float& temperature, float& humidity, const char*& sensor_type)>;

/**
 * @brief REST API endpoint handlers
 *
 * Registers and handles all /api/* routes.
 * Works with WebServer to provide JSON responses.
 */
class ApiEndpoints {
public:
    /**
     * @brief Register all API routes with the web server
     * @param server AsyncWebServer instance
     */
    static void registerRoutes(AsyncWebServer& server);

    // ============ Data Provider Callbacks ============

    /**
     * @brief Set sensor data provider
     */
    static void onSensorData(SensorDataCallback callback);

    /**
     * @brief Set sensor status provider
     */
    static void onSensorStatus(SensorStatusCallback callback);

    /**
     * @brief Set relay state provider
     */
    static void onRelayState(RelayStateCallback callback);

    /**
     * @brief Set relay control handler
     */
    static void onRelayControl(ApiRelayControlCallback callback);

    /**
     * @brief Set calibration handler
     */
    static void onCalibration(ApiCalibrationCallback callback);

    /**
     * @brief Set paired devices provider (Hub only)
     */
    static void onPairedDevices(PairedDevicesCallback callback);

    /**
     * @brief Set delete device handler (Hub only)
     */
    static void onDeleteDevice(DeleteDeviceCallback callback);

    /**
     * @brief Set environmental data provider (Greenhouse only)
     */
    static void onEnvironmentalData(EnvironmentalDataCallback callback);

    // ============ Response Helpers ============

    /**
     * @brief Send JSON success response
     */
    static void sendSuccess(AsyncWebServerRequest* request, const char* message = nullptr);

    /**
     * @brief Send JSON error response
     */
    static void sendError(AsyncWebServerRequest* request, int code, const char* message);

    /**
     * @brief Send JSON document response
     */
    static void sendJson(AsyncWebServerRequest* request, JsonDocument& doc, int code = 200);

private:
    // Callback storage
    static SensorDataCallback s_sensor_callback;
    static SensorStatusCallback s_sensor_status_callback;
    static RelayStateCallback s_relay_state_callback;
    static ApiRelayControlCallback s_relay_control_callback;
    static ApiCalibrationCallback s_calibration_callback;
    static PairedDevicesCallback s_paired_devices_callback;
    static DeleteDeviceCallback s_delete_device_callback;
    static EnvironmentalDataCallback s_env_callback;

    // ============ Endpoint Handlers ============

    // Status & Info
    static void handleGetStatus(AsyncWebServerRequest* request);
    static void handleGetSystemInfo(AsyncWebServerRequest* request);
    static void handlePostReboot(AsyncWebServerRequest* request);

    // OTA
    static void handlePostOta(AsyncWebServerRequest* request);
    static void handleOtaUpload(AsyncWebServerRequest* request, const String& filename,
                                size_t index, uint8_t* data, size_t len, bool final);

    // Sensors
    static void handleGetSensors(AsyncWebServerRequest* request);
    static void handleGetSensor(AsyncWebServerRequest* request, uint8_t index);
    static void handlePostCalibrate(AsyncWebServerRequest* request, uint8_t index);

    // Configuration
    static void handleGetConfig(AsyncWebServerRequest* request);
    static void handlePostConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    static void handleGetConfigSection(AsyncWebServerRequest* request, const String& section);
    static void handlePostConfigSection(AsyncWebServerRequest* request, const String& section,
                                        uint8_t* data, size_t len);

    // Relays (Greenhouse)
    static void handleGetRelays(AsyncWebServerRequest* request);
    static void handlePostRelay(AsyncWebServerRequest* request, uint8_t index,
                                uint8_t* data, size_t len);

    // Paired Devices (Hub)
    static void handleGetDevices(AsyncWebServerRequest* request);
    static void handleDeleteDevice(AsyncWebServerRequest* request, uint8_t index);

    // WiFi
    static void handleGetWifiNetworks(AsyncWebServerRequest* request);
    static void handlePostWifiConnect(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    // ============ JSON Builders ============

    static void buildStatusJson(JsonDocument& doc);
    static void buildSystemInfoJson(JsonDocument& doc);
    static void buildSensorJson(JsonObject& obj, uint8_t index);
    static void buildRelayJson(JsonObject& obj, uint8_t index);
    static void buildConfigJson(JsonDocument& doc);
    static void buildWifiConfigJson(JsonObject& obj);
    static void buildMqttConfigJson(JsonObject& obj);
    static void buildSensorConfigJson(JsonArray& arr);
    static void buildRelayConfigJson(JsonArray& arr);

    // ============ JSON Parsers ============

    static bool parseWifiConfig(JsonObject& obj);
    static bool parseMqttConfig(JsonObject& obj);
    static bool parseSensorConfig(JsonArray& arr);
    static bool parseRelayConfig(JsonArray& arr);
};

} // namespace iwmp
