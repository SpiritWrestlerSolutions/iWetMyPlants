// wifi.cpp - joins your WiFi, or opens the "iWetMyPlants-xxxxxx" setup network when it can't.
// The setup network and home WiFi are never on together: retrying WiFi hops channels and
// knocks phones off the setup network.
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_system.h>
#include <algorithm>
#include "app.h"

namespace {
constexpr uint32_t GIVE_UP_MS = 120000;  // WiFi down this long -> open the setup network
constexpr uint32_t SETUP_MS = 300000;    // keep it open this long, longer while a phone is on it
constexpr int MAX_NETS = 20;

DNSServer dns;
bool setupMode, online, scanning;
uint32_t since;      // when the current mode started, or when WiFi was last seen up
uint32_t scannedAt;

struct Net { char ssid[33]; int8_t rssi; bool open; };
Net nets[MAX_NETS];
int netCount;

// Keeps the strongest MAX_NETS distinct names from the last scan.
void keepScan(int n) {
  netCount = 0;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty() || ssid.length() > 32) continue;   // hidden networks
    int rssi = WiFi.RSSI(i);
    int j = 0;
    while (j < netCount && ssid != nets[j].ssid) j++;
    if (j == netCount) {
      if (netCount == MAX_NETS) {   // full: replace the weakest if this one beats it
        j = std::min_element(nets, nets + netCount, [](const Net& a, const Net& b) { return a.rssi < b.rssi; }) - nets;
        if (nets[j].rssi >= rssi) continue;
      } else {
        netCount++;
      }
      strcpy(nets[j].ssid, ssid.c_str());
      nets[j].rssi = -128;
    }
    if (rssi > nets[j].rssi) {
      nets[j].rssi = rssi;
      nets[j].open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
  }
  std::sort(nets, nets + netCount, [](const Net& a, const Net& b) { return a.rssi > b.rssi; });
  WiFi.scanDelete();
  scannedAt = millis();
}

void lowerTxPowerOnC3() {
#ifdef IWMP_C3
  WiFi.setTxPower(WIFI_POWER_8_5dBm);   // SuperMini antennas misbehave at full power
#endif
}

void startSta() {
  if (setupMode) {
    dns.stop();
    WiFi.softAPdisconnect(true);
    setupMode = false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.waitStatusBits(STA_STARTED_BIT, 1000);   // setTxPower silently fails before this
  lowerTxPowerOnC3();
  WiFi.setSleep(false);                         // mains powered: answer requests promptly
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);
  online = false;
  since = millis();
  Serial.printf("Joining WiFi \"%s\"\n", cfg.wifi_ssid);
}

void startSetup() {
  // Scan now, while only the station side runs. Changing radio mode later would drop a
  // connected phone (and, on the C3, the access point with it).
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  keepScan(WiFi.scanNetworks());

  char name[24];
  snprintf(name, sizeof name, "iWetMyPlants-%s", deviceId() + 5);
  WiFi.softAP(name);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));  // after softAP(), or the C3 hands out no addresses
  WiFi.waitStatusBits(AP_STARTED_BIT, 1000);
  lowerTxPowerOnC3();
  dns.start(53, "*", WiFi.softAPIP());   // every name resolves to us: phones show the setup page
  setupMode = true;
  online = false;
  since = millis();
  Serial.printf("Setup network \"%s\" is open: http://192.168.4.1/\n", name);
}
}  // namespace

const char* deviceId() {
  static char id[12];
  if (!id[0]) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(id, sizeof id, "iwmp-%02x%02x%02x", mac[3], mac[4], mac[5]);
  }
  return id;
}

void wifiBegin() {
  WiFi.persistent(false);       // credentials live in our config, not in the WiFi driver's flash
  WiFi.setHostname(deviceId());
  if (cfg.wifi_ssid[0]) startSta();
  else startSetup();
}

void wifiLoop() {
  uint32_t now = millis();
  if (setupMode) {
    dns.processNextRequest();
    if (WiFi.softAPgetStationNum() > 0) since = now;              // a phone is on it: stay open
    if (cfg.wifi_ssid[0] && now - since >= SETUP_MS) startSta();   // nobody came: try WiFi again
    return;
  }

  bool up = WiFi.isConnected();
  if (up != online) {
    online = up;
    if (up) {
      Serial.printf("WiFi up: %s  http://%s.local/\n", WiFi.localIP().toString().c_str(), deviceId());
      MDNS.begin(deviceId());
      MDNS.addService("http", "tcp", 80);
    } else {
      Serial.println("WiFi down");
      MDNS.end();
    }
  }
  if (up) since = now;
  else if (now - since >= GIVE_UP_MS) startSetup();

  if (scanning) {
    int n = WiFi.scanComplete();
    if (n >= 0) keepScan(n);
    if (n != WIFI_SCAN_RUNNING) scanning = false;
  }
}

bool wifiConnected() { return online; }
bool wifiSetupMode() { return setupMode; }

void wifiScanJson(JsonObject out) {
  // In setup mode the list comes from the scan made before the network opened.
  if (!setupMode && !scanning && (netCount == 0 || millis() - scannedAt > 30000) &&
      WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING)
    scanning = true;
  out["scanning"] = scanning;
  JsonArray list = out["nets"].to<JsonArray>();
  for (int i = 0; i < netCount; i++) {
    JsonObject n = list.add<JsonObject>();
    n["ssid"] = nets[i].ssid;
    n["rssi"] = nets[i].rssi;
    n["open"] = nets[i].open;
  }
}
