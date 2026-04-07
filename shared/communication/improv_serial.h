/**
 * @file improv_serial.h
 * @brief Improv WiFi Serial provisioning protocol
 *
 * Implements https://www.improv-wifi.com/serial/ so that esp-web-tools can
 * configure WiFi credentials over USB serial immediately after flashing,
 * without requiring the user to connect to the device AP manually.
 */

#pragma once
#include <Arduino.h>
#include <functional>

namespace iwmp {

class ImprovSerial {
public:
    enum class State : uint8_t {
        AUTHORIZED   = 0x02,  // Ready for credentials
        PROVISIONING = 0x03,  // Attempting WiFi connect
        PROVISIONED  = 0x04,  // Connected — URL available
    };
    enum class Error : uint8_t {
        NONE              = 0x00,
        INVALID_RPC       = 0x01,
        UNKNOWN_RPC       = 0x02,
        UNABLE_TO_CONNECT = 0x03,
    };

    /**
     * Callback invoked when the browser sends WiFi credentials.
     * Implementation must attempt to connect to WiFi and populate outUrl
     * (e.g. "http://192.168.1.100") on success.
     * @return true on successful WiFi connection, false on failure.
     */
    using ConnectCb = std::function<bool(const char* ssid, const char* pwd, String& outUrl)>;

    /**
     * Attach serial stream and reset state.  Call once when entering AP_MODE.
     */
    void begin(Stream& serial);

    /**
     * Process incoming bytes and send periodic state broadcasts.
     * Call every loop iteration while in AP_MODE.
     */
    void loop();

    /** True once WiFi credentials have been received and connection succeeded. */
    bool isProvisioned() const { return _state == State::PROVISIONED; }

    void setConnectCallback(ConnectCb cb) { _connectCb = cb; }

    /** Set device info returned by CMD_GET_DEVICE_INFO (0x03). */
    void setDeviceInfo(const char* fw_name, const char* fw_version,
                       const char* hw_chip,  const char* dev_name);

    /**
     * Announce to the installer browser that the device is already connected.
     * Does NOT set wasReProvisioned() � that only fires when the user submits
     * new credentials via the browser dialog.
     */
    void broadcastProvisioned(const String& url);

    /**
     * True once the user submitted credentials via the browser dialog AND
     * the device connected successfully.  Distinct from isProvisioned().
     */
    bool wasReProvisioned() const { return _reProvisioned; }

private:
    void parseBuffer();
    void handleRpcCommand(const uint8_t* data, uint8_t len);
    void handleWifiSettings(const uint8_t* data, uint8_t len);
    void handleIdentify();
    void handleGetDeviceInfo();
    void sendCurrentState(State s);
    void sendError(Error e);
    void sendRpcResult(const String& url);
    void sendPacket(uint8_t type, const uint8_t* data, uint8_t len);

    Stream*   _serial        = nullptr;
    State     _state         = State::AUTHORIZED;
    ConnectCb _connectCb;
    uint32_t  _lastBroadcast = 0;
    uint8_t   _rxBuf[270]   = {};
    uint16_t  _rxLen         = 0;
    bool      _reProvisioned = false;
    String    _provisioned_url;

    // Device info for CMD_GET_DEVICE_INFO responses
    char _fw_name[32]    = "iWetMyPlants";
    char _fw_version[16] = "1.0.0";
    char _hw_chip[16]    = "ESP32";
    char _dev_name[32]   = "iWetMyPlants";
};

} // namespace iwmp
