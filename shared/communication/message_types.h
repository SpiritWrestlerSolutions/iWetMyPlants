/**
 * @file message_types.h
 * @brief ESP-NOW message protocol definitions for iWetMyPlants v1.0
 *
 * Defines all message types exchanged between Hub, Remote, and Greenhouse devices.
 * All structures are packed to ensure consistent wire format across devices.
 */

#pragma once

#include <Arduino.h>

namespace iwmp {

// Protocol version for compatibility checking
static constexpr uint8_t PROTOCOL_VERSION = 1;

// Maximum ESP-NOW payload size
static constexpr size_t ESPNOW_MAX_PAYLOAD = 250;

// Message header flags
namespace MsgFlags {
    static constexpr uint8_t REQUIRES_ACK = 0x01;   // Sender expects ACK
    static constexpr uint8_t IS_RETRY = 0x02;       // This is a retry
}

// Device capability flags (for AnnounceMsg)
namespace Capabilities {
    static constexpr uint8_t MOISTURE_SENSOR = 0x01;
    static constexpr uint8_t ENV_SENSOR = 0x02;
    static constexpr uint8_t RELAY_CONTROL = 0x04;
    static constexpr uint8_t BATTERY_POWERED = 0x08;
    static constexpr uint8_t WIFI_ENABLED = 0x10;
    static constexpr uint8_t MQTT_ENABLED = 0x20;
}

/**
 * @brief Message type identifiers
 */
enum class MessageType : uint8_t {
    // ============ Sensor Data (0x01-0x0F) ============
    MOISTURE_READING = 0x01,        // Moisture sensor reading
    ENVIRONMENTAL_READING = 0x02,   // Temperature/humidity reading
    BATTERY_STATUS = 0x03,          // Battery level report
    MULTI_SENSOR_READING = 0x04,    // Multiple sensors in one message

    // ============ Control Commands (0x10-0x1F) ============
    RELAY_COMMAND = 0x10,           // Turn relay on/off
    CALIBRATION_COMMAND = 0x11,     // Start calibration
    CONFIG_COMMAND = 0x12,          // Configuration update
    WAKE_COMMAND = 0x13,            // Wake sleeping device
    REBOOT_COMMAND = 0x14,          // Reboot device
    OTA_COMMAND = 0x15,             // OTA update notification

    // ============ Discovery & Pairing (0x20-0x2F) ============
    ANNOUNCE = 0x20,                // Device announcement/discovery
    PAIR_REQUEST = 0x21,            // Request to pair with hub
    PAIR_RESPONSE = 0x22,           // Hub response to pair request
    HEARTBEAT = 0x23,               // Keep-alive heartbeat
    UNPAIR = 0x24,                  // Remove pairing

    // ============ Acknowledgments (0xF0-0xFF) ============
    ACK = 0xF0,                     // Positive acknowledgment
    NACK = 0xF1                     // Negative acknowledgment (with reason)
};

// ============================================================================
// BASE MESSAGE HEADER
// ============================================================================

/**
 * @brief Common header for all ESP-NOW messages
 */
struct MessageHeader {
    uint8_t protocol_version;       // Protocol version (currently 1)
    MessageType type;               // Message type identifier
    uint8_t sender_mac[6];          // Sender's MAC address
    uint8_t sequence_number;        // For deduplication and ACK matching
    uint8_t flags;                  // Message flags (MsgFlags::*)
    uint32_t timestamp;             // Unix timestamp or millis()
} __attribute__((packed));

static_assert(sizeof(MessageHeader) == 14, "MessageHeader size mismatch");

// ============================================================================
// SENSOR DATA MESSAGES
// ============================================================================

/**
 * @brief Moisture sensor reading message
 */
struct MoistureReadingMsg {
    MessageHeader header;
    uint8_t sensor_index;           // Sensor index (0-7)
    uint16_t raw_value;             // Raw ADC value
    uint8_t moisture_percent;       // Calibrated percentage (0-100)
    int8_t rssi;                    // WiFi/ESP-NOW signal strength
} __attribute__((packed));

static_assert(sizeof(MoistureReadingMsg) <= ESPNOW_MAX_PAYLOAD, "MoistureReadingMsg too large");

/**
 * @brief Environmental (temperature/humidity) reading message
 */
struct EnvironmentalReadingMsg {
    MessageHeader header;
    int16_t temperature_c_x10;      // Temperature * 10 (e.g., 235 = 23.5°C)
    uint16_t humidity_percent_x10;  // Humidity * 10 (e.g., 655 = 65.5%)
} __attribute__((packed));

static_assert(sizeof(EnvironmentalReadingMsg) <= ESPNOW_MAX_PAYLOAD, "EnvironmentalReadingMsg too large");

/**
 * @brief Battery status message
 */
struct BatteryStatusMsg {
    MessageHeader header;
    uint16_t voltage_mv;            // Battery voltage in millivolts
    uint8_t percent;                // Estimated percentage (0-100)
    uint8_t charging : 1;           // Is USB power connected
    uint8_t low_battery : 1;        // Low battery warning
    uint8_t reserved : 6;           // Reserved for future use
} __attribute__((packed));

static_assert(sizeof(BatteryStatusMsg) <= ESPNOW_MAX_PAYLOAD, "BatteryStatusMsg too large");

// ============================================================================
// CONTROL MESSAGES
// ============================================================================

/**
 * @brief Relay control command
 */
struct RelayCommandMsg {
    MessageHeader header;
    uint8_t relay_index;            // Relay index (0-3)
    uint8_t state : 1;              // true = ON, false = OFF
    uint8_t override_safety : 1;    // Override safety limits (use with caution)
    uint8_t reserved : 6;
    uint32_t duration_sec;          // Auto-off duration (0 = indefinite)
} __attribute__((packed));

static_assert(sizeof(RelayCommandMsg) <= ESPNOW_MAX_PAYLOAD, "RelayCommandMsg too large");

/**
 * @brief Calibration command
 */
struct CalibrationCommandMsg {
    MessageHeader header;
    uint8_t sensor_index;           // Sensor to calibrate
    uint8_t calibration_point;      // 0 = dry, 1 = wet, 2 = cancel
    uint16_t manual_value;          // Optional: manually specified value (0 = use current reading)
} __attribute__((packed));

static_assert(sizeof(CalibrationCommandMsg) <= ESPNOW_MAX_PAYLOAD, "CalibrationCommandMsg too large");

/**
 * @brief Configuration update command
 */
struct ConfigCommandMsg {
    MessageHeader header;
    uint8_t config_section;         // Which config section to update
    uint8_t payload_length;         // Length of config data
    uint8_t payload[200];           // Config data (JSON or binary)
} __attribute__((packed));

// ============================================================================
// DISCOVERY & PAIRING MESSAGES
// ============================================================================

/**
 * @brief Device announcement message
 */
struct AnnounceMsg {
    MessageHeader header;
    uint8_t device_type;            // DeviceType enum value
    char device_name[32];           // User-friendly name
    char firmware_version[16];      // Firmware version string
    uint8_t capabilities;           // Capabilities flags
    uint8_t sensor_count;           // Number of moisture sensors
    uint8_t relay_count;            // Number of relays
    int8_t rssi;                    // Current signal strength
} __attribute__((packed));

static_assert(sizeof(AnnounceMsg) <= ESPNOW_MAX_PAYLOAD, "AnnounceMsg too large");

/**
 * @brief Pair request message (sent by Remote/Greenhouse to Hub)
 */
struct PairRequestMsg {
    MessageHeader header;
    uint8_t device_type;            // DeviceType enum value
    char device_name[32];           // User-friendly name
    char firmware_version[16];      // Firmware version string
    uint8_t capabilities;           // Capabilities flags
} __attribute__((packed));

static_assert(sizeof(PairRequestMsg) <= ESPNOW_MAX_PAYLOAD, "PairRequestMsg too large");

/**
 * @brief Pair response message (sent by Hub to requester)
 */
struct PairResponseMsg {
    MessageHeader header;
    uint8_t accepted : 1;           // true = pairing accepted
    uint8_t reserved : 7;
    uint8_t assigned_channel;       // WiFi channel to use
    uint16_t reporting_interval_sec;// Suggested reporting interval
    uint8_t hub_mac[6];             // Hub's MAC for future messages
} __attribute__((packed));

static_assert(sizeof(PairResponseMsg) <= ESPNOW_MAX_PAYLOAD, "PairResponseMsg too large");

/**
 * @brief Heartbeat message
 */
struct HeartbeatMsg {
    MessageHeader header;
    uint32_t uptime_sec;            // Device uptime in seconds
    uint8_t status;                 // Device status flags
    int8_t rssi;                    // Signal strength
    uint16_t free_heap;             // Free heap memory (KB)
} __attribute__((packed));

static_assert(sizeof(HeartbeatMsg) <= ESPNOW_MAX_PAYLOAD, "HeartbeatMsg too large");

// ============================================================================
// ACKNOWLEDGMENT MESSAGES
// ============================================================================

/**
 * @brief Acknowledgment message
 */
struct AckMsg {
    MessageHeader header;
    uint8_t acked_sequence;         // Sequence number being acknowledged
    MessageType acked_type;         // Type of message being acknowledged
} __attribute__((packed));

static_assert(sizeof(AckMsg) <= ESPNOW_MAX_PAYLOAD, "AckMsg too large");

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Check if message type requires acknowledgment by default
 */
inline bool messageRequiresAck(MessageType type) {
    switch (type) {
        case MessageType::RELAY_COMMAND:
        case MessageType::CALIBRATION_COMMAND:
        case MessageType::CONFIG_COMMAND:
        case MessageType::PAIR_REQUEST:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Initialize message header with common fields
 */
inline void initMessageHeader(MessageHeader& header, MessageType type,
                               const uint8_t* sender_mac, uint8_t sequence) {
    header.protocol_version = PROTOCOL_VERSION;
    header.type = type;
    memcpy(header.sender_mac, sender_mac, 6);
    header.sequence_number = sequence;
    header.flags = messageRequiresAck(type) ? MsgFlags::REQUIRES_ACK : 0;
    header.timestamp = millis();
}

/**
 * @brief Format MAC address as string
 */
inline void formatMac(const uint8_t* mac, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief Compare two MAC addresses
 */
inline bool compareMac(const uint8_t* mac1, const uint8_t* mac2) {
    return memcmp(mac1, mac2, 6) == 0;
}

/**
 * @brief Copy MAC address
 */
inline void copyMac(uint8_t* dest, const uint8_t* src) {
    memcpy(dest, src, 6);
}

} // namespace iwmp
