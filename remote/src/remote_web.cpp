/**
 * @file remote_web.cpp
 * @brief Lightweight web server for Remote device
 *
 * ~18 routes, ~10KB HTML. Created once, never destroyed.
 */

#include "remote_web.h"
#include "remote_controller.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "logger.h"
#include <ArduinoJson.h>
#include <WiFi.h>

namespace iwmp {

static constexpr const char* TAG = "Web";

// ============================================================
// Status Page HTML (~3KB)
// ============================================================

const char REMOTE_STATUS_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>iWetMyPlants Remote</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#f5f5f5;padding:1rem;max-width:480px;margin:0 auto}
h1{background:#4CAF50;color:#fff;padding:.8rem;margin:-1rem -1rem 1rem;text-align:center;font-size:1.2rem}
.c{background:#fff;border-radius:8px;padding:1rem;margin-bottom:.8rem;box-shadow:0 1px 3px rgba(0,0,0,.1)}
.c h2{font-size:.95rem;color:#333;margin-bottom:.5rem;border-bottom:1px solid #eee;padding-bottom:.3rem}
.r{display:flex;justify-content:space-between;padding:.25rem 0;font-size:.9rem}
.r .l{color:#666}
.r .v{font-weight:600;color:#333}
.big{font-size:2.5rem;text-align:center;color:#4CAF50;font-weight:700;padding:.5rem 0}
.big small{font-size:.9rem;color:#888;display:block;font-weight:400}
a.btn{display:block;text-align:center;background:#2196F3;color:#fff;padding:.6rem;border-radius:4px;text-decoration:none;font-size:.95rem;margin-top:.5rem}
.off{color:#999}.on{color:#4CAF50}
.warn{background:#fff3cd;border:1px solid #ffc107;border-radius:8px;padding:.8rem;margin-bottom:.8rem;text-align:center;font-size:.95rem;color:#856404}
.warn b{display:block;margin-bottom:.3rem}
.warn button{background:#ff9800;color:#fff;border:none;padding:.5rem 1rem;border-radius:4px;cursor:pointer;margin-top:.5rem;font-size:.9rem}
.mode{background:#e3f2fd;border-radius:8px;padding:.5rem .8rem;margin-bottom:.8rem;font-size:.85rem;color:#1565c0;text-align:center}
</style></head><body>
<h1 id="dn">iWetMyPlants Remote</h1>
<div id="override" style="display:none" class="warn"><b>Override Mode Active</b>Button press forced Standalone mode.<br>Settings and sensor data available below.
<button onclick="retMode()">Return to Configured Mode</button></div>
<div id="modebadge" class="mode" style="display:none"></div>
<div class="c">
<div class="big"><span id="pct">--</span>%<small id="sn">Sensor</small></div>
<div class="r"><span class="l">Raw Value</span><span class="v" id="raw">--</span></div>
</div>
<div class="c"><h2>WiFi</h2>
<div class="r"><span class="l">SSID</span><span class="v" id="ssid">--</span></div>
<div class="r"><span class="l">IP</span><span class="v" id="ip">--</span></div>
<div class="r"><span class="l">Signal</span><span class="v" id="rssi">--</span></div>
</div>
<div class="c"><h2>Hub</h2>
<div class="r"><span class="l">Address</span><span class="v" id="hub">--</span></div>
<div class="r"><span class="l">Last Report</span><span class="v" id="hrpt">--</span></div>
</div>
<div class="c"><h2>MQTT</h2>
<div class="r"><span class="l">Status</span><span class="v" id="mqtt">--</span></div>
</div>
<div class="c"><h2>System</h2>
<div class="r"><span class="l">Mode</span><span class="v" id="opmode">--</span></div>
<div class="r"><span class="l">Uptime</span><span class="v" id="up">--</span></div>
<div class="r"><span class="l">Free Heap</span><span class="v" id="heap">--</span></div>
<div class="r"><span class="l">Firmware</span><span class="v" id="fw">--</span></div>
</div>
<a class="btn" href="/settings">Settings</a>
<script>
function fmt(s){var h=Math.floor(s/3600),m=Math.floor(s%3600/60),sc=s%60;return h+'h '+m+'m '+sc+'s'}
var mnames=['WiFi','Standalone','Low Power'];
function upd(d){
document.getElementById('dn').textContent=d.device_name||'iWetMyPlants Remote';
document.getElementById('pct').textContent=d.moisture_percent;
document.getElementById('raw').textContent=d.raw_value;
document.getElementById('sn').textContent=d.sensor_name||'Sensor';
document.getElementById('ssid').textContent=d.ssid||'Not connected';
document.getElementById('ip').textContent=d.ip||'--';
document.getElementById('rssi').textContent=d.wifi_connected?(d.rssi+' dBm'):'--';
document.getElementById('hub').textContent=d.hub_address||'Not configured';
if(d.hub_last_report>0){document.getElementById('hrpt').textContent=(d.hub_last_status?'OK':'Failed')+' @ '+fmt(d.hub_last_report)}
else{document.getElementById('hrpt').textContent='Never'}
var ms=document.getElementById('mqtt');
if(!d.mqtt_enabled){ms.textContent='Disabled';ms.className='v off'}
else if(d.mqtt_connected){ms.textContent='Connected';ms.className='v on'}
else{ms.textContent='Disconnected';ms.className='v off'}
document.getElementById('opmode').textContent=mnames[d.operating_mode]||'Unknown';
document.getElementById('up').textContent=fmt(d.uptime);
document.getElementById('heap').textContent=Math.round(d.free_heap/1024)+' KB';
document.getElementById('fw').textContent=d.firmware||'--';
if(d.override_active){document.getElementById('override').style.display='block'}
var mb=document.getElementById('modebadge');
if(d.operating_mode!==undefined&&!d.override_active){mb.textContent='Mode: '+mnames[d.operating_mode];mb.style.display='block'}
}
async function retMode(){
try{await fetch('/api/system/return-mode',{method:'POST'});
document.getElementById('override').innerHTML='<b>Returning to configured mode...</b>'}catch(e){}}
function poll(){fetch('/api/status').then(r=>r.json()).then(upd).catch(()=>{})}
poll();setInterval(poll,5000);
</script></body></html>)rawliteral";

// ============================================================
// Settings Page HTML (~6KB)
// ============================================================

const char REMOTE_SETTINGS_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings - iWetMyPlants</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#f5f5f5;padding:1rem;max-width:480px;margin:0 auto}
h1{background:#4CAF50;color:#fff;padding:.8rem;margin:-1rem -1rem 1rem;text-align:center;font-size:1.2rem}
.c{background:#fff;border-radius:8px;padding:1rem;margin-bottom:.8rem;box-shadow:0 1px 3px rgba(0,0,0,.1)}
.c h2{font-size:.95rem;color:#333;margin-bottom:.5rem;border-bottom:1px solid #eee;padding-bottom:.3rem}
label{display:block;margin-bottom:.2rem;font-weight:500;font-size:.85rem;color:#555}
input[type=text],input[type=password],input[type=number],select{width:100%;padding:.45rem;border:1px solid #ddd;border-radius:4px;margin-bottom:.6rem;font-size:.95rem}
button{padding:.5rem 1rem;border:none;border-radius:4px;cursor:pointer;font-size:.9rem;width:100%;margin-bottom:.4rem;color:#fff}
.bp{background:#4CAF50}.bs{background:#2196F3}.br{background:#f44336}.bo{background:#ff9800}
.nl{max-height:180px;overflow-y:auto;border:1px solid #ddd;border-radius:4px;display:none;margin-bottom:.6rem}
.ni{padding:.4rem .6rem;border-bottom:1px solid #eee;cursor:pointer;display:flex;justify-content:space-between;font-size:.9rem}
.ni:hover{background:#f0f0f0}
.ni .r{color:#999;font-size:.8rem}
small{color:#888;display:block;margin-top:-.4rem;margin-bottom:.5rem;font-size:.8rem}
#msg{padding:.4rem;border-radius:4px;display:none;text-align:center;margin-top:.4rem;font-size:.9rem}
.ok{background:#c8e6c9;color:#2e7d32}.er{background:#ffcdd2;color:#c62828}
a.back{display:block;text-align:center;color:#2196F3;text-decoration:none;padding:.5rem;font-size:.9rem}
.cb{display:flex;align-items:center;margin-bottom:.6rem}
.cb input{width:auto;margin:0 .4rem 0 0}
.cb label{margin:0;font-size:.9rem}
.ro{background:#f5f5f5;border-color:#eee;color:#666}
.hidden{display:none}
</style></head><body>
<h1>Settings</h1>

<div class="c"><h2>Operating Mode</h2>
<label>Mode</label>
<select id="opmode" onchange="modeChanged()">
<option value="0">WiFi (connect to network)</option>
<option value="1">Standalone (local AP only)</option>
<option value="2">Low Power (deep sleep + ESP-NOW)</option>
</select>
<small>WiFi: connects to router, reports to Hub. Standalone: runs its own AP. Low Power: sleeps between readings.</small>
<div id="espnow-cfg" class="hidden">
<label>ESP-NOW Channel (1-14)</label>
<select id="enchan"></select>
<small>Must match Hub's WiFi channel for ESP-NOW to work</small>
<label>Hub MAC Address</label>
<input type="text" id="enmac" placeholder="AA:BB:CC:DD:EE:FF" maxlength="17">
<small>MAC of your Hub (check Hub dashboard)</small>
</div>
<div id="power-cfg" class="hidden">
<label>Sleep Duration (seconds)</label>
<input type="number" id="sleeps" value="300" min="60" max="3600">
<small>Time between readings (60-3600 sec)</small>
<label>Wake Button Pin (GPIO)</label>
<input type="number" id="wakepin" value="5" min="0" max="5">
<small>ESP32-C3: only GPIO 0-5 valid for deep sleep wake</small>
</div>
<button class="bo" onclick="saveMode()">Save Mode &amp; Reboot</button>
</div>

<div class="c"><h2>WiFi</h2>
<button class="bs" onclick="scan()">Scan for Networks</button>
<div class="nl" id="nl"></div>
<label>Network Name (SSID)</label>
<input type="text" id="ssid" maxlength="32">
<label>Password</label>
<input type="password" id="pw" maxlength="64">
<label>Hub Address</label>
<input type="text" id="hub" placeholder="192.168.1.50" maxlength="39">
<small>IP of your Hub (leave empty for standalone)</small>
<button class="bp" onclick="saveWifi()">Save WiFi &amp; Reboot</button>
</div>

<div class="c"><h2>MQTT</h2>
<div class="cb"><input type="checkbox" id="mqen"><label for="mqen">Enable MQTT</label></div>
<label>Broker</label>
<input type="text" id="mqbr" placeholder="192.168.1.100" maxlength="64">
<label>Port</label>
<input type="number" id="mqpt" value="1883" min="1" max="65535">
<label>Username</label>
<input type="text" id="mqus" maxlength="32">
<label>Password</label>
<input type="password" id="mqpw" maxlength="64">
<label>Base Topic</label>
<input type="text" id="mqtp" placeholder="iwmp" maxlength="64">
<small>Publishes to {topic}/{device_id}/state</small>
<button class="bs" onclick="saveMqtt()">Save MQTT</button>
</div>

<div class="c"><h2>Sensor</h2>
<label>Sensor Name</label>
<input type="text" id="sname" maxlength="31">
<label>Input Type</label>
<input type="text" id="stype" class="ro" readonly>
<label>Dry Value (air)</label>
<input type="number" id="sdry" min="0" max="65535">
<label>Wet Value (water)</label>
<input type="number" id="swet" min="0" max="65535">
<small>Calibration: hold sensor in air/water and note raw value from status page</small>
<button class="bs" onclick="saveSensor()">Save Sensor</button>
</div>

<div class="c"><h2>System</h2>
<label>Device ID</label>
<input type="text" id="did" class="ro" readonly>
<label>Free Heap</label>
<input type="text" id="heap" class="ro" readonly>
<button class="br" onclick="reboot()">Reboot Device</button>
</div>

<div class="c"><h2>Import Hub Config</h2>
<small>On your Hub, go to Settings &rarr; tap <b>Copy Remote Config</b>, then paste here.</small>
<textarea id="impjson" rows="3" placeholder='{"hub_mac":"AA:BB:CC:DD:EE:FF","channel":6,...}' style="width:100%;font-family:monospace;font-size:.75rem;padding:.4rem;border:1px solid #ddd;border-radius:4px;margin:.5rem 0 .4rem;resize:vertical"></textarea>
<button class="bs" onclick="importHub()">Import &amp; Reboot</button>
</div>

<div id="msg"></div>
<a class="back" href="/">&#8592; Back to Status</a>

<script>
function $(id){return document.getElementById(id)}
function msg(t,ok){var m=$('msg');m.textContent=t;m.className=ok?'ok':'er';m.style.display='block';setTimeout(()=>{m.style.display='none'},4000)}

// Populate channel dropdown
(function(){var s=$('enchan');for(var i=1;i<=14;i++){var o=document.createElement('option');o.value=i;o.textContent='Channel '+i;s.appendChild(o)}})();

function modeChanged(){
var v=parseInt($('opmode').value);
$('espnow-cfg').className=(v==2)?'':'hidden';
$('power-cfg').className=(v==2)?'':'hidden';
}

async function saveMode(){
var mode=parseInt($('opmode').value);
var body={operating_mode:mode};
if(mode==2){
 body.espnow_channel=parseInt($('enchan').value)||1;
 body.hub_mac=$('enmac').value.trim();
 body.sleep_duration=parseInt($('sleeps').value)||300;
 body.wake_button_pin=parseInt($('wakepin').value);
 if(!body.hub_mac||body.hub_mac.length<17){msg('Enter Hub MAC address (AA:BB:CC:DD:EE:FF)',false);return}
}
try{
 var r=await fetch('/api/config/mode',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
 var d=await r.json();
 if(d.success){msg(d.message,true)}else{msg(d.error||'Failed',false)}
}catch(e){msg('Error: '+e.message,false)}
}

async function scan(){
 $('nl').style.display='block';$('nl').innerHTML='<div class="ni">Scanning...</div>';
 try{await fetch('/api/wifi/networks');
  for(var i=0;i<10;i++){
   await new Promise(r=>setTimeout(r,1500));
   var d=await(await fetch('/api/wifi/networks')).json();
   if(!d.scanning){
    if(d.networks&&d.networks.length){
     $('nl').innerHTML=d.networks.map(n=>
      '<div class="ni" onclick="$(\'ssid\').value=\''+n.ssid.replace(/'/g,"\\'")+'\'"><span>'+n.ssid+'</span><span class="r">'+n.rssi+'dBm</span></div>'
     ).join('');
    }else{$('nl').innerHTML='<div class="ni">No networks found</div>'}
    return;
   }
  }
  $('nl').innerHTML='<div class="ni">Scan timeout</div>';
 }catch(e){$('nl').innerHTML='<div class="ni">Scan error</div>'}
}

async function saveWifi(){
 var s=$('ssid').value.trim();
 if(!s){msg('Enter a network name',false);return}
 try{
  var r=await fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},
   body:JSON.stringify({ssid:s,password:$('pw').value,hub_address:$('hub').value.trim()})});
  var d=await r.json();
  if(d.success){msg(d.message,true)}else{msg(d.error||'Failed',false)}
 }catch(e){msg('Error: '+e.message,false)}
}

async function saveMqtt(){
 try{
  var r=await fetch('/api/config/mqtt',{method:'POST',headers:{'Content-Type':'application/json'},
   body:JSON.stringify({enabled:$('mqen').checked,broker:$('mqbr').value.trim(),
    port:parseInt($('mqpt').value)||1883,username:$('mqus').value.trim(),
    password:$('mqpw').value,base_topic:$('mqtp').value.trim()})});
  var d=await r.json();
  msg(d.message||'Saved',d.success);
 }catch(e){msg('Error: '+e.message,false)}
}

async function saveSensor(){
 try{
  var r=await fetch('/api/config/sensor',{method:'POST',headers:{'Content-Type':'application/json'},
   body:JSON.stringify({name:$('sname').value.trim(),
    dry_value:parseInt($('sdry').value)||0,wet_value:parseInt($('swet').value)||0})});
  var d=await r.json();
  msg(d.message||'Saved',d.success);
 }catch(e){msg('Error: '+e.message,false)}
}

async function reboot(){
 if(!confirm('Reboot device?'))return;
 try{await fetch('/api/system/reboot',{method:'POST'});msg('Rebooting...',true)}
 catch(e){msg('Error',false)}
}

async function importHub(){
var t=$('impjson').value.trim();
if(!t){msg('Paste config JSON from Hub',false);return}
try{
 var r=await fetch('/api/espnow/import',{method:'POST',headers:{'Content-Type':'application/json'},body:t});
 var d=await r.json();
 if(d.success){msg(d.message,true)}else{msg(d.error||'Failed',false)}
}catch(e){msg('Error: '+e.message,false)}}

fetch('/api/config').then(r=>r.json()).then(d=>{
 if(d.wifi){$('ssid').value=d.wifi.ssid||'';$('hub').value=d.wifi.hub_address||''}
 if(d.mqtt){$('mqen').checked=d.mqtt.enabled;$('mqbr').value=d.mqtt.broker||'';
  $('mqpt').value=d.mqtt.port||1883;$('mqus').value=d.mqtt.username||'';
  $('mqtp').value=d.mqtt.base_topic||'iwmp'}
 if(d.sensor){$('sname').value=d.sensor.name||'';$('stype').value=d.sensor.input_type_name||'';
  $('sdry').value=d.sensor.dry_value||0;$('swet').value=d.sensor.wet_value||0}
 if(d.system){$('did').value=d.system.device_id||'';
  $('heap').value=Math.round((d.system.free_heap||0)/1024)+' KB'}
 if(d.mode!==undefined){
  $('opmode').value=d.mode.operating_mode||0;
  $('enchan').value=d.mode.espnow_channel||1;
  $('enmac').value=d.mode.hub_mac||'';
  $('sleeps').value=d.mode.sleep_duration||300;
  $('wakepin').value=d.mode.wake_button_pin!==undefined?d.mode.wake_button_pin:5;
  modeChanged();
 }
}).catch(()=>{});
</script></body></html>)rawliteral";

// ============================================================
// RemoteWeb Implementation
// ============================================================

bool RemoteWeb::begin(RemoteController* controller) {
    if (_running) {
        LOG_W(TAG, "Web server already running");
        return true;
    }

    _ctrl = controller;
    _server = new AsyncWebServer(80);

    registerRoutes();

    _server->begin();
    _running = true;

    LOG_I(TAG, "Server started — heap: %u", ESP.getFreeHeap());
    return true;
}

void RemoteWeb::registerRoutes() {
    RemoteWeb* self = this;

    // ---- HTML pages ----

    _server->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        LOG_I(TAG, "GET / from %s", request->client()->remoteIP().toString().c_str());
        request->send(200, "text/html", REMOTE_STATUS_HTML);
    });

    _server->on("/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", REMOTE_SETTINGS_HTML);
    });

    // ---- JSON API ----

    _server->on("/api/status", HTTP_GET, [self](AsyncWebServerRequest* r) {
        self->handleGetStatus(r);
    });

    _server->on("/api/config", HTTP_GET, [self](AsyncWebServerRequest* r) {
        self->handleGetConfig(r);
    });

    _server->on("/api/wifi/connect", HTTP_POST,
        [](AsyncWebServerRequest* r) {},
        nullptr,
        [self](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t) {
            self->handlePostWifiConnect(r, d, l, i, t);
        }
    );

    _server->on("/api/wifi/networks", HTTP_GET, [self](AsyncWebServerRequest* r) {
        self->handleGetWifiNetworks(r);
    });

    _server->on("/api/config/mqtt", HTTP_POST,
        [](AsyncWebServerRequest* r) {},
        nullptr,
        [self](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t) {
            self->handlePostMqttConfig(r, d, l, i, t);
        }
    );

    _server->on("/api/config/sensor", HTTP_POST,
        [](AsyncWebServerRequest* r) {},
        nullptr,
        [self](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t) {
            self->handlePostSensorConfig(r, d, l, i, t);
        }
    );

    _server->on("/api/config/mode", HTTP_POST,
        [](AsyncWebServerRequest* r) {},
        nullptr,
        [self](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t) {
            self->handlePostModeConfig(r, d, l, i, t);
        }
    );

    _server->on("/api/system/reboot", HTTP_POST, [self](AsyncWebServerRequest* r) {
        self->handlePostReboot(r);
    });

    _server->on("/api/system/return-mode", HTTP_POST, [self](AsyncWebServerRequest* r) {
        self->handlePostReturnMode(r);
    });

    _server->on("/api/espnow/import", HTTP_POST,
        [](AsyncWebServerRequest* r) {},
        nullptr,
        [self](AsyncWebServerRequest* request, uint8_t* data, size_t len,
               size_t index, size_t total) {
            if (index != 0) return;
            self->handlePostEspNowImport(request, data, len);
        });

    // ---- Captive portal redirects ----
    // Use beginResponse() instead of redirect() to include Content-Length: 0.
    // Android times out (~400ms) waiting for a body if none is declared.

    auto captiveRedirect = [](AsyncWebServerRequest* r) {
        LOG_I(TAG, "Captive probe: %s %s (host: %s)",
              r->methodToString(), r->url().c_str(),
              r->host().c_str());
        AsyncWebServerResponse* resp = r->beginResponse(302, "text/plain", "");
        resp->addHeader("Location", "http://192.168.4.1/");
        resp->addHeader("Cache-Control", "no-cache, no-store");
        r->send(resp);
    };
    _server->on("/generate_204",        HTTP_GET, captiveRedirect);
    _server->on("/gen_204",             HTTP_GET, captiveRedirect);
    _server->on("/hotspot-detect.html", HTTP_GET, captiveRedirect);
    _server->on("/connecttest.txt",     HTTP_GET, captiveRedirect);
    _server->on("/ncsi.txt",            HTTP_GET, captiveRedirect);
    _server->on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send(204);
    });

    // ---- Catch-all ----

    _server->onNotFound([](AsyncWebServerRequest* request) {
        LOG_I(TAG, "404: %s %s (host: %s)",
              request->methodToString(), request->url().c_str(),
              request->host().c_str());
        if (request->url().startsWith("/api/")) {
            request->send(404, "application/json", "{\"error\":\"Not found\"}");
        } else {
            request->redirect("/");
        }
    });
}

// ============================================================
// API Handlers
// ============================================================

void RemoteWeb::handleGetStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;

    doc["moisture_percent"] = _ctrl->getLastMoisturePercent();
    doc["raw_value"] = _ctrl->getLastRawValue();
    doc["sensor_name"] = Config.getMoistureSensor(0).sensor_name;

    doc["wifi_connected"] = WiFi.isConnected();
    doc["ssid"] = WiFi.isConnected() ? WiFi.SSID() : "";
    doc["ip"] = WiFi.isConnected() ? WiFi.localIP().toString() : "";
    doc["rssi"] = WiFi.isConnected() ? (int)WiFi.RSSI() : 0;

    doc["hub_address"] = Config.getWifi().hub_address;
    doc["hub_last_report"] = _ctrl->getLastHubReportTime();
    doc["hub_last_status"] = _ctrl->getLastHubReportSuccess();

    doc["mqtt_enabled"] = Config.getMqtt().enabled;
    doc["mqtt_connected"] = _ctrl->isMqttConnected();

    doc["operating_mode"] = Config.getPower().operating_mode;
    doc["override_active"] = _ctrl->isOverrideActive();

    doc["uptime"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["device_name"] = Config.getDeviceName();
    doc["firmware"] = IWMP_VERSION;
    doc["state"] = static_cast<int>(_ctrl->getState());

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void RemoteWeb::handleGetConfig(AsyncWebServerRequest* request) {
    JsonDocument doc;

    const auto& wifi = Config.getWifi();
    auto wo = doc["wifi"].to<JsonObject>();
    wo["ssid"] = wifi.ssid;
    wo["hub_address"] = wifi.hub_address;

    const auto& mqtt = Config.getMqtt();
    auto mo = doc["mqtt"].to<JsonObject>();
    mo["enabled"] = mqtt.enabled;
    mo["broker"] = mqtt.broker;
    mo["port"] = mqtt.port;
    mo["username"] = mqtt.username;
    mo["base_topic"] = mqtt.base_topic;

    const auto& sensor = Config.getMoistureSensor(0);
    auto so = doc["sensor"].to<JsonObject>();
    so["name"] = sensor.sensor_name;
    so["input_type"] = static_cast<int>(sensor.input_type);
    so["input_type_name"] = _ctrl->getSensorTypeName();
    so["dry_value"] = sensor.dry_value;
    so["wet_value"] = sensor.wet_value;
    so["enabled"] = sensor.enabled;

    auto sys = doc["system"].to<JsonObject>();
    sys["device_id"] = Config.getDeviceId();
    sys["device_name"] = Config.getDeviceName();
    sys["free_heap"] = ESP.getFreeHeap();

    // Mode config
    const auto& pwr = Config.getPower();
    const auto& espnow = Config.getEspNow();
    auto md = doc["mode"].to<JsonObject>();
    md["operating_mode"] = pwr.operating_mode;
    md["espnow_channel"] = espnow.channel;

    // Format Hub MAC as string
    char mac_str[18] = "";
    const uint8_t* hm = espnow.hub_mac;
    bool has_mac = false;
    for (int i = 0; i < 6; i++) {
        if (hm[i] != 0) { has_mac = true; break; }
    }
    if (has_mac) {
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 hm[0], hm[1], hm[2], hm[3], hm[4], hm[5]);
    }
    md["hub_mac"] = mac_str;
    md["sleep_duration"] = pwr.deep_sleep_duration_sec;
    md["wake_button_pin"] = pwr.wake_button_pin;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void RemoteWeb::handlePostWifiConnect(AsyncWebServerRequest* request,
                                       uint8_t* data, size_t len,
                                       size_t index, size_t total) {
    if (index != 0) return;

    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    const char* ssid = doc["ssid"] | "";
    const char* password = doc["password"] | "";
    const char* hub_addr = doc["hub_address"] | "";

    if (strlen(ssid) == 0) {
        request->send(400, "application/json", "{\"error\":\"SSID required\"}");
        return;
    }

    auto& cfg = Config.getConfigMutable();
    strlcpy(cfg.wifi.ssid, ssid, sizeof(cfg.wifi.ssid));
    strlcpy(cfg.wifi.password, password, sizeof(cfg.wifi.password));
    strlcpy(cfg.wifi.hub_address, hub_addr, sizeof(cfg.wifi.hub_address));
    Config.save();

    LOG_I(TAG, "WiFi saved: %s (hub: %s)", ssid,
          strlen(hub_addr) > 0 ? hub_addr : "none");

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"WiFi saved! Rebooting...\"}");

    _ctrl->scheduleReboot(1500);
}

void RemoteWeb::handleGetWifiNetworks(AsyncWebServerRequest* request) {
    int16_t result = WiFi.scanComplete();

    if (result == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);
        request->send(200, "application/json", "{\"scanning\":true}");
        return;
    }

    if (result == WIFI_SCAN_RUNNING) {
        request->send(200, "application/json", "{\"scanning\":true}");
        return;
    }

    JsonDocument doc;
    doc["scanning"] = false;
    JsonArray networks = doc["networks"].to<JsonArray>();
    for (int i = 0; i < result && i < 20; i++) {
        JsonObject net = networks.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["encrypted"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void RemoteWeb::handlePostMqttConfig(AsyncWebServerRequest* request,
                                      uint8_t* data, size_t len,
                                      size_t index, size_t total) {
    if (index != 0) return;

    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    auto& mqtt = Config.getMqttMutable();
    mqtt.enabled = doc["enabled"] | mqtt.enabled;
    if (!doc["broker"].isNull())     strlcpy(mqtt.broker, doc["broker"] | "", sizeof(mqtt.broker));
    mqtt.port = doc["port"] | mqtt.port;
    if (!doc["username"].isNull())   strlcpy(mqtt.username, doc["username"] | "", sizeof(mqtt.username));
    if (!doc["password"].isNull())   strlcpy(mqtt.password, doc["password"] | "", sizeof(mqtt.password));
    if (!doc["base_topic"].isNull()) strlcpy(mqtt.base_topic, doc["base_topic"] | "", sizeof(mqtt.base_topic));
    Config.save();

    LOG_I(TAG, "MQTT saved (enabled=%d, broker=%s)", mqtt.enabled, mqtt.broker);
    _ctrl->onMqttConfigChanged();

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"MQTT settings saved\"}");
}

void RemoteWeb::handlePostSensorConfig(AsyncWebServerRequest* request,
                                        uint8_t* data, size_t len,
                                        size_t index, size_t total) {
    if (index != 0) return;

    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    auto& sensor = Config.getMoistureSensorMutable(0);
    if (!doc["name"].isNull())      strlcpy(sensor.sensor_name, doc["name"] | "", sizeof(sensor.sensor_name));
    sensor.dry_value = doc["dry_value"] | sensor.dry_value;
    sensor.wet_value = doc["wet_value"] | sensor.wet_value;
    Config.save();

    LOG_I(TAG, "Sensor saved: %s (dry=%u, wet=%u)",
          sensor.sensor_name, sensor.dry_value, sensor.wet_value);
    _ctrl->onSensorConfigChanged();

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Sensor settings saved\"}");
}

void RemoteWeb::handlePostModeConfig(AsyncWebServerRequest* request,
                                      uint8_t* data, size_t len,
                                      size_t index, size_t total) {
    if (index != 0) return;

    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    uint8_t mode = doc["operating_mode"] | 0;
    if (mode > (uint8_t)RemoteMode::LOW_POWER) {
        request->send(400, "application/json", "{\"error\":\"Invalid mode\"}");
        return;
    }

    auto& pwr = Config.getPowerMutable();
    pwr.operating_mode = mode;

    // Low Power mode: save ESP-NOW and power settings
    if (mode == (uint8_t)RemoteMode::LOW_POWER) {
        auto& espnow = Config.getEspNowMutable();

        // ESP-NOW channel
        uint8_t channel = doc["espnow_channel"] | 1;
        if (channel < 1 || channel > 14) channel = 1;
        espnow.channel = channel;
        espnow.enabled = true;

        // Hub MAC address
        const char* mac_str = doc["hub_mac"] | "";
        if (strlen(mac_str) == 17) {
            unsigned int m[6];
            if (sscanf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
                       &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
                for (int i = 0; i < 6; i++) {
                    espnow.hub_mac[i] = (uint8_t)m[i];
                }
            }
        }

        // Sleep duration
        uint32_t sleep = doc["sleep_duration"] | 300;
        if (sleep < 60) sleep = 60;
        if (sleep > 3600) sleep = 3600;
        pwr.deep_sleep_duration_sec = sleep;

        // Wake button pin
        uint8_t pin = doc["wake_button_pin"] | 5;
        if (pin > 5) pin = 5;  // C3 only supports GPIO 0-5
        pwr.wake_button_pin = pin;
        pwr.wake_on_button = true;

        LOG_I(TAG, "Low Power config: ch=%d, sleep=%lu, btn=GPIO%d",
              channel, sleep, pin);
    }

    Config.save();

    const char* mode_names[] = {"WiFi", "Standalone", "Low Power"};
    LOG_I(TAG, "Mode saved: %s — rebooting", mode_names[mode]);

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Mode saved! Rebooting...\"}");

    _ctrl->scheduleReboot(1500);
}

void RemoteWeb::handlePostReboot(AsyncWebServerRequest* request) {
    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Rebooting...\"}");
    _ctrl->scheduleReboot(1000);
}

void RemoteWeb::handlePostEspNowImport(AsyncWebServerRequest* request,
                                            uint8_t* data, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    auto& cfg = Config.getConfigMutable();

    // WiFi credentials
    const char* ssid = doc["wifi_ssid"] | "";
    const char* pwd  = doc["wifi_password"] | "";
    const char* ip   = doc["hub_ip"] | "";
    if (strlen(ssid) > 0) {
        strlcpy(cfg.wifi.ssid, ssid, sizeof(cfg.wifi.ssid));
        strlcpy(cfg.wifi.password, pwd, sizeof(cfg.wifi.password));
    }
    if (strlen(ip) > 0) {
        strlcpy(cfg.wifi.hub_address, ip, sizeof(cfg.wifi.hub_address));
    }

    // ESP-NOW settings (hub MAC + channel -- used by Low Power mode)
    const char* mac_str = doc["hub_mac"] | "";
    if (strlen(mac_str) == 17) {
        unsigned int m[6];
        if (sscanf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
                   &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
            for (int i = 0; i < 6; i++) cfg.espnow.hub_mac[i] = (uint8_t)m[i];
        }
    }
    uint8_t channel = doc["channel"] | (uint8_t)1;
    if (channel < 1 || channel > 14) channel = 1;
    cfg.espnow.channel = channel;
    cfg.espnow.enabled = true;

    Config.save();

    LOG_I(TAG, "ESP-NOW import: ssid=%s hub_ip=%s mac=%s ch=%d",
          ssid, ip, mac_str, channel);

    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Config imported! Rebooting...\"}");
    _ctrl->scheduleReboot(1500);
}
void RemoteWeb::handlePostReturnMode(AsyncWebServerRequest* request) {
    request->send(200, "application/json",
        "{\"success\":true,\"message\":\"Returning to configured mode...\"}");
    _ctrl->returnToConfiguredMode();
}

} // namespace iwmp
