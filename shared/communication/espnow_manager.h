/**
 * @file espnow_manager.h
 * @brief ESP-NOW communication manager for iWetMyPlants v2.0
 *
 * Provides reliable ESP-NOW communication with:
 * - Automatic retry on failure
 * - ACK-based confirmation
 * - Message deduplication
 * - Statistics tracking
 * - Peer management
 */

#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <functional>
#include <vector>
#include <deque>
#include "message_types.h"

namespace iwmp {

// Configuration constants
static constexpr uint8_t ESPNOW_MAX_PEERS = 20;
static constexpr uint8_t ESPNOW_DEFAULT_RETRIES = 3;
static constexpr uint32_t ESPNOW_DEFAULT_RETRY_DELAY_MS = 50;
static constexpr uint32_t ESPNOW_DEFAULT_ACK_TIMEOUT_MS = 100;
static constexpr uint32_t ESPNOW_DEDUP_WINDOW_MS = 5000;  // 5 seconds
static constexpr uint8_t ESPNOW_DEDUP_MAX_ENTRIES = 32;

// Broadcast MAC address
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/**
 * @brief Callback types
 */
using ReceiveCallback = std::function<void(const uint8_t* mac, const uint8_t* data, int len)>;
using SendCompleteCallback = std::function<void(const uint8_t* mac, bool success)>;
using MessageCallback = std::function<void(const uint8_t* mac, const MessageHeader* header, const uint8_t* payload, size_t len)>;

/**
 * @brief Statistics tracking
 */
struct EspNowStats {
    uint32_t packets_sent = 0;
    uint32_t packets_received = 0;
    uint32_t packets_lost = 0;
    uint32_t acks_received = 0;
    uint32_t acks_timeout = 0;
    uint32_t retries = 0;
    uint32_t duplicates_filtered = 0;

    float getDeliveryRate() const {
        if (packets_sent == 0) return 100.0f;
        return 100.0f * (packets_sent - packets_lost) / packets_sent;
    }

    void reset() {
        packets_sent = 0;
        packets_received = 0;
        packets_lost = 0;
        acks_received = 0;
        acks_timeout = 0;
        retries = 0;
        duplicates_filtered = 0;
    }
};

/**
 * @brief ESP-NOW communication manager
 */
class EspNowManager {
public:
    /**
     * @brief Get singleton instance
     */
    static EspNowManager& getInstance();

    // Delete copy/move
    EspNowManager(const EspNowManager&) = delete;
    EspNowManager& operator=(const EspNowManager&) = delete;

    /**
     * @brief Initialize ESP-NOW
     * @param channel WiFi channel (must match router if WiFi is used)
     * @return true if successful
     */
    bool begin(uint8_t channel = 1);

    /**
     * @brief Shutdown ESP-NOW
     */
    void end();

    /**
     * @brief Check if ESP-NOW is initialized
     */
    bool isInitialized() const { return _initialized; }

    /**
     * @brief Get current WiFi channel
     */
    uint8_t getChannel() const { return _channel; }

    /**
     * @brief Get own MAC address
     */
    const uint8_t* getMac() const { return _own_mac; }

    /**
     * @brief Get own MAC address as string
     */
    void getMacString(char* buffer, size_t size) const;

    // ============ Send Methods ============

    /**
     * @brief Send raw data to a peer
     * @param peer_mac Destination MAC address
     * @param data Data to send
     * @param len Data length
     * @return true if send initiated (not guaranteed delivery)
     */
    bool send(const uint8_t* peer_mac, const uint8_t* data, size_t len);

    /**
     * @brief Send with acknowledgment and wait for ACK
     * @param peer_mac Destination MAC address
     * @param data Data to send
     * @param len Data length
     * @param timeout_ms ACK timeout in milliseconds
     * @return true if ACK received within timeout
     */
    bool sendWithAck(const uint8_t* peer_mac, const uint8_t* data, size_t len,
                     uint32_t timeout_ms = ESPNOW_DEFAULT_ACK_TIMEOUT_MS);

    /**
     * @brief Send with automatic retry on failure
     * @param peer_mac Destination MAC address
     * @param data Data to send
     * @param len Data length
     * @param max_retries Maximum retry attempts
     * @param retry_delay_ms Delay between retries
     * @return true if successfully sent (with ACK if required)
     */
    bool sendWithRetry(const uint8_t* peer_mac, const uint8_t* data, size_t len,
                       uint8_t max_retries = ESPNOW_DEFAULT_RETRIES,
                       uint32_t retry_delay_ms = ESPNOW_DEFAULT_RETRY_DELAY_MS);

    /**
     * @brief Broadcast data to all peers
     * @param data Data to send
     * @param len Data length
     * @return true if broadcast initiated
     */
    bool broadcast(const uint8_t* data, size_t len);

    // ============ Message Building Helpers ============

    /**
     * @brief Send a moisture reading
     */
    bool sendMoistureReading(const uint8_t* peer_mac, uint8_t sensor_index,
                             uint16_t raw_value, uint8_t moisture_percent);

    /**
     * @brief Send environmental reading
     */
    bool sendEnvironmentalReading(const uint8_t* peer_mac,
                                   int16_t temp_c_x10, uint16_t humidity_x10);

    /**
     * @brief Send battery status
     */
    bool sendBatteryStatus(const uint8_t* peer_mac, uint16_t voltage_mv,
                           uint8_t percent, bool charging);

    /**
     * @brief Send relay command
     */
    bool sendRelayCommand(const uint8_t* peer_mac, uint8_t relay_index,
                          bool state, uint32_t duration_sec = 0);

    /**
     * @brief Send announcement
     */
    bool sendAnnounce(uint8_t device_type, const char* name,
                      const char* version, uint8_t capabilities);

    /**
     * @brief Send pair request
     */
    bool sendPairRequest(const uint8_t* hub_mac, uint8_t device_type,
                         const char* name, uint8_t capabilities);

    /**
     * @brief Send pair response
     */
    bool sendPairResponse(const uint8_t* peer_mac, bool accepted,
                          uint8_t channel, uint16_t interval);

    /**
     * @brief Send heartbeat
     */
    bool sendHeartbeat(const uint8_t* peer_mac, uint32_t uptime, uint8_t status);

    /**
     * @brief Send ACK for received message
     */
    bool sendAck(const uint8_t* peer_mac, uint8_t sequence, MessageType type);

    /**
     * @brief Send NACK for received message
     */
    bool sendNack(const uint8_t* peer_mac, uint8_t sequence, MessageType type,
                  NackReason reason, const char* message = nullptr);

    // ============ Peer Management ============

    /**
     * @brief Add a peer
     * @param mac Peer MAC address
     * @param channel WiFi channel (0 = current)
     * @param encrypt Enable encryption
     * @param lmk Local Master Key (16 bytes, required if encrypt=true)
     * @return true if successful
     */
    bool addPeer(const uint8_t* mac, uint8_t channel = 0, bool encrypt = false,
                 const uint8_t* lmk = nullptr);

    /**
     * @brief Remove a peer
     */
    bool removePeer(const uint8_t* mac);

    /**
     * @brief Check if peer exists
     */
    bool peerExists(const uint8_t* mac);

    /**
     * @brief Get number of registered peers
     */
    uint8_t getPeerCount() const;

    /**
     * @brief Clear all peers
     */
    void clearAllPeers();

    // ============ Callbacks ============

    /**
     * @brief Set raw receive callback
     */
    void onReceive(ReceiveCallback callback);

    /**
     * @brief Set send complete callback
     */
    void onSendComplete(SendCompleteCallback callback);

    /**
     * @brief Set typed message callback (parsed header)
     */
    void onMessage(MessageCallback callback);

    // ============ Statistics ============

    /**
     * @brief Get statistics
     */
    const EspNowStats& getStats() const { return _stats; }

    /**
     * @brief Reset statistics
     */
    void resetStats() { _stats.reset(); }

    /**
     * @brief Get packets sent count
     */
    uint32_t getPacketsSent() const { return _stats.packets_sent; }

    /**
     * @brief Get packets received count
     */
    uint32_t getPacketsReceived() const { return _stats.packets_received; }

    /**
     * @brief Get packets lost count
     */
    uint32_t getPacketsLost() const { return _stats.packets_lost; }

    /**
     * @brief Get delivery rate percentage
     */
    float getDeliveryRate() const { return _stats.getDeliveryRate(); }

    // ============ Encryption ============

    /**
     * @brief Set Primary Master Key (PMK) for encryption
     * @param pmk 16-byte key
     * @return true if successful
     */
    bool setPMK(const uint8_t* pmk);

    // ============ Utility ============

    /**
     * @brief Process pending operations (call in loop)
     */
    void update();

    /**
     * @brief Get next sequence number
     */
    uint8_t getNextSequence();

    /**
     * @brief Print debug info to Serial
     */
    void printDebugInfo() const;

private:
    EspNowManager() = default;
    ~EspNowManager() = default;

    bool _initialized = false;
    uint8_t _channel = 1;
    uint8_t _own_mac[6] = {0};
    uint8_t _sequence_number = 0;
    EspNowStats _stats;

    // Callbacks
    ReceiveCallback _receive_callback = nullptr;
    SendCompleteCallback _send_complete_callback = nullptr;
    MessageCallback _message_callback = nullptr;

    // Pending ACK tracking
    struct PendingAck {
        uint8_t peer_mac[6];
        uint8_t sequence;
        uint32_t sent_time;
        volatile bool received;
        volatile bool send_success;
    };
    std::vector<PendingAck> _pending_acks;
    static constexpr size_t MAX_PENDING_ACKS = 8;

    // Message deduplication
    struct RecentMessage {
        uint8_t sender_mac[6];
        uint8_t sequence;
        uint32_t received_time;
    };
    std::deque<RecentMessage> _recent_messages;

    // Internal methods
    bool isDuplicate(const uint8_t* mac, uint8_t sequence);
    void recordMessage(const uint8_t* mac, uint8_t sequence);
    void cleanupOldMessages();
    void cleanupPendingAcks();

    PendingAck* findPendingAck(const uint8_t* mac, uint8_t sequence);
    void removePendingAck(const uint8_t* mac, uint8_t sequence);

    // Static callbacks for ESP-NOW
    static void onDataSentStatic(const uint8_t* mac, esp_now_send_status_t status);
    static void onDataRecvStatic(const uint8_t* mac_addr, const uint8_t* data, int len);

    // Instance callbacks
    void onDataSent(const uint8_t* mac, esp_now_send_status_t status);
    void onDataRecv(const uint8_t* mac, const uint8_t* data, int len);

    // Process received ACK
    void processAck(const uint8_t* mac, const AckMsg* ack);
};

// Global ESP-NOW manager accessor
extern EspNowManager& EspNow;

} // namespace iwmp
