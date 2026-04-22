/**
 * @file greenhouse_controller.h
 * @brief Greenhouse Manager device controller
 *
 * Environmental monitor + relay executor. Receives commands from Hub/HA
 * and reports temperature/humidity. No local moisture sensing or
 * autonomous automation — greenhouse is a "pro add-on" that extends
 * the system rather than acting independently.
 */

#pragma once

#include <Arduino.h>
#include <memory>
#include "relay_manager.h"
#include "espnow_manager.h"
#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "web_server.h"
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
    void begin();
    void loop();

    GreenhouseState getState() const { return _state; }
    RelayManager& getRelays() { return _relays; }

    float getTemperature() const { return _last_temperature; }
    float getHumidity() const { return _last_humidity; }

    /**
     * @brief Handle relay command from Hub (ESP-NOW)
     */
    void onRelayCommand(const RelayCommandMsg& msg);

    /**
     * @brief Set relay state
     * @param index Relay index (matches config slot)
     * @param state Desired state
     * @param duration_sec Duration (0 = indefinite)
     */
    bool setRelay(uint8_t index, bool state, uint32_t duration_sec = 0);

    /**
     * @brief Emergency stop all relays
     */
    void emergencyStop();

private:
    GreenhouseState _state = GreenhouseState::BOOT;
    RelayManager _relays;

    // Environmental sensor (either DHT or SHT)
    std::unique_ptr<DhtSensor> _dht_sensor;
    std::unique_ptr<ShtSensor> _sht_sensor;

    float _last_temperature = NAN;
    float _last_humidity = NAN;

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

    // ============ Internal ============
    void initializeEnvSensor();
    void readEnvSensor();
    void publishState();
    void setupWebRoutes();
    void onEspNowReceive(const uint8_t* mac, const uint8_t* data, int len);
};

extern GreenhouseController Greenhouse;

} // namespace iwmp
