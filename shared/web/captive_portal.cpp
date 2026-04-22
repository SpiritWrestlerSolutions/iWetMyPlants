/**
 * @file captive_portal.cpp
 * @brief Captive portal implementation
 */

#include "captive_portal.h"
#include <WiFi.h>
#include <ArduinoJson.h>
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
    int n = WiFi.scanComplete();

    // Use ArduinoJson for output so SSID strings are JSON-escaped
    // properly. Manual concat against unsanitised SSIDs corrupts the
    // JSON if a network name contains a quote or backslash and is the
    // server side of an XSS chain when consumed by innerHTML.
    JsonDocument doc;

    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);  // Start async scan
        doc["scanning"] = true;
    } else if (n == WIFI_SCAN_RUNNING) {
        doc["scanning"] = true;
    } else {
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < n; i++) {
            JsonObject net = arr.add<JsonObject>();
            net["ssid"]   = WiFi.SSID(i);  // ArduinoJson handles escaping
            net["rssi"]   = WiFi.RSSI(i);
            net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        WiFi.scanDelete();
    }

    char buf[2048];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len >= sizeof(buf)) {
        // Too many networks to fit in buffer — return what serialised
        // (truncated JSON would be invalid). Cap at empty array.
        request->send(200, "application/json", "[]");
        return;
    }
    request->send(200, "application/json", buf);
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
                <label for="password">Password <span id="pwdHint" class="hint"></span></label>
                <input type="password" id="password" name="password" maxlength="64">
            </div>

            <button type="submit">Connect</button>
        </form>

        <p class="info">Select a network from the list or enter manually.</p>
    </div>

    <script>
        // Build network list with DOM APIs so SSID strings (server-controlled
        // but ultimately attacker-controlled at the radio layer) cannot
        // execute as HTML. innerHTML interpolation of WiFi.SSID() is XSS.
        var scanAttempts = 0;
        var MAX_SCAN_POLLS = 10;  // ~20 seconds at 2s interval

        function rssiBars(r) {
            if (r > -50) return 4;
            if (r > -60) return 3;
            if (r > -70) return 2;
            return 1;
        }

        function rssiLabel(r) {
            if (r > -50) return 'Excellent';
            if (r > -60) return 'Good';
            if (r > -70) return 'Fair';
            return 'Weak';
        }

        function clearChildren(el) {
            while (el.firstChild) el.removeChild(el.firstChild);
        }

        function makeRescanLink(text) {
            var p = document.createElement('p');
            var a = document.createElement('a');
            a.href = '#';
            a.textContent = text || 'Scan again';
            a.addEventListener('click', function(e) {
                e.preventDefault();
                scanAttempts = 0;
                scanNetworks();
            });
            p.appendChild(a);
            return p;
        }

        function renderNetworks(container, list) {
            clearChildren(container);
            if (!list.length) {
                var p = document.createElement('p');
                p.textContent = 'No networks found. ';
                container.appendChild(p);
                container.appendChild(makeRescanLink());
                return;
            }
            var ul = document.createElement('ul');
            ul.className = 'network-list';
            list.forEach(function(net) {
                var li = document.createElement('li');
                li.title = (net.secure ? 'Secured' : 'Open') +
                           ' · ' + rssiLabel(net.rssi) + ' (' + net.rssi + ' dBm)';
                li.addEventListener('click', function() {
                    selectNetwork(net.ssid, net.secure);
                });

                var ssid = document.createElement('span');
                ssid.className = 'ssid';
                ssid.textContent = net.ssid;  // safe: textContent escapes

                var meta = document.createElement('span');
                meta.className = 'meta';
                meta.textContent = (net.secure ? '🔒 ' : '') +
                                   '●'.repeat(rssiBars(net.rssi)) +
                                   '○'.repeat(4 - rssiBars(net.rssi));

                li.appendChild(ssid);
                li.appendChild(meta);
                ul.appendChild(li);
            });
            container.appendChild(ul);
            container.appendChild(makeRescanLink());
        }

        function scanNetworks() {
            var container = document.getElementById('networks');
            fetch('/scan')
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    if (data && data.scanning) {
                        scanAttempts++;
                        if (scanAttempts >= MAX_SCAN_POLLS) {
                            clearChildren(container);
                            var p = document.createElement('p');
                            p.textContent = 'Scan timed out. ';
                            container.appendChild(p);
                            container.appendChild(makeRescanLink('Try again'));
                            return;
                        }
                        setTimeout(scanNetworks, 2000);
                        return;
                    }
                    scanAttempts = 0;
                    renderNetworks(container, Array.isArray(data) ? data : []);
                })
                .catch(function() {
                    clearChildren(container);
                    var p = document.createElement('p');
                    p.textContent = 'Scan failed. ';
                    container.appendChild(p);
                    container.appendChild(makeRescanLink('Retry'));
                });
        }

        function selectNetwork(ssid, secure) {
            document.getElementById('ssid').value = ssid;
            var pwd = document.getElementById('password');
            var hint = document.getElementById('pwdHint');
            if (secure) {
                pwd.disabled = false;
                pwd.placeholder = '';
                hint.textContent = '';
                pwd.focus();
            } else {
                pwd.value = '';
                pwd.disabled = true;
                pwd.placeholder = 'Open network — leave blank';
                hint.textContent = '(open network)';
            }
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

.hint {
    color: #2d8f47;
    font-size: 13px;
    font-weight: normal;
    margin-left: 6px;
}

input:disabled {
    background: #f5f5f5;
    color: #999;
    cursor: not-allowed;
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
        .countdown { font-size: 14px; color: #999; margin-top: 24px; }
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
        <p>If successful, this access point will close. Reconnect to your normal WiFi network and find the device on its new IP.</p>
        <p>If connection fails, the setup page will reappear.</p>
        <p class="countdown" id="countdown">This page will close in 30s…</p>
    </div>
    <script>
        // Countdown helps users on phones that don't auto-dismiss the
        // captive portal panel after the AP goes away.
        var secs = 30;
        var el = document.getElementById('countdown');
        var t = setInterval(function() {
            secs--;
            if (secs <= 0) {
                clearInterval(t);
                el.textContent = 'You can close this page now.';
                try { window.close(); } catch (e) {}
                return;
            }
            el.textContent = 'This page will close in ' + secs + 's…';
        }, 1000);
    </script>
</body>
</html>
)rawliteral";
}

} // namespace iwmp
