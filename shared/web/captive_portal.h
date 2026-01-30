/**
 * @file captive_portal.h
 * @brief Captive portal for initial WiFi configuration
 *
 * When the device cannot connect to WiFi (no credentials or connection failed),
 * it starts an Access Point and serves a captive portal for configuration.
 *
 * Features:
 * - Auto-starts AP mode when WiFi connection fails
 * - DNS server redirects all domains to device IP
 * - Simple web interface for entering WiFi credentials
 * - Automatic transition to STA mode after configuration
 * - Timeout fallback to configured behavior
 */

#pragma once

#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <functional>
#include "../config/config_schema.h"

namespace iwmp {

/**
 * @brief WiFi credentials callback
 *
 * Called when user submits WiFi credentials through portal.
 * @param ssid Network SSID
 * @param password Network password
 * @return true if credentials should be saved and connection attempted
 */
using CredentialsCallback = std::function<bool(const char* ssid, const char* password)>;

/**
 * @brief Portal status callback
 *
 * Called when portal state changes.
 */
using PortalStatusCallback = std::function<void(bool active)>;

/**
 * @brief Captive portal state
 */
enum class PortalState : uint8_t {
    INACTIVE,           // Portal not running
    STARTING,           // Starting AP and DNS
    ACTIVE,             // Portal active, waiting for user
    CONNECTING,         // Attempting WiFi connection
    CONNECTED,          // Successfully connected to WiFi
    FAILED,             // Connection failed
    TIMEOUT             // Portal timed out
};

/**
 * @brief Captive portal for WiFi configuration
 *
 * Provides an access point with a web interface for configuring
 * WiFi credentials when the device cannot connect to a network.
 */
class CaptivePortal {
public:
    /**
     * @brief Get singleton instance
     */
    static CaptivePortal& getInstance();

    /**
     * @brief Start the captive portal
     * @param ap_ssid Access point SSID
     * @param ap_password Access point password (empty for open)
     * @param timeout_ms Portal timeout in ms (0 = no timeout)
     * @return true if portal started successfully
     */
    bool begin(const char* ap_ssid, const char* ap_password = "",
               uint32_t timeout_ms = 300000);

    /**
     * @brief Start with device identity for auto-naming
     * @param identity Device identity for AP name generation
     * @param timeout_ms Portal timeout in ms
     */
    bool begin(const DeviceIdentity& identity, uint32_t timeout_ms = 300000);

    /**
     * @brief Stop the captive portal
     */
    void stop();

    /**
     * @brief Process DNS requests (call in loop)
     */
    void loop();

    /**
     * @brief Check if portal is active
     */
    bool isActive() const { return _state == PortalState::ACTIVE; }

    /**
     * @brief Get current portal state
     */
    PortalState getState() const { return _state; }

    /**
     * @brief Get portal IP address
     */
    IPAddress getIP() const { return _ap_ip; }

    /**
     * @brief Get connected client count
     */
    uint8_t getClientCount() const;

    /**
     * @brief Set credentials callback
     */
    void onCredentials(CredentialsCallback callback);

    /**
     * @brief Set status change callback
     */
    void onStatusChange(PortalStatusCallback callback);

    /**
     * @brief Attempt WiFi connection with provided credentials
     * @param ssid Network SSID
     * @param password Network password
     * @param timeout_ms Connection timeout
     * @return true if connection successful
     */
    bool tryConnect(const char* ssid, const char* password, uint32_t timeout_ms = 15000);

private:
    CaptivePortal();
    ~CaptivePortal() = default;
    CaptivePortal(const CaptivePortal&) = delete;
    CaptivePortal& operator=(const CaptivePortal&) = delete;

    // Components
    AsyncWebServer* _server = nullptr;
    DNSServer _dns;

    // State
    PortalState _state = PortalState::INACTIVE;
    IPAddress _ap_ip;
    uint32_t _start_time = 0;
    uint32_t _timeout_ms = 0;

    // Callbacks
    CredentialsCallback _credentials_callback = nullptr;
    PortalStatusCallback _status_callback = nullptr;

    // Configuration
    char _ap_ssid[33];
    char _ap_password[65];

    // Internal methods
    void setupRoutes();
    void servePortalPage(AsyncWebServerRequest* request);
    void handleCredentials(AsyncWebServerRequest* request);
    void handleScanNetworks(AsyncWebServerRequest* request);
    void setState(PortalState state);

    // HTML content
    static const char* getPortalHtml();
    static const char* getPortalCss();
    static const char* getSuccessHtml();
};

/**
 * @brief Global captive portal accessor
 */
#define Portal CaptivePortal::getInstance()

} // namespace iwmp
