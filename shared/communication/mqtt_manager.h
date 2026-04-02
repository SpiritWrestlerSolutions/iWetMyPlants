/**
 * @file mqtt_manager.h
 * @brief MQTT communication with Home Assistant auto-discovery
 *
 * Provides MQTT connectivity with:
 * - Automatic reconnection handling
 * - Home Assistant MQTT auto-discovery
 * - JSON state publishing
 * - LWT (Last Will and Testament) for availability
 * - Command subscription for relays
 */

#pragma once

#include <Arduino.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include "config_schema.h"

namespace iwmp {

// MQTT configuration constants
static constexpr uint16_t MQTT_DEFAULT_PORT = 1883;
static constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
static constexpr uint8_t MQTT_MAX_RECONNECT_ATTEMPTS = 10;
static constexpr uint8_t MQTT_DEFAULT_QOS = 1;
static constexpr size_t MQTT_JSON_BUFFER_SIZE = 1024;

// Home Assistant discovery constants
static constexpr const char* HA_MANUFACTURER = "Spirit Wrestler Woodcraft";
static constexpr const char* HA_MODEL_HUB = "iWetMyPlants Hub";
static constexpr const char* HA_MODEL_REMOTE = "iWetMyPlants Remote";
static constexpr const char* HA_MODEL_GREENHOUSE = "iWetMyPlants Greenhouse";

/**
 * @brief Sensor readings structure for state publishing
 */
struct SensorReadings {
    // Moisture sensors
    struct MoistureReading {
        bool valid = false;
        uint8_t index = 0;
        uint16_t raw_value = 0;
        uint8_t percent = 0;
    };
    MoistureReading moisture[IWMP_MAX_SENSORS];
    uint8_t moisture_count = 0;

    // Environmental
    bool has_environmental = false;
    float temperature_c = 0.0f;
    float humidity_percent = 0.0f;

    // Battery (for remotes)
    bool has_battery = false;
    uint16_t battery_mv = 0;
    uint8_t battery_percent = 0;
    bool battery_charging = false;

    // Relay states (for greenhouse)
    struct RelayState {
        bool valid = false;
        uint8_t index = 0;
        bool state = false;
    };
    RelayState relays[4];
    uint8_t relay_count = 0;

    // System info
    uint32_t uptime_sec = 0;
    int8_t rssi = 0;
    uint32_t free_heap = 0;
};

/**
 * @brief Callback types
 */
using MqttConnectCallback = std::function<void(bool session_present)>;
using MqttDisconnectCallback = std::function<void(AsyncMqttClientDisconnectReason reason)>;
using MqttMessageCallback = std::function<void(const char* topic, const char* payload, size_t len)>;
using RelayCommandCallback = std::function<void(uint8_t relay_index, bool state, uint32_t duration)>;

/**
 * @brief MQTT Manager for Home Assistant integration
 */
class MqttManager {
public:
    /**
     * @brief Get singleton instance
     */
    static MqttManager& getInstance();

    // Delete copy/move
    MqttManager(const MqttManager&) = delete;
    MqttManager& operator=(const MqttManager&) = delete;

    /**
     * @brief Initialize MQTT manager
     * @param config MQTT configuration
     * @param identity Device identity for topics
     * @return true if successful
     */
    bool begin(const MqttConfig& config, const DeviceIdentity& identity);

    /**
     * @brief Shutdown MQTT
     */
    void end();

    /**
     * @brief Check if initialized
     */
    bool isInitialized() const { return _initialized; }

    /**
     * @brief Update configuration (reconnects if needed)
     */
    void updateConfig(const MqttConfig& config);

    // ============ Connection ============

    /**
     * @brief Connect to MQTT broker
     */
    void connect();

    /**
     * @brief Disconnect from MQTT broker
     */
    void disconnect();

    /**
     * @brief Check if connected
     */
    bool isConnected() const;

    /**
     * @brief Process MQTT events (call in loop)
     */
    void loop();

    // ============ Home Assistant Discovery ============

    /**
     * @brief Publish all HA discovery configs
     */
    void publishDiscovery();

    /**
     * @brief Remove all HA discovery configs (for clean unpair)
     */
    void removeDiscovery();

    /**
     * @brief Publish discovery for moisture sensor
     */
    void publishMoistureDiscovery(uint8_t sensor_index, const char* sensor_name);

    /**
     * @brief Publish discovery for temperature sensor
     */
    void publishTemperatureDiscovery();

    /**
     * @brief Publish discovery for humidity sensor
     */
    void publishHumidityDiscovery();

    /**
     * @brief Publish discovery for relay switch
     */
    void publishRelayDiscovery(uint8_t relay_index, const char* relay_name);

    /**
     * @brief Publish discovery for battery sensor
     */
    void publishBatteryDiscovery();

    /**
     * @brief Publish discovery for signal strength sensor
     */
    void publishRssiDiscovery();

    // ============ State Publishing ============

    /**
     * @brief Publish current sensor state
     */
    void publishState(const SensorReadings& readings);

    /**
     * @brief Publish availability status
     * @param online true = online, false = offline
     */
    void publishAvailability(bool online);

    /**
     * @brief Publish single moisture reading
     */
    void publishMoistureReading(uint8_t sensor_index, uint16_t raw_value, uint8_t percent);

    /**
     * @brief Publish environmental reading
     */
    void publishEnvironmentalReading(float temperature_c, float humidity_percent);

    /**
     * @brief Publish battery status
     */
    void publishBatteryStatus(uint16_t voltage_mv, uint8_t percent, bool charging);

    /**
     * @brief Publish relay state
     */
    void publishRelayState(uint8_t relay_index, bool state);

    // ============ Raw Publish/Subscribe ============

    /**
     * @brief Publish to a topic
     */
    bool publish(const char* topic, const char* payload, bool retain = false, uint8_t qos = MQTT_DEFAULT_QOS);

    /**
     * @brief Subscribe to a topic
     */
    bool subscribe(const char* topic, uint8_t qos = MQTT_DEFAULT_QOS);

    /**
     * @brief Unsubscribe from a topic
     */
    bool unsubscribe(const char* topic);

    // ============ Callbacks ============

    /**
     * @brief Set connection callback
     */
    void onConnect(MqttConnectCallback callback);

    /**
     * @brief Set disconnect callback
     */
    void onDisconnect(MqttDisconnectCallback callback);

    /**
     * @brief Set message callback
     */
    void onMessage(MqttMessageCallback callback);

    /**
     * @brief Set relay command callback
     */
    void onRelayCommand(RelayCommandCallback callback);

    // ============ Topic Builders ============

    /**
     * @brief Get state topic
     */
    String getStateTopic() const;

    /**
     * @brief Get availability topic
     */
    String getAvailabilityTopic() const;

    /**
     * @brief Get command topic for entity
     */
    String getCommandTopic(const char* entity) const;

    /**
     * @brief Get discovery topic for entity
     */
    String getDiscoveryTopic(const char* component, const char* entity) const;

    // ============ Debug ============

    /**
     * @brief Print debug info
     */
    void printDebugInfo() const;

private:
    MqttManager() = default;
    ~MqttManager() = default;

    bool _initialized = false;
    AsyncMqttClient _client;
    MqttConfig _config;
    DeviceIdentity _identity;

    // Connection state
    bool _connecting = false;
    uint32_t _last_reconnect_attempt = 0;
    uint8_t _reconnect_attempts = 0;

    // Callbacks
    MqttConnectCallback _connect_callback = nullptr;
    MqttDisconnectCallback _disconnect_callback = nullptr;
    MqttMessageCallback _message_callback = nullptr;
    RelayCommandCallback _relay_callback = nullptr;

    // Cached topics
    String _state_topic;
    String _availability_topic;
    String _command_base_topic;

    // Internal callbacks
    void onMqttConnect(bool session_present);
    void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
    void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties,
                       size_t len, size_t index, size_t total);

    // Discovery payload builders
    String buildDeviceJson() const;
    String buildMoistureDiscoveryPayload(uint8_t sensor_index, const char* sensor_name) const;
    String buildTemperatureDiscoveryPayload() const;
    String buildHumidityDiscoveryPayload() const;
    String buildRelayDiscoveryPayload(uint8_t relay_index, const char* relay_name) const;
    String buildBatteryDiscoveryPayload() const;
    String buildBatteryVoltageDiscoveryPayload() const;
    String buildRssiDiscoveryPayload() const;

    // Helpers
    void buildTopics();
    void setupLWT();
    void subscribeToCommands();
    void handleRelayCommand(const char* payload, size_t len);
    const char* getModelName() const;
    String getUniqueId(const char* entity) const;
};

// Global MQTT manager accessor
extern MqttManager& Mqtt;

} // namespace iwmp
