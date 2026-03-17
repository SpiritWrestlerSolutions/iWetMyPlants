/**
 * @file captive_portal.cpp
 * @brief Captive portal implementation
 */

#include "captive_portal.h"
#include <WiFi.h>
#include "../utils/logger.h"

namespace iwmp {

// DNS server port
static constexpr uint16_t DNS_PORT = 53;
static constexpr const char* TAG = "Portal";

// Default AP IP configuration
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

CaptivePortal& CaptivePortal::getInstance() {
    static CaptivePortal instance;
    return instance;
}

CaptivePortal::CaptivePortal() {
    _ap_ssid[0] = '\0';
    _ap_password[0] = '\0';
    _ap_ip = AP_IP;
}

bool CaptivePortal::begin(const char* ap_ssid, const char* ap_password, uint32_t timeout_ms) {
    if (_state != PortalState::INACTIVE) {
        stop();
    }

    setState(PortalState::STARTING);

    // Store configuration
    strlcpy(_ap_ssid, ap_ssid, sizeof(_ap_ssid));
    strlcpy(_ap_password, ap_password, sizeof(_ap_password));
    _timeout_ms = timeout_ms;
    _start_time = millis();

    // Disconnect from any existing WiFi
    WiFi.disconnect(true);
    delay(100);

    // Configure AP
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);

    // Start AP
    bool ap_started;
    if (strlen(_ap_password) >= 8) {
        ap_started = WiFi.softAP(_ap_ssid, _ap_password);
    } else {
        ap_started = WiFi.softAP(_ap_ssid);
    }

    if (!ap_started) {
        LOG_E(TAG, "Failed to start AP");
        setState(PortalState::INACTIVE);
        return false;
    }

    _ap_ip = WiFi.softAPIP();
    LOG_I(TAG, "AP started: %s @ %s", _ap_ssid, _ap_ip.toString().c_str());

    // Start DNS server (redirect all domains to AP IP)
    if (!_dns.start(DNS_PORT, "*", _ap_ip)) {
        LOG_E(TAG, "Failed to start DNS");
        WiFi.softAPdisconnect(true);
        setState(PortalState::INACTIVE);
        return false;
    }

    // Create web server
    _server = new AsyncWebServer(80);
    setupRoutes();
    _server->begin();

    LOG_I(TAG, "Web server started");
    setState(PortalState::ACTIVE);

    return true;
}

bool CaptivePortal::begin(const DeviceIdentity& identity, uint32_t timeout_ms) {
    // Generate AP name from device ID
    char ap_name[33];
    snprintf(ap_name, sizeof(ap_name), "iWetMyPlants-%s", identity.device_id);
    return begin(ap_name, "", timeout_ms);
}

void CaptivePortal::stop() {
    if (_state == PortalState::INACTIVE) {
        return;
    }

    // Stop DNS
    _dns.stop();

    // Stop and delete web server
    if (_server) {
        _server->end();
        delete _server;
        _server = nullptr;
    }

    // Stop AP
    WiFi.softAPdisconnect(true);

    setState(PortalState::INACTIVE);
    LOG_I(TAG, "Stopped");
}

void CaptivePortal::loop() {
    if (_state != PortalState::ACTIVE) {
        return;
    }

    // Process DNS requests
    _dns.processNextRequest();

    // Check timeout
    if (_timeout_ms > 0 && (millis() - _start_time) > _timeout_ms) {
        LOG_W(TAG, "Timeout reached");
        setState(PortalState::TIMEOUT);
        stop();
    }
}

uint8_t CaptivePortal::getClientCount() const {
    return WiFi.softAPgetStationNum();
}

void CaptivePortal::onCredentials(CredentialsCallback callback) {
    _credentials_callback = callback;
}

void CaptivePortal::onStatusChange(PortalStatusCallback callback) {
    _status_callback = callback;
}

bool CaptivePortal::tryConnect(const char* ssid, const char* password, uint32_t timeout_ms) {
    setState(PortalState::CONNECTING);

    // Keep AP running while attempting connection
    WiFi.mode(WIFI_AP_STA);

    WiFi.begin(ssid, password);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
        delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
        LOG_I(TAG, "Connected to %s, IP: %s",
                      ssid, WiFi.localIP().toString().c_str());
        setState(PortalState::CONNECTED);
        return true;
    }

    LOG_W(TAG, "Failed to connect to %s", ssid);
    setState(PortalState::FAILED);

    // Return to AP-only mode
    WiFi.mode(WIFI_AP);
    setState(PortalState::ACTIVE);

    return false;
}

void CaptivePortal::setState(PortalState state) {
    if (_state != state) {
        _state = state;
        if (_status_callback) {
            _status_callback(state == PortalState::ACTIVE);
        }
    }
}

void CaptivePortal::setupRoutes() {
    if (!_server) {
        return;
    }

    // Captive portal detection endpoints
    // These are checked by various OS/browsers
    _server->on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest* request) {
        request->redirect("http://" + _ap_ip.toString() + "/");
    });

    _server->on("/fwlink", HTTP_GET, [this](AsyncWebServerRequest* request) {
        request->redirect("http://" + _ap_ip.toString() + "/");
    });

    _server->on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest* request) {
        servePortalPage(request);
    });

    _server->on("/canonical.html", HTTP_GET, [this](AsyncWebServerRequest* request) {
        servePortalPage(request);
    });

    _server->on("/success.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "success");
    });

    // Main portal page
    _server->on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        servePortalPage(request);
    });

    // CSS
    _server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/css", getPortalCss());
    });

    // WiFi network scan
    _server->on("/scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleScanNetworks(request);
    });

    // Credential submission
    _server->on("/connect", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleCredentials(request);
    });

    // Success page
    _server->on("/success", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", getSuccessHtml());
    });

    // Catch-all redirect
    _server->onNotFound([this](AsyncWebServerRequest* request) {
        request->redirect("http://" + _ap_ip.toString() + "/");
    });
}

void CaptivePortal::servePortalPage(AsyncWebServerRequest* request) {
    request->send(200, "text/html", getPortalHtml());
}

void CaptivePortal::handleCredentials(AsyncWebServerRequest* request) {
    if (!request->hasParam("ssid", true)) {
        request->send(400, "text/plain", "Missing SSID");
        return;
    }

    String ssid = request->getParam("ssid", true)->value();
    String password = request->hasParam("password", true) ?
                      request->getParam("password", true)->value() : "";

    LOG_I(TAG, "Received credentials for: %s", ssid.c_str());

    // Notify callback
    bool should_connect = true;
    if (_credentials_callback) {
        should_connect = _credentials_callback(ssid.c_str(), password.c_str());
    }

    if (should_connect) {
        // Redirect to success page before attempting connection
        request->redirect("/success");

        // Attempt connection after short delay
        // (allows redirect response to be sent)
        delay(500);

        if (tryConnect(ssid.c_str(), password.c_str())) {
            // Success - portal will stop when main code handles CONNECTED state
        }
    } else {
        request->send(400, "text/plain", "Credentials rejected");
    }
}

void CaptivePortal::handleScanNetworks(AsyncWebServerRequest* request) {
    String json = "[";

    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);  // Start async scan
        json = "{\"scanning\":true}";
    } else if (n == WIFI_SCAN_RUNNING) {
        json = "{\"scanning\":true}";
    } else {
        for (int i = 0; i < n; i++) {
            if (i > 0) {
                json += ",";
            }
            json += "{";
            json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
            json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
            json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            json += "}";
        }
        json += "]";
        WiFi.scanDelete();
    }

    request->send(200, "application/json", json);
}

// ============ HTML Content ============

const char* CaptivePortal::getPortalHtml() {
    return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>iWetMyPlants Setup</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <div class="container">
        <h1>iWetMyPlants</h1>
        <h2>WiFi Setup</h2>

        <div id="networks">
            <p>Scanning for networks...</p>
        </div>

        <form id="wifi-form" action="/connect" method="POST">
            <div class="form-group">
                <label for="ssid">Network Name (SSID)</label>
                <input type="text" id="ssid" name="ssid" required maxlength="32">
            </div>

            <div class="form-group">
                <label for="password">Password</label>
                <input type="password" id="password" name="password" maxlength="64">
            </div>

            <button type="submit">Connect</button>
        </form>

        <p class="info">Select a network from the list or enter manually.</p>
    </div>

    <script>
        function scanNetworks() {
            fetch('/scan')
                .then(r => r.json())
                .then(data => {
                    if (data.scanning) {
                        setTimeout(scanNetworks, 2000);
                        return;
                    }

                    const container = document.getElementById('networks');
                    if (!data.length) {
                        container.innerHTML = '<p>No networks found. <a href="#" onclick="scanNetworks()">Scan again</a></p>';
                        return;
                    }

                    let html = '<ul class="network-list">';
                    data.forEach(net => {
                        const icon = net.secure ? '&#128274;' : '';
                        const bars = net.rssi > -50 ? 4 : net.rssi > -60 ? 3 : net.rssi > -70 ? 2 : 1;
                        html += '<li onclick="selectNetwork(\'' + net.ssid.replace(/'/g, "\\'") + '\')">';
                        html += '<span class="ssid">' + net.ssid + '</span>';
                        html += '<span class="meta">' + icon + ' ' + '&#9679;'.repeat(bars) + '</span>';
                        html += '</li>';
                    });
                    html += '</ul>';
                    html += '<p><a href="#" onclick="scanNetworks()">Scan again</a></p>';
                    container.innerHTML = html;
                })
                .catch(() => {
                    setTimeout(scanNetworks, 3000);
                });
        }

        function selectNetwork(ssid) {
            document.getElementById('ssid').value = ssid;
            document.getElementById('password').focus();
        }

        scanNetworks();
    </script>
</body>
</html>
)rawliteral";
}

const char* CaptivePortal::getPortalCss() {
    return R"rawliteral(
* {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
}

body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    background: linear-gradient(135deg, #1a5f2a 0%, #2d8f47 100%);
    min-height: 100vh;
    display: flex;
    justify-content: center;
    align-items: center;
    padding: 20px;
}

.container {
    background: white;
    border-radius: 16px;
    padding: 32px;
    width: 100%;
    max-width: 400px;
    box-shadow: 0 10px 40px rgba(0,0,0,0.2);
}

h1 {
    color: #1a5f2a;
    text-align: center;
    margin-bottom: 8px;
    font-size: 28px;
}

h2 {
    color: #666;
    text-align: center;
    font-weight: normal;
    margin-bottom: 24px;
    font-size: 16px;
}

.network-list {
    list-style: none;
    margin-bottom: 16px;
    max-height: 200px;
    overflow-y: auto;
    border: 1px solid #ddd;
    border-radius: 8px;
}

.network-list li {
    padding: 12px 16px;
    border-bottom: 1px solid #eee;
    cursor: pointer;
    display: flex;
    justify-content: space-between;
    align-items: center;
}

.network-list li:last-child {
    border-bottom: none;
}

.network-list li:hover {
    background: #f5f5f5;
}

.network-list .ssid {
    font-weight: 500;
}

.network-list .meta {
    color: #666;
    font-size: 14px;
}

.form-group {
    margin-bottom: 16px;
}

.form-group label {
    display: block;
    margin-bottom: 6px;
    color: #333;
    font-weight: 500;
}

.form-group input {
    width: 100%;
    padding: 12px;
    border: 1px solid #ddd;
    border-radius: 8px;
    font-size: 16px;
}

.form-group input:focus {
    outline: none;
    border-color: #2d8f47;
    box-shadow: 0 0 0 3px rgba(45,143,71,0.1);
}

button {
    width: 100%;
    padding: 14px;
    background: #2d8f47;
    color: white;
    border: none;
    border-radius: 8px;
    font-size: 16px;
    font-weight: 600;
    cursor: pointer;
    transition: background 0.2s;
}

button:hover {
    background: #1a5f2a;
}

.info {
    text-align: center;
    color: #666;
    font-size: 14px;
    margin-top: 16px;
}

a {
    color: #2d8f47;
}
)rawliteral";
}

const char* CaptivePortal::getSuccessHtml() {
    return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Connecting...</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #1a5f2a 0%, #2d8f47 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 16px;
            padding: 48px 32px;
            text-align: center;
            max-width: 400px;
        }
        h1 { color: #1a5f2a; margin-bottom: 16px; }
        p { color: #666; line-height: 1.6; }
        .spinner {
            width: 48px;
            height: 48px;
            border: 4px solid #eee;
            border-top-color: #2d8f47;
            border-radius: 50%;
            animation: spin 1s linear infinite;
            margin: 24px auto;
        }
        @keyframes spin {
            to { transform: rotate(360deg); }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Connecting...</h1>
        <div class="spinner"></div>
        <p>Attempting to connect to your WiFi network.</p>
        <p>If successful, this access point will close and you can reconnect to your normal network.</p>
        <p>If connection fails, the setup page will reappear.</p>
    </div>
</body>
</html>
)rawliteral";
}

} // namespace iwmp
