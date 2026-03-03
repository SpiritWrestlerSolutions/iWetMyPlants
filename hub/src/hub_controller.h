/**
 * @file hub_controller.h
 * @brief Hub device main controller
 *
 * Central coordinator that receives ESP-NOW data from Remotes,
 * bridges to MQTT/Home Assistant, and manages paired devices.
 */

#pragma once

#include <Arduino.h>
#include <memory>
#include "device_registry.h"
#include "espnow_manager.h"
#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "sensor_interface.h"
#include "message_types.h"
#include "improv_serial.h"

namespace iwmp {

enum class HubState {
    BOOT,
    LOAD_CONFIG,
    WIFI_CONNECT,
    MQTT_CONNECT,
    AP_MODE,
    OPERATIONAL
};

/**
 * @brief Hub device controller
 */
class HubController {
public:
    /**
     * @brief Initialize hub
     */
    void begin();

    /**
     * @brief Main loop processing
     */
    void loop();

    /**
     * @brief Get current state
     * @return Hub state
     */
    HubState getState() const { return _state; }

    /**
     * @brief Get device registry
     * @return Reference to device registry
     */
    DeviceRegistry& getRegistry() { return _registry; }

    // ============ Device Management ============

    /**
     * @brief Handle device announce message
     * @param msg Announce message
     */
    void onDeviceAnnounce(const AnnounceMsg& msg);

    /**
     * @brief Handle pair request
     * @param msg Pair request message
     */
    void onPairRequest(const PairRequestMsg& msg);

    /**
     * @brief Get connected device count
     * @return Number of online devices
     */
    uint8_t getConnectedDeviceCount() const;

    // ============ Data Handling ============

    /**
     * @brief Handle moisture reading from remote
     * @param msg Moisture reading message
     */
    void onMoistureReading(const MoistureReadingMsg& msg);

    /**
     * @brief Handle environmental reading
     * @param msg Environmental reading message
     */
    void onEnvironmentalReading(const EnvironmentalReadingMsg& msg);

    /**
     * @brief Handle battery status
     * @param msg Battery status message
     */
    void onBatteryStatus(const BatteryStatusMsg& msg);

    // ============ Command Forwarding ============

    /**
     * @brief Send relay command to greenhouse
     * @param target_mac Target device MAC
     * @param relay Relay index
     * @param state Desired state
     * @param duration Duration in seconds (0 = indefinite)
     */
    void sendRelayCommand(const uint8_t* target_mac, uint8_t relay,
                          bool state, uint32_t duration);

    /**
     * @brief Send calibration command
     * @param target_mac Target device MAC
     * @param sensor Sensor index
     * @param point Calibration point (0=dry, 1=wet)
     */
    void sendCalibrationCommand(const uint8_t* target_mac,
                                uint8_t sensor, uint8_t point);

    /**
     * @brief Send wake command to sleeping remote
     * @param target_mac Target device MAC
     */
    void sendWakeCommand(const uint8_t* target_mac);

private:
    HubState _state = HubState::BOOT;
    DeviceRegistry _registry;

    // Local sensors (optional hub-attached sensors)
    std::unique_ptr<MoistureSensor> _local_sensors[IWMP_MAX_SENSORS];
    uint8_t _local_sensor_count = 0;

    // Timing
    uint32_t _last_publish_time = 0;
    uint32_t _last_device_check_time = 0;
    uint32_t _state_enter_time = 0;

    // Improv WiFi Serial provisioning
    ImprovSerial _improv;
    bool _improvStarted = false;

    static constexpr uint32_t DEVICE_CHECK_INTERVAL_MS = 10000;
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
    static constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = 10000;

    // ============ State Machine ============

    void enterState(HubState new_state);
    void handleBootState();
    void handleLoadConfigState();
    void handleWifiConnectState();
    void handleMqttConnectState();
    void handleApModeState();
    void handleOperationalState();

    // ============ Internal Methods ============

    /**
     * @brief Initialize local sensors attached to hub
     */
    void initializeLocalSensors();

    /**
     * @brief Process local sensors
     */
    void processLocalSensors();

    /**
     * @brief Select sensor for calibration
     * @param index Sensor index
     */
    void selectSensorForCalibration(uint8_t index);

    /**
     * @brief Check device timeouts
     */
    void checkDeviceTimeouts();

    /**
     * @brief Publish aggregated state to MQTT
     */
    void publishAggregatedState();

    /**
     * @brief Handle ESP-NOW receive callback
     */
    void onEspNowReceive(const uint8_t* mac, const uint8_t* data, int len);

    /**
     * @brief Handle MQTT message callback
     */
    void onMqttMessage(const char* topic, const char* payload);

    /**
     * @brief Setup web server routes
     */
    void setupWebRoutes();

    /**
     * @brief Check if config button is pressed
     * @return true if button held
     */
    bool isConfigButtonPressed();
};

// Global hub controller instance
extern HubController Hub;

} // namespace iwmp
