/**
 * @file improv_serial.cpp
 * @brief Improv WiFi Serial provisioning protocol implementation
 *
 * Reference: https://www.improv-wifi.com/serial/
 */

#include "improv_serial.h"
#include "../utils/logger.h"

namespace iwmp {

static constexpr const char* TAG = "Improv";

// Protocol constants
static const uint8_t MAGIC[6]    = { 'I', 'M', 'P', 'R', 'O', 'V' };
static constexpr uint8_t VERSION = 0x01;

static constexpr uint8_t TYPE_CURRENT_STATE = 0x01;
static constexpr uint8_t TYPE_ERROR_STATE   = 0x02;
static constexpr uint8_t TYPE_RPC_COMMAND   = 0x03;
static constexpr uint8_t TYPE_RPC_RESULT    = 0x04;

static constexpr uint8_t CMD_WIFI_SETTINGS   = 0x01;
static constexpr uint8_t CMD_IDENTIFY        = 0x02;
static constexpr uint8_t CMD_GET_DEVICE_INFO = 0x03;

static constexpr uint32_t BROADCAST_INTERVAL_MS = 1000;

// Minimum packet size: 6 magic + 1 version + 1 type + 1 data_len + 1 checksum
static constexpr uint16_t MIN_PACKET = 10;

// ── Public ────────────────────────────────────────────────────────────────────

void ImprovSerial::begin(Stream& serial) {
    _serial       = &serial;
    _state        = State::AUTHORIZED;
    _rxLen        = 0;
    _lastBroadcast = 0;
    // Discard any bytes already in the RX FIFO (e.g. boot-time garbage)
    while (_serial->available()) _serial->read();
    LOG_I(TAG, "Improv WiFi Serial ready — waiting for browser");
}

void ImprovSerial::loop() {
    if (!_serial) return;

    // Broadcast current state every second so esp-web-tools can detect us
    if (millis() - _lastBroadcast >= BROADCAST_INTERVAL_MS) {
        sendCurrentState(_state);
        _lastBroadcast = millis();
    }

    // Drain available RX bytes into buffer
    while (_serial->available() && _rxLen < (uint16_t)sizeof(_rxBuf)) {
        _rxBuf[_rxLen++] = (uint8_t)_serial->read();
    }

    if (_rxLen >= MIN_PACKET) {
        parseBuffer();
    }
}

// ── Private ───────────────────────────────────────────────────────────────────

void ImprovSerial::parseBuffer() {
    while (_rxLen >= MIN_PACKET) {
        // Locate the "IMPROV" magic sequence
        uint16_t magicPos = 0;
        bool found = false;
        for (uint16_t i = 0; i + 6 <= _rxLen; i++) {
            if (memcmp(&_rxBuf[i], MAGIC, 6) == 0) {
                magicPos = i;
                found = true;
                break;
            }
        }

        if (!found) {
            // Keep the last 5 bytes — a partial magic could span two reads
            if (_rxLen > 5) {
                uint16_t keep = 5;
                memmove(_rxBuf, _rxBuf + (_rxLen - keep), keep);
                _rxLen = keep;
            }
            return;
        }

        // Discard garbage before the magic
        if (magicPos > 0) {
            memmove(_rxBuf, _rxBuf + magicPos, _rxLen - magicPos);
            _rxLen -= magicPos;
        }

        if (_rxLen < MIN_PACKET) return;  // Need more bytes

        uint8_t  pkt_version  = _rxBuf[6];
        uint8_t  pkt_type     = _rxBuf[7];
        uint8_t  pkt_data_len = _rxBuf[8];
        uint16_t pkt_total    = (uint16_t)(9 + pkt_data_len + 1);  // header + data + checksum

        if (pkt_version != VERSION) {
            LOG_W(TAG, "Unknown version: %d — skipping byte", pkt_version);
            memmove(_rxBuf, _rxBuf + 1, _rxLen - 1);
            _rxLen--;
            continue;
        }

        if (_rxLen < pkt_total) return;  // Wait for rest of packet

        // Verify checksum (sum of all bytes before checksum, mod 256)
        uint8_t chk = 0;
        for (uint16_t i = 0; i < pkt_total - 1; i++) chk += _rxBuf[i];

        if (_rxBuf[pkt_total - 1] != chk) {
            LOG_W(TAG, "Checksum mismatch (exp=0x%02X got=0x%02X type=0x%02X dlen=%u) dropped",
                  chk, _rxBuf[pkt_total - 1], pkt_type, pkt_data_len);
            // Do NOT send INVALID_RPC: aborts browser dialog permanently.
            // Drop silently and let the browser retransmit.
            memmove(_rxBuf, _rxBuf + pkt_total, _rxLen - pkt_total);
            _rxLen -= pkt_total;
            continue;
        }

        // Dispatch
        if (pkt_type == TYPE_RPC_COMMAND) {
            handleRpcCommand(&_rxBuf[9], pkt_data_len);
        }
        // Silently ignore other types (CURRENT_STATE echo, etc.)

        // Consume this packet
        memmove(_rxBuf, _rxBuf + pkt_total, _rxLen - pkt_total);
        _rxLen -= pkt_total;
    }
}

void ImprovSerial::handleRpcCommand(const uint8_t* data, uint8_t len) {
    if (len < 1) {
        sendError(Error::INVALID_RPC);
        return;
    }
    if (data[0] == CMD_WIFI_SETTINGS) {
        handleWifiSettings(data, len);
    } else if (data[0] == CMD_IDENTIFY) {
        handleIdentify();
    } else if (data[0] == CMD_GET_DEVICE_INFO) {
        handleGetDeviceInfo();
    } else {
        LOG_W(TAG, "Unknown RPC command: 0x%02X", data[0]);
        sendError(Error::UNKNOWN_RPC);
    }
}

void ImprovSerial::handleWifiSettings(const uint8_t* data, uint8_t len) {
    // Layout: [CMD_WIFI_SETTINGS][ssid_len][ssid...][pwd_len][pwd...]
    if (len < 3) { sendError(Error::INVALID_RPC); return; }

    uint8_t ssid_len = data[1];
    if (ssid_len == 0 || (uint16_t)2 + ssid_len + 1 > len) {
        sendError(Error::INVALID_RPC);
        return;
    }

    char ssid[33] = {};
    memcpy(ssid, &data[2], ssid_len < 32 ? ssid_len : 32);

    char password[65] = {};
    uint8_t pwd_start = 2 + ssid_len;
    if (pwd_start < len) {
        uint8_t pwd_len = data[pwd_start];
        if ((uint16_t)pwd_start + 1 + pwd_len <= len) {
            memcpy(password, &data[pwd_start + 1], pwd_len < 64 ? pwd_len : 64);
        }
    }

    LOG_I(TAG, "Improv: received credentials for SSID '%s' — connecting", ssid);
    sendCurrentState(State::PROVISIONING);
    _serial->flush();  // Ensure PROVISIONING reaches browser before blocking

    String url;
    bool ok = _connectCb ? _connectCb(ssid, password, url) : false;

    if (ok) {
        sendRpcResult(url);
        sendCurrentState(State::PROVISIONED);
        _reProvisioned = true;
        LOG_I(TAG, "Improv: provisioned — device at %s", url.c_str());
        // Caller detects isProvisioned() and reboots after TX drains
    } else {
        LOG_W(TAG, "Improv: WiFi connect failed — ready to retry");
        sendError(Error::UNABLE_TO_CONNECT);
        sendCurrentState(State::AUTHORIZED);  // Allow retry without page refresh
    }
}

void ImprovSerial::setDeviceInfo(const char* fw_name, const char* fw_version,
                                 const char* hw_chip,  const char* dev_name) {
    strlcpy(_fw_name,    fw_name,    sizeof(_fw_name));
    strlcpy(_fw_version, fw_version, sizeof(_fw_version));
    strlcpy(_hw_chip,    hw_chip,    sizeof(_hw_chip));
    strlcpy(_dev_name,   dev_name,   sizeof(_dev_name));
}

void ImprovSerial::handleIdentify() {
    // Spec: blink device LED.  No response packet required.
    LOG_I(TAG, "Improv: CMD_IDENTIFY (no LED configured)");
}

void ImprovSerial::handleGetDeviceInfo() {
    // Response: [cmd][count=4][len][fw_name][len][fw_ver][len][chip][len][dev_name]
    const char* strings[4] = { _fw_name, _fw_version, _hw_chip, _dev_name };
    uint8_t buf[148];  // 2 header + 4*(1 len byte + up to 32 chars) = 138 max
    uint8_t pos = 0;
    buf[pos++] = CMD_GET_DEVICE_INFO;
    buf[pos++] = 4;  // four strings follow
    for (int i = 0; i < 4; i++) {
        uint8_t slen = (uint8_t)strnlen(strings[i], 32);
        if (pos + 1 + slen > sizeof(buf)) break;
        buf[pos++] = slen;
        memcpy(&buf[pos], strings[i], slen);
        pos += slen;
    }
    sendPacket(TYPE_RPC_RESULT, buf, pos);
    LOG_I(TAG, "Improv: CMD_GET_DEVICE_INFO -> %s %s", _fw_name, _fw_version);
}

void ImprovSerial::sendCurrentState(State s) {
    _state = s;
    uint8_t data = (uint8_t)s;
    sendPacket(TYPE_CURRENT_STATE, &data, 1);
}

void ImprovSerial::sendError(Error e) {
    uint8_t data = (uint8_t)e;
    sendPacket(TYPE_ERROR_STATE, &data, 1);
}

void ImprovSerial::sendRpcResult(const String& url) {
    // Payload: [CMD_WIFI_SETTINGS][count=1][url_len][url_bytes...]
    uint8_t url_len = (uint8_t)min((int)url.length(), 255);
    uint8_t buf[259];  // 3 header bytes + up to 255 URL bytes + 1 spare
    buf[0] = CMD_WIFI_SETTINGS;  // Echo command we're responding to
    buf[1] = 0x01;               // One URL follows
    buf[2] = url_len;
    memcpy(&buf[3], url.c_str(), url_len);
    sendPacket(TYPE_RPC_RESULT, buf, (uint8_t)(3 + url_len));
}

void ImprovSerial::sendPacket(uint8_t type, const uint8_t* data, uint8_t len) {
    if (!_serial) return;

    // Build the full packet in a stack buffer and send in one write() call.
    // HardwareSerial::write(buf, n) holds the UART TX mutex for the entire
    // operation — this prevents FreeRTOS tasks (AsyncWebServer callbacks,
    // logger) from inserting bytes mid-frame, which corrupts the binary
    // protocol and causes checksum mismatches that silently drop the packet.
    uint8_t buf[270];
    uint8_t pos = 0;
    uint8_t chk = 0;

    memcpy(&buf[pos], MAGIC, 6);
    for (int i = 0; i < 6; i++) chk += MAGIC[i];
    pos += 6;

    buf[pos] = VERSION;  chk += VERSION;  pos++;
    buf[pos] = type;     chk += type;     pos++;
    buf[pos] = len;      chk += len;      pos++;

    for (uint8_t i = 0; i < len; i++) {
        buf[pos++] = data[i];
        chk += data[i];
    }

    buf[pos++] = chk;
    _serial->write(buf, pos);
}

void ImprovSerial::broadcastProvisioned(const String& url) {
    if (!_serial) return;
    // Tell the browser the device is already connected with its URL.
    // Does NOT set _reProvisioned � wasReProvisioned() only fires when
    // the user actively submits new credentials via the browser dialog.
    sendRpcResult(url);
    sendCurrentState(State::PROVISIONED);
}

} // namespace iwmp
