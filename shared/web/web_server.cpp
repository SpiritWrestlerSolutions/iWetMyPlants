/**
 * @file web_server.cpp
 * @brief Async web server implementation
 */

#include "web_server.h"
#include "api_endpoints.h"
#include "../utils/ota_manager.h"
#include "../calibration/rapid_read.h"

namespace iwmp {

// Forward declarations for embedded HTML/CSS/JS getters
const char* getIndexHtml();
const char* getSettingsHtml();
const char* getWifiSettingsHtml();
const char* getMqttSettingsHtml();
const char* getSensorsHtml();
const char* getCalibrationHtml();
const char* getRelaysHtml();
const char* getDevicesHtml();
const char* getStyleCss();
const char* getAppJs();

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

    // Initialize OTA manager
    Ota.begin(_server);

    // Initialize rapid read WebSocket for calibration
    RapidRead.begin(_server);

    // Start server
    _server->begin();
    _running = true;

    Serial.printf("[Web] Server started on port %d\n", WEB_SERVER_PORT);
    return true;
}

void WebServer::update() {
    if (!_running) {
        return;
    }

    // Check for pending OTA reboot
    Ota.checkPendingReboot();

    // Update rapid read for calibration WebSocket
    RapidRead.update();
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
                            _calibration_callback(sensor, "dry");
                        }
                    } else if (strcmp(type, "set_wet") == 0) {
                        uint8_t sensor = doc["sensor"] | 0;
                        if (_calibration_callback) {
                            _calibration_callback(sensor, "wet");
                        }
                    } else if (strcmp(type, "save") == 0) {
                        uint8_t sensor = doc["sensor"] | 0;
                        if (_calibration_callback) {
                            _calibration_callback(sensor, "save");
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

// Helper to send HTML with no-cache headers
static void sendHtmlNoCache(AsyncWebServerRequest* request, const char* html) {
    AsyncWebServerResponse* response = request->beginResponse(200, "text/html", html);
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    request->send(response);
}

void WebServer::registerStaticRoutes() {
    // Register more specific routes FIRST (important for route matching)

    // Settings sub-pages (register before /settings)
    _server->on("/settings/wifi", HTTP_GET, [](AsyncWebServerRequest* request) {
        Serial.println("[Web] GET /settings/wifi");
        sendHtmlNoCache(request, getWifiSettingsHtml());
    });

    _server->on("/settings/mqtt", HTTP_GET, [](AsyncWebServerRequest* request) {
        Serial.println("[Web] GET /settings/mqtt");
        sendHtmlNoCache(request, getMqttSettingsHtml());
    });

    // Sensors sub-pages (register before /sensors)
    _server->on("/sensors/calibrate", HTTP_GET, [](AsyncWebServerRequest* request) {
        Serial.println("[Web] GET /sensors/calibrate");
        sendHtmlNoCache(request, getCalibrationHtml());
    });

    // Main pages
    _server->on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        Serial.println("[Web] GET /");
        sendHtmlNoCache(request, getIndexHtml());
    });

    _server->on("/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        Serial.println("[Web] GET /settings");
        sendHtmlNoCache(request, getSettingsHtml());
    });

    _server->on("/sensors", HTTP_GET, [](AsyncWebServerRequest* request) {
        Serial.println("[Web] GET /sensors");
        sendHtmlNoCache(request, getSensorsHtml());
    });

    _server->on("/relays", HTTP_GET, [](AsyncWebServerRequest* request) {
        Serial.println("[Web] GET /relays");
        sendHtmlNoCache(request, getRelaysHtml());
    });

    _server->on("/devices", HTTP_GET, [](AsyncWebServerRequest* request) {
        Serial.println("[Web] GET /devices");
        sendHtmlNoCache(request, getDevicesHtml());
    });

    // Static assets
    _server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/css", getStyleCss());
    });

    _server->on("/app.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/javascript", getAppJs());
    });

    // Captive portal detection endpoints
    // MUST use absolute URL with AP IP — relative redirects resolve against
    // the original domain (e.g. connectivitycheck.gstatic.com), causing the
    // browser to leave the AP network and hit ERR_ADDRESS_UNREACHABLE.
    static const char* CAPTIVE_REDIRECT = "http://192.168.4.1/settings/wifi";

    // Android
    _server->on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect(CAPTIVE_REDIRECT);
    });
    _server->on("/gen_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect(CAPTIVE_REDIRECT);
    });
    // iOS
    _server->on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect(CAPTIVE_REDIRECT);
    });
    // Windows
    _server->on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect(CAPTIVE_REDIRECT);
    });
    _server->on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect(CAPTIVE_REDIRECT);
    });
    // Firefox
    _server->on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect(CAPTIVE_REDIRECT);
    });
    // Fallback for favicon and other noise
    _server->on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(204);
    });

    // Catch-all: redirect unknown requests to WiFi setup page
    // This handles any captive portal probe we didn't explicitly list
    _server->onNotFound([](AsyncWebServerRequest* request) {
        // Don't redirect API calls — return 404 for those
        if (request->url().startsWith("/api/")) {
            request->send(404, "application/json", "{\"error\":\"Not found\"}");
            return;
        }
        Serial.printf("[Web] Redirect: %s -> captive portal\n", request->url().c_str());
        request->redirect(CAPTIVE_REDIRECT);
    });
}

void WebServer::registerApiRoutes() {
    // Register all API endpoints
    ApiEndpoints::registerRoutes(*_server);
}

// ============ Embedded HTML/CSS/JS ============
// These return the embedded web content

const char* getIndexHtml() {
    return R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>iWetMyPlants</title><link rel="stylesheet" href="/style.css">
<style>
.sg{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:.7rem;margin-top:.5rem}
.sc{background:#f9f9f9;border-radius:10px;padding:.8rem;transition:box-shadow .2s}
.sc:hover{box-shadow:0 2px 8px rgba(0,0,0,.12)}
.sc-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:.1rem}
.sc-hdr b{font-size:.88rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:72%}
.sc-badge{padding:1px 6px;border-radius:10px;font-size:.65rem;font-weight:600}
.sc-ok{background:#c8e6c9;color:#2e7d32}.sc-err{background:#ffcdd2;color:#c62828}.sc-off{background:#e0e0e0;color:#888}
.sc-pct{font-size:2.1rem;font-weight:700;text-align:center;padding:.1rem 0;line-height:1.1}
.sc-pct small{font-size:.72rem;color:#888;font-weight:400;display:block;margin-top:2px}
.sc-bar{height:6px;background:#e0e0e0;border-radius:3px;margin:.3rem 0;overflow:hidden}
.sc-fill{height:100%;border-radius:3px;transition:width .5s}
.sc-warn{display:flex;align-items:center;gap:4px;font-size:.72rem;color:#e65100;padding:2px 0}
.sc-row{display:flex;justify-content:space-between;font-size:.72rem;color:#888}
.rg{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:.7rem;margin-top:.5rem}
.rd{background:#f9f9f9;border-radius:10px;padding:.8rem;transition:box-shadow .2s}
.rd:hover{box-shadow:0 2px 8px rgba(0,0,0,.12)}
.rd-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:.1rem}
.rd-hdr b{font-size:.88rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:70%}
.rd-badge{padding:1px 6px;border-radius:10px;font-size:.65rem;font-weight:600}
.rd-on{background:#c8e6c9;color:#2e7d32}.rd-off{background:#ffcdd2;color:#c62828}
.rd-pct{font-size:2.1rem;font-weight:700;text-align:center;padding:.1rem 0;line-height:1.1}
.rd-pct small{font-size:.72rem;color:#888;font-weight:400;display:block;margin-top:2px}
.rd-bar{height:6px;background:#e0e0e0;border-radius:3px;margin:.3rem 0;overflow:hidden}
.rd-fill{height:100%;border-radius:3px;transition:width .5s}
.rd-row{display:flex;justify-content:space-between;font-size:.72rem;color:#888}
.sys-bar{display:flex;gap:1.5rem;flex-wrap:wrap;font-size:.85rem;padding:.4rem 0}
.sys-bar .si{display:flex;align-items:center;gap:.3rem}
.sys-bar .dot{width:8px;height:8px;border-radius:50%;display:inline-block}
.sys-bar b{font-weight:600}
.sec-hdr{display:flex;justify-content:space-between;align-items:center}
.sec-hdr h2{margin-bottom:.5rem;border-bottom:none;padding-bottom:0}
.sec-sub{font-size:.78rem;color:#666;font-weight:400}
</style></head><body>
<header><h1>iWetMyPlants</h1><span id="device-name">Loading...</span></header>
<nav><a href="/" class="active">Dashboard</a><a href="/sensors">Sensors</a><a href="/devices">Devices</a><a href="/settings">Settings</a></nav>
<main>
<div class="card">
<div class="sys-bar">
<div class="si"><span class="dot" id="wifi-dot" style="background:#ccc"></span> <span id="wifi-label">WiFi: --</span></div>
<div class="si">Uptime: <b id="uptime">--</b></div>
<div class="si">Heap: <b id="free-heap">--</b> KB</div>
</div>
</div>
<div class="card"><div class="sec-hdr"><h2>Local Sensors</h2><span class="sec-sub" id="s-count"></span></div>
<div class="sg" id="sensor-cards"><p style="color:#888">Loading...</p></div>
<a href="/sensors" style="display:block;text-align:center;margin-top:.5rem;font-size:.78rem;color:#2196F3;text-decoration:none">Configure sensors &rarr;</a>
</div>
<div class="card"><div class="sec-hdr"><h2>Remote Devices</h2><span class="sec-sub" id="rd-count"></span></div>
<div id="remote-devs"><p style="color:#888">Loading...</p></div>
<a href="/devices" style="display:block;text-align:center;margin-top:.5rem;font-size:.78rem;color:#2196F3;text-decoration:none">Charts &amp; history &rarr;</a>
</div>
</main>
<footer><span id="connection-status">Connected</span><span id="version">v2.0.0</span></footer>
<script>
function esc(s){var d=document.createElement('div');d.textContent=s;return d.innerHTML}
function bc(p){return p>60?'#4CAF50':p>30?'#FF9800':'#f44336'}
function sigLabel(r){return r>-50?'Excellent':r>-60?'Good':r>-70?'Fair':'Weak'}
function fmt(ms){var s=Math.floor(ms/1000),h=Math.floor(s/3600),m=Math.floor((s%3600)/60);return h+'h '+m+'m'}
function loadStatus(){fetch('/api/status').then(function(r){return r.json()}).then(function(d){
 var dn=document.getElementById('device-name');if(dn)dn.textContent=d.device_id||'';
 var wd=document.getElementById('wifi-dot'),wl=document.getElementById('wifi-label');
 if(d.wifi){wd.style.background=d.wifi.connected?'#4CAF50':'#f44336';wl.textContent='WiFi: '+(d.wifi.connected?(d.wifi.ssid+' ('+d.wifi.rssi+' dBm)'):'Disconnected')}
 var up=document.getElementById('uptime');if(up&&d.uptime_ms!==undefined)up.textContent=fmt(d.uptime_ms);
 var fh=document.getElementById('free-heap');if(fh&&d.free_heap!==undefined)fh.textContent=Math.round(d.free_heap/1024);
}).catch(function(){})}
function loadSensors(){fetch('/api/sensors').then(function(r){return r.json()}).then(function(d){
 var c=document.getElementById('sensor-cards'),ss=d.sensors||[];
 var enabled=ss.filter(function(s){return s.enabled});
 var cnt=document.getElementById('s-count');
 cnt.textContent='('+enabled.length+' active)';
 if(!enabled.length){c.innerHTML='<p style="color:#888">No sensors enabled. <a href="/sensors">Add sensors</a></p>';return}
 c.innerHTML=enabled.map(function(s){
  var p=(s.percent!==undefined)?s.percent:0;
  var w=s.warning_level||0;
  var warn=(w>0&&s.percent!==undefined&&p<=w);
  var hw=(s.input_type===1)?(s.hw_connected?'sc-ok':'sc-err'):'sc-ok';
  return'<div class="sc'+(warn?' sc-warning':'')+'"><div class="sc-hdr"><b>'+esc(s.name||'Sensor')+'</b>'
  +'<span class="sc-badge '+(s.ready?hw:'sc-off')+'">'+(s.ready?'OK':'--')+'</span></div>'
  +'<div class="sc-pct" style="color:'+bc(p)+'">'+(s.percent!==undefined?p:'--')+'<small>'+(s.percent!==undefined?'% moisture':'offline')+'</small></div>'
  +'<div class="sc-bar"><div class="sc-fill" style="width:'+p+'%;background:'+bc(p)+'"></div></div>'
  +(warn?'<div class="sc-warn">&#9888; Below '+w+'%</div>':'')
  +'<div class="sc-row"><span>Raw: '+(s.raw!==undefined?s.raw:'--')+'</span><span>'+['ADC','ADS','Mux'][s.input_type]+'</span></div>'
  +'</div>'
 }).join('');
}).catch(function(){})}
function loadDevs(){fetch('/api/devices').then(function(r){return r.json()}).then(function(d){
 var c=document.getElementById('remote-devs'),devs=d.devices||[];
 var cnt=document.getElementById('rd-count');
 if(!devs.length){c.innerHTML='<p style="color:#888">No remote devices paired yet. Set a Remote\'s Hub Address to this Hub\'s IP.</p>';cnt.textContent='';return}
 var on=devs.filter(function(v){return v.online}).length;
 cnt.textContent='('+on+'/'+devs.length+' online)';
 c.innerHTML='<div class="rg">'+devs.map(function(v){
  var nm=v.name&&v.name!=='Unknown'?esc(v.name):v.mac;
  var p=v.moisture_percent||0;
  return'<div class="rd"><div class="rd-hdr"><b>'+nm+'</b>'
  +'<span class="rd-badge '+(v.online?'rd-on':'rd-off')+'">'+(v.online?'Online':'Offline')+'</span></div>'
  +'<div class="rd-pct" style="color:'+bc(p)+'">'+p+'<small>% moisture</small></div>'
  +'<div class="rd-bar"><div class="rd-fill" style="width:'+p+'%;background:'+bc(p)+'"></div></div>'
  +'<div class="rd-row"><span>Signal: '+v.rssi+' dBm ('+sigLabel(v.rssi)+')</span></div>'
  +'</div>'
 }).join('')+'</div>';
}).catch(function(){})}
loadStatus();loadSensors();loadDevs();
setInterval(loadStatus,5000);setInterval(loadSensors,5000);setInterval(loadDevs,5000);
</script></body></html>)rawliteral";
}

const char* getSettingsHtml() {
    return R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Settings - iWetMyPlants</title><link rel="stylesheet" href="/style.css"></head><body><header><a href="/" class="back-link">&larr; Back</a><h1>Settings</h1></header><main><div class="card"><h2>Device Settings</h2><form id="device-form"><div class="form-group"><label for="device-name">Device Name</label><input type="text" id="device-name" name="device_name" maxlength="31"></div><button type="submit" class="btn-primary">Save</button></form></div><div class="card"><h2>Configuration</h2><div class="system-actions"><a href="/settings/wifi" class="btn btn-secondary">WiFi Settings</a><a href="/settings/mqtt" class="btn btn-secondary">MQTT Settings</a></div></div><div class="card"><h2>System</h2><div class="system-actions"><button id="btn-reboot" class="btn btn-warning">Reboot Device</button><button id="btn-reset" class="btn btn-danger">Factory Reset</button></div></div></main><script src="/app.js"></script></body></html>)rawliteral";
}

const char* getWifiSettingsHtml() {
    return R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>WiFi Settings</title><link rel="stylesheet" href="/style.css"></head><body><header><a href="/settings" class="back-link">&larr; Back</a><h1>WiFi Settings</h1></header><main><div class="card"><h2>Current Status</h2><div id="wifi-status"><div class="reading"><span class="label">Connected</span><span class="value" id="wifi-connected">--</span></div><div class="reading"><span class="label">SSID</span><span class="value" id="current-ssid">--</span></div><div class="reading"><span class="label">IP Address</span><span class="value" id="current-ip">--</span></div><div class="reading"><span class="label">Signal</span><span class="value" id="current-rssi">--</span></div></div></div><div class="card"><h2>WiFi Configuration</h2><form id="wifi-form"><div class="form-group"><label for="ssid">Network Name (SSID)</label><input type="text" id="ssid" name="ssid" maxlength="32" required></div><div class="form-group"><button type="button" id="scan-btn" class="btn btn-secondary" style="width:100%">Scan for Networks</button><div id="network-list" style="display:none;margin-top:10px;max-height:200px;overflow-y:auto"></div></div><div class="form-group"><label for="password">Password</label><input type="password" id="password" name="password" maxlength="64"><label style="font-size:.8em;margin-top:5px"><input type="checkbox" id="show-password"> Show password</label></div><div class="form-group"><label><input type="checkbox" id="static-ip" name="use_static_ip"> Use Static IP</label></div><div id="static-ip-fields" style="display:none"><div class="form-group"><label for="ip">IP Address</label><input type="text" id="ip" name="static_ip" placeholder="192.168.1.100"></div><div class="form-group"><label for="gateway">Gateway</label><input type="text" id="gateway" name="gateway" placeholder="192.168.1.1"></div><div class="form-group"><label for="subnet">Subnet Mask</label><input type="text" id="subnet" name="subnet" placeholder="255.255.255.0"></div></div><div class="form-group"><label for="hub-addr">Hub Address (for sensor reporting)</label><input type="text" id="hub-addr" name="hub_address" placeholder="192.168.1.50" maxlength="39"><small style="color:#666;display:block;margin-top:4px">Leave empty for standalone mode</small></div><button type="submit" class="btn-primary" style="width:100%">Connect</button></form></div></main><script>async function fetchJson(u){return(await fetch(u)).json()}async function postJson(u,d){return(await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})).json()}async function loadWifiStatus(){try{const d=await fetchJson('/api/status');if(d.wifi){document.getElementById('wifi-connected').textContent=d.wifi.connected?'Yes':'No';document.getElementById('current-ssid').textContent=d.wifi.ssid||'--';document.getElementById('current-ip').textContent=d.wifi.ip||'--';document.getElementById('current-rssi').textContent=d.wifi.rssi?d.wifi.rssi+' dBm':'--'}const c=await fetchJson('/api/config/wifi');if(c.ssid)document.getElementById('ssid').value=c.ssid;if(c.use_static_ip)document.getElementById('static-ip').checked=true;if(c.hub_address)document.getElementById('hub-addr').value=c.hub_address}catch(e){console.error(e)}}let scanInterval=null;document.getElementById('scan-btn').addEventListener('click',async()=>{const b=document.getElementById('scan-btn'),l=document.getElementById('network-list');b.textContent='Scanning...';b.disabled=true;l.style.display='block';l.innerHTML='<p>Scanning...</p>';await fetchJson('/api/wifi/networks');let a=0;scanInterval=setInterval(async()=>{const d=await fetchJson('/api/wifi/networks');if(!d.scanning||a++>10){clearInterval(scanInterval);b.textContent='Scan for Networks';b.disabled=false;if(d.networks&&d.networks.length>0){l.innerHTML=d.networks.map(n=>'<div class="ni" data-ssid="'+n.ssid+'"><span>'+n.ssid+'</span><span class="sg">'+n.rssi+' dBm'+(n.encryption?' 🔒':'')+'</span></div>').join('');l.querySelectorAll('.ni').forEach(i=>{i.addEventListener('click',()=>{document.getElementById('ssid').value=i.dataset.ssid;l.style.display='none';document.getElementById('password').focus()})})}else{l.innerHTML='<p>No networks found</p>'}}},1000)});document.getElementById('static-ip').addEventListener('change',e=>{document.getElementById('static-ip-fields').style.display=e.target.checked?'block':'none'});document.getElementById('show-password').addEventListener('change',e=>{document.getElementById('password').type=e.target.checked?'text':'password'});document.getElementById('wifi-form').addEventListener('submit',async function(e){e.preventDefault();const ssid=document.getElementById('ssid').value,password=document.getElementById('password').value;if(!ssid){alert('Please enter a network name');return}if(confirm('Connect to "'+ssid+'"? Device will reboot.')){try{const r=await postJson('/api/wifi/connect',{ssid:ssid,password:password,hub_address:document.getElementById('hub-addr').value});alert(r.message||'Connecting...')}catch(err){alert('Error: '+err.message)}}});loadWifiStatus()</script><style>.ni{padding:10px;border-bottom:1px solid #e0e0e0;cursor:pointer;display:flex;justify-content:space-between}.ni:hover{background:#f0f0f0}.ni span:first-child{font-weight:500}.sg{color:#666;font-size:.9em}</style></body></html>)rawliteral";
}

const char* getMqttSettingsHtml() {
    return R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>MQTT Settings</title><link rel="stylesheet" href="/style.css"></head><body><header><a href="/settings" class="back-link">&larr; Back</a><h1>MQTT Settings</h1></header><main><div class="card"><h2>MQTT Broker</h2><form id="mqtt-form"><div class="form-group"><label><input type="checkbox" id="mqtt-enabled" name="enabled"> Enable MQTT</label></div><div class="form-group"><label for="broker">Broker Address</label><input type="text" id="broker" name="broker" placeholder="192.168.1.10"></div><div class="form-group"><label for="port">Port</label><input type="number" id="port" name="port" value="1883"></div><div class="form-group"><label for="mqtt-user">Username (optional)</label><input type="text" id="mqtt-user" name="username"></div><div class="form-group"><label for="mqtt-pass">Password (optional)</label><input type="password" id="mqtt-pass" name="password"></div><div class="form-group"><label><input type="checkbox" id="ha-discovery" name="ha_discovery_enabled" checked> Home Assistant Discovery</label></div><button type="submit" class="btn-primary">Save MQTT Settings</button></form></div></main><script src="/app.js"></script></body></html>)rawliteral";
}

const char* getSensorsHtml() {
    return R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Sensors - iWetMyPlants</title><link rel="stylesheet" href="/style.css">
<style>
.sg{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:.7rem;margin-top:.5rem}
.sc{background:#f9f9f9;border-radius:10px;padding:.8rem;cursor:pointer;transition:box-shadow .2s,transform .1s}
.sc:hover{box-shadow:0 2px 8px rgba(0,0,0,.12);transform:translateY(-1px)}
.sc.active{box-shadow:0 0 0 2px #4CAF50}
.sc-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:.1rem}
.sc-hdr b{font-size:.88rem;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:72%}
.sc-badge{padding:1px 6px;border-radius:10px;font-size:.65rem;font-weight:600}
.sc-ok{background:#c8e6c9;color:#2e7d32}.sc-err{background:#ffcdd2;color:#c62828}.sc-off{background:#e0e0e0;color:#888}
.sc-pct{font-size:2rem;font-weight:700;text-align:center;padding:.1rem 0;line-height:1.1}
.sc-pct small{font-size:.72rem;color:#888;font-weight:400;display:block;margin-top:2px}
.sc-bar{height:5px;background:#e0e0e0;border-radius:3px;margin:.25rem 0;overflow:hidden}
.sc-fill{height:100%;border-radius:3px;transition:width .5s}
.sc-warn{font-size:.7rem;color:#e65100;padding:2px 0}
.sc-row{display:flex;justify-content:space-between;font-size:.7rem;color:#888}
.sd{background:#f9f9f9;border-radius:8px;padding:.6rem .8rem;margin-bottom:.4rem;display:flex;justify-content:space-between;align-items:center;cursor:pointer}
.sd:hover{background:#f0f0f0}
.sd-name{font-weight:500;font-size:.9rem}
.sd-info{font-size:.78rem;color:#888}
.add-btn{display:block;width:100%;padding:.6rem;border:2px dashed #ccc;border-radius:8px;background:none;color:#888;font-size:.85rem;cursor:pointer;text-align:center;margin-top:.5rem}
.add-btn:hover{border-color:#4CAF50;color:#4CAF50}
.pi{background:#e3f2fd;padding:.5rem;border-radius:4px;font-size:.8rem;margin-top:.5rem}
.io{margin-top:.5rem}
.fr{display:flex;gap:.75rem}
.fr .form-group{flex:1}
.hw-ok{background:#c8e6c9;color:#2e7d32;padding:2px 6px;border-radius:3px;font-size:.7rem;margin-left:4px}
.hw-err{background:#ffcdd2;color:#c62828;padding:2px 6px;border-radius:3px;font-size:.7rem;margin-left:4px}
@media(max-width:500px){.fr{flex-direction:column}.fr .form-group{margin-left:0!important}}
</style></head><body>
<header><a href="/" class="back-link">&larr; Back</a><h1>Sensors</h1></header>
<nav><a href="/">Dashboard</a><a href="/sensors" class="active">Sensors</a><a href="/devices">Devices</a><a href="/settings">Settings</a></nav>
<main>
<div class="card"><h2>Active Sensors</h2>
<div class="sg" id="sensor-grid"><p style="color:#888">Loading...</p></div>
</div>
<div class="card"><h2>All Sensors</h2><p style="font-size:.8rem;color:#888;margin-bottom:.5rem">Tap a sensor to configure it</p>
<div id="sensor-list"><p>Loading...</p></div>
<button class="add-btn" id="add-sensor-btn">+ Add Sensor</button>
</div>
<div class="card" id="sensor-config-card" style="display:none"><h2 id="cfg-title">Sensor Configuration</h2>
<form id="sensor-config-form"><input type="hidden" id="cfg-sensor-index" value="0">
<div class="form-group"><label for="cfg-name">Sensor Name</label><input type="text" id="cfg-name" maxlength="31"></div>
<div class="form-group"><label><input type="checkbox" id="cfg-enabled"> Enabled</label></div>
<div class="form-group"><label for="cfg-input-type">Input Source</label>
<select id="cfg-input-type"><option value="0">Direct ADC</option><option value="1">ADS1115</option><option value="2">Multiplexer</option></select></div>
<div id="direct-adc-options" class="io"><div class="form-group"><label for="cfg-adc-pin">GPIO Pin</label>
<select id="cfg-adc-pin"><option value="32">GPIO32</option><option value="33">GPIO33</option><option value="34">GPIO34</option><option value="35">GPIO35</option><option value="36">GPIO36</option><option value="39">GPIO39</option></select></div>
<div class="pi">Sensor&#8594;GPIO, GND&#8594;GND, VCC&#8594;3.3V</div></div>
<div id="ads1115-options" class="io" style="display:none">
<div class="fr"><div class="form-group"><label for="cfg-ads-channel">Channel</label>
<select id="cfg-ads-channel"><option value="0">A0</option><option value="1">A1</option><option value="2">A2</option><option value="3">A3</option></select></div>
<div class="form-group"><label for="cfg-ads-addr">I2C Address</label>
<select id="cfg-ads-addr"><option value="72">0x48</option><option value="73">0x49</option><option value="74">0x4A</option><option value="75">0x4B</option></select></div></div>
<div class="pi">SDA&#8594;21, SCL&#8594;22, VDD&#8594;3.3V</div></div>
<div id="mux-options" class="io" style="display:none"><div class="form-group"><label for="cfg-mux-channel">Mux Channel</label><select id="cfg-mux-channel"></select></div>
<div class="pi">SIG&#8594;34, S0-S3&#8594;16-19</div></div>
<h3 style="margin-top:1rem">Warning Level</h3>
<div class="form-group"><label for="cfg-warning">Moisture % threshold (0 = disabled)</label>
<input type="number" id="cfg-warning" min="0" max="100" value="30">
<small style="display:block;color:#666;margin-top:3px">Alert when moisture drops below this level</small></div>
<h3>Calibration</h3>
<div class="fr"><div class="form-group"><label for="cfg-dry">Dry Value</label><input type="number" id="cfg-dry" min="0" max="65535"></div>
<div class="form-group"><label for="cfg-wet">Wet Value</label><input type="number" id="cfg-wet" min="0" max="65535"></div></div>
<a href="/sensors/calibrate" class="btn btn-secondary" style="display:inline-block;margin-bottom:1rem">Calibration Tool</a>
<h3>Sampling</h3>
<div class="fr"><div class="form-group"><label for="cfg-samples">Samples</label><input type="number" id="cfg-samples" min="1" max="100" value="10"></div>
<div class="form-group"><label for="cfg-delay">Delay (ms)</label><input type="number" id="cfg-delay" min="0" max="1000" value="10"></div></div>
<button type="submit" class="btn btn-primary" style="width:100%;margin-top:.5rem">Save Configuration</button>
</form></div>
<div class="card"><h2>Environmental</h2>
<div id="env-readings"><div class="reading"><span class="label">Temperature</span><span class="value" id="temperature">--</span><span class="unit">&deg;C</span></div>
<div class="reading"><span class="label">Humidity</span><span class="value" id="humidity">--</span><span class="unit">%</span></div></div></div>
</main>
<script>
function esc(s){var d=document.createElement('div');d.textContent=s;return d.innerHTML}
function bc(p){return p>60?'#4CAF50':p>30?'#FF9800':'#f44336'}
async function fetchJson(u){const r=await fetch(u);if(!r.ok)throw new Error('HTTP '+r.status);return r.json()}
async function postJson(u,d){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)});const t=await r.text();let j;try{j=JSON.parse(t)}catch(e){j={}}if(!r.ok)throw new Error(j.error||'HTTP '+r.status);return j}
const muxSelect=document.getElementById('cfg-mux-channel');
for(let i=0;i<16;i++){const o=document.createElement('option');o.value=i;o.textContent='C'+i;muxSelect.appendChild(o)}
document.getElementById('cfg-input-type').addEventListener('change',e=>{const v=parseInt(e.target.value);
document.getElementById('direct-adc-options').style.display=v===0?'block':'none';
document.getElementById('ads1115-options').style.display=v===1?'block':'none';
document.getElementById('mux-options').style.display=v===2?'block':'none'});
let sensors=[],editIdx=-1;
async function loadSensors(){try{const d=await fetchJson('/api/sensors');sensors=d.sensors||[];
const grid=document.getElementById('sensor-grid');
const enabled=sensors.filter(s=>s.enabled);
if(!enabled.length){grid.innerHTML='<p style="color:#888">No sensors enabled</p>'}
else{grid.innerHTML=enabled.map(s=>{
 var p=(s.percent!==undefined)?s.percent:0;
 var w=s.warning_level||0;var warn=(w>0&&s.percent!==undefined&&p<=w);
 var hw=(s.input_type===1)?(s.hw_connected?'sc-ok':'sc-err'):'sc-ok';
 return'<div class="sc" onclick="editSensor('+s.index+')"><div class="sc-hdr"><b>'+esc(s.name||'Sensor')+'</b>'
 +'<span class="sc-badge '+(s.ready?hw:'sc-off')+'">'+(s.ready?'OK':'--')+'</span></div>'
 +'<div class="sc-pct" style="color:'+bc(p)+'">'+(s.percent!==undefined?p:'--')+'<small>'+(s.percent!==undefined?'% moisture':'offline')+'</small></div>'
 +'<div class="sc-bar"><div class="sc-fill" style="width:'+p+'%;background:'+bc(p)+'"></div></div>'
 +(warn?'<div class="sc-warn">&#9888; Below '+w+'%</div>':'')
 +'<div class="sc-row"><span>Raw: '+(s.raw!==undefined?s.raw:'--')+'</span><span>'+['ADC','ADS','Mux'][s.input_type]+'</span></div>'
 +'</div>'}).join('')}
const list=document.getElementById('sensor-list');
const T=['ADC','ADS','Mux'];
list.innerHTML=sensors.map((s,i)=>{
 let src=T[s.input_type]+(s.input_type===0?' GPIO'+s.adc_pin:s.input_type===1?' 0x'+s.ads_i2c_address.toString(16)+' Ch'+s.ads_channel:' Ch'+s.mux_channel);
 let hw=s.input_type===1&&s.enabled?(s.hw_connected?'<span class="hw-ok">OK</span>':'<span class="hw-err">ERR</span>'):'';
 return'<div class="sd" onclick="editSensor('+i+')">'
 +'<div><span class="sd-name">'+(s.enabled?'':'<span style="color:#ccc">&#9679;</span> ')+esc(s.name||'Sensor '+(i+1))+'</span>'
 +'<div class="sd-info">'+src+' | '+(s.percent!==undefined?s.percent+'%':'--')+' '+hw+'</div></div>'
 +'<span style="color:#ccc;font-size:1.2rem">&#8250;</span></div>'
}).join('')}catch(e){console.error(e)}}
document.getElementById('add-sensor-btn').addEventListener('click',()=>{
 const first=sensors.findIndex(s=>!s.enabled);
 if(first>=0)editSensor(first);else alert('All 16 sensor slots in use')});
function editSensor(i){const s=sensors[i];if(!s)return;editIdx=i;
document.getElementById('cfg-title').textContent='Configure: '+(s.name||'Sensor '+(i+1));
document.getElementById('cfg-sensor-index').value=i;
document.getElementById('cfg-name').value=s.name||'';
document.getElementById('cfg-enabled').checked=s.enabled;
document.getElementById('cfg-input-type').value=s.input_type||0;
document.getElementById('cfg-adc-pin').value=s.adc_pin||34;
document.getElementById('cfg-ads-channel').value=s.ads_channel||0;
document.getElementById('cfg-ads-addr').value=s.ads_i2c_address||72;
document.getElementById('cfg-mux-channel').value=s.mux_channel||0;
document.getElementById('cfg-dry').value=s.dry_value||4095;
document.getElementById('cfg-wet').value=s.wet_value||1500;
document.getElementById('cfg-samples').value=s.reading_samples||10;
document.getElementById('cfg-delay').value=s.sample_delay_ms||10;
document.getElementById('cfg-warning').value=s.warning_level||0;
document.getElementById('cfg-input-type').dispatchEvent(new Event('change'));
document.getElementById('sensor-config-card').style.display='block';
document.getElementById('sensor-config-card').scrollIntoView({behavior:'smooth'})}
document.getElementById('sensor-config-form').addEventListener('submit',async e=>{e.preventDefault();
const i=parseInt(document.getElementById('cfg-sensor-index').value);
const cfg={name:document.getElementById('cfg-name').value,
enabled:document.getElementById('cfg-enabled').checked,
input_type:parseInt(document.getElementById('cfg-input-type').value),
adc_pin:parseInt(document.getElementById('cfg-adc-pin').value),
ads_channel:parseInt(document.getElementById('cfg-ads-channel').value),
ads_i2c_address:parseInt(document.getElementById('cfg-ads-addr').value),
mux_channel:parseInt(document.getElementById('cfg-mux-channel').value),
dry_value:parseInt(document.getElementById('cfg-dry').value),
wet_value:parseInt(document.getElementById('cfg-wet').value),
reading_samples:parseInt(document.getElementById('cfg-samples').value),
sample_delay_ms:parseInt(document.getElementById('cfg-delay').value),
warning_level:parseInt(document.getElementById('cfg-warning').value)};
const arr=[];for(let j=0;j<=i;j++)arr.push(j===i?cfg:{});
try{await postJson('/api/config/sensors',arr);alert('Saved! Reboot to apply input changes.');loadSensors();
document.getElementById('sensor-config-card').style.display='none'}catch(e){alert('Error: '+e.message)}});
loadSensors();setInterval(loadSensors,5000)
</script></body></html>)rawliteral";
}

const char* getCalibrationHtml() {
    return R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Calibration</title><link rel="stylesheet" href="/style.css"></head><body><header><a href="/sensors" class="back-link">&larr; Back</a><h1>Calibration</h1></header><main><div class="card"><h2>Select Sensor</h2><select id="sensor-select" class="fc"><option value="0">Loading...</option></select><div id="sensor-info" style="margin-top:.5rem;font-size:.9em;color:#666"></div></div><div class="card"><h2>Live Reading</h2><div id="ws-status" class="wss dc">Disconnected</div><div class="live-reading"><div class="reading-display"><span class="label">Raw</span><span class="value" id="raw-value">--</span></div><div class="reading-display"><span class="label">Average</span><span class="value" id="avg-value">--</span></div><div class="reading-display"><span class="label">Percent</span><span class="value" id="pct-value">--</span></div></div><div class="cg"><canvas id="reading-graph" height="120"></canvas></div><div class="sc"><button id="btn-start" class="btn btn-primary">Start</button><button id="btn-stop" class="btn btn-secondary" disabled>Stop</button><select id="sample-rate" style="margin-left:1rem"><option value="5">5Hz</option><option value="10" selected>10Hz</option><option value="20">20Hz</option><option value="50">50Hz</option></select></div></div><div class="card"><h2>Calibration Points</h2><p class="ht">Set dry value in air, wet value in water.</p><div class="calibration-points"><div class="cal-point"><h3>Dry (0%)</h3><div class="cv"><span class="sl">Current:</span><span class="sv" id="saved-dry">--</span></div><div class="cv"><span class="sl">Capture:</span><span class="cpv" id="capture-dry">--</span></div><button id="btn-set-dry" class="btn btn-primary">Capture Dry</button></div><div class="cal-point"><h3>Wet (100%)</h3><div class="cv"><span class="sl">Current:</span><span class="sv" id="saved-wet">--</span></div><div class="cv"><span class="sl">Capture:</span><span class="cpv" id="capture-wet">--</span></div><button id="btn-set-wet" class="btn btn-primary">Capture Wet</button></div></div><div class="ca"><button id="btn-save-cal" class="btn btn-success" disabled>Save Calibration</button><button id="btn-reset-cal" class="btn btn-warning">Reset</button></div></div><div class="card"><h2>Manual Entry</h2><div class="fr"><div class="form-group" style="flex:1"><label for="manual-dry">Dry Value</label><input type="number" id="manual-dry" min="0" max="65535"></div><div class="form-group" style="flex:1;margin-left:1rem"><label for="manual-wet">Wet Value</label><input type="number" id="manual-wet" min="0" max="65535"></div></div><button id="btn-save-manual" class="btn btn-secondary">Save Manual</button></div></main><script>async function fetchJson(u){return(await fetch(u)).json()}async function postJson(u,d){return(await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})).json()}let ws=null,currentSensor=0,sensorData=[],graphData=[],capturedDry=null,capturedWet=null;const MAX_GP=100,canvas=document.getElementById('reading-graph'),ctx=canvas.getContext('2d');function drawGraph(){const w=canvas.width=canvas.offsetWidth,h=canvas.height;ctx.fillStyle='#f5f5f5';ctx.fillRect(0,0,w,h);if(graphData.length<2)return;const vals=graphData.map(d=>d.raw);let min=Math.min(...vals),max=Math.max(...vals);if(max-min<100){min-=50;max+=50}ctx.strokeStyle='#e0e0e0';ctx.lineWidth=1;for(let i=0;i<=4;i++){const y=h*i/4;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.stroke()}if(capturedDry!==null){ctx.strokeStyle='#FF9800';ctx.setLineDash([5,5]);ctx.beginPath();ctx.moveTo(0,h-((capturedDry-min)/(max-min))*h);ctx.lineTo(w,h-((capturedDry-min)/(max-min))*h);ctx.stroke();ctx.setLineDash([])}if(capturedWet!==null){ctx.strokeStyle='#2196F3';ctx.setLineDash([5,5]);ctx.beginPath();ctx.moveTo(0,h-((capturedWet-min)/(max-min))*h);ctx.lineTo(w,h-((capturedWet-min)/(max-min))*h);ctx.stroke();ctx.setLineDash([])}ctx.strokeStyle='#4CAF50';ctx.lineWidth=2;ctx.beginPath();graphData.forEach((d,i)=>{const x=(i/(graphData.length-1))*w,y=h-((d.raw-min)/(max-min))*h;i===0?ctx.moveTo(x,y):ctx.lineTo(x,y)});ctx.stroke();ctx.strokeStyle='#388E3C';ctx.lineWidth=1;ctx.beginPath();graphData.forEach((d,i)=>{const x=(i/(graphData.length-1))*w,y=h-((d.avg-min)/(max-min))*h;i===0?ctx.moveTo(x,y):ctx.lineTo(x,y)});ctx.stroke();ctx.fillStyle='#666';ctx.font='10px sans-serif';ctx.fillText(max.toFixed(0),5,12);ctx.fillText(min.toFixed(0),5,h-3)}async function loadSensors(){try{const d=await fetchJson('/api/sensors');sensorData=d.sensors||[];document.getElementById('sensor-select').innerHTML=sensorData.map((s,i)=>'<option value="'+i+'">'+(s.name||'Sensor '+(i+1))+(s.enabled?'':' (off)')+'</option>').join('');updateSensorInfo()}catch(e){}}function updateSensorInfo(){const s=sensorData[currentSensor];if(!s)return;const T=['ADC','ADS','Mux'];let p=s.input_type===0?'GPIO'+s.adc_pin:s.input_type===1?'Ch'+s.ads_channel:'Ch'+s.mux_channel;document.getElementById('sensor-info').textContent=T[s.input_type]+' ('+p+')';document.getElementById('saved-dry').textContent=s.dry_value;document.getElementById('saved-wet').textContent=s.wet_value;document.getElementById('manual-dry').value=s.dry_value;document.getElementById('manual-wet').value=s.wet_value}document.getElementById('sensor-select').addEventListener('change',e=>{currentSensor=parseInt(e.target.value);updateSensorInfo();graphData=[];capturedDry=null;capturedWet=null;document.getElementById('capture-dry').textContent='--';document.getElementById('capture-wet').textContent='--';document.getElementById('btn-save-cal').disabled=true});function connectWs(){if(ws&&ws.readyState===WebSocket.OPEN)return;ws=new WebSocket('ws://'+location.host+'/ws/calibration');ws.onopen=()=>{document.getElementById('ws-status').className='wss cn';document.getElementById('ws-status').textContent='Connected';document.getElementById('btn-start').disabled=false};ws.onclose=()=>{document.getElementById('ws-status').className='wss dc';document.getElementById('ws-status').textContent='Reconnecting...';document.getElementById('btn-start').disabled=true;document.getElementById('btn-stop').disabled=true;setTimeout(connectWs,2000)};ws.onerror=e=>{};ws.onmessage=e=>{const d=JSON.parse(e.data);if(d.type==='reading'){document.getElementById('raw-value').textContent=d.raw;document.getElementById('avg-value').textContent=d.avg;document.getElementById('pct-value').textContent=d.pct+'%';graphData.push({raw:d.raw,avg:d.avg});while(graphData.length>MAX_GP)graphData.shift();drawGraph()}else if(d.type==='started'){document.getElementById('btn-start').disabled=true;document.getElementById('btn-stop').disabled=false}else if(d.type==='stopped'){document.getElementById('btn-start').disabled=false;document.getElementById('btn-stop').disabled=true}else if(d.type==='status'&&d.active){document.getElementById('btn-start').disabled=true;document.getElementById('btn-stop').disabled=false}}}document.getElementById('btn-start').addEventListener('click',()=>{if(ws&&ws.readyState===WebSocket.OPEN){const r=parseInt(document.getElementById('sample-rate').value);ws.send(JSON.stringify({cmd:'rate',value:r}));ws.send(JSON.stringify({cmd:'start'}))}});document.getElementById('btn-stop').addEventListener('click',()=>{if(ws&&ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify({cmd:'stop'}))});function capturePt(type){const v=parseInt(document.getElementById('avg-value').textContent);if(!isNaN(v)&&v>0){if(type==='dry'){capturedDry=v;document.getElementById('capture-dry').textContent=v}else{capturedWet=v;document.getElementById('capture-wet').textContent=v}document.getElementById('btn-save-cal').disabled=!(capturedDry!==null&&capturedWet!==null)}else alert('Start rapid read first')}document.getElementById('btn-set-dry').addEventListener('click',()=>capturePt('dry'));document.getElementById('btn-set-wet').addEventListener('click',()=>capturePt('wet'));function buildCfg(dv,wv){const s=[];for(let i=0;i<sensorData.length;i++)s.push(i===currentSensor?{dry_value:dv,wet_value:wv}:{});return s}document.getElementById('btn-save-cal').addEventListener('click',async()=>{if(capturedDry===null||capturedWet===null)return;try{await postJson('/api/config/sensors',buildCfg(capturedDry,capturedWet));alert('Saved!');loadSensors()}catch(e){alert('Error: '+e.message)}});document.getElementById('btn-reset-cal').addEventListener('click',async()=>{if(!confirm('Reset?'))return;try{await postJson('/api/config/sensors',buildCfg(4095,1500));alert('Reset!');capturedDry=null;capturedWet=null;document.getElementById('capture-dry').textContent='--';document.getElementById('capture-wet').textContent='--';loadSensors()}catch(e){alert('Error: '+e.message)}});document.getElementById('btn-save-manual').addEventListener('click',async()=>{const d=parseInt(document.getElementById('manual-dry').value),w=parseInt(document.getElementById('manual-wet').value);if(isNaN(d)||isNaN(w)){alert('Enter valid numbers');return}try{await postJson('/api/config/sensors',buildCfg(d,w));alert('Saved!');loadSensors()}catch(e){alert('Error: '+e.message)}});loadSensors();connectWs()</script><style>.wss{padding:.25rem .5rem;border-radius:4px;font-size:.8rem;margin-bottom:1rem;text-align:center}.cn{background:#c8e6c9;color:#2e7d32}.dc{background:#ffcdd2;color:#c62828}.sc{display:flex;align-items:center;margin-top:1rem;flex-wrap:wrap;gap:.5rem}.calibration-points{display:grid;grid-template-columns:1fr 1fr;gap:1rem}.cal-point{background:#f9f9f9;padding:1rem;border-radius:8px;text-align:center}.cal-point h3{margin-bottom:.25rem;font-size:1rem}.cv{display:flex;justify-content:space-between;font-size:.9rem;margin:.25rem 0}.sl{color:#666}.sv{font-weight:600;color:#333}.cpv{font-weight:600;color:#4CAF50}.ca{margin-top:1rem;display:flex;gap:1rem;flex-wrap:wrap}.cg{margin:1rem 0;background:#f5f5f5;border-radius:4px}.ht{font-size:.85rem;color:#666;margin-bottom:1rem}.fr{display:flex;gap:1rem}.fc{width:100%;padding:.5rem;border:1px solid #e0e0e0;border-radius:4px}@media(max-width:500px){.calibration-points{grid-template-columns:1fr}.fr{flex-direction:column}.fr .form-group{margin-left:0!important}}</style></body></html>)rawliteral";
}

const char* getRelaysHtml() {
    return R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>Relays</title><link rel="stylesheet" href="/style.css"></head><body><header><a href="/" class="back-link">&larr; Back</a><h1>Relay Control</h1></header><main><div class="card"><h2>Relays</h2><div id="relay-buttons"><p>Loading...</p></div></div></main><script src="/app.js"></script></body></html>)rawliteral";
}

const char* getDevicesHtml() {
    return R"rawliteral(<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Remote Devices - iWetMyPlants</title>
<link rel="stylesheet" href="/style.css">
<style>
.dev{background:var(--cb);border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,.1);padding:1rem;margin-bottom:1rem}
.dev-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:.5rem}
.dev-hdr h2{font-size:1.1rem;margin:0}
.badge{padding:2px 8px;border-radius:12px;font-size:.75rem;font-weight:600}
.badge.on{background:#c8e6c9;color:#2e7d32}.badge.off{background:#ffcdd2;color:#c62828}
.moisture-big{text-align:center;padding:.5rem 0}
.moisture-big .val{font-size:2.5rem;font-weight:700;color:var(--pc)}
.moisture-big .lbl{font-size:.85rem;color:var(--tm);display:block}
.chart-wrap{background:var(--bg);border-radius:6px;padding:4px;margin:.5rem 0}
.chart-wrap canvas{width:100%;height:100px;display:block}
.info-grid{display:grid;grid-template-columns:1fr 1fr;gap:.25rem .75rem;font-size:.85rem;padding-top:.5rem;border-top:1px solid var(--bc)}
.info-grid .k{color:var(--tm)}.info-grid .v{text-align:right;font-weight:500}
.empty{text-align:center;padding:2rem 1rem;color:var(--tm)}
.empty b{display:block;font-size:1rem;color:var(--tc);margin-bottom:.5rem}
.hdr-count{font-size:.85rem;font-weight:400;color:rgba(255,255,255,.8)}
</style></head><body>
<header><a href="/" class="back-link">&larr; Back</a><h1>Remote Devices <span class="hdr-count" id="hcount"></span></h1></header>
<main><div id="devs"></div>
<div class="card empty" id="nodev" style="display:none">
<b>No remote devices found</b>
Set a Remote's Hub Address to this Hub's IP and it will appear here automatically.
</div></main>
<script>
var H={},MP=180;
function poll(){
 fetch('/api/devices').then(function(r){return r.json()}).then(function(d){
  var devs=d.devices||[];
  var now=d.hub_uptime||0;
  document.getElementById('nodev').style.display=devs.length?'none':'block';
  document.getElementById('hcount').textContent=devs.length?('('+devs.filter(function(x){return x.online}).length+'/'+devs.length+' online)'):'';
  devs.forEach(function(v){
   if(!H[v.mac])H[v.mac]=[];
   H[v.mac].push({t:Date.now(),p:v.moisture_percent});
   if(H[v.mac].length>MP)H[v.mac].shift();
  });
  render(devs);
 }).catch(function(){});
}
function render(devs){
 var h='';
 devs.forEach(function(d){
  var id='c'+d.mac.replace(/:/g,'');
  var on=d.online;
  var ago=calcAgo(d);
  h+='<div class="dev"><div class="dev-hdr"><h2>'+esc(d.name)+'</h2>'
   +'<span class="badge '+(on?'on':'off')+'">'+( on?'Online':'Offline')+'</span></div>'
   +'<div class="moisture-big"><span class="val">'+d.moisture_percent+'%</span><span class="lbl">Moisture</span></div>'
   +'<div class="chart-wrap"><canvas id="'+id+'"></canvas></div>'
   +'<div class="info-grid">'
   +'<span class="k">Signal</span><span class="v">'+d.rssi+' dBm</span>'
   +'<span class="k">Last Seen</span><span class="v">'+ago+'</span>'
   +'<span class="k">MAC</span><span class="v">'+d.mac+'</span>';
  if(d.battery_percent!==undefined)h+='<span class="k">Battery</span><span class="v">'+d.battery_percent+'%</span>';
  h+='</div></div>';
 });
 document.getElementById('devs').innerHTML=h;
 devs.forEach(function(d){drawChart(d.mac)});
}
function drawChart(mac){
 var el=document.getElementById('c'+mac.replace(/:/g,''));
 if(!el)return;
 var data=H[mac]||[];
 var dpr=window.devicePixelRatio||1;
 var rw=el.parentElement.clientWidth-8,rh=100;
 el.width=rw*dpr;el.height=rh*dpr;
 el.style.width=rw+'px';el.style.height=rh+'px';
 var c=el.getContext('2d');c.scale(dpr,dpr);
 var P={t:5,r:5,b:14,l:26},pw=rw-P.l-P.r,ph=rh-P.t-P.b;
 c.clearRect(0,0,rw,rh);
 c.strokeStyle='#e0e0e0';c.lineWidth=.5;
 c.fillStyle='#bbb';c.font='9px sans-serif';c.textAlign='right';
 for(var i=0;i<=4;i++){var y=P.t+ph*(1-i/4);c.beginPath();c.moveTo(P.l,y);c.lineTo(P.l+pw,y);c.stroke();c.fillText(i*25+'%',P.l-3,y+3)}
 if(data.length<2){c.fillStyle='#ccc';c.font='11px sans-serif';c.textAlign='center';c.fillText('Collecting data\u2026',P.l+pw/2,P.t+ph/2);return}
 c.beginPath();c.strokeStyle='#4CAF50';c.lineWidth=1.5;c.lineJoin='round';
 for(var i=0;i<data.length;i++){var x=P.l+(i/(data.length-1))*pw,y=P.t+ph*(1-data[i].p/100);if(i===0)c.moveTo(x,y);else c.lineTo(x,y)}
 c.stroke();
 c.lineTo(P.l+pw,P.t+ph);c.lineTo(P.l,P.t+ph);c.closePath();c.fillStyle='rgba(76,175,80,.08)';c.fill();
 var sec=(data[data.length-1].t-data[0].t)/1000;
 c.fillStyle='#bbb';c.font='9px sans-serif';c.textAlign='center';
 c.fillText('Last '+(sec<60?Math.round(sec)+'s':Math.round(sec/60)+'m'),P.l+pw/2,rh-2);
}
function calcAgo(d){
 if(!d.last_seen)return'Never';
 var arr=H[d.mac];if(!arr||arr.length<2)return'Just now';
 var last=arr[arr.length-1].t,prev=arr[arr.length-2].t;
 if(last-prev<6000)return'Just now';
 var s=Math.round((Date.now()-last)/1000);
 if(s<60)return s+'s ago';if(s<3600)return Math.round(s/60)+'m ago';
 return Math.round(s/3600)+'h ago';
}
function esc(s){var d=document.createElement('div');d.textContent=s;return d.innerHTML}
poll();setInterval(poll,5000);
</script></body></html>)rawliteral";
}

const char* getStyleCss() {
    return R"rawliteral(:root{--pc:#4CAF50;--pd:#388E3C;--sc:#2196F3;--dc:#f44336;--wc:#FF9800;--bg:#f5f5f5;--cb:#fff;--tc:#333;--tm:#666;--bc:#e0e0e0}*{box-sizing:border-box;margin:0;padding:0}body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--tc);line-height:1.6}header{background:var(--pc);color:#fff;padding:1rem;display:flex;justify-content:space-between;align-items:center}header h1{font-size:1.5rem}header .back-link{color:#fff;text-decoration:none}nav{background:#fff;padding:.5rem 1rem;display:flex;gap:1rem;border-bottom:1px solid var(--bc)}nav a{color:var(--tm);text-decoration:none;padding:.5rem 1rem;border-radius:4px}nav a.active{color:var(--pc);background:rgba(76,175,80,.1)}main{padding:1rem;max-width:800px;margin:0 auto}.card{background:var(--cb);border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,.1);padding:1rem;margin-bottom:1rem}.card h2{font-size:1.1rem;margin-bottom:1rem;border-bottom:1px solid var(--bc);padding-bottom:.5rem}.reading{display:flex;justify-content:space-between;padding:.5rem 0;border-bottom:1px solid var(--bc)}.reading:last-child{border-bottom:none}.reading .value{font-size:1.5rem;font-weight:600;color:var(--pc)}.form-group{margin-bottom:1rem}.form-group label{display:block;margin-bottom:.25rem;color:var(--tm)}.form-group input[type="text"],.form-group input[type="password"],.form-group input[type="number"],.form-group select{width:100%;padding:.5rem;border:1px solid var(--bc);border-radius:4px}button,.btn{padding:.5rem 1rem;border:none;border-radius:4px;cursor:pointer;text-decoration:none;display:inline-block}.btn-primary{background:var(--pc);color:#fff}.btn-secondary{background:var(--sc);color:#fff}.btn-warning{background:var(--wc);color:#fff}.btn-danger{background:var(--dc);color:#fff}.btn-success{background:var(--pc);color:#fff}.system-actions{display:flex;gap:1rem;flex-wrap:wrap}.live-reading{display:grid;grid-template-columns:repeat(3,1fr);gap:1rem}.reading-display{text-align:center;padding:1rem;background:var(--bg);border-radius:8px}.reading-display .value{display:block;font-size:2rem;font-weight:bold;color:var(--pc)}.calibration-points{display:grid;grid-template-columns:repeat(2,1fr);gap:1rem}.cal-point{padding:1rem;background:var(--bg);border-radius:8px;text-align:center}.calibration-actions{text-align:center;margin-top:1rem}footer{background:#fff;border-top:1px solid var(--bc);padding:.5rem 1rem;display:flex;justify-content:space-between;font-size:.8rem;color:var(--tm);position:fixed;bottom:0;left:0;right:0})rawliteral";
}

const char* getAppJs() {
    return R"rawliteral(async function fetchJson(u){return(await fetch(u)).json()}async function postJson(u,d){return(await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})).json()}function $(id){return document.getElementById(id)}function formatUptime(ms){const s=Math.floor(ms/1000),h=Math.floor(s/3600),m=Math.floor((s%3600)/60);return h+'h '+m+'m '+(s%60)+'s'}async function loadStatus(){try{const d=await fetchJson('/api/status');if($('device-name'))$('device-name').textContent=d.device_id||'Unknown';if($('wifi-status'))$('wifi-status').textContent=(d.wifi&&d.wifi.connected)?'Connected':'Disconnected';if($('uptime'))$('uptime').textContent=(d.uptime_ms!==undefined)?formatUptime(d.uptime_ms):'--';if($('free-heap'))$('free-heap').textContent=(d.free_heap!==undefined)?Math.round(d.free_heap/1024):'--'}catch(e){console.error('loadStatus:',e)}}async function loadSensors(){try{const d=await fetchJson('/api/sensors'),c=$('sensor-readings')||$('sensor-list');if(!c)return;let h='';if(d.sensors)d.sensors.forEach((s,i)=>{h+='<div class="reading"><span class="label">'+(s.name||'Sensor '+(i+1))+'</span><span class="value">'+(s.percent!==undefined?s.percent:'--')+'%</span></div>'});c.innerHTML=h||'<p>No sensors configured</p>'}catch(e){console.error('loadSensors:',e)}}document.addEventListener('DOMContentLoaded',()=>{loadStatus();loadSensors();setInterval(loadStatus,5000);setInterval(loadSensors,10000)});document.querySelectorAll('form').forEach(f=>{f.addEventListener('submit',async e=>{e.preventDefault();try{await postJson('/api/config',Object.fromEntries(new FormData(f)));alert('Settings saved!')}catch(e){alert('Error saving settings')}})});$('btn-reboot')?.addEventListener('click',async()=>{if(confirm('Reboot device?')){await postJson('/api/system/reboot',{});alert('Device rebooting...')}});$('btn-reset')?.addEventListener('click',async()=>{if(confirm('Factory reset? This will erase all settings!')){await postJson('/api/config/reset',{});alert('Device reset. Rebooting...')}});)rawliteral";
}

} // namespace iwmp
