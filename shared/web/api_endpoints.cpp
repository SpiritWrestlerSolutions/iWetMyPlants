/**
 * @file api_endpoints.cpp
 * @brief REST API endpoint handlers implementation
 */

#include "api_endpoints.h"
#include "../utils/logger.h"
#include "../config/config_manager.h"
#include "../utils/ota_manager.h"
#include "../utils/error_tracker.h"
#include <WiFi.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <esp_mac.h>

namespace iwmp {

static constexpr const char* TAG = "API";

// Static callback storage
SensorDataCallback ApiEndpoints::s_sensor_callback = nullptr;
SensorStatusCallback ApiEndpoints::s_sensor_status_callback = nullptr;
RelayStateCallback ApiEndpoints::s_relay_state_callback = nullptr;
ApiRelayControlCallback ApiEndpoints::s_relay_control_callback = nullptr;
ApiCalibrationCallback ApiEndpoints::s_calibration_callback = nullptr;
PairedDevicesCallback ApiEndpoints::s_paired_devices_callback = nullptr;
EnvironmentalDataCallback ApiEndpoints::s_env_callback = nullptr;
DeleteDeviceCallback ApiEndpoints::s_delete_device_callback = nullptr;

// ============ Callback Registration ============

void ApiEndpoints::onSensorData(SensorDataCallback callback) {
    s_sensor_callback = callback;
}

void ApiEndpoints::onSensorStatus(SensorStatusCallback callback) {
    s_sensor_status_callback = callback;
}

void ApiEndpoints::onRelayState(RelayStateCallback callback) {
    s_relay_state_callback = callback;
}

void ApiEndpoints::onRelayControl(ApiRelayControlCallback callback) {
    s_relay_control_callback = callback;
}

void ApiEndpoints::onCalibration(ApiCalibrationCallback callback) {
    s_calibration_callback = callback;
}

void ApiEndpoints::onPairedDevices(PairedDevicesCallback callback) {
    s_paired_devices_callback = callback;
}

void ApiEndpoints::onEnvironmentalData(EnvironmentalDataCallback callback) {
    s_env_callback = callback;
}

void ApiEndpoints::onDeleteDevice(DeleteDeviceCallback callback) {
    s_delete_device_callback = callback;
}

// ============ Response Helpers ============

void ApiEndpoints::sendSuccess(AsyncWebServerRequest* request, const char* message) {
    JsonDocument doc;
    doc["success"] = true;
    if (message) {
        doc["message"] = message;
    }
    sendJson(request, doc);
}

void ApiEndpoints::sendError(AsyncWebServerRequest* request, int code, const char* message) {
    JsonDocument doc;
    doc["success"] = false;
    doc["error"] = message;
    sendJson(request, doc, code);
}

void ApiEndpoints::sendJson(AsyncWebServerRequest* request, JsonDocument& doc, int code) {
    String response;
    serializeJson(doc, response);
    request->send(code, "application/json", response);
}

// ============ Route Registration ============

void ApiEndpoints::registerRoutes(AsyncWebServer& server) {
    LOG_I(TAG, "Registering API routes...");

    // Status & System
    server.on("/api/status", HTTP_GET, handleGetStatus);
    server.on("/api/system/info", HTTP_GET, handleGetSystemInfo);
    server.on("/api/system/reboot", HTTP_POST, handlePostReboot);

    // OTA Progress endpoint
    server.on("/api/system/ota/progress", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        const auto& progress = Ota.getProgress();

        doc["state"] = static_cast<int>(progress.state);
        doc["state_name"] = progress.state == OtaState::IDLE ? "idle" :
                           progress.state == OtaState::RECEIVING ? "receiving" :
                           progress.state == OtaState::VERIFYING ? "verifying" :
                           progress.state == OtaState::COMPLETE ? "complete" : "error";
        doc["total_size"] = progress.total_size;
        doc["received"] = progress.received;
        doc["percent"] = progress.percent;

        if (progress.state == OtaState::ERROR) {
            doc["error"] = progress.error_message;
        }

        sendJson(request, doc);
    });

    // Error history endpoint
    server.on("/api/system/errors", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;

        doc["total_errors"] = Errors.totalErrors();
        doc["recent_count"] = Errors.count();
        doc["has_critical"] = Errors.hasCriticalErrors();
        doc["time_since_last_ms"] = Errors.timeSinceLastError();

        JsonArray errors = doc["errors"].to<JsonArray>();
        for (size_t i = 0; i < Errors.count(); i++) {
            const ErrorRecord* rec = Errors.getRecord(i);
            if (rec) {
                JsonObject err = errors.add<JsonObject>();
                err["code"] = static_cast<int>(rec->code);
                err["severity"] = static_cast<int>(rec->severity);
                err["message"] = getErrorMessage(rec->code);
                err["context"] = rec->context;
                err["line"] = rec->line;
                err["timestamp_ms"] = rec->timestamp;
            }
        }

        sendJson(request, doc);
    });

    // Clear errors endpoint
    server.on("/api/system/errors/clear", HTTP_POST, [](AsyncWebServerRequest* request) {
        Errors.clear();
        sendSuccess(request, "Error history cleared");
    });

    // Sensors
    server.on("/api/sensors", HTTP_GET, handleGetSensors);

    // Sensor by index - explicit routes for 0-15
    static const char* sensor_paths[] = {
        "/api/sensors/0", "/api/sensors/1", "/api/sensors/2", "/api/sensors/3",
        "/api/sensors/4", "/api/sensors/5", "/api/sensors/6", "/api/sensors/7",
        "/api/sensors/8", "/api/sensors/9", "/api/sensors/10", "/api/sensors/11",
        "/api/sensors/12", "/api/sensors/13", "/api/sensors/14", "/api/sensors/15"
    };
    for (int i = 0; i < 16; i++) {
        server.on(sensor_paths[i], HTTP_GET, [](AsyncWebServerRequest* request) {
            String url = request->url();
            uint8_t idx = url.substring(url.lastIndexOf('/') + 1).toInt();
            handleGetSensor(request, idx);
        });
    }

    // Calibration endpoints - explicit routes for sensors 0-15, dry/wet actions
    static const char* cal_dry_paths[] = {
        "/api/sensors/0/calibrate/dry", "/api/sensors/1/calibrate/dry",
        "/api/sensors/2/calibrate/dry", "/api/sensors/3/calibrate/dry",
        "/api/sensors/4/calibrate/dry", "/api/sensors/5/calibrate/dry",
        "/api/sensors/6/calibrate/dry", "/api/sensors/7/calibrate/dry",
        "/api/sensors/8/calibrate/dry", "/api/sensors/9/calibrate/dry",
        "/api/sensors/10/calibrate/dry", "/api/sensors/11/calibrate/dry",
        "/api/sensors/12/calibrate/dry", "/api/sensors/13/calibrate/dry",
        "/api/sensors/14/calibrate/dry", "/api/sensors/15/calibrate/dry"
    };
    static const char* cal_wet_paths[] = {
        "/api/sensors/0/calibrate/wet", "/api/sensors/1/calibrate/wet",
        "/api/sensors/2/calibrate/wet", "/api/sensors/3/calibrate/wet",
        "/api/sensors/4/calibrate/wet", "/api/sensors/5/calibrate/wet",
        "/api/sensors/6/calibrate/wet", "/api/sensors/7/calibrate/wet",
        "/api/sensors/8/calibrate/wet", "/api/sensors/9/calibrate/wet",
        "/api/sensors/10/calibrate/wet", "/api/sensors/11/calibrate/wet",
        "/api/sensors/12/calibrate/wet", "/api/sensors/13/calibrate/wet",
        "/api/sensors/14/calibrate/wet", "/api/sensors/15/calibrate/wet"
    };

    for (int i = 0; i < 16; i++) {
        server.on(cal_dry_paths[i], HTTP_POST, [](AsyncWebServerRequest* request) {
            String url = request->url();
            // Extract sensor index from URL like /api/sensors/X/calibrate/dry
            int start = 13; // length of "/api/sensors/"
            int end = url.indexOf('/', start);
            uint8_t sensor_idx = url.substring(start, end).toInt();

            if (s_calibration_callback) {
                if (s_calibration_callback(sensor_idx, "dry")) {
                    sendSuccess(request, "Dry calibration point set");
                } else {
                    sendError(request, 400, "Calibration failed");
                }
            } else {
                sendError(request, 501, "Calibration not available");
            }
        });

        server.on(cal_wet_paths[i], HTTP_POST, [](AsyncWebServerRequest* request) {
            String url = request->url();
            int start = 13;
            int end = url.indexOf('/', start);
            uint8_t sensor_idx = url.substring(start, end).toInt();

            if (s_calibration_callback) {
                if (s_calibration_callback(sensor_idx, "wet")) {
                    sendSuccess(request, "Wet calibration point set");
                } else {
                    sendError(request, 400, "Calibration failed");
                }
            } else {
                sendError(request, 501, "Calibration not available");
            }
        });
    }

    // Config sections - register SPECIFIC routes FIRST (before general /api/config)
    server.on("/api/config/wifi", HTTP_GET, [](AsyncWebServerRequest* request) {
        handleGetConfigSection(request, "wifi");
    });
    server.on("/api/config/wifi", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handlePostConfigSection(request, "wifi", data, len);
        }
    );

    server.on("/api/config/mqtt", HTTP_GET, [](AsyncWebServerRequest* request) {
        handleGetConfigSection(request, "mqtt");
    });
    server.on("/api/config/mqtt", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handlePostConfigSection(request, "mqtt", data, len);
        }
    );

    server.on("/api/config/sensors", HTTP_GET, [](AsyncWebServerRequest* request) {
        LOG_D(TAG, "GET /api/config/sensors");
        handleGetConfigSection(request, "sensors");
    });
    server.on("/api/config/sensors", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            // Body handler sends response - main handler does nothing
        },
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            // Only process when we have all the data
            if (index + len == total) {
                handlePostConfigSection(request, "sensors", data, len);
            }
        }
    );

    server.on("/api/config/relays", HTTP_GET, [](AsyncWebServerRequest* request) {
        handleGetConfigSection(request, "relays");
    });
    server.on("/api/config/relays", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            // Only process when we have all the data
            if (index + len == total) {
                handlePostConfigSection(request, "relays", data, len);
            }
        }
    );

    server.on("/api/config/espnow", HTTP_GET, [](AsyncWebServerRequest* request) {
        handleGetConfigSection(request, "espnow");
    });
    server.on("/api/config/espnow", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handlePostConfigSection(request, "espnow", data, len);
        }
    );

    server.on("/api/config/env_sensor", HTTP_GET, [](AsyncWebServerRequest* request) {
        handleGetConfigSection(request, "env_sensor");
    });
    server.on("/api/config/env_sensor", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handlePostConfigSection(request, "env_sensor", data, len);
        }
    );

    // General /api/config routes - register AFTER specific section routes
    server.on("/api/config", HTTP_GET, handleGetConfig);

    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handlePostConfig(request, data, len);
        }
    );

    // Relays (Greenhouse) - explicit routes
    server.on("/api/relays", HTTP_GET, handleGetRelays);

    // Relay control routes 0-7
    static const char* relay_paths[] = {
        "/api/relays/0", "/api/relays/1", "/api/relays/2", "/api/relays/3",
        "/api/relays/4", "/api/relays/5", "/api/relays/6", "/api/relays/7"
    };
    for (int i = 0; i < 8; i++) {
        server.on(relay_paths[i], HTTP_POST,
            [](AsyncWebServerRequest* request) {},
            nullptr,
            [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                String url = request->url();
                uint8_t relay_idx = url.substring(url.lastIndexOf('/') + 1).toInt();
                handlePostRelay(request, relay_idx, data, len);
            }
        );
    }

    // Paired Devices (Hub)
    server.on("/api/devices", HTTP_GET, handleGetDevices);

    // Device deletion routes 0-15
    static const char* device_paths[] = {
        "/api/devices/0", "/api/devices/1", "/api/devices/2", "/api/devices/3",
        "/api/devices/4", "/api/devices/5", "/api/devices/6", "/api/devices/7",
        "/api/devices/8", "/api/devices/9", "/api/devices/10", "/api/devices/11",
        "/api/devices/12", "/api/devices/13", "/api/devices/14", "/api/devices/15"
    };
    for (int i = 0; i < 16; i++) {
        server.on(device_paths[i], HTTP_DELETE,
            [](AsyncWebServerRequest* request) {
                String url = request->url();
                uint8_t device_idx = url.substring(url.lastIndexOf('/') + 1).toInt();
                handleDeleteDevice(request, device_idx);
            }
        );
    }

    // WiFi
    server.on("/api/wifi/networks", HTTP_GET, handleGetWifiNetworks);

    server.on("/api/wifi/connect", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handlePostWifiConnect(request, data, len);
        }
    );

    // Environmental sensor (Greenhouse only)
    server.on("/api/environment", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        float temp = NAN, hum = NAN;
        const char* stype = "None";
        if (s_env_callback && s_env_callback(temp, hum, stype)) {
            if (!isnan(temp)) doc["temperature"] = temp;
            if (!isnan(hum)) doc["humidity"] = hum;
            doc["sensor_type"] = stype;
            doc["valid"] = !isnan(temp) && !isnan(hum);
        } else {
            doc["valid"] = false;
            doc["sensor_type"] = "None";
        }
        sendJson(request, doc);
    });

        LOG_I(TAG, "Routes registered");
}

// ============ Status & System Handlers ============

void ApiEndpoints::handleGetStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    buildStatusJson(doc);
    sendJson(request, doc);
}

void ApiEndpoints::handleGetSystemInfo(AsyncWebServerRequest* request) {
    JsonDocument doc;
    buildSystemInfoJson(doc);
    sendJson(request, doc);
}

void ApiEndpoints::handlePostReboot(AsyncWebServerRequest* request) {
    sendSuccess(request, "Rebooting...");

    // Delay reboot to allow response to be sent
    delay(100);
    ESP.restart();
}

// ============ Sensor Handlers ============

void ApiEndpoints::handleGetSensors(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray sensors = doc["sensors"].to<JsonArray>();

    const auto& config = Config.getConfig();
    for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
        JsonObject sensor = sensors.add<JsonObject>();
        buildSensorJson(sensor, i);
    }

    doc["count"] = IWMP_MAX_SENSORS;
    sendJson(request, doc);
}

void ApiEndpoints::handleGetSensor(AsyncWebServerRequest* request, uint8_t index) {
    const auto& config = Config.getConfig();

    if (index >= IWMP_MAX_SENSORS) {
        sendError(request, 404, "Sensor not found");
        return;
    }

    JsonDocument doc;
    JsonObject sensor = doc.to<JsonObject>();
    buildSensorJson(sensor, index);
    sendJson(request, doc);
}

void ApiEndpoints::handlePostCalibrate(AsyncWebServerRequest* request, uint8_t index) {
    // Parse body for action
    // Body should be: {"action": "dry"} or {"action": "wet"}

    // Note: In async handler, body is already available
    // For simplicity, check URL parameter as fallback
    String action = "";
    if (request->hasParam("action")) {
        action = request->getParam("action")->value();
    }

    if (action.isEmpty()) {
        sendError(request, 400, "Missing action parameter (dry/wet)");
        return;
    }

    if (s_calibration_callback) {
        if (s_calibration_callback(index, action.c_str())) {
            sendSuccess(request, "Calibration point set");
        } else {
            sendError(request, 400, "Calibration failed");
        }
    } else {
        sendError(request, 501, "Calibration not available");
    }
}

// ============ Configuration Handlers ============

void ApiEndpoints::handleGetConfig(AsyncWebServerRequest* request) {
    JsonDocument doc;
    buildConfigJson(doc);
    sendJson(request, doc);
}

void ApiEndpoints::handlePostConfig(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    bool changed = false;

    // Parse each section if present
    if (doc.containsKey("wifi")) {
        JsonObject wifi = doc["wifi"];
        if (parseWifiConfig(wifi)) {
            changed = true;
        }
    }

    if (doc.containsKey("mqtt")) {
        JsonObject mqtt = doc["mqtt"];
        if (parseMqttConfig(mqtt)) {
            changed = true;
        }
    }

    if (doc.containsKey("sensors")) {
        JsonArray sensors = doc["sensors"];
        if (parseSensorConfig(sensors)) {
            changed = true;
        }
    }

    if (doc.containsKey("relays")) {
        JsonArray relays = doc["relays"];
        if (parseRelayConfig(relays)) {
            changed = true;
        }
    }

    if (changed) {
        Config.save();
        sendSuccess(request, "Configuration saved");
    } else {
        sendError(request, 400, "No valid configuration provided");
    }
}

void ApiEndpoints::handleGetConfigSection(AsyncWebServerRequest* request, const String& section) {
    JsonDocument doc;

    if (section == "wifi") {
        JsonObject obj = doc.to<JsonObject>();
        buildWifiConfigJson(obj);
    } else if (section == "mqtt") {
        JsonObject obj = doc.to<JsonObject>();
        buildMqttConfigJson(obj);
    } else if (section == "sensors") {
        JsonArray arr = doc.to<JsonArray>();
        buildSensorConfigJson(arr);
    } else if (section == "relays") {
        JsonArray arr = doc.to<JsonArray>();
        buildRelayConfigJson(arr);
    } else if (section == "espnow") {
        const auto& config = Config.getConfig();
        doc["channel"] = config.espnow.channel;
        doc["encryption"] = config.espnow.encryption_enabled;
    } else if (section == "env_sensor") {
        JsonObject obj = doc.to<JsonObject>();
        const auto& env = Config.getEnvSensor();
        obj["enabled"] = env.enabled;
        obj["sensor_type"] = static_cast<uint8_t>(env.sensor_type);
        obj["pin"] = env.pin;
        obj["i2c_address"] = env.i2c_address;
        obj["read_interval_sec"] = env.read_interval_sec;
    } else {
        sendError(request, 404, "Unknown config section");
        return;
    }

    sendJson(request, doc);
}

void ApiEndpoints::handlePostConfigSection(AsyncWebServerRequest* request, const String& section,
                                           uint8_t* data, size_t len) {
    LOG_D(TAG, "POST /api/config/%s (%d bytes)", section.c_str(), len);

    if (len == 0) {
        sendError(request, 400, "Empty request body");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        LOG_E(TAG, "JSON parse error: %s", error.c_str());
        sendError(request, 400, "Invalid JSON");
        return;
    }

    bool success = false;

    if (section == "wifi") {
        JsonObject obj = doc.as<JsonObject>();
        success = parseWifiConfig(obj);
    } else if (section == "mqtt") {
        JsonObject obj = doc.as<JsonObject>();
        success = parseMqttConfig(obj);
    } else if (section == "sensors") {
        if (!doc.is<JsonArray>()) {
            LOG_E(TAG, "Error: sensors data is not an array");
            sendError(request, 400, "Expected array for sensors");
            return;
        }
        JsonArray arr = doc.as<JsonArray>();
        LOG_D(TAG, "Saving sensors, array size: %d", arr.size());
        if (arr.size() == 0) {
            LOG_E(TAG, "Error: sensors array is empty");
            sendError(request, 400, "Empty sensors array");
            return;
        }
        success = parseSensorConfig(arr);
        LOG_D(TAG, "parseSensorConfig returned: %s", success ? "true" : "false");
    } else if (section == "relays") {
        if (!doc.is<JsonArray>()) {
            LOG_E(TAG, "Error: relays data is not an array");
            sendError(request, 400, "Expected array for relays");
            return;
        }
        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() == 0) {
            LOG_E(TAG, "Error: relays array is empty");
            sendError(request, 400, "Empty relays array");
            return;
        }
        success = parseRelayConfig(arr);
    } else if (section == "espnow") {
        auto& config = Config.getConfigMutable();
        if (doc.containsKey("channel")) {
            config.espnow.channel = doc["channel"];
            success = true;
        }
        if (doc.containsKey("encryption")) {
            config.espnow.encryption_enabled = doc["encryption"];
            success = true;
        }
    } else if (section == "env_sensor") {
        auto& env = Config.getEnvSensorMutable();
        if (doc.containsKey("enabled")) {
            env.enabled = doc["enabled"].as<bool>();
            success = true;
        }
        if (doc.containsKey("sensor_type")) {
            env.sensor_type = static_cast<EnvSensorType>(doc["sensor_type"].as<uint8_t>());
            success = true;
        }
        if (doc.containsKey("pin")) {
            env.pin = doc["pin"].as<uint8_t>();
            success = true;
        }
        if (doc.containsKey("i2c_address")) {
            env.i2c_address = doc["i2c_address"].as<uint8_t>();
            success = true;
        }
        if (doc.containsKey("read_interval_sec")) {
            env.read_interval_sec = doc["read_interval_sec"].as<uint16_t>();
            success = true;
        }
    }

    if (success) {
        Config.save();
        LOG_I(TAG, "Config saved successfully");
        sendSuccess(request, "Configuration saved");
    } else {
        LOG_E(TAG, "Failed to parse configuration");
        sendError(request, 400, "Failed to parse configuration");
    }
}

// ============ Relay Handlers ============

void ApiEndpoints::handleGetRelays(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray relays = doc["relays"].to<JsonArray>();

    const auto& config = Config.getConfig();
    for (uint8_t i = 0; i < IWMP_MAX_RELAYS; i++) {
        JsonObject relay = relays.add<JsonObject>();
        buildRelayJson(relay, i);
    }

    doc["count"] = IWMP_MAX_RELAYS;
    sendJson(request, doc);
}

void ApiEndpoints::handlePostRelay(AsyncWebServerRequest* request, uint8_t index,
                                   uint8_t* data, size_t len) {
    const auto& config = Config.getConfig();

    if (index >= IWMP_MAX_RELAYS) {
        sendError(request, 404, "Relay not found");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    if (!doc.containsKey("state")) {
        sendError(request, 400, "Missing 'state' field");
        return;
    }

    bool state = doc["state"].as<bool>();

    if (s_relay_control_callback) {
        if (s_relay_control_callback(index, state)) {
            sendSuccess(request, state ? "Relay ON" : "Relay OFF");
        } else {
            sendError(request, 500, "Failed to control relay");
        }
    } else {
        sendError(request, 501, "Relay control not available");
    }
}

// ============ Paired Device Handlers (Hub) ============

void ApiEndpoints::handleGetDevices(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray devices = doc["devices"].to<JsonArray>();

    if (s_paired_devices_callback) {
        PairedDeviceInfo device_list[16];
        size_t count = s_paired_devices_callback(device_list, 16);

        for (size_t i = 0; i < count; i++) {
            JsonObject device = devices.add<JsonObject>();

            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     device_list[i].mac[0], device_list[i].mac[1], device_list[i].mac[2],
                     device_list[i].mac[3], device_list[i].mac[4], device_list[i].mac[5]);

            device["mac"] = mac_str;
            device["name"] = device_list[i].name;
            device["type"] = device_list[i].device_type;
            device["rssi"] = device_list[i].rssi;
            device["online"] = device_list[i].online;
            device["last_seen"] = device_list[i].last_seen;
            device["moisture_percent"] = device_list[i].moisture_percent;
            if (!isnan(device_list[i].temperature)) {
                device["temperature"] = device_list[i].temperature;
            }
            if (!isnan(device_list[i].humidity)) {
                device["humidity"] = device_list[i].humidity;
            }
            if (device_list[i].battery_percent < 255) {
                device["battery_percent"] = device_list[i].battery_percent;
            }
        }

        doc["count"] = count;
    } else {
        doc["count"] = 0;
    }

    sendJson(request, doc);
}

void ApiEndpoints::handleDeleteDevice(AsyncWebServerRequest* request, uint8_t index) {
    if (!s_delete_device_callback) {
        sendError(request, 501, "Not available");
        return;
    }
    if (s_delete_device_callback(index)) {
        sendSuccess(request, "Device removed");
    } else {
        sendError(request, 404, "Device not found");
    }
}

// ============ WiFi Handlers ============

void ApiEndpoints::handleGetWifiNetworks(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();

    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);  // Start async scan
        doc["scanning"] = true;
    } else if (n == WIFI_SCAN_RUNNING) {
        doc["scanning"] = true;
    } else {
        doc["scanning"] = false;
        for (int i = 0; i < n; i++) {
            JsonObject network = networks.add<JsonObject>();
            network["ssid"] = WiFi.SSID(i);
            network["rssi"] = WiFi.RSSI(i);
            network["encryption"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            network["channel"] = WiFi.channel(i);
        }
        WiFi.scanDelete();
    }

    doc["count"] = n > 0 ? n : 0;
    sendJson(request, doc);
}

void ApiEndpoints::handlePostWifiConnect(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    if (!doc.containsKey("ssid")) {
        sendError(request, 400, "Missing SSID");
        return;
    }

    auto& config = Config.getConfigMutable();
    strlcpy(config.wifi.ssid, doc["ssid"] | "", sizeof(config.wifi.ssid));
    strlcpy(config.wifi.password, doc["password"] | "", sizeof(config.wifi.password));

    if (doc.containsKey("hub_address")) {
        strlcpy(config.wifi.hub_address, doc["hub_address"] | "", sizeof(config.wifi.hub_address));
    }

    Config.save();

    sendSuccess(request, "WiFi credentials saved. Rebooting...");

    delay(100);
    ESP.restart();
}

// ============ JSON Builders ============

void ApiEndpoints::buildStatusJson(JsonDocument& doc) {
    const auto& config = Config.getConfig();

    doc["device_id"] = config.identity.device_id;
    doc["device_type"] = config.identity.device_type;
    doc["firmware_version"] = config.identity.firmware_version;

    // WiFi status
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = WiFi.isConnected();
    wifi["ssid"] = WiFi.SSID();
    wifi["rssi"] = WiFi.RSSI();
    wifi["ip"] = WiFi.localIP().toString();

    // Uptime
    doc["uptime_ms"] = millis();
    doc["free_heap"] = ESP.getFreeHeap();

    // Sensor summary
    doc["sensor_count"] = IWMP_MAX_SENSORS;
    doc["relay_count"] = IWMP_MAX_RELAYS;
}

void ApiEndpoints::buildSystemInfoJson(JsonDocument& doc) {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    doc["chip_model"] = ESP.getChipModel();
    doc["chip_revision"] = chip_info.revision;
    doc["chip_cores"] = chip_info.cores;
    doc["flash_size"] = ESP.getFlashChipSize();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["min_free_heap"] = ESP.getMinFreeHeap();
    doc["sdk_version"] = ESP.getSdkVersion();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    doc["mac_address"] = mac_str;

    doc["uptime_ms"] = millis();
}

void ApiEndpoints::buildSensorJson(JsonObject& obj, uint8_t index) {
    const auto& config = Config.getConfig();

    if (index >= IWMP_MAX_SENSORS) {
        return;
    }

    const auto& sensor_cfg = config.moisture_sensors[index];

    obj["index"] = index;
    obj["name"] = sensor_cfg.sensor_name;
    obj["enabled"] = sensor_cfg.enabled;
    obj["input_type"] = static_cast<uint8_t>(sensor_cfg.input_type);
    obj["adc_pin"] = sensor_cfg.adc_pin;
    obj["ads_channel"] = sensor_cfg.ads_channel;
    obj["ads_i2c_address"] = sensor_cfg.ads_i2c_address;
    obj["mux_channel"] = sensor_cfg.mux_channel;
    obj["dry_value"] = sensor_cfg.dry_value;
    obj["wet_value"] = sensor_cfg.wet_value;
    obj["warning_level"] = sensor_cfg.warning_level;

    // Get live reading if callback available. Always emit `valid` so the
    // UI can distinguish "no reading yet" (—) from "reading is 0%".
    if (s_sensor_callback) {
        uint16_t raw = 0;
        uint8_t percent = 0;
        bool valid = false;
        uint32_t age_sec = 0;
        if (s_sensor_callback(index, raw, percent, valid, age_sec)) {
            obj["valid"] = valid;
            if (valid) {
                obj["raw"]     = raw;
                obj["percent"] = percent;
                obj["age_sec"] = age_sec;
            }
        }
    }

    // Get sensor hardware status if callback available
    if (s_sensor_status_callback) {
        SensorStatusInfo status = {};
        if (s_sensor_status_callback(index, status)) {
            obj["ready"] = status.ready;
            obj["hw_connected"] = status.hw_connected;
            obj["input_type_name"] = status.input_type;
        }
    }
}

void ApiEndpoints::buildRelayJson(JsonObject& obj, uint8_t index) {
    const auto& config = Config.getConfig();

    if (index >= IWMP_MAX_RELAYS) {
        return;
    }

    const auto& relay_cfg = config.relays[index];

    obj["index"] = index;
    obj["name"] = relay_cfg.relay_name;
    obj["enabled"] = relay_cfg.enabled;
    obj["pin"] = relay_cfg.gpio_pin;
    obj["active_low"] = relay_cfg.active_low;
    obj["max_on_time_sec"] = relay_cfg.max_on_time_sec;
    obj["min_off_time_sec"] = relay_cfg.min_off_time_sec;
    obj["cooldown_sec"] = relay_cfg.cooldown_sec;

    // Get live state if callback available
    if (s_relay_state_callback) {
        bool state = false;
        if (s_relay_state_callback(index, state)) {
            obj["state"] = state;
        }
    }
}

void ApiEndpoints::buildConfigJson(JsonDocument& doc) {
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    buildWifiConfigJson(wifi);

    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    buildMqttConfigJson(mqtt);

    JsonArray sensors = doc["sensors"].to<JsonArray>();
    buildSensorConfigJson(sensors);

    JsonArray relays = doc["relays"].to<JsonArray>();
    buildRelayConfigJson(relays);
}

void ApiEndpoints::buildWifiConfigJson(JsonObject& obj) {
    const auto& config = Config.getConfig();

    obj["ssid"] = config.wifi.ssid;
    // Don't expose password
    obj["use_static_ip"] = config.wifi.use_static_ip;
    obj["channel"] = config.wifi.wifi_channel;
    obj["hub_address"] = config.wifi.hub_address;
}

void ApiEndpoints::buildMqttConfigJson(JsonObject& obj) {
    const auto& config = Config.getConfig();

    obj["enabled"] = config.mqtt.enabled;
    obj["server"] = config.mqtt.broker;
    obj["port"] = config.mqtt.port;
    obj["username"] = config.mqtt.username;
    // Don't expose password
    obj["base_topic"] = config.mqtt.base_topic;
    obj["ha_discovery"] = config.mqtt.ha_discovery_enabled;
}

void ApiEndpoints::buildSensorConfigJson(JsonArray& arr) {
    const auto& config = Config.getConfig();

    for (uint8_t i = 0; i < IWMP_MAX_SENSORS; i++) {
        JsonObject sensor = arr.add<JsonObject>();
        sensor["name"] = config.moisture_sensors[i].sensor_name;
        sensor["enabled"] = config.moisture_sensors[i].enabled;
        sensor["input_type"] = static_cast<uint8_t>(config.moisture_sensors[i].input_type);
        sensor["adc_pin"] = config.moisture_sensors[i].adc_pin;
        sensor["ads_channel"] = config.moisture_sensors[i].ads_channel;
        sensor["ads_i2c_address"] = config.moisture_sensors[i].ads_i2c_address;
        sensor["mux_channel"] = config.moisture_sensors[i].mux_channel;
        sensor["dry_value"] = config.moisture_sensors[i].dry_value;
        sensor["wet_value"] = config.moisture_sensors[i].wet_value;
        sensor["sample_delay_ms"] = config.moisture_sensors[i].sample_delay_ms;
        sensor["reading_samples"] = config.moisture_sensors[i].reading_samples;
        sensor["warning_level"] = config.moisture_sensors[i].warning_level;
    }
}

void ApiEndpoints::buildRelayConfigJson(JsonArray& arr) {
    const auto& config = Config.getConfig();

    for (uint8_t i = 0; i < IWMP_MAX_RELAYS; i++) {
        JsonObject relay = arr.add<JsonObject>();
        relay["name"] = config.relays[i].relay_name;
        relay["enabled"] = config.relays[i].enabled;
        relay["pin"] = config.relays[i].gpio_pin;
        relay["active_low"] = config.relays[i].active_low;
        relay["max_on_time_sec"] = config.relays[i].max_on_time_sec;
        relay["min_off_time_sec"] = config.relays[i].min_off_time_sec;
        relay["cooldown_sec"] = config.relays[i].cooldown_sec;
    }
}

// ============ JSON Parsers ============

bool ApiEndpoints::parseWifiConfig(JsonObject& obj) {
    auto& config = Config.getConfigMutable();
    bool changed = false;

    if (obj.containsKey("ssid")) {
        strlcpy(config.wifi.ssid, obj["ssid"], sizeof(config.wifi.ssid));
        changed = true;
    }

    if (obj.containsKey("password")) {
        strlcpy(config.wifi.password, obj["password"], sizeof(config.wifi.password));
        changed = true;
    }

    if (obj.containsKey("use_static_ip")) {
        config.wifi.use_static_ip = obj["use_static_ip"];
        changed = true;
    }

    if (obj.containsKey("channel")) {
        config.wifi.wifi_channel = obj["channel"];
        changed = true;
    }

    if (obj.containsKey("hub_address")) {
        strlcpy(config.wifi.hub_address, obj["hub_address"] | "", sizeof(config.wifi.hub_address));
        changed = true;
    }

    return changed;
}

bool ApiEndpoints::parseMqttConfig(JsonObject& obj) {
    auto& config = Config.getConfigMutable();
    bool changed = false;

    if (obj.containsKey("enabled")) {
        config.mqtt.enabled = obj["enabled"];
        changed = true;
    }

    if (obj.containsKey("server")) {
        strlcpy(config.mqtt.broker, obj["server"], sizeof(config.mqtt.broker));
        changed = true;
    }

    if (obj.containsKey("port")) {
        config.mqtt.port = obj["port"];
        changed = true;
    }

    if (obj.containsKey("username")) {
        strlcpy(config.mqtt.username, obj["username"], sizeof(config.mqtt.username));
        changed = true;
    }

    if (obj.containsKey("password")) {
        strlcpy(config.mqtt.password, obj["password"], sizeof(config.mqtt.password));
        changed = true;
    }

    if (obj.containsKey("base_topic")) {
        strlcpy(config.mqtt.base_topic, obj["base_topic"], sizeof(config.mqtt.base_topic));
        changed = true;
    }

    if (obj.containsKey("ha_discovery")) {
        config.mqtt.ha_discovery_enabled = obj["ha_discovery"];
        changed = true;
    }

    return changed;
}

bool ApiEndpoints::parseSensorConfig(JsonArray& arr) {
    auto& config = Config.getConfigMutable();
    bool changed = false;

    size_t index = 0;
    for (JsonObject sensor : arr) {
        if (index >= IWMP_MAX_SENSORS) {
            break;
        }

        // Skip empty objects (placeholders for unchanged sensors)
        if (sensor.size() == 0) {
            index++;
            continue;
        }

        LOG_D(TAG, "Processing sensor %d (%d fields)", index, sensor.size());

        if (sensor["name"].is<const char*>()) {
            strlcpy(config.moisture_sensors[index].sensor_name, sensor["name"],
                    sizeof(config.moisture_sensors[index].sensor_name));
            changed = true;
        }

        if (sensor["enabled"].is<bool>()) {
            config.moisture_sensors[index].enabled = sensor["enabled"];
            changed = true;
        }

        // Input type (0=Direct ADC, 1=ADS1115, 2=Mux)
        if (sensor["input_type"].is<int>()) {
            SensorInputType old_type = config.moisture_sensors[index].input_type;
            SensorInputType new_type = static_cast<SensorInputType>(sensor["input_type"].as<int>());
            config.moisture_sensors[index].input_type = new_type;
            changed = true;

            // Auto-update calibration defaults when switching input types
            // Only if calibration values weren't explicitly provided in this request
            if (old_type != new_type && !sensor.containsKey("dry_value") && !sensor.containsKey("wet_value")) {
                if (new_type == SensorInputType::ADS1115) {
                    // Switching TO ADS1115: use 16-bit defaults
                    config.moisture_sensors[index].dry_value = 45000;  // ADS1115 typical dry
                    config.moisture_sensors[index].wet_value = 18000;  // ADS1115 typical wet
                    LOG_D(TAG, "Auto-set ADS1115 calibration defaults for sensor %d", index);
                } else if (old_type == SensorInputType::ADS1115) {
                    // Switching FROM ADS1115: use 12-bit defaults
                    config.moisture_sensors[index].dry_value = 3500;   // Direct ADC typical dry
                    config.moisture_sensors[index].wet_value = 1500;   // Direct ADC typical wet
                    LOG_D(TAG, "Auto-set Direct ADC calibration defaults for sensor %d", index);
                }
            }
        }

        // Direct ADC pin
        if (sensor["adc_pin"].is<int>()) {
            config.moisture_sensors[index].adc_pin = sensor["adc_pin"];
            changed = true;
        }

        // ADS1115 channel (0-3)
        if (sensor["ads_channel"].is<int>()) {
            config.moisture_sensors[index].ads_channel = sensor["ads_channel"];
            changed = true;
        }

        // ADS1115 I2C address
        if (sensor["ads_i2c_address"].is<int>()) {
            config.moisture_sensors[index].ads_i2c_address = sensor["ads_i2c_address"];
            changed = true;
        }

        // Mux channel (0-15)
        if (sensor["mux_channel"].is<int>()) {
            config.moisture_sensors[index].mux_channel = sensor["mux_channel"];
            changed = true;
        }

        // Calibration values
        if (sensor["dry_value"].is<int>()) {
            config.moisture_sensors[index].dry_value = sensor["dry_value"];
            changed = true;
        }

        if (sensor["wet_value"].is<int>()) {
            config.moisture_sensors[index].wet_value = sensor["wet_value"];
            changed = true;
        }

        // Sampling configuration
        if (sensor["sample_delay_ms"].is<int>()) {
            config.moisture_sensors[index].sample_delay_ms = sensor["sample_delay_ms"];
            changed = true;
        }

        if (sensor["reading_samples"].is<int>()) {
            config.moisture_sensors[index].reading_samples = sensor["reading_samples"];
            changed = true;
        }

        if (sensor["warning_level"].is<int>()) {
            config.moisture_sensors[index].warning_level = sensor["warning_level"];
            changed = true;
        }

        index++;
    }

    return changed;
}

bool ApiEndpoints::parseRelayConfig(JsonArray& arr) {
    auto& config = Config.getConfigMutable();
    bool changed = false;

    size_t index = 0;
    for (JsonObject relay : arr) {
        if (index >= IWMP_MAX_RELAYS) {
            break;
        }

        if (relay["name"].is<const char*>()) {
            strlcpy(config.relays[index].relay_name, relay["name"],
                    sizeof(config.relays[index].relay_name));
            changed = true;
        }

        if (relay["enabled"].is<bool>()) {
            config.relays[index].enabled = relay["enabled"];
            changed = true;
        }

        if (relay["active_low"].is<bool>()) {
            config.relays[index].active_low = relay["active_low"];
            changed = true;
        }

        if (relay["pin"].is<int>()) {
            config.relays[index].gpio_pin = relay["pin"];
            changed = true;
        }

        if (relay["max_on_time_sec"].is<int>()) {
            config.relays[index].max_on_time_sec = relay["max_on_time_sec"];
            changed = true;
        }

        if (relay["min_off_time_sec"].is<int>()) {
            config.relays[index].min_off_time_sec = relay["min_off_time_sec"];
            changed = true;
        }

        if (relay["cooldown_sec"].is<int>()) {
            config.relays[index].cooldown_sec = relay["cooldown_sec"];
            changed = true;
        }

        index++;
    }

    return changed;
}

} // namespace iwmp
