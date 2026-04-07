/**
 * @file espnow_manager.cpp
 * @brief ESP-NOW communication manager implementation
 */

#include "espnow_manager.h"
#include <esp_wifi.h>
#include <esp_mac.h>
#include "../utils/logger.h"

namespace iwmp {

// Global instance
EspNowManager& EspNow = EspNowManager::getInstance();

// Pointer for static callbacks
static EspNowManager* s_instance = nullptr;
static constexpr const char* TAG = "ESP-NOW";

EspNowManager& EspNowManager::getInstance() {
    static EspNowManager instance;
    return instance;
}

bool EspNowManager::begin(uint8_t channel) {
    if (_initialized) {
        return true;
    }

    s_instance = this;
    _channel = channel;

    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }

    // Get own MAC address (efuse read — safe before WiFi start)
    esp_read_mac(_own_mac, ESP_MAC_WIFI_STA);

    // Ensure WiFi has STA enabled (required for ESP-NOW) without
    // disrupting AP mode if it's already running
    wifi_mode_t current_mode;
    if (esp_wifi_get_mode(&current_mode) != ESP_OK) {
        current_mode = WIFI_MODE_NULL;
    }
    if (current_mode == WIFI_MODE_AP) {
        WiFi.mode(WIFI_AP_STA);
    } else if (current_mode == WIFI_MODE_NULL || current_mode == WIFI_MODE_MAX) {
        WiFi.mode(WIFI_STA);
    }
    // If already STA or AP_STA, leave as-is

    // Only force the channel when WiFi STA is NOT connected to an AP.
    // When STA is connected, the AP manages the channel and calling
    // esp_wifi_set_channel() can silently corrupt the TCP/IP stack
    // (especially on ESP32-C3), making the web server unreachable.
    if (!WiFi.isConnected()) {
        esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    }

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        LOG_E(TAG, "Init failed");
        return false;
    }

    // Register callbacks
    esp_now_register_send_cb(onDataSentStatic);
    esp_now_register_recv_cb(onDataRecvStatic);

    // Add broadcast peer by default
    addPeer(BROADCAST_MAC, _channel, false);

    _initialized = true;

    char mac_str[18];
    getMacString(mac_str, sizeof(mac_str));
    LOG_I(TAG, "Initialized on channel %d, MAC: %s", _channel, mac_str);

    return true;
}

void EspNowManager::end() {
    if (!_initialized) {
        return;
    }

    esp_now_unregister_send_cb();
    esp_now_unregister_recv_cb();
    esp_now_deinit();

    _pending_acks.clear();
    _recent_messages.clear();

    _initialized = false;
    s_instance = nullptr;

    LOG_I(TAG, "Shutdown");
}

void EspNowManager::getMacString(char* buffer, size_t size) const {
    formatMac(_own_mac, buffer, size);
}

// ============ Send Methods ============

bool EspNowManager::send(const uint8_t* peer_mac, const uint8_t* data, size_t len) {
    if (!_initialized || len > ESPNOW_MAX_PAYLOAD) {
        return false;
    }

    // Ensure peer exists - add if missing (kept until explicitly removed
    // or clearAllPeers is called; removing immediately after esp_now_send
    // races with the async send completion)
    if (!peerExists(peer_mac) && !compareMac(peer_mac, BROADCAST_MAC)) {
        addPeer(peer_mac, _channel, false);
    }

    esp_err_t result = esp_now_send(peer_mac, data, len);

    if (result == ESP_OK) {
        _stats.packets_sent++;
        return true;
    }

    LOG_E(TAG, "Send failed: %s", esp_err_to_name(result));
    return false;
}

bool EspNowManager::sendWithAck(const uint8_t* peer_mac, const uint8_t* data, size_t len,
                                 uint32_t timeout_ms) {
    if (!_initialized || len < sizeof(MessageHeader)) {
        return false;
    }

    // Get sequence from header
    const MessageHeader* header = reinterpret_cast<const MessageHeader*>(data);
    uint8_t sequence = header->sequence_number;

    // Create pending ACK entry
    PendingAck pending;
    copyMac(pending.peer_mac, peer_mac);
    pending.sequence = sequence;
    pending.sent_time = millis();
    pending.received = false;
    pending.send_success = false;

    // Limit pending ACKs
    if (_pending_acks.size() >= MAX_PENDING_ACKS) {
        cleanupPendingAcks();
    }
    _pending_acks.push_back(pending);

    // Send the message
    if (!send(peer_mac, data, len)) {
        removePendingAck(peer_mac, sequence);
        _stats.packets_lost++;
        return false;
    }

    // Wait for ACK
    uint32_t start = millis();
    PendingAck* ack_entry = findPendingAck(peer_mac, sequence);

    while (ack_entry && !ack_entry->received && (millis() - start) < timeout_ms) {
        delay(1);  // Allow callbacks to process
        ack_entry = findPendingAck(peer_mac, sequence);
    }

    bool success = ack_entry && ack_entry->received;

    if (success) {
        _stats.acks_received++;
    } else {
        _stats.acks_timeout++;
        _stats.packets_lost++;
    }

    removePendingAck(peer_mac, sequence);
    return success;
}

bool EspNowManager::sendWithRetry(const uint8_t* peer_mac, const uint8_t* data, size_t len,
                                   uint8_t max_retries, uint32_t retry_delay_ms) {
    if (!_initialized || len < sizeof(MessageHeader)) {
        return false;
    }

    // Check if message requires ACK
    const MessageHeader* header = reinterpret_cast<const MessageHeader*>(data);
    bool needs_ack = (header->flags & MsgFlags::REQUIRES_ACK) != 0;

    // Make mutable copy for retry flag
    uint8_t buffer[ESPNOW_MAX_PAYLOAD];
    memcpy(buffer, data, len);
    MessageHeader* mutable_header = reinterpret_cast<MessageHeader*>(buffer);

    for (uint8_t attempt = 0; attempt <= max_retries; attempt++) {
        if (attempt > 0) {
            mutable_header->flags |= MsgFlags::IS_RETRY;
            _stats.retries++;
            delay(retry_delay_ms);
        }

        bool success;
        if (needs_ack) {
            success = sendWithAck(peer_mac, buffer, len);
        } else {
            success = send(peer_mac, buffer, len);
        }

        if (success) {
            return true;
        }

        LOG_D(TAG, "Retry %d/%d for seq %d",
                      attempt + 1, max_retries, mutable_header->sequence_number);
    }

    // packets_lost already incremented by sendWithAck/send per attempt
    return false;
}

bool EspNowManager::broadcast(const uint8_t* data, size_t len) {
    return send(BROADCAST_MAC, data, len);
}

// ============ Message Building Helpers ============

bool EspNowManager::sendMoistureReading(const uint8_t* peer_mac, uint8_t sensor_index,
                                         uint16_t raw_value, uint8_t moisture_percent) {
    MoistureReadingMsg msg;
    memset(&msg, 0, sizeof(msg));

    initMessageHeader(msg.header, MessageType::MOISTURE_READING, _own_mac, getNextSequence());
    msg.sensor_index = sensor_index;
    msg.raw_value = raw_value;
    msg.moisture_percent = moisture_percent;
    msg.rssi = WiFi.RSSI();

    return sendWithRetry(peer_mac, (uint8_t*)&msg, sizeof(msg));
}

bool EspNowManager::sendEnvironmentalReading(const uint8_t* peer_mac,
                                              int16_t temp_c_x10, uint16_t humidity_x10) {
    EnvironmentalReadingMsg msg;
    memset(&msg, 0, sizeof(msg));

    initMessageHeader(msg.header, MessageType::ENVIRONMENTAL_READING, _own_mac, getNextSequence());
    msg.temperature_c_x10 = temp_c_x10;
    msg.humidity_percent_x10 = humidity_x10;

    return sendWithRetry(peer_mac, (uint8_t*)&msg, sizeof(msg));
}

bool EspNowManager::sendBatteryStatus(const uint8_t* peer_mac, uint16_t voltage_mv,
                                       uint8_t percent, bool charging) {
    BatteryStatusMsg msg;
    memset(&msg, 0, sizeof(msg));

    initMessageHeader(msg.header, MessageType::BATTERY_STATUS, _own_mac, getNextSequence());
    msg.voltage_mv = voltage_mv;
    msg.percent = percent;
    msg.charging = charging ? 1 : 0;
    msg.low_battery = (voltage_mv < 3300) ? 1 : 0;

    // Battery status is informational - single send, no blocking retry
    return send(peer_mac, (uint8_t*)&msg, sizeof(msg));
}

bool EspNowManager::sendRelayCommand(const uint8_t* peer_mac, uint8_t relay_index,
                                      bool state, uint32_t duration_sec) {
    RelayCommandMsg msg;
    memset(&msg, 0, sizeof(msg));

    initMessageHeader(msg.header, MessageType::RELAY_COMMAND, _own_mac, getNextSequence());
    msg.header.flags |= MsgFlags::REQUIRES_ACK;  // Always require ACK for commands
    msg.relay_index = relay_index;
    msg.state = state ? 1 : 0;
    msg.duration_sec = duration_sec;

    return sendWithRetry(peer_mac, (uint8_t*)&msg, sizeof(msg));
}

bool EspNowManager::sendAnnounce(uint8_t device_type, const char* name,
                                  const char* version, uint8_t capabilities) {
    AnnounceMsg msg;
    memset(&msg, 0, sizeof(msg));

    initMessageHeader(msg.header, MessageType::ANNOUNCE, _own_mac, getNextSequence());
    msg.device_type = device_type;
    strncpy(msg.device_name, name, sizeof(msg.device_name) - 1);
    strncpy(msg.firmware_version, version, sizeof(msg.firmware_version) - 1);
    msg.capabilities = capabilities;
    msg.rssi = WiFi.RSSI();

    return broadcast((uint8_t*)&msg, sizeof(msg));
}

bool EspNowManager::sendPairRequest(const uint8_t* hub_mac, uint8_t device_type,
                                     const char* name, uint8_t capabilities) {
    PairRequestMsg msg;
    memset(&msg, 0, sizeof(msg));

    initMessageHeader(msg.header, MessageType::PAIR_REQUEST, _own_mac, getNextSequence());
    msg.header.flags |= MsgFlags::REQUIRES_ACK;
    msg.device_type = device_type;
    strncpy(msg.device_name, name, sizeof(msg.device_name) - 1);
    msg.capabilities = capabilities;

    return sendWithRetry(hub_mac, (uint8_t*)&msg, sizeof(msg));
}

bool EspNowManager::sendPairResponse(const uint8_t* peer_mac, bool accepted,
                                      uint8_t channel, uint16_t interval) {
    PairResponseMsg msg;
    memset(&msg, 0, sizeof(msg));

    initMessageHeader(msg.header, MessageType::PAIR_RESPONSE, _own_mac, getNextSequence());
    msg.accepted = accepted ? 1 : 0;
    msg.assigned_channel = channel;
    msg.reporting_interval_sec = interval;
    copyMac(msg.hub_mac, _own_mac);

    return send(peer_mac, (uint8_t*)&msg, sizeof(msg));
}

bool EspNowManager::sendHeartbeat(const uint8_t* peer_mac, uint32_t uptime, uint8_t status) {
    HeartbeatMsg msg;
    memset(&msg, 0, sizeof(msg));

    initMessageHeader(msg.header, MessageType::HEARTBEAT, _own_mac, getNextSequence());
    msg.uptime_sec = uptime;
    msg.status = status;
    msg.rssi = WiFi.RSSI();
    msg.free_heap = ESP.getFreeHeap() / 1024;

    return send(peer_mac, (uint8_t*)&msg, sizeof(msg));
}

bool EspNowManager::sendAck(const uint8_t* peer_mac, uint8_t sequence, MessageType type) {
    AckMsg msg;
    memset(&msg, 0, sizeof(msg));

    initMessageHeader(msg.header, MessageType::ACK, _own_mac, getNextSequence());
    msg.acked_sequence = sequence;
    msg.acked_type = type;

    return send(peer_mac, (uint8_t*)&msg, sizeof(msg));
}

bool EspNowManager::sendNack(const uint8_t* peer_mac, uint8_t sequence, MessageType type,
                              NackReason reason, const char* message) {
    NackMsg msg;
    memset(&msg, 0, sizeof(msg));

    initMessageHeader(msg.header, MessageType::NACK, _own_mac, getNextSequence());
    msg.nacked_sequence = sequence;
    msg.nacked_type = type;
    msg.reason = reason;
    if (message) {
        strncpy(msg.message, message, sizeof(msg.message) - 1);
    }

    return send(peer_mac, (uint8_t*)&msg, sizeof(msg));
}

// ============ Peer Management ============

bool EspNowManager::addPeer(const uint8_t* mac, uint8_t channel, bool encrypt,
                             const uint8_t* lmk) {
    if (!_initialized) {
        return false;
    }

    // Check if already exists
    if (peerExists(mac)) {
        return true;
    }

    esp_now_peer_info_t peer_info;
    memset(&peer_info, 0, sizeof(peer_info));
    copyMac(peer_info.peer_addr, mac);
    // Channel 0 = use the current interface channel (avoids mismatch when
    // the router assigns a channel different from the configured default).
    peer_info.channel = 0;
    peer_info.encrypt = encrypt;

    if (encrypt && lmk) {
        memcpy(peer_info.lmk, lmk, 16);
    }

    esp_err_t result = esp_now_add_peer(&peer_info);
    if (result != ESP_OK) {
        LOG_E(TAG, "Add peer failed: %s", esp_err_to_name(result));
        return false;
    }

    return true;
}

bool EspNowManager::removePeer(const uint8_t* mac) {
    if (!_initialized) {
        return false;
    }

    return esp_now_del_peer(mac) == ESP_OK;
}

bool EspNowManager::peerExists(const uint8_t* mac) {
    if (!_initialized) {
        return false;
    }

    return esp_now_is_peer_exist(mac);
}

uint8_t EspNowManager::getPeerCount() const {
    if (!_initialized) {
        return 0;
    }

    esp_now_peer_num_t num;
    if (esp_now_get_peer_num(&num) == ESP_OK) {
        return num.total_num;
    }
    return 0;
}

void EspNowManager::clearAllPeers() {
    if (!_initialized) {
        return;
    }

    esp_now_peer_info_t peer;
    while (esp_now_fetch_peer(true, &peer) == ESP_OK) {
        esp_now_del_peer(peer.peer_addr);
    }

    // Re-add broadcast peer
    addPeer(BROADCAST_MAC, _channel, false);
}

// ============ Callbacks ============

void EspNowManager::onReceive(ReceiveCallback callback) {
    _receive_callback = callback;
}

void EspNowManager::onSendComplete(SendCompleteCallback callback) {
    _send_complete_callback = callback;
}

void EspNowManager::onMessage(MessageCallback callback) {
    _message_callback = callback;
}

// ============ Encryption ============

bool EspNowManager::setPMK(const uint8_t* pmk) {
    if (!_initialized) {
        return false;
    }

    return esp_now_set_pmk(pmk) == ESP_OK;
}

// ============ Utility ============

void EspNowManager::update() {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        cleanupOldMessages();
        cleanupPendingAcks();
        xSemaphoreGive(_mutex);
    }
}

uint8_t EspNowManager::getNextSequence() {
    return ++_sequence_number;
}

void EspNowManager::printDebugInfo() const {
    char mac_str[18];
    formatMac(_own_mac, mac_str, sizeof(mac_str));
    LOG_D(TAG, "======== ESP-NOW Debug Info ========");
    LOG_D(TAG, "MAC: %s", mac_str);
    LOG_D(TAG, "Channel: %d", _channel);
    LOG_D(TAG, "Initialized: %s", _initialized ? "yes" : "no");
    LOG_D(TAG, "Peers: %d", getPeerCount());
    LOG_D(TAG, "--- Statistics ---");
    LOG_D(TAG, "  Packets sent: %u", _stats.packets_sent);
    LOG_D(TAG, "  Packets received: %u", _stats.packets_received);
    LOG_D(TAG, "  Packets lost: %u", _stats.packets_lost);
    LOG_D(TAG, "  ACKs received: %u", _stats.acks_received);
    LOG_D(TAG, "  ACKs timeout: %u", _stats.acks_timeout);
    LOG_D(TAG, "  Retries: %u", _stats.retries);
    LOG_D(TAG, "  Duplicates filtered: %u", _stats.duplicates_filtered);
    LOG_D(TAG, "  Delivery rate: %.1f%%", _stats.getDeliveryRate());
    LOG_D(TAG, "====================================");
}

// ============ Private Methods ============

bool EspNowManager::isDuplicate(const uint8_t* mac, uint8_t sequence) {
    for (const auto& msg : _recent_messages) {
        if (compareMac(msg.sender_mac, mac) && msg.sequence == sequence) {
            return true;
        }
    }
    return false;
}

void EspNowManager::recordMessage(const uint8_t* mac, uint8_t sequence) {
    RecentMessage recent;
    copyMac(recent.sender_mac, mac);
    recent.sequence = sequence;
    recent.received_time = millis();

    _recent_messages.push_back(recent);

    // Limit size
    while (_recent_messages.size() > ESPNOW_DEDUP_MAX_ENTRIES) {
        _recent_messages.pop_front();
    }
}

void EspNowManager::cleanupOldMessages() {
    uint32_t now = millis();
    while (!_recent_messages.empty() &&
           (now - _recent_messages.front().received_time) > ESPNOW_DEDUP_WINDOW_MS) {
        _recent_messages.pop_front();
    }
}

void EspNowManager::cleanupPendingAcks() {
    uint32_t now = millis();
    auto it = _pending_acks.begin();
    while (it != _pending_acks.end()) {
        if ((now - it->sent_time) > ESPNOW_DEFAULT_ACK_TIMEOUT_MS * 2) {
            it = _pending_acks.erase(it);
        } else {
            ++it;
        }
    }
}

EspNowManager::PendingAck* EspNowManager::findPendingAck(const uint8_t* mac, uint8_t sequence) {
    for (auto& ack : _pending_acks) {
        if (compareMac(ack.peer_mac, mac) && ack.sequence == sequence) {
            return &ack;
        }
    }
    return nullptr;
}

void EspNowManager::removePendingAck(const uint8_t* mac, uint8_t sequence) {
    auto it = _pending_acks.begin();
    while (it != _pending_acks.end()) {
        if (compareMac(it->peer_mac, mac) && it->sequence == sequence) {
            it = _pending_acks.erase(it);
        } else {
            ++it;
        }
    }
}

// ============ Static Callbacks ============

void EspNowManager::onDataSentStatic(const uint8_t* mac, esp_now_send_status_t status) {
    if (s_instance) {
        s_instance->onDataSent(mac, status);
    }
}

void EspNowManager::onDataRecvStatic(const uint8_t* mac, const uint8_t* data, int len) {
    if (s_instance && mac) {
        s_instance->onDataRecv(mac, data, len);
    }
}

// ============ Instance Callbacks ============

void EspNowManager::onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
    bool success = (status == ESP_NOW_SEND_SUCCESS);

    // Update pending ACKs send status
    for (auto& ack : _pending_acks) {
        if (compareMac(ack.peer_mac, mac)) {
            ack.send_success = success;
        }
    }

    // Call user callback
    if (_send_complete_callback) {
        _send_complete_callback(mac, success);
    }
}

void EspNowManager::onDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (len < (int)sizeof(MessageHeader)) {
        xSemaphoreGive(_mutex);
        return;
    }

    _stats.packets_received++;

    const MessageHeader* header = reinterpret_cast<const MessageHeader*>(data);

    // Check protocol version
    if (header->protocol_version != PROTOCOL_VERSION) {
        LOG_W(TAG, "Protocol version mismatch: %d vs %d",
                      header->protocol_version, PROTOCOL_VERSION);
        xSemaphoreGive(_mutex);
        return;
    }

    // Handle ACK messages specially
    if (header->type == MessageType::ACK) {
        processAck(mac, reinterpret_cast<const AckMsg*>(data));
        xSemaphoreGive(_mutex);
        return;
    }

    // Check for duplicate (skip ACKs as they're handled above)
    if (isDuplicate(mac, header->sequence_number)) {
        _stats.duplicates_filtered++;
        // Still send ACK for duplicates if requested
        if (header->flags & MsgFlags::REQUIRES_ACK) {
            sendAck(mac, header->sequence_number, header->type);
        }
        xSemaphoreGive(_mutex);
        return;
    }

    // Record message for deduplication
    recordMessage(mac, header->sequence_number);

    // Send ACK if requested
    if (header->flags & MsgFlags::REQUIRES_ACK) {
        sendAck(mac, header->sequence_number, header->type);
    }

    xSemaphoreGive(_mutex);

    // Call raw receive callback
    if (_receive_callback) {
        _receive_callback(mac, data, len);
    }

    // Call typed message callback
    if (_message_callback) {
        _message_callback(mac, header, data, len);
    }
}

void EspNowManager::processAck(const uint8_t* mac, const AckMsg* ack) {
    // Find matching pending ACK
    for (auto& pending : _pending_acks) {
        if (compareMac(pending.peer_mac, mac) && pending.sequence == ack->acked_sequence) {
            pending.received = true;
            return;
        }
    }
}

} // namespace iwmp
