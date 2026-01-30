/**
 * @file wifi_manager.h
 * @brief WiFi connection handling with AP mode and captive portal
 *
 * Manages WiFi station connection, access point mode for configuration,
 * and DNS-based captive portal for easy setup.
 */

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <functional>
#include "config_schema.h"

namespace iwmp {

enum class WifiState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    AP_MODE,
    AP_WITH_CAPTIVE_PORTAL
};

class WifiManager {
public:
    using ConnectCallback = std::function<void()>;
    using DisconnectCallback = std::function<void(uint8_t reason)>;

    /**
     * @brief Initialize WiFi manager
     * @param config WiFi configuration
     * @return true if initialized
     */
    bool begin(const WifiConfig& config);

    /**
     * @brief Connect to configured WiFi network
     * @return true if connection initiated
     */
    bool connect();

    /**
     * @brief Disconnect from WiFi
     */
    void disconnect();

    /**
     * @brief Get current WiFi channel (important for ESP-NOW)
     * @return Current channel number
     */
    uint8_t getCurrentChannel();

    /**
     * @brief Start Access Point mode
     * @param ssid AP SSID
     * @param password AP password (nullptr for open)
     * @return true if AP started
     */
    bool startAP(const char* ssid, const char* password = nullptr);

    /**
     * @brief Stop Access Point mode
     */
    void stopAP();

    /**
     * @brief Start captive portal (must be in AP mode)
     */
    void startCaptivePortal();

    /**
     * @brief Stop captive portal
     */
    void stopCaptivePortal();

    /**
     * @brief Check if connected to WiFi
     * @return true if connected
     */
    bool isConnected() const;

    /**
     * @brief Get current IP address
     * @return IP address
     */
    IPAddress getIP() const;

    /**
     * @brief Get AP IP address
     * @return AP IP address (usually 192.168.4.1)
     */
    IPAddress getAPIP() const;

    /**
     * @brief Get current RSSI
     * @return Signal strength in dBm
     */
    int8_t getRSSI() const;

    /**
     * @brief Get current state
     * @return WiFi state
     */
    WifiState getState() const { return _state; }

    /**
     * @brief Set connection callback
     * @param cb Callback function
     */
    void onConnect(ConnectCallback cb) { _connect_callback = cb; }

    /**
     * @brief Set disconnection callback
     * @param cb Callback function
     */
    void onDisconnect(DisconnectCallback cb) { _disconnect_callback = cb; }

    /**
     * @brief Process WiFi events (call in loop)
     */
    void loop();

    /**
     * @brief Get MAC address as string
     * @return MAC address string (format: AABBCCDDEEFF)
     */
    String getMacAddress() const;

    /**
     * @brief Scan for available networks
     * @return Number of networks found
     */
    int16_t scanNetworks();

    /**
     * @brief Get scanned network info
     * @param index Network index
     * @param ssid Output SSID
     * @param rssi Output RSSI
     * @param encrypted Output encryption status
     * @return true if valid index
     */
    bool getScannedNetwork(uint8_t index, String& ssid, int32_t& rssi, bool& encrypted);

private:
    WifiConfig _config;
    WifiState _state = WifiState::DISCONNECTED;
    DNSServer _dns_server;
    bool _captive_portal_active = false;

    ConnectCallback _connect_callback;
    DisconnectCallback _disconnect_callback;

    uint32_t _connect_start_time = 0;
    uint32_t _last_reconnect_attempt = 0;
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;

    /**
     * @brief Handle DNS requests for captive portal
     */
    void handleDNS();

    /**
     * @brief Sync ESP-NOW channel with WiFi
     */
    void syncEspNowChannel();

    /**
     * @brief Apply static IP configuration
     */
    void applyStaticIP();

    // WiFi event handlers
    static void onWiFiEvent(WiFiEvent_t event);
};

// Global WiFi manager instance
extern WifiManager WiFiMgr;

} // namespace iwmp
