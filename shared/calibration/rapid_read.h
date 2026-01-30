/**
 * @file rapid_read.h
 * @brief Fast sampling for calibration via WebSocket
 *
 * Provides real-time sensor readings during calibration
 * using WebSocket for low-latency updates.
 */

#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <deque>
#include "sensor_interface.h"

namespace iwmp {

/**
 * @brief WebSocket-based rapid sensor reading server
 */
class RapidReadServer {
public:
    /**
     * @brief Initialize rapid read server
     * @param server Async web server instance
     * @param path WebSocket path (default "/ws/calibration")
     */
    void begin(AsyncWebServer* server, const char* path = "/ws/calibration");

    /**
     * @brief Set active sensor for rapid reading
     * @param sensor Sensor to read
     */
    void setSensor(MoistureSensor* sensor);

    /**
     * @brief Start rapid reading
     */
    void start();

    /**
     * @brief Stop rapid reading
     */
    void stop();

    /**
     * @brief Check if rapid reading is active
     * @return true if active
     */
    bool isActive() const { return _active; }

    /**
     * @brief Update rapid read (call frequently in loop)
     */
    void update();

    /**
     * @brief Set sample rate
     * @param samples_per_second Samples per second (1-50)
     */
    void setSampleRate(uint16_t samples_per_second);

    /**
     * @brief Set averaging window
     * @param samples Number of samples to average
     */
    void setAveragingWindow(uint8_t samples);

    /**
     * @brief Get current averaged reading
     * @return Averaged ADC value
     */
    uint16_t getCurrentAverage() const;

    /**
     * @brief Get current percentage
     * @return Moisture percentage
     */
    uint8_t getCurrentPercent() const;

private:
    AsyncWebSocket* _ws = nullptr;
    MoistureSensor* _sensor = nullptr;
    bool _active = false;

    uint16_t _sample_rate = 10;  // Samples per second
    uint8_t _averaging_window = 5;
    uint32_t _sample_interval_ms = 100;
    uint32_t _last_sample_time = 0;

    std::deque<uint16_t> _recent_values;

    /**
     * @brief Handle WebSocket events
     */
    void onWebSocketEvent(AsyncWebSocket* server,
                          AsyncWebSocketClient* client,
                          AwsEventType type,
                          void* arg,
                          uint8_t* data,
                          size_t len);

    /**
     * @brief Handle incoming WebSocket message
     * @param client Client that sent message
     * @param data Message data
     * @param len Message length
     */
    void handleMessage(AsyncWebSocketClient* client, uint8_t* data, size_t len);

    /**
     * @brief Broadcast reading to all connected clients
     * @param raw Raw ADC value
     * @param average Averaged value
     * @param percent Moisture percentage
     */
    void broadcastReading(uint16_t raw, uint16_t average, uint8_t percent);

    /**
     * @brief Calculate average from recent values
     * @return Averaged value
     */
    uint16_t calculateAverage() const;
};

// Global rapid read server
extern RapidReadServer RapidRead;

} // namespace iwmp
