/**
 * @file wifi_manager.cpp
 * @brief WiFi connection handling implementation
 */

#include "wifi_manager.h"
#include "../utils/logger.h"
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_mac.h>

namespace iwmp {

// Global WiFi manager instance
WifiManager WiFiMgr;

// Static instance pointer for event handler
static WifiManager* s_instance = nullptr;

// DNS server IP for captive portal
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

static constexpr uint16_t DNS_PORT = 53;
static constexpr const char* TAG = "WiFi";

bool WifiManager::begin(const WifiConfig& config) {
    _config = config;

    // Disable credential auto-persistence — we manage credentials in our NVS.
    // Without this the Arduino WiFi library may attempt to auto-reconnect to
    // previously stored networks, putting the radio in AP+STA mode and
    // causing STA scanning to steal the channel from the AP beacons.
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);

    // Only register event handler once
    if (!s_instance) {
        WiFi.onEvent(onWiFiEvent);
    }
    s_instance = this;

    LOG_I(TAG, "WiFi manager initialized");
    return true;
}

bool WifiManager::connect() {
    if (strlen(_config.ssid) == 0) {
        LOG_E(TAG, "No SSID configured");
        return false;
    }

    // Ensure STA mode (preserve AP if running)
    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    if (current_mode == WIFI_MODE_AP) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_STA);
    }

    _state = WifiState::CONNECTING;
    _connect_start_time = millis();

    // Disconnect any existing STA connection but keep radio on.
    // disconnect(true) calls esp_wifi_stop() which shuts the radio down;
    // WiFi.begin() then has to cold-start it, which can silently fail on C3.
    WiFi.disconnect(false);

    // Build hostname from MAC (efuse read — safe before WiFi start)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char hostname[24];
    snprintf(hostname, sizeof(hostname), "iwmp-%02x%02x%02x",
             mac[3], mac[4], mac[5]);

    LOG_I(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Set hostname via Arduino API
    WiFi.setHostname(hostname);

    // Also set directly on the STA netif (more reliable on ESP32-C3)
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif) {
        esp_netif_set_hostname(sta_netif, hostname);
    }

    LOG_I(TAG, "Hostname: %s", hostname);

    // Apply static IP if configured
    if (_config.use_static_ip) {
        applyStaticIP();
    }

    LOG_I(TAG, "Connecting to %s...", _config.ssid);
    WiFi.begin(_config.ssid, _config.password);

    return true;
}

void WifiManager::disconnect() {
    WiFi.disconnect(true);
    _state = WifiState::DISCONNECTED;
    LOG_I(TAG, "Disconnected");
}

uint8_t WifiManager::getCurrentChannel() {
    uint8_t primary_channel;
    wifi_second_chan_t secondary;
    esp_wifi_get_channel(&primary_channel, &secondary);
    return primary_channel;
}

bool WifiManager::startAP(const char* ssid, const char* password) {
    // Channel 6 — centre of the 2.4 GHz band, universally visible on all
    // regulatory domains.
    static constexpr uint8_t AP_CHANNEL = 6;

    // Let softAP() handle mode/netif setup internally.  Calling WiFi.mode()
    // first causes a STA→AP driver restart on C3 that silently breaks the AP.
    bool result = WiFi.softAP(ssid,
                              (password && strlen(password) >= 8) ? password : nullptr,
                              AP_CHANNEL);
    if (result) {
        // Explicitly configure AP IP/gateway/subnet after softAP().
        // On ESP32-C3, omitting this call leaves the DHCP server in an
        // uninitialised state — clients associate but never receive an IP.
        WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);

        // Disable modem sleep so beacons are never gated
        esp_wifi_set_ps(WIFI_PS_NONE);
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        delay(200);  // settle before DNS/HTTP bind

        wifi_second_chan_t sec;
        uint8_t ch = 0;
        esp_wifi_get_channel(&ch, &sec);
        LOG_I(TAG, "AP started: %s @ %s ch=%u txpwr=%d (free heap: %u)",
              ssid, WiFi.softAPIP().toString().c_str(),
              ch, WiFi.getTxPower(), ESP.getFreeHeap());
        _state = WifiState::AP_MODE;
    } else {
        LOG_E(TAG, "softAP failed");
    }

    return result;
}

void WifiManager::stopAP() {
    if (_captive_portal_active) {
        stopCaptivePortal();
    }

    WiFi.softAPdisconnect(true);

    // Restore STA mode if we were connected
    if (WiFi.isConnected()) {
        WiFi.mode(WIFI_STA);
        _state = WifiState::CONNECTED;
    } else {
        _state = WifiState::DISCONNECTED;
    }

    LOG_I(TAG, "AP stopped");
}

void WifiManager::startCaptivePortal() {
    if (_state != WifiState::AP_MODE && _state != WifiState::AP_WITH_CAPTIVE_PORTAL) {
        LOG_W(TAG, "Cannot start captive portal - not in AP mode");
        return;
    }

    // Start DNS server to redirect all requests to AP IP
    _dns_server.start(DNS_PORT, "*", AP_IP);
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

    LOG_I(TAG, "Captive portal stopped");
}

bool WifiManager::isConnected() const {
    return WiFi.isConnected();
}

IPAddress WifiManager::getIP() const {
    return WiFi.localIP();
}

IPAddress WifiManager::getAPIP() const {
    return WiFi.softAPIP();
}

int8_t WifiManager::getRSSI() const {
    return WiFi.RSSI();
}

void WifiManager::loop() {
    // Handle DNS for captive portal
    if (_captive_portal_active) {
        handleDNS();
    }

    // Handle connection timeout
    if (_state == WifiState::CONNECTING) {
        if ((millis() - _connect_start_time) > CONNECT_TIMEOUT_MS) {
            LOG_W(TAG, "Connection timeout");
            _state = WifiState::DISCONNECTED;
            if (_disconnect_callback) {
                _disconnect_callback(WIFI_REASON_AUTH_EXPIRE);
            }
        }
    }

    // Auto-reconnect with exponential backoff. _reconnect_attempts is
    // reset to 0 in the STA_GOT_IP event handler.
    if (_state == WifiState::DISCONNECTED && strlen(_config.ssid) > 0) {
        uint32_t interval = RECONNECT_INTERVAL_MS << _reconnect_attempts;
        if (interval > RECONNECT_INTERVAL_MAX_MS || _reconnect_attempts >= 6) {
            interval = RECONNECT_INTERVAL_MAX_MS;
        }
        if ((millis() - _last_reconnect_attempt) > interval) {
            _last_reconnect_attempt = millis();
            _reconnect_attempts++;
            LOG_I(TAG, "Reconnect attempt %u (next interval %lus)",
                  _reconnect_attempts, (unsigned long)(interval / 1000));
            connect();
        }
    }
}

String WifiManager::getMacAddress() const {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(mac_str);
}

int16_t WifiManager::scanNetworks() {
    LOG_I(TAG, "Starting network scan...");
    return WiFi.scanNetworks(true);  // Async scan
}

bool WifiManager::getScannedNetwork(uint8_t index, String& ssid, int32_t& rssi, bool& encrypted) {
    int16_t count = WiFi.scanComplete();
    if (count < 0 || index >= count) {
        return false;
    }

    ssid = WiFi.SSID(index);
    rssi = WiFi.RSSI(index);
    encrypted = (WiFi.encryptionType(index) != WIFI_AUTH_OPEN);

    return true;
}

void WifiManager::handleDNS() {
    // Process a few DNS requests per call — phones send several probes
    // during captive portal detection. Keep this small (2-3) so that on
    // single-core chips (ESP32-C3) the AsyncTCP task gets enough CPU
    // time to actually serve HTTP responses.
    _dns_server.processNextRequest();
    _dns_server.processNextRequest();
}

void WifiManager::syncEspNowChannel() {
    uint8_t channel = getCurrentChannel();
    LOG_I(TAG, "WiFi on channel %d, ESP-NOW synced", channel);
}

void WifiManager::applyStaticIP() {
    IPAddress ip(_config.static_ip);
    IPAddress gateway(_config.gateway);
    IPAddress subnet(_config.subnet);
    IPAddress dns(_config.dns);

    if (!WiFi.config(ip, gateway, subnet, dns)) {
        LOG_E(TAG, "Failed to configure static IP");
    } else {
        LOG_I(TAG, "Static IP configured: %s", ip.toString().c_str());
    }
}

void WifiManager::onWiFiEvent(WiFiEvent_t event) {
    if (!s_instance) return;

    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            s_instance->_state = WifiState::CONNECTED;
            s_instance->_last_reconnect_attempt = 0;
            s_instance->_reconnect_attempts = 0;
            LOG_I(TAG, "Connected! IP: %s", WiFi.localIP().toString().c_str());
            s_instance->syncEspNowChannel();
            if (s_instance->_connect_callback) {
                s_instance->_connect_callback();
            }
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            // Only log and callback if we were previously connected or connecting
            if (s_instance->_state == WifiState::CONNECTED ||
                s_instance->_state == WifiState::CONNECTING) {
                s_instance->_state = WifiState::DISCONNECTED;
                LOG_W(TAG, "Disconnected from WiFi");
                if (s_instance->_disconnect_callback) {
                    s_instance->_disconnect_callback(0);  // Reason not available in this callback
                }
            }
            break;
        }

        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            LOG_I(TAG, "STA connected, waiting for IP...");
            break;

        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            LOG_I(TAG, "Client connected to AP");
            break;

        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
            LOG_I(TAG, "Client got DHCP lease (IP assigned)");
            break;

        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            LOG_I(TAG, "Client disconnected from AP");
            break;

        default:
            break;
    }
}

} // namespace iwmp
