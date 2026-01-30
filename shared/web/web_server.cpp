/**
 * @file web_server.cpp
 * @brief Async web server implementation
 */

#include "web_server.h"
#include "api_endpoints.h"

namespace iwmp {

// Global instance
WebServer& Web = WebServer::getInstance();

WebServer& WebServer::getInstance() {
    static WebServer instance;
    return instance;
}

WebServer::WebServer() {
}

WebServer::~WebServer() {
    end();
}

bool WebServer::begin(const DeviceIdentity& identity) {
    if (_running) {
        return true;
    }

    _identity = identity;

    // Create server
    _server = new AsyncWebServer(WEB_SERVER_PORT);
    if (!_server) {
        Serial.println("[Web] Failed to create server");
        return false;
    }

    // Create WebSocket
    _ws = new AsyncWebSocket("/ws");
    if (!_ws) {
        Serial.println("[Web] Failed to create WebSocket");
        delete _server;
        _server = nullptr;
        return false;
    }

    // WebSocket event handler
    _ws->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onWsEvent(server, client, type, arg, data, len);
    });

    _server->addHandler(_ws);

    // Register routes
    registerRoutes();

    // Start server
    _server->begin();
    _running = true;

    Serial.printf("[Web] Server started on port %d\n", WEB_SERVER_PORT);
    return true;
}

void WebServer::end() {
    if (!_running) {
        return;
    }

    if (_ws) {
        _ws->closeAll();
        delete _ws;
        _ws = nullptr;
    }

    if (_server) {
        _server->end();
        delete _server;
        _server = nullptr;
    }

    _running = false;
    Serial.println("[Web] Server stopped");
}

void WebServer::registerRoutes() {
    registerStaticRoutes();
    registerApiRoutes();
}

void WebServer::addRoute(const char* uri, WebRequestMethodComposite method,
                         ArRequestHandlerFunction handler) {
    if (_server) {
        _server->on(uri, method, handler);
    }
}

void WebServer::addRouteWithBody(const char* uri, WebRequestMethodComposite method,
                                  ArRequestHandlerFunction handler,
                                  ArBodyHandlerFunction bodyHandler) {
    if (_server) {
        _server->on(uri, method, handler, nullptr, bodyHandler);
    }
}

void WebServer::broadcastWs(const char* message) {
    if (_ws && _ws->count() > 0) {
        _ws->textAll(message);
    }
}

void WebServer::broadcastWsJson(const JsonDocument& doc) {
    if (_ws && _ws->count() > 0) {
        String json;
        serializeJson(doc, json);
        _ws->textAll(json);
    }
}

void WebServer::sendRapidReading(uint8_t sensor_index, uint16_t raw, uint16_t avg, uint8_t percent) {
    if (!_ws || _ws->count() == 0) {
        return;
    }

    JsonDocument doc;
    doc["type"] = "reading";
    doc["sensor"] = sensor_index;
    doc["raw"] = raw;
    doc["avg"] = avg;
    doc["percent"] = percent;
    doc["timestamp"] = millis();

    broadcastWsJson(doc);
}

void WebServer::sendJson(AsyncWebServerRequest* request, int code, const JsonDocument& doc) {
    String json;
    serializeJson(doc, json);
    request->send(code, "application/json", json);
}

void WebServer::sendError(AsyncWebServerRequest* request, int code, const char* message) {
    JsonDocument doc;
    doc["error"] = true;
    doc["message"] = message;
    sendJson(request, code, doc);
}

void WebServer::sendSuccess(AsyncWebServerRequest* request, const char* message) {
    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = message;
    sendJson(request, 200, doc);
}

bool WebServer::parseJsonBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                               JsonDocument& doc, String& error) {
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
        error = "Invalid JSON: ";
        error += err.c_str();
        return false;
    }
    return true;
}

void WebServer::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                          AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("[WS] Client #%u connected from %s\n",
                          client->id(), client->remoteIP().toString().c_str());
            break;

        case WS_EVT_DISCONNECT:
            Serial.printf("[WS] Client #%u disconnected\n", client->id());
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                // Handle WebSocket message
                data[len] = 0;  // Null terminate

                JsonDocument doc;
                if (deserializeJson(doc, data) == DeserializationError::Ok) {
                    const char* type = doc["type"] | "";

                    // Handle calibration commands
                    if (strcmp(type, "set_dry") == 0) {
                        uint8_t sensor = doc["sensor"] | 0;
                        if (_calibration_callback) {
                            _calibration_callback(sensor, 0);  // 0 = dry
                        }
                    } else if (strcmp(type, "set_wet") == 0) {
                        uint8_t sensor = doc["sensor"] | 0;
                        if (_calibration_callback) {
                            _calibration_callback(sensor, 1);  // 1 = wet
                        }
                    } else if (strcmp(type, "save") == 0) {
                        uint8_t sensor = doc["sensor"] | 0;
                        if (_calibration_callback) {
                            _calibration_callback(sensor, 2);  // 2 = save
                        }
                    }
                }
            }
            break;
        }

        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void WebServer::registerStaticRoutes() {
    // Dashboard
    _server->on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        request->send(200, "text/html", getIndexHtml());
    });

    // Settings pages
    _server->on("/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", getSettingsHtml());
    });

    _server->on("/settings/wifi", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", getWifiSettingsHtml());
    });

    _server->on("/settings/mqtt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", getMqttSettingsHtml());
    });

    // Sensors
    _server->on("/sensors", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", getSensorsHtml());
    });

    _server->on("/sensors/calibrate", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", getCalibrationHtml());
    });

    // Relays (Greenhouse only)
    _server->on("/relays", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", getRelaysHtml());
    });

    // Devices (Hub only)
    _server->on("/devices", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", getDevicesHtml());
    });

    // CSS
    _server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/css", getStyleCss());
    });

    // JavaScript
    _server->on("/app.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/javascript", getAppJs());
    });

    // 404 handler
    _server->onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not Found");
    });
}

void WebServer::registerApiRoutes() {
    // Register all API endpoints
    ApiEndpoints::registerAll(_server, this);
}

// ============ Embedded HTML/CSS/JS ============
// These return the embedded web content

const char* getIndexHtml() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>iWetMyPlants</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <header>
        <h1>iWetMyPlants</h1>
        <span id="device-name">Loading...</span>
    </header>
    <nav>
        <a href="/" class="active">Dashboard</a>
        <a href="/sensors">Sensors</a>
        <a href="/settings">Settings</a>
    </nav>
    <main>
        <div class="card">
            <h2>System Status</h2>
            <div id="status-cards">
                <div class="reading">
                    <span class="label">WiFi</span>
                    <span class="value" id="wifi-status">--</span>
                </div>
                <div class="reading">
                    <span class="label">Uptime</span>
                    <span class="value" id="uptime">--</span>
                </div>
                <div class="reading">
                    <span class="label">Free Memory</span>
                    <span class="value" id="free-heap">--</span><span class="unit">KB</span>
                </div>
            </div>
        </div>
        <div class="card">
            <h2>Sensor Readings</h2>
            <div id="sensor-readings">
                <p>Loading...</p>
            </div>
        </div>
    </main>
    <footer>
        <span id="connection-status">Connected</span>
        <span id="version">v2.0.0</span>
    </footer>
    <script src="/app.js"></script>
</body>
</html>
)rawliteral";
}

const char* getSettingsHtml() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Settings - iWetMyPlants</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <header>
        <a href="/" class="back-link">&larr; Back</a>
        <h1>Settings</h1>
    </header>
    <main>
        <div class="card">
            <h2>Device Settings</h2>
            <form id="device-form">
                <div class="form-group">
                    <label for="device-name">Device Name</label>
                    <input type="text" id="device-name" name="device_name" maxlength="31">
                </div>
                <button type="submit" class="btn-primary">Save</button>
            </form>
        </div>
        <div class="card">
            <h2>Configuration</h2>
            <div class="system-actions">
                <a href="/settings/wifi" class="btn btn-secondary">WiFi Settings</a>
                <a href="/settings/mqtt" class="btn btn-secondary">MQTT Settings</a>
            </div>
        </div>
        <div class="card">
            <h2>System</h2>
            <div class="system-actions">
                <button id="btn-reboot" class="btn btn-warning">Reboot Device</button>
                <button id="btn-reset" class="btn btn-danger">Factory Reset</button>
            </div>
        </div>
    </main>
    <script src="/app.js"></script>
</body>
</html>
)rawliteral";
}

const char* getWifiSettingsHtml() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WiFi Settings - iWetMyPlants</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <header>
        <a href="/settings" class="back-link">&larr; Back</a>
        <h1>WiFi Settings</h1>
    </header>
    <main>
        <div class="card">
            <h2>WiFi Configuration</h2>
            <form id="wifi-form">
                <div class="form-group">
                    <label for="ssid">Network Name (SSID)</label>
                    <input type="text" id="ssid" name="ssid" maxlength="32" required>
                    <button type="button" id="scan-btn" class="btn btn-secondary">Scan</button>
                    <div class="network-list" id="network-list" style="display:none;"></div>
                </div>
                <div class="form-group">
                    <label for="password">Password</label>
                    <input type="password" id="password" name="password" maxlength="64">
                </div>
                <div class="form-group">
                    <label>
                        <input type="checkbox" id="static-ip" name="use_static_ip">
                        Use Static IP
                    </label>
                </div>
                <div id="static-ip-fields" style="display:none;">
                    <div class="form-group">
                        <label for="ip">IP Address</label>
                        <input type="text" id="ip" name="static_ip" placeholder="192.168.1.100">
                    </div>
                    <div class="form-group">
                        <label for="gateway">Gateway</label>
                        <input type="text" id="gateway" name="gateway" placeholder="192.168.1.1">
                    </div>
                    <div class="form-group">
                        <label for="subnet">Subnet Mask</label>
                        <input type="text" id="subnet" name="subnet" placeholder="255.255.255.0">
                    </div>
                </div>
                <button type="submit" class="btn-primary">Save WiFi Settings</button>
            </form>
        </div>
    </main>
    <script src="/app.js"></script>
</body>
</html>
)rawliteral";
}

const char* getMqttSettingsHtml() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MQTT Settings - iWetMyPlants</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <header>
        <a href="/settings" class="back-link">&larr; Back</a>
        <h1>MQTT Settings</h1>
    </header>
    <main>
        <div class="card">
            <h2>MQTT Broker</h2>
            <form id="mqtt-form">
                <div class="form-group">
                    <label>
                        <input type="checkbox" id="mqtt-enabled" name="enabled">
                        Enable MQTT
                    </label>
                </div>
                <div class="form-group">
                    <label for="broker">Broker Address</label>
                    <input type="text" id="broker" name="broker" placeholder="192.168.1.10">
                </div>
                <div class="form-group">
                    <label for="port">Port</label>
                    <input type="number" id="port" name="port" value="1883">
                </div>
                <div class="form-group">
                    <label for="mqtt-user">Username (optional)</label>
                    <input type="text" id="mqtt-user" name="username">
                </div>
                <div class="form-group">
                    <label for="mqtt-pass">Password (optional)</label>
                    <input type="password" id="mqtt-pass" name="password">
                </div>
                <div class="form-group">
                    <label>
                        <input type="checkbox" id="ha-discovery" name="ha_discovery_enabled" checked>
                        Home Assistant Discovery
                    </label>
                </div>
                <button type="submit" class="btn-primary">Save MQTT Settings</button>
            </form>
        </div>
    </main>
    <script src="/app.js"></script>
</body>
</html>
)rawliteral";
}

const char* getSensorsHtml() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Sensors - iWetMyPlants</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <header>
        <a href="/" class="back-link">&larr; Back</a>
        <h1>Sensors</h1>
    </header>
    <main>
        <div class="card">
            <h2>Moisture Sensors</h2>
            <div id="sensor-list">
                <p>Loading...</p>
            </div>
        </div>
        <div class="card">
            <h2>Environmental</h2>
            <div id="env-readings">
                <div class="reading">
                    <span class="label">Temperature</span>
                    <span class="value" id="temperature">--</span><span class="unit">&deg;C</span>
                </div>
                <div class="reading">
                    <span class="label">Humidity</span>
                    <span class="value" id="humidity">--</span><span class="unit">%</span>
                </div>
            </div>
        </div>
    </main>
    <script src="/app.js"></script>
</body>
</html>
)rawliteral";
}

const char* getCalibrationHtml() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Calibration - iWetMyPlants</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <header>
        <a href="/sensors" class="back-link">&larr; Back</a>
        <h1>Sensor Calibration</h1>
    </header>
    <main>
        <div class="card">
            <h2>Select Sensor</h2>
            <select id="sensor-select" class="form-group">
                <option value="0">Sensor 1</option>
            </select>
        </div>
        <div class="card">
            <h2>Live Reading</h2>
            <div class="live-reading">
                <div class="reading-display">
                    <span class="label">Raw</span>
                    <span class="value" id="raw-value">--</span>
                </div>
                <div class="reading-display">
                    <span class="label">Average</span>
                    <span class="value" id="avg-value">--</span>
                </div>
                <div class="reading-display">
                    <span class="label">Percent</span>
                    <span class="value" id="pct-value">--</span>
                </div>
            </div>
            <div class="calibration-graph">
                <canvas id="reading-graph" height="150"></canvas>
            </div>
        </div>
        <div class="card">
            <h2>Calibration Points</h2>
            <div class="calibration-points">
                <div class="cal-point">
                    <h3>Dry Point</h3>
                    <p>Place sensor in air</p>
                    <span class="saved-value" id="dry-value">--</span>
                    <button id="btn-set-dry" class="btn btn-primary">Set Dry</button>
                </div>
                <div class="cal-point">
                    <h3>Wet Point</h3>
                    <p>Place sensor in water</p>
                    <span class="saved-value" id="wet-value">--</span>
                    <button id="btn-set-wet" class="btn btn-primary">Set Wet</button>
                </div>
            </div>
            <div class="calibration-actions">
                <button id="btn-save-cal" class="btn btn-success">Save Calibration</button>
            </div>
        </div>
    </main>
    <script src="/app.js"></script>
    <script>
        // WebSocket for rapid readings
        const ws = new WebSocket('ws://' + location.host + '/ws');
        ws.onmessage = function(event) {
            const data = JSON.parse(event.data);
            if (data.type === 'reading') {
                document.getElementById('raw-value').textContent = data.raw;
                document.getElementById('avg-value').textContent = data.avg;
                document.getElementById('pct-value').textContent = data.percent + '%';
            }
        };
        document.getElementById('btn-set-dry').onclick = () => ws.send(JSON.stringify({type:'set_dry', sensor:0}));
        document.getElementById('btn-set-wet').onclick = () => ws.send(JSON.stringify({type:'set_wet', sensor:0}));
        document.getElementById('btn-save-cal').onclick = () => ws.send(JSON.stringify({type:'save', sensor:0}));
    </script>
</body>
</html>
)rawliteral";
}

const char* getRelaysHtml() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Relays - iWetMyPlants</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <header>
        <a href="/" class="back-link">&larr; Back</a>
        <h1>Relay Control</h1>
    </header>
    <main>
        <div class="card">
            <h2>Relays</h2>
            <div id="relay-buttons">
                <p>Loading...</p>
            </div>
        </div>
    </main>
    <script src="/app.js"></script>
</body>
</html>
)rawliteral";
}

const char* getDevicesHtml() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Devices - iWetMyPlants</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <header>
        <a href="/" class="back-link">&larr; Back</a>
        <h1>Paired Devices</h1>
    </header>
    <main>
        <div class="card">
            <h2>Connected Devices</h2>
            <div id="device-list">
                <p>Loading...</p>
            </div>
        </div>
        <div class="card">
            <h2>Pairing</h2>
            <button id="btn-discover" class="btn btn-primary">Scan for Devices</button>
        </div>
    </main>
    <script src="/app.js"></script>
</body>
</html>
)rawliteral";
}

const char* getStyleCss() {
    return R"rawliteral(
:root {
    --primary-color: #4CAF50;
    --primary-dark: #388E3C;
    --secondary-color: #2196F3;
    --danger-color: #f44336;
    --warning-color: #FF9800;
    --background-color: #f5f5f5;
    --card-background: #ffffff;
    --text-color: #333333;
    --text-muted: #666666;
    --border-color: #e0e0e0;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: var(--background-color); color: var(--text-color); line-height: 1.6; }
header { background: var(--primary-color); color: white; padding: 1rem; display: flex; justify-content: space-between; align-items: center; }
header h1 { font-size: 1.5rem; }
header .back-link { color: white; text-decoration: none; }
nav { background: white; padding: 0.5rem 1rem; display: flex; gap: 1rem; border-bottom: 1px solid var(--border-color); }
nav a { color: var(--text-muted); text-decoration: none; padding: 0.5rem 1rem; border-radius: 4px; }
nav a.active { color: var(--primary-color); background: rgba(76,175,80,0.1); }
main { padding: 1rem; max-width: 800px; margin: 0 auto; }
.card { background: var(--card-background); border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); padding: 1rem; margin-bottom: 1rem; }
.card h2 { font-size: 1.1rem; margin-bottom: 1rem; border-bottom: 1px solid var(--border-color); padding-bottom: 0.5rem; }
.reading { display: flex; justify-content: space-between; padding: 0.5rem 0; border-bottom: 1px solid var(--border-color); }
.reading:last-child { border-bottom: none; }
.reading .value { font-size: 1.5rem; font-weight: 600; color: var(--primary-color); }
.form-group { margin-bottom: 1rem; }
.form-group label { display: block; margin-bottom: 0.25rem; color: var(--text-muted); }
.form-group input[type="text"], .form-group input[type="password"], .form-group input[type="number"], .form-group select { width: 100%; padding: 0.5rem; border: 1px solid var(--border-color); border-radius: 4px; }
button, .btn { padding: 0.5rem 1rem; border: none; border-radius: 4px; cursor: pointer; text-decoration: none; display: inline-block; }
.btn-primary { background: var(--primary-color); color: white; }
.btn-secondary { background: var(--secondary-color); color: white; }
.btn-warning { background: var(--warning-color); color: white; }
.btn-danger { background: var(--danger-color); color: white; }
.btn-success { background: var(--primary-color); color: white; }
.system-actions { display: flex; gap: 1rem; flex-wrap: wrap; }
.live-reading { display: grid; grid-template-columns: repeat(3, 1fr); gap: 1rem; }
.reading-display { text-align: center; padding: 1rem; background: var(--background-color); border-radius: 8px; }
.reading-display .value { display: block; font-size: 2rem; font-weight: bold; color: var(--primary-color); }
.calibration-points { display: grid; grid-template-columns: repeat(2, 1fr); gap: 1rem; }
.cal-point { padding: 1rem; background: var(--background-color); border-radius: 8px; text-align: center; }
.calibration-actions { text-align: center; margin-top: 1rem; }
footer { background: white; border-top: 1px solid var(--border-color); padding: 0.5rem 1rem; display: flex; justify-content: space-between; font-size: 0.8rem; color: var(--text-muted); position: fixed; bottom: 0; left: 0; right: 0; }
)rawliteral";
}

const char* getAppJs() {
    return R"rawliteral(
// iWetMyPlants Web App JavaScript

async function fetchJson(url) {
    const res = await fetch(url);
    return res.json();
}

async function postJson(url, data) {
    const res = await fetch(url, {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(data)
    });
    return res.json();
}

async function loadStatus() {
    try {
        const data = await fetchJson('/api/status');
        if (data.device_name) document.getElementById('device-name').textContent = data.device_name;
        if (data.wifi_connected !== undefined) document.getElementById('wifi-status').textContent = data.wifi_connected ? 'Connected' : 'Disconnected';
        if (data.uptime) document.getElementById('uptime').textContent = formatUptime(data.uptime);
        if (data.free_heap) document.getElementById('free-heap').textContent = Math.round(data.free_heap / 1024);
    } catch (e) { console.error('Status load failed:', e); }
}

async function loadSensors() {
    try {
        const data = await fetchJson('/api/sensors');
        const container = document.getElementById('sensor-readings') || document.getElementById('sensor-list');
        if (!container) return;
        let html = '';
        if (data.sensors) {
            data.sensors.forEach((s, i) => {
                html += `<div class="reading"><span class="label">${s.name || 'Sensor '+(i+1)}</span><span class="value">${s.percent}%</span></div>`;
            });
        }
        container.innerHTML = html || '<p>No sensors configured</p>';
    } catch (e) { console.error('Sensors load failed:', e); }
}

function formatUptime(seconds) {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return `${h}h ${m}m ${s}s`;
}

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    loadStatus();
    loadSensors();
    setInterval(loadStatus, 5000);
    setInterval(loadSensors, 10000);
});

// Form handlers
document.querySelectorAll('form').forEach(form => {
    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const formData = new FormData(form);
        const data = Object.fromEntries(formData);
        try {
            await postJson('/api/config', data);
            alert('Settings saved!');
        } catch (e) {
            alert('Error saving settings');
        }
    });
});

// Button handlers
document.getElementById('btn-reboot')?.addEventListener('click', async () => {
    if (confirm('Reboot device?')) {
        await postJson('/api/system/reboot', {});
        alert('Device rebooting...');
    }
});

document.getElementById('btn-reset')?.addEventListener('click', async () => {
    if (confirm('Factory reset? This will erase all settings!')) {
        await postJson('/api/config/reset', {});
        alert('Device reset. Rebooting...');
    }
});
)rawliteral";
}

} // namespace iwmp
