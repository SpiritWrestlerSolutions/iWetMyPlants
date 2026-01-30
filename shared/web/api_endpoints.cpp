/**
 * @file api_endpoints.cpp
 * @brief REST API endpoint handlers implementation
 */

#include "api_endpoints.h"
#include "../config/config_manager.h"
#include <WiFi.h>
#include <esp_system.h>
#include <esp_chip_info.h>

namespace iwmp {

// Static callback storage
SensorDataCallback ApiEndpoints::s_sensor_callback = nullptr;
RelayStateCallback ApiEndpoints::s_relay_state_callback = nullptr;
RelayControlCallback ApiEndpoints::s_relay_control_callback = nullptr;
CalibrationCallback ApiEndpoints::s_calibration_callback = nullptr;
PairedDevicesCallback ApiEndpoints::s_paired_devices_callback = nullptr;

// ============ Callback Registration ============

void ApiEndpoints::onSensorData(SensorDataCallback callback) {
    s_sensor_callback = callback;
}

void ApiEndpoints::onRelayState(RelayStateCallback callback) {
    s_relay_state_callback = callback;
}

void ApiEndpoints::onRelayControl(RelayControlCallback callback) {
    s_relay_control_callback = callback;
}

void ApiEndpoints::onCalibration(CalibrationCallback callback) {
    s_calibration_callback = callback;
}

void ApiEndpoints::onPairedDevices(PairedDevicesCallback callback) {
    s_paired_devices_callback = callback;
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
    // Status & System
    server.on("/api/status", HTTP_GET, handleGetStatus);
    server.on("/api/system/info", HTTP_GET, handleGetSystemInfo);
    server.on("/api/system/reboot", HTTP_POST, handlePostReboot);

    // Sensors
    server.on("/api/sensors", HTTP_GET, handleGetSensors);

    // Sensor by index (with calibration)
    server.on("^\\/api\\/sensors\\/(\\d+)$", HTTP_GET, [](AsyncWebServerRequest* request) {
        uint8_t index = request->pathArg(0).toInt();
        handleGetSensor(request, index);
    });

    server.on("^\\/api\\/sensors\\/(\\d+)\\/calibrate$", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            // Body handler is separate
        },
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            uint8_t sensor_idx = request->pathArg(0).toInt();
            handlePostCalibrate(request, sensor_idx);
        }
    );

    // Simplified calibrate endpoint that accepts action in URL
    server.on("^\\/api\\/sensors\\/(\\d+)\\/calibrate\\/(dry|wet)$", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            uint8_t sensor_idx = request->pathArg(0).toInt();
            String action = request->pathArg(1);

            if (s_calibration_callback) {
                if (s_calibration_callback(sensor_idx, action.c_str())) {
                    sendSuccess(request, "Calibration point set");
                } else {
                    sendError(request, 400, "Calibration failed");
                }
            } else {
                sendError(request, 501, "Calibration not available");
            }
        }
    );

    // Configuration
    server.on("/api/config", HTTP_GET, handleGetConfig);

    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handlePostConfig(request, data, len);
        }
    );

    // Config sections
    server.on("^\\/api\\/config\\/(wifi|mqtt|sensors|relays|espnow)$", HTTP_GET,
        [](AsyncWebServerRequest* request) {
            handleGetConfigSection(request, request->pathArg(0));
        }
    );

    server.on("^\\/api\\/config\\/(wifi|mqtt|sensors|relays|espnow)$", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handlePostConfigSection(request, request->pathArg(0), data, len);
        }
    );

    // Relays (Greenhouse)
    server.on("/api/relays", HTTP_GET, handleGetRelays);

    server.on("^\\/api\\/relays\\/(\\d+)$", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            uint8_t relay_idx = request->pathArg(0).toInt();
            handlePostRelay(request, relay_idx, data, len);
        }
    );

    // Paired Devices (Hub)
    server.on("/api/devices", HTTP_GET, handleGetDevices);

    server.on("^\\/api\\/devices\\/(\\d+)$", HTTP_DELETE,
        [](AsyncWebServerRequest* request) {
            uint8_t device_idx = request->pathArg(0).toInt();
            handleDeleteDevice(request, device_idx);
        }
    );

    // WiFi
    server.on("/api/wifi/networks", HTTP_GET, handleGetWifiNetworks);

    server.on("/api/wifi/connect", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handlePostWifiConnect(request, data, len);
        }
    );

    Serial.println("[API] Routes registered");
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

    const auto& config = Config.get();
    for (uint8_t i = 0; i < config.sensor_count; i++) {
        JsonObject sensor = sensors.add<JsonObject>();
        buildSensorJson(sensor, i);
    }

    doc["count"] = config.sensor_count;
    sendJson(request, doc);
}

void ApiEndpoints::handleGetSensor(AsyncWebServerRequest* request, uint8_t index) {
    const auto& config = Config.get();

    if (index >= config.sensor_count) {
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
        const auto& config = Config.get();
        doc["channel"] = config.espnow.channel;
        doc["encryption"] = config.espnow.encryption_enabled;
    } else {
        sendError(request, 404, "Unknown config section");
        return;
    }

    sendJson(request, doc);
}

void ApiEndpoints::handlePostConfigSection(AsyncWebServerRequest* request, const String& section,
                                           uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
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
        JsonArray arr = doc.as<JsonArray>();
        success = parseSensorConfig(arr);
    } else if (section == "relays") {
        JsonArray arr = doc.as<JsonArray>();
        success = parseRelayConfig(arr);
    } else if (section == "espnow") {
        auto& config = Config.get();
        if (doc.containsKey("channel")) {
            config.espnow.channel = doc["channel"];
            success = true;
        }
        if (doc.containsKey("encryption")) {
            config.espnow.encryption_enabled = doc["encryption"];
            success = true;
        }
    }

    if (success) {
        Config.save();
        sendSuccess(request, "Configuration saved");
    } else {
        sendError(request, 400, "Failed to parse configuration");
    }
}

// ============ Relay Handlers ============

void ApiEndpoints::handleGetRelays(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray relays = doc["relays"].to<JsonArray>();

    const auto& config = Config.get();
    for (uint8_t i = 0; i < config.relay_count; i++) {
        JsonObject relay = relays.add<JsonObject>();
        buildRelayJson(relay, i);
    }

    doc["count"] = config.relay_count;
    sendJson(request, doc);
}

void ApiEndpoints::handlePostRelay(AsyncWebServerRequest* request, uint8_t index,
                                   uint8_t* data, size_t len) {
    const auto& config = Config.get();

    if (index >= config.relay_count) {
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
        }

        doc["count"] = count;
    } else {
        doc["count"] = 0;
    }

    sendJson(request, doc);
}

void ApiEndpoints::handleDeleteDevice(AsyncWebServerRequest* request, uint8_t index) {
    // TODO: Implement device removal from pairing list
    sendError(request, 501, "Not implemented");
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

    auto& config = Config.get();
    strlcpy(config.wifi.ssid, doc["ssid"] | "", sizeof(config.wifi.ssid));
    strlcpy(config.wifi.password, doc["password"] | "", sizeof(config.wifi.password));
    config.wifi.enabled = true;

    Config.save();

    sendSuccess(request, "WiFi credentials saved. Rebooting...");

    delay(100);
    ESP.restart();
}

// ============ JSON Builders ============

void ApiEndpoints::buildStatusJson(JsonDocument& doc) {
    const auto& config = Config.get();

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
    doc["sensor_count"] = config.sensor_count;
    doc["relay_count"] = config.relay_count;
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
    WiFi.macAddress(mac);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    doc["mac_address"] = mac_str;

    doc["uptime_ms"] = millis();
}

void ApiEndpoints::buildSensorJson(JsonObject& obj, uint8_t index) {
    const auto& config = Config.get();

    if (index >= config.sensor_count) {
        return;
    }

    const auto& sensor_cfg = config.sensors[index];

    obj["index"] = index;
    obj["name"] = sensor_cfg.name;
    obj["enabled"] = sensor_cfg.enabled;
    obj["input_type"] = sensor_cfg.input_type;
    obj["pin"] = sensor_cfg.pin;
    obj["dry_value"] = sensor_cfg.dry_value;
    obj["wet_value"] = sensor_cfg.wet_value;

    // Get live reading if callback available
    if (s_sensor_callback) {
        uint16_t raw = 0;
        uint8_t percent = 0;
        if (s_sensor_callback(index, raw, percent)) {
            obj["raw"] = raw;
            obj["percent"] = percent;
        }
    }
}

void ApiEndpoints::buildRelayJson(JsonObject& obj, uint8_t index) {
    const auto& config = Config.get();

    if (index >= config.relay_count) {
        return;
    }

    const auto& relay_cfg = config.relays[index];

    obj["index"] = index;
    obj["name"] = relay_cfg.name;
    obj["enabled"] = relay_cfg.enabled;
    obj["pin"] = relay_cfg.pin;
    obj["active_low"] = relay_cfg.active_low;
    obj["auto_mode"] = relay_cfg.auto_mode;

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
    const auto& config = Config.get();

    obj["ssid"] = config.wifi.ssid;
    // Don't expose password
    obj["enabled"] = config.wifi.enabled;
    obj["ap_mode"] = config.wifi.ap_mode;
    obj["ap_ssid"] = config.wifi.ap_ssid;
}

void ApiEndpoints::buildMqttConfigJson(JsonObject& obj) {
    const auto& config = Config.get();

    obj["enabled"] = config.mqtt.enabled;
    obj["server"] = config.mqtt.server;
    obj["port"] = config.mqtt.port;
    obj["username"] = config.mqtt.username;
    // Don't expose password
    obj["topic_prefix"] = config.mqtt.topic_prefix;
    obj["ha_discovery"] = config.mqtt.ha_discovery;
}

void ApiEndpoints::buildSensorConfigJson(JsonArray& arr) {
    const auto& config = Config.get();

    for (uint8_t i = 0; i < config.sensor_count; i++) {
        JsonObject sensor = arr.add<JsonObject>();
        sensor["name"] = config.sensors[i].name;
        sensor["enabled"] = config.sensors[i].enabled;
        sensor["input_type"] = config.sensors[i].input_type;
        sensor["pin"] = config.sensors[i].pin;
        sensor["dry_value"] = config.sensors[i].dry_value;
        sensor["wet_value"] = config.sensors[i].wet_value;
        sensor["sample_interval_ms"] = config.sensors[i].sample_interval_ms;
        sensor["sample_count"] = config.sensors[i].sample_count;
    }
}

void ApiEndpoints::buildRelayConfigJson(JsonArray& arr) {
    const auto& config = Config.get();

    for (uint8_t i = 0; i < config.relay_count; i++) {
        JsonObject relay = arr.add<JsonObject>();
        relay["name"] = config.relays[i].name;
        relay["enabled"] = config.relays[i].enabled;
        relay["pin"] = config.relays[i].pin;
        relay["active_low"] = config.relays[i].active_low;
        relay["auto_mode"] = config.relays[i].auto_mode;
        relay["min_on_time_s"] = config.relays[i].min_on_time_s;
        relay["max_on_time_s"] = config.relays[i].max_on_time_s;
        relay["cooldown_s"] = config.relays[i].cooldown_s;
    }
}

// ============ JSON Parsers ============

bool ApiEndpoints::parseWifiConfig(JsonObject& obj) {
    auto& config = Config.get();
    bool changed = false;

    if (obj.containsKey("ssid")) {
        strlcpy(config.wifi.ssid, obj["ssid"], sizeof(config.wifi.ssid));
        changed = true;
    }

    if (obj.containsKey("password")) {
        strlcpy(config.wifi.password, obj["password"], sizeof(config.wifi.password));
        changed = true;
    }

    if (obj.containsKey("enabled")) {
        config.wifi.enabled = obj["enabled"];
        changed = true;
    }

    if (obj.containsKey("ap_mode")) {
        config.wifi.ap_mode = obj["ap_mode"];
        changed = true;
    }

    if (obj.containsKey("ap_ssid")) {
        strlcpy(config.wifi.ap_ssid, obj["ap_ssid"], sizeof(config.wifi.ap_ssid));
        changed = true;
    }

    if (obj.containsKey("ap_password")) {
        strlcpy(config.wifi.ap_password, obj["ap_password"], sizeof(config.wifi.ap_password));
        changed = true;
    }

    return changed;
}

bool ApiEndpoints::parseMqttConfig(JsonObject& obj) {
    auto& config = Config.get();
    bool changed = false;

    if (obj.containsKey("enabled")) {
        config.mqtt.enabled = obj["enabled"];
        changed = true;
    }

    if (obj.containsKey("server")) {
        strlcpy(config.mqtt.server, obj["server"], sizeof(config.mqtt.server));
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

    if (obj.containsKey("topic_prefix")) {
        strlcpy(config.mqtt.topic_prefix, obj["topic_prefix"], sizeof(config.mqtt.topic_prefix));
        changed = true;
    }

    if (obj.containsKey("ha_discovery")) {
        config.mqtt.ha_discovery = obj["ha_discovery"];
        changed = true;
    }

    return changed;
}

bool ApiEndpoints::parseSensorConfig(JsonArray& arr) {
    auto& config = Config.get();
    bool changed = false;

    size_t index = 0;
    for (JsonObject sensor : arr) {
        if (index >= IWMP_MAX_SENSORS) {
            break;
        }

        if (sensor.containsKey("name")) {
            strlcpy(config.sensors[index].name, sensor["name"],
                    sizeof(config.sensors[index].name));
            changed = true;
        }

        if (sensor.containsKey("enabled")) {
            config.sensors[index].enabled = sensor["enabled"];
            changed = true;
        }

        if (sensor.containsKey("dry_value")) {
            config.sensors[index].dry_value = sensor["dry_value"];
            changed = true;
        }

        if (sensor.containsKey("wet_value")) {
            config.sensors[index].wet_value = sensor["wet_value"];
            changed = true;
        }

        if (sensor.containsKey("sample_interval_ms")) {
            config.sensors[index].sample_interval_ms = sensor["sample_interval_ms"];
            changed = true;
        }

        if (sensor.containsKey("sample_count")) {
            config.sensors[index].sample_count = sensor["sample_count"];
            changed = true;
        }

        index++;
    }

    return changed;
}

bool ApiEndpoints::parseRelayConfig(JsonArray& arr) {
    auto& config = Config.get();
    bool changed = false;

    size_t index = 0;
    for (JsonObject relay : arr) {
        if (index >= IWMP_MAX_RELAYS) {
            break;
        }

        if (relay.containsKey("name")) {
            strlcpy(config.relays[index].name, relay["name"],
                    sizeof(config.relays[index].name));
            changed = true;
        }

        if (relay.containsKey("enabled")) {
            config.relays[index].enabled = relay["enabled"];
            changed = true;
        }

        if (relay.containsKey("active_low")) {
            config.relays[index].active_low = relay["active_low"];
            changed = true;
        }

        if (relay.containsKey("auto_mode")) {
            config.relays[index].auto_mode = relay["auto_mode"];
            changed = true;
        }

        if (relay.containsKey("min_on_time_s")) {
            config.relays[index].min_on_time_s = relay["min_on_time_s"];
            changed = true;
        }

        if (relay.containsKey("max_on_time_s")) {
            config.relays[index].max_on_time_s = relay["max_on_time_s"];
            changed = true;
        }

        if (relay.containsKey("cooldown_s")) {
            config.relays[index].cooldown_s = relay["cooldown_s"];
            changed = true;
        }

        index++;
    }

    return changed;
}

} // namespace iwmp
