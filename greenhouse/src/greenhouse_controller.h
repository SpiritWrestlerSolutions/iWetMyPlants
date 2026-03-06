/**
 * @file greenhouse_controller.h
 * @brief Greenhouse Manager device controller
 *
 * Environmental control with relay automation and sensor-to-relay bindings.
 */

#pragma once

#include <Arduino.h>
#include "relay_manager.h"
#include "automation_engine.h"
#include "espnow_manager.h"
#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "sensor_interface.h"
#include "dht_sensor.h"
#include "sht_sensor.h"
#include "message_types.h"
#include "improv_serial.h"

namespace iwmp {

enum class GreenhouseState {
    BOOT,
    LOAD_CONFIG,
    WIFI_CONNECT,
    MQTT_CONNECT,
    AP_MODE,
    OPERATIONAL
};

/**
 * @brief Greenhouse Manager device controller
 */
class GreenhouseController {
public:
    /**
     * @brief Initialize greenhouse controller
     */
    void begin();

    /**
     * @brief Main loop processing
     */
    void loop();

    /**
     * @brief Get current state
     * @return Greenhouse state
     */
    GreenhouseState getState() const { return _state; }

    /**
     * @brief Get relay manager
     * @return Reference to relay manager
     */
    RelayManager& getRelays() { return _relays; }

    /**
     * @brief Get automation engine
     * @return Reference to automation engine
     */
    AutomationEngine& getAutomation() { return _automation; }

    // ============ Sensor Access ============

    /**
     * @brief Get moisture sensor
     * @param index Sensor index
     * @return Pointer to sensor or nullptr
     */
    MoistureSensor* getMoistureSensor(uint8_t index);

    /**
     * @brief Get temperature reading
     * @return Temperature in Celsius (NAN if unavailable)
     */
    float getTemperature() const { return _last_temperature; }

    /**
     * @brief Get humidity reading
     * @return Relative humidity percentage (NAN if unavailable)
     */
    float getHumidity() const { return _last_humidity; }

    // ============ ESP-NOW Handlers ============

    /**
     * @brief Handle relay command from Hub
     * @param msg Relay command message
     */
    void onRelayCommand(const RelayCommandMsg& msg);

    /**
     * @brief Handle moisture reading (for automation)
     * @param msg Moisture reading message
     */
    void onMoistureReading(const MoistureReadingMsg& msg);

    // ============ Manual Control ============

    /**
     * @brief Set relay state manually
     * @param index Relay index
     * @param state Desired state
     * @param duration_sec Duration (0 = indefinite)
     * @return true if successful
     */
    bool setRelay(uint8_t index, bool state, uint32_t duration_sec = 0);

    /**
     * @brief Emergency stop all relays
     */
    void emergencyStop();

    /**
     * @brief Enable/disable automation
     * @param enabled Automation state
     */
    void setAutomationEnabled(bool enabled);

    /**
     * @brief Check if automation is enabled
     * @return true if enabled
     */
    bool isAutomationEnabled() const;

private:
    GreenhouseState _state = GreenhouseState::BOOT;
    RelayManager _relays;
    AutomationEngine _automation;

    // Sensors
    std::unique_ptr<MoistureSensor> _moisture_sensors[IWMP_MAX_SENSORS];
    uint8_t _moisture_sensor_count = 0;

    // Environmental sensor (either DHT or SHT)
    std::unique_ptr<DhtSensor> _dht_sensor;
    std::unique_ptr<ShtSensor> _sht_sensor;

    // Cached readings
    float _last_temperature = NAN;
    float _last_humidity = NAN;

    // Timing
    uint32_t _last_sensor_read_time = 0;
    uint32_t _last_publish_time = 0;
    uint32_t _state_enter_time = 0;

    // Improv WiFi Serial provisioning
    ImprovSerial _improv;
    bool _improvStarted = false;

    static constexpr uint32_t SENSOR_READ_INTERVAL_MS = 10000;
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
    static constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = 10000;

    // ============ State Machine ============

    void enterState(GreenhouseState new_state);
    void handleBootState();
    void handleLoadConfigState();
    void handleWifiConnectState();
    void handleMqttConnectState();
    void handleApModeState();
    void handleOperationalState();

    // ============ Internal Methods ============

    /**
     * @brief Initialize sensors from config
     */
    void initializeSensors();

    /**
     * @brief Initialize environmental sensor
     */
    void initializeEnvSensor();

    /**
     * @brief Read all sensors
     */
    void readSensors();

    /**
     * @brief Read environmental sensor
     */
    void readEnvSensor();

    /**
     * @brief Publish state to MQTT
     */
    void publishState();

    /**
     * @brief Setup web server routes
     */
    void setupWebRoutes();

    /**
     * @brief Handle ESP-NOW receive
     */
    void onEspNowReceive(const uint8_t* mac, const uint8_t* data, int len);

    /**
     * @brief Handle MQTT message
     */
    void onMqttMessage(const char* topic, const char* payload);

    /**
     * @brief Check if config button pressed
     * @return true if button held
     */
    bool isConfigButtonPressed();
};

// Global greenhouse controller instance
extern GreenhouseController Greenhouse;

} // namespace iwmp
