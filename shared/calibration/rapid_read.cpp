/**
 * @file rapid_read.cpp
 * @brief Fast sampling for calibration via WebSocket
 */

#include "rapid_read.h"
#include "logger.h"
#include <ArduinoJson.h>

namespace iwmp {

// Global rapid read server instance
RapidReadServer RapidRead;

static constexpr const char* TAG = "Rapid";

void RapidReadServer::begin(AsyncWebServer* server, const char* path) {
    if (!server) {
        LOG_E(TAG, "Null server provided");
        return;
    }

    // Create WebSocket handler
    _ws = new AsyncWebSocket(path);

    // Set event handler
    _ws->onEvent([this](AsyncWebSocket* server,
                        AsyncWebSocketClient* client,
                        AwsEventType type,
                        void* arg,
                        uint8_t* data,
                        size_t len) {
        onWebSocketEvent(server, client, type, arg, data, len);
    });

    // Add to server
    server->addHandler(_ws);

    LOG_I(TAG, "Rapid read WebSocket initialized at %s", path);
}

void RapidReadServer::setSensor(MoistureSensor* sensor) {
    _sensor = sensor;
    _recent_values.clear();

    if (sensor) {
        LOG_I(TAG, "Sensor set: %s", sensor->getName());
    }
}

void RapidReadServer::start() {
    if (!_sensor) {
        LOG_W(TAG, "No sensor set");
        return;
    }

    _active = true;
    _recent_values.clear();
    _last_sample_time = millis();

    LOG_I(TAG, "Rapid reading started (rate=%d Hz)", _sample_rate);
}

void RapidReadServer::stop() {
    _active = false;
    LOG_I(TAG, "Rapid reading stopped");
}

void RapidReadServer::update() {
    if (!_active || !_sensor || !_ws) {
        return;
    }

    uint32_t now = millis();
    if ((now - _last_sample_time) < _sample_interval_ms) {
        return;
    }
    _last_sample_time = now;

    // Read sensor
    uint16_t raw = _sensor->readRaw();

    // Add to recent values
    _recent_values.push_back(raw);
    while (_recent_values.size() > _averaging_window) {
        _recent_values.pop_front();
    }

    // Calculate average
    uint16_t average = calculateAverage();

    // Calculate percentage using sensor's calibration
    uint8_t percent = _sensor->rawToPercent(average);

    // Broadcast to all connected clients
    broadcastReading(raw, average, percent);
}

void RapidReadServer::setSampleRate(uint16_t samples_per_second) {
    // Clamp to valid range
    if (samples_per_second < 1) samples_per_second = 1;
    if (samples_per_second > 50) samples_per_second = 50;

    _sample_rate = samples_per_second;
    _sample_interval_ms = 1000 / samples_per_second;

    LOG_D(TAG, "Sample rate set to %d Hz (interval=%lu ms)",
          _sample_rate, _sample_interval_ms);
}

void RapidReadServer::setAveragingWindow(uint8_t samples) {
    // Clamp to valid range
    if (samples < 1) samples = 1;
    if (samples > 50) samples = 50;

    _averaging_window = samples;
    LOG_D(TAG, "Averaging window set to %d samples", _averaging_window);
}

uint16_t RapidReadServer::getCurrentAverage() const {
    return calculateAverage();
}

uint8_t RapidReadServer::getCurrentPercent() const {
    if (!_sensor) {
        return 0;
    }
    return _sensor->rawToPercent(calculateAverage());
}

void RapidReadServer::onWebSocketEvent(AsyncWebSocket* server,
                                        AsyncWebSocketClient* client,
                                        AwsEventType type,
                                        void* arg,
                                        uint8_t* data,
                                        size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            LOG_I(TAG, "WebSocket client connected: %u", client->id());
            // Send initial status
            {
                JsonDocument doc;
                doc["type"] = "status";
                doc["active"] = _active;
                doc["sensor"] = _sensor ? _sensor->getName() : "none";
                doc["rate"] = _sample_rate;
                doc["window"] = _averaging_window;

                String json;
                serializeJson(doc, json);
                client->text(json);
            }
            break;

        case WS_EVT_DISCONNECT:
            LOG_I(TAG, "WebSocket client disconnected: %u", client->id());
            break;

        case WS_EVT_DATA:
            handleMessage(client, data, len);
            break;

        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void RapidReadServer::handleMessage(AsyncWebSocketClient* client, uint8_t* data, size_t len) {
    // Null terminate
    char* msg = (char*)data;
    if (len > 0 && msg[len-1] != '\0') {
        // Create null-terminated copy
        char buffer[256];
        size_t copy_len = min(len, sizeof(buffer) - 1);
        memcpy(buffer, msg, copy_len);
        buffer[copy_len] = '\0';
        msg = buffer;
    }

    LOG_D(TAG, "Received: %s", msg);

    // Parse JSON command
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, msg);

    if (error) {
        LOG_W(TAG, "JSON parse error: %s", error.c_str());
        return;
    }

    const char* cmd = doc["cmd"];
    if (!cmd) {
        return;
    }

    // Handle commands
    if (strcmp(cmd, "start") == 0) {
        start();

        // Send confirmation
        JsonDocument response;
        response["type"] = "started";
        response["rate"] = _sample_rate;

        String json;
        serializeJson(response, json);
        client->text(json);
    }
    else if (strcmp(cmd, "stop") == 0) {
        stop();

        JsonDocument response;
        response["type"] = "stopped";

        String json;
        serializeJson(response, json);
        client->text(json);
    }
    else if (strcmp(cmd, "rate") == 0) {
        uint16_t rate = doc["value"] | 10;
        setSampleRate(rate);

        JsonDocument response;
        response["type"] = "rate_set";
        response["rate"] = _sample_rate;

        String json;
        serializeJson(response, json);
        client->text(json);
    }
    else if (strcmp(cmd, "window") == 0) {
        uint8_t window = doc["value"] | 5;
        setAveragingWindow(window);

        JsonDocument response;
        response["type"] = "window_set";
        response["window"] = _averaging_window;

        String json;
        serializeJson(response, json);
        client->text(json);
    }
    else if (strcmp(cmd, "status") == 0) {
        JsonDocument response;
        response["type"] = "status";
        response["active"] = _active;
        response["sensor"] = _sensor ? _sensor->getName() : "none";
        response["rate"] = _sample_rate;
        response["window"] = _averaging_window;
        response["average"] = getCurrentAverage();
        response["percent"] = getCurrentPercent();

        String json;
        serializeJson(response, json);
        client->text(json);
    }
}

void RapidReadServer::broadcastReading(uint16_t raw, uint16_t average, uint8_t percent) {
    if (!_ws || _ws->count() == 0) {
        return;
    }

    // Build JSON message
    JsonDocument doc;
    doc["type"] = "reading";
    doc["raw"] = raw;
    doc["avg"] = average;
    doc["pct"] = percent;
    doc["ts"] = millis();

    String json;
    serializeJson(doc, json);

    // Broadcast to all clients
    _ws->textAll(json);
}

uint16_t RapidReadServer::calculateAverage() const {
    if (_recent_values.empty()) {
        return 0;
    }

    uint32_t sum = 0;
    for (uint16_t val : _recent_values) {
        sum += val;
    }

    return sum / _recent_values.size();
}

} // namespace iwmp
