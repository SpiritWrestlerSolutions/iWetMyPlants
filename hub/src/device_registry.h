/**
 * @file device_registry.h
 * @brief Paired device tracking for Hub
 *
 * Manages registered devices, their status, and persistence.
 */

#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>

namespace iwmp {

/**
 * @brief Registered device information
 */
struct RegisteredDevice {
    uint8_t mac[6];                 // Device MAC address
    uint8_t device_type;            // 0=Hub, 1=Remote, 2=Greenhouse
    char device_name[32];           // User-friendly name
    char firmware_version[16];      // Firmware version
    uint32_t last_seen;             // Unix timestamp
    int8_t last_rssi;               // Last signal strength
    bool paired;                    // Is device paired
    bool online;                    // Based on heartbeat timeout

    // Last known readings (for display/caching)
    uint8_t last_moisture_percent;
    float last_temperature;
    float last_humidity;
    uint8_t last_battery_percent;
};

/**
 * @brief Device registry for Hub
 */
class DeviceRegistry {
public:
    using DeviceCallback = std::function<void(RegisteredDevice&)>;

    /**
     * @brief Initialize registry
     */
    void begin();

    /**
     * @brief Add or update device
     * @param mac Device MAC address
     * @param type Device type
     * @param name Device name
     * @return true if added/updated
     */
    bool addDevice(const uint8_t* mac, uint8_t type, const char* name);

    /**
     * @brief Remove device
     * @param mac Device MAC address
     * @return true if removed
     */
    bool removeDevice(const uint8_t* mac);

    /**
     * @brief Get device by MAC
     * @param mac Device MAC address
     * @return Pointer to device or nullptr
     */
    RegisteredDevice* getDevice(const uint8_t* mac);

    /**
     * @brief Update last seen timestamp
     * @param mac Device MAC address
     * @param rssi Signal strength
     */
    void updateLastSeen(const uint8_t* mac, int8_t rssi);

    /**
     * @brief Update device readings
     * @param mac Device MAC address
     * @param moisture Moisture percentage
     * @param temp Temperature (NAN if not available)
     * @param humidity Humidity (NAN if not available)
     * @param battery Battery percentage (255 if not available)
     */
    void updateReadings(const uint8_t* mac, uint8_t moisture,
                        float temp = NAN, float humidity = NAN,
                        uint8_t battery = 255);

    /**
     * @brief Iterate all devices
     * @param callback Function to call for each device
     */
    void forEachDevice(DeviceCallback callback);

    /**
     * @brief Get device count
     * @return Total registered devices
     */
    size_t getDeviceCount() const { return _devices.size(); }

    /**
     * @brief Get online device count
     * @return Number of online devices
     */
    size_t getOnlineDeviceCount() const;

    /**
     * @brief Check device timeouts and update online status
     */
    void checkTimeouts();

    /**
     * @brief Set offline timeout
     * @param timeout_sec Timeout in seconds
     */
    void setOfflineTimeout(uint32_t timeout_sec) { _offline_timeout_sec = timeout_sec; }

    // ============ Persistence ============

    /**
     * @brief Save registry to NVS
     * @return true if saved
     */
    bool saveToNVS();

    /**
     * @brief Load registry from NVS
     * @return true if loaded
     */
    bool loadFromNVS();

    /**
     * @brief Clear all devices
     */
    void clear();

    // ============ Utility ============

    /**
     * @brief Convert MAC to string
     * @param mac MAC address
     * @return MAC string (format: AA:BB:CC:DD:EE:FF)
     */
    static String macToString(const uint8_t* mac);

    /**
     * @brief Parse string to MAC
     * @param str MAC string
     * @param mac Output buffer
     * @return true if parsed
     */
    static bool stringToMac(const char* str, uint8_t* mac);

private:
    std::vector<RegisteredDevice> _devices;
    uint32_t _offline_timeout_sec = 300;  // 5 minutes default

    static constexpr size_t MAX_DEVICES = 16;
    static constexpr const char* NVS_NAMESPACE = "iwmp_registry";

    /**
     * @brief Find device index
     * @param mac Device MAC address
     * @return Index or -1 if not found
     */
    int findDeviceIndex(const uint8_t* mac);
};

} // namespace iwmp
