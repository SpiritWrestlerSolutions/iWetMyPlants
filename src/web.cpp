// web.cpp - the unit's web page and its small JSON API. Runs inside loop(), one request at a time.
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>
#include <esp_system.h>
#include "app.h"

// The page, embedded by board_build.embed_files.
extern const uint8_t pageStart[] asm("_binary_src_index_html_start");
extern const uint8_t pageEnd[] asm("_binary_src_index_html_end");

namespace {
WebServer server(80);
bool rebootPending, otaOk;
uint32_t rebootAt;

void scheduleReboot() {   // after the reply has gone out
  rebootPending = true;
  rebootAt = millis() + 750;
}

void sendJson(int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

void sendOk() {
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

void sendError(int code, const char* msg) {
  JsonDocument d;
  d["error"] = msg;
  sendJson(code, d);
}

bool authorised() {
  return server.hasHeader("X-IWMP") && (!cfg.admin_pass[0] || server.authenticate("admin", cfg.admin_pass));
}

// Every change goes through here. Browsers won't send a custom header to another site
// without asking first, and this server never says yes, so other websites can't change
// anything. With an admin password set, the browser also asks for it.
bool allowed() {
  if (!server.hasHeader("X-IWMP")) {
    sendError(403, "Changes are only accepted from this unit's own page");
    return false;
  }
  if (cfg.admin_pass[0] && !server.authenticate("admin", cfg.admin_pass)) {
    server.requestAuthentication(BASIC_AUTH, "iWetMyPlants");
    return false;
  }
  return true;
}

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "restarted";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog (it got stuck)";
    case ESP_RST_BROWNOUT: return "power dip";
    case ESP_RST_DEEPSLEEP: return "woke from sleep";
    case ESP_RST_EXT: return "reset button";
    default: return "other";
  }
}

void handleState() {
  JsonDocument d;
  d["id"] = deviceId();
  d["name"] = cfg.name;
  d["fw"] = IWMP_VERSION;
  d["chip"] = ESP.getChipModel();
  d["up"] = millis() / 1000;
  d["heap"] = ESP.getFreeHeap();
  d["reset"] = resetReason();
  JsonObject w = d["wifi"].to<JsonObject>();
  w["setup"] = wifiSetupMode();
  w["ssid"] = cfg.wifi_ssid;
  if (wifiConnected()) {
    w["ip"] = WiFi.localIP().toString();
    w["rssi"] = WiFi.RSSI();
  }
  sendJson(200, d);
}

void handleConfigGet() {
  JsonDocument d;
  configToJson(cfg, d.to<JsonObject>(), false);
  JsonObject pins = d["pins"].to<JsonObject>();
  copyArray(ADC_PINS, pins["adc"].to<JsonArray>());
  copyArray(OUT_PINS, pins["out"].to<JsonArray>());
  pins["sda"] = SDA_PIN;
  pins["scl"] = SCL_PIN;
  sendJson(200, d);
}

void handleConfigPost() {
  if (!allowed()) return;
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain")) || !in.is<JsonObject>())
    return sendError(400, "That wasn't valid JSON");
  if (const char* problem = configApply(cfg, in.as<JsonObjectConst>())) return sendError(400, problem);
  if (!configSave(cfg)) sendError(500, "Couldn't save the settings. Restarting with the old ones.");
  else sendOk();
  scheduleReboot();   // one code path for every change: nothing is ever half-applied
}

void handleScan() {
  JsonDocument d;
  wifiScanJson(d.to<JsonObject>());
  sendJson(200, d);
}

// OTA. The core calls this for each chunk before the route handler runs, so the checks
// happen at the start of the upload.
void handleUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaOk = authorised() && Update.begin(UPDATE_SIZE_UNKNOWN);
    if (otaOk) Serial.printf("Firmware update started: %s\n", up.filename.c_str());
  } else if (!otaOk) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      Update.abort();
      otaOk = false;
    }
    feedLoopWDT();   // the whole upload runs inside one handleClient() call
  } else if (up.status == UPLOAD_FILE_END) {
    otaOk = Update.end(true);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaOk = false;
  }
}

void handleUploadDone() {
  bool ok = otaOk;
  otaOk = false;
  if (!allowed()) return;
  if (!ok) return sendError(400, Update.hasError() ? Update.errorString() : "That file isn't firmware for this unit");
  sendOk();
  Serial.println("Firmware update done, restarting");
  scheduleReboot();
}

void handleNotFound() {
  if (wifiSetupMode()) {   // captive portal: send every stray request to the setup page
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.sendHeader("Cache-Control", "no-cache");
    server.send(302, "text/plain", "");   // empty body: some Android versions need it
  } else {
    server.send(404, "text/plain", "Not found");
  }
}
}  // namespace

void webBegin() {
  static const char* headers[] = {"X-IWMP"};
  server.collectHeaders(headers, 1);

  server.on("/", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html", (PGM_P)pageStart, pageEnd - pageStart);
  });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/reboot", HTTP_POST, [] {
    if (!allowed()) return;
    sendOk();
    scheduleReboot();
  });
  server.on("/api/reset", HTTP_POST, [] {
    if (!allowed()) return;
    configErase();
    sendOk();
    scheduleReboot();
  });
  server.on("/update", HTTP_POST, handleUploadDone, handleUpload);
  server.onNotFound(handleNotFound);
  server.begin();
}

void webLoop() {
  server.handleClient();
  if (rebootPending && int32_t(millis() - rebootAt) >= 0) ESP.restart();
}
