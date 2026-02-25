/**
 * @file wifi_manager.cpp
 * @brief WiFi connection handling implementation
 */

#include "wifi_manager.h"
#include "logger.h"

namespace iwmp {

// Global WiFi manager instance
WifiManager WiFiMgr;

static const char* TAG = "WiFiMgr";

bool WifiManager::begin(const WifiConfig& config) {
    _config = config;
    WiFi.onEvent(WifiManager::onWiFiEvent);
    return true;
}

bool WifiManager::connect() {
    if (_state == WifiState::CONNECTED) return true;

    WiFi.mode(WIFI_STA);

    if (_config.use_static_ip && _config.static_ip != 0) {
        applyStaticIP();
    }

    WiFi.begin(_config.ssid, _config.password);
    _state = WifiState::CONNECTING;
    _connect_start_time = millis();

    LOG_I(TAG, "Connecting to SSID: %s", _config.ssid);
    return true;
}

void WifiManager::disconnect() {
    WiFi.disconnect(true);
    _state = WifiState::DISCONNECTED;
}

uint8_t WifiManager::getCurrentChannel() {
    return (uint8_t)WiFi.channel();
}

bool WifiManager::startAP(const char* ssid, const char* password) {
    WiFi.mode(WIFI_AP_STA);
    bool result = (password && strlen(password) > 0)
                  ? WiFi.softAP(ssid, password)
                  : WiFi.softAP(ssid);
    if (result) {
        _state = WifiState::AP_MODE;
        LOG_I(TAG, "AP started: %s, IP: %s", ssid, WiFi.softAPIP().toString().c_str());
    }
    return result;
}

void WifiManager::stopAP() {
    WiFi.softAPdisconnect(true);
    if (_state == WifiState::AP_MODE || _state == WifiState::AP_WITH_CAPTIVE_PORTAL) {
        _state = WifiState::DISCONNECTED;
    }
}

void WifiManager::startCaptivePortal() {
    _dns_server.start(53, "*", WiFi.softAPIP());
    _captive_portal_active = true;
    _state = WifiState::AP_WITH_CAPTIVE_PORTAL;
    LOG_I(TAG, "Captive portal started");
}

void WifiManager::stopCaptivePortal() {
    _dns_server.stop();
    _captive_portal_active = false;
    if (_state == WifiState::AP_WITH_CAPTIVE_PORTAL) {
        _state = WifiState::AP_MODE;
    }
}

bool WifiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

IPAddress WifiManager::getIP() const {
    return WiFi.localIP();
}

IPAddress WifiManager::getAPIP() const {
    return WiFi.softAPIP();
}

int8_t WifiManager::getRSSI() const {
    return (int8_t)WiFi.RSSI();
}

void WifiManager::loop() {
    if (_captive_portal_active) {
        handleDNS();
    }

    if (_state == WifiState::CONNECTING) {
        if (isConnected()) {
            _state = WifiState::CONNECTED;
            LOG_I(TAG, "Connected! IP: %s", WiFi.localIP().toString().c_str());
            if (_connect_callback) _connect_callback();
        } else if (millis() - _connect_start_time > CONNECT_TIMEOUT_MS) {
            LOG_W(TAG, "Connection timeout");
            _state = WifiState::DISCONNECTED;
        }
    }

    if (_state == WifiState::CONNECTED && !isConnected()) {
        LOG_W(TAG, "WiFi disconnected");
        _state = WifiState::DISCONNECTED;
        if (_disconnect_callback) _disconnect_callback(0);
    }
}

String WifiManager::getMacAddress() const {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[13];
    snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

int16_t WifiManager::scanNetworks() {
    return (int16_t)WiFi.scanNetworks();
}

bool WifiManager::getScannedNetwork(uint8_t index, String& ssid, int32_t& rssi, bool& encrypted) {
    ssid = WiFi.SSID(index);
    if (ssid.length() == 0) return false;
    rssi      = WiFi.RSSI(index);
    encrypted = (WiFi.encryptionType(index) != WIFI_AUTH_OPEN);
    return true;
}

void WifiManager::handleDNS() {
    _dns_server.processNextRequest();
}

void WifiManager::syncEspNowChannel() {
    // Channel sync is handled externally by EspNowManager
}

void WifiManager::applyStaticIP() {
    IPAddress ip(_config.static_ip);
    IPAddress gateway(_config.gateway);
    IPAddress subnet(_config.subnet);
    IPAddress dns(_config.dns);
    WiFi.config(ip, gateway, subnet, dns);
}

void WifiManager::onWiFiEvent(WiFiEvent_t event) {
    (void)event;
}

} // namespace iwmp
