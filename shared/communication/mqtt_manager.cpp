/**
 * @file mqtt_manager.cpp
 * @brief MQTT communication implementation with Home Assistant auto-discovery
 */

#include "mqtt_manager.h"
#include <WiFi.h>

namespace iwmp {

// Global instance
MqttManager& Mqtt = MqttManager::getInstance();

// Static pointer for callbacks
static MqttManager* s_mqtt_instance = nullptr;

MqttManager& MqttManager::getInstance() {
    static MqttManager instance;
    return instance;
}

bool MqttManager::begin(const MqttConfig& config, const DeviceIdentity& identity) {
    if (_initialized) {
        return true;
    }

    s_mqtt_instance = this;
    _config = config;
    _identity = identity;

    // Build topic strings
    buildTopics();

    // Setup client callbacks
    _client.onConnect([](bool session_present) {
        if (s_mqtt_instance) {
            s_mqtt_instance->onMqttConnect(session_present);
        }
    });

    _client.onDisconnect([](AsyncMqttClientDisconnectReason reason) {
        if (s_mqtt_instance) {
            s_mqtt_instance->onMqttDisconnect(reason);
        }
    });

    _client.onMessage([](char* topic, char* payload, AsyncMqttClientMessageProperties props,
                         size_t len, size_t index, size_t total) {
        if (s_mqtt_instance) {
            s_mqtt_instance->onMqttMessage(topic, payload, props, len, index, total);
        }
    });

    // Configure connection
    _client.setServer(_config.broker, _config.port);

    if (_config.username[0] != '\0') {
        _client.setCredentials(_config.username, _config.password);
    }

    // Setup LWT
    setupLWT();

    // Set client ID
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "iwmp_%s", _identity.device_id);
    _client.setClientId(client_id);

    _initialized = true;

    Serial.printf("[MQTT] Initialized for broker %s:%d\n", _config.broker, _config.port);

    return true;
}

void MqttManager::end() {
    if (!_initialized) {
        return;
    }

    if (isConnected()) {
        publishAvailability(false);
        disconnect();
    }

    _initialized = false;
    s_mqtt_instance = nullptr;

    Serial.println("[MQTT] Shutdown");
}

void MqttManager::updateConfig(const MqttConfig& config) {
    bool was_connected = isConnected();

    if (was_connected) {
        disconnect();
    }

    _config = config;
    buildTopics();
    _client.setServer(_config.broker, _config.port);

    if (_config.username[0] != '\0') {
        _client.setCredentials(_config.username, _config.password);
    } else {
        _client.setCredentials(nullptr, nullptr);
    }

    setupLWT();

    if (was_connected && _config.enabled) {
        connect();
    }
}

// ============ Connection ============

void MqttManager::connect() {
    if (!_initialized || !_config.enabled) {
        return;
    }

    if (isConnected() || _connecting) {
        return;
    }

    if (!WiFi.isConnected()) {
        Serial.println("[MQTT] Cannot connect - WiFi not connected");
        return;
    }

    Serial.printf("[MQTT] Connecting to %s:%d...\n", _config.broker, _config.port);
    _connecting = true;
    _client.connect();
}

void MqttManager::disconnect() {
    if (!_initialized) {
        return;
    }

    _connecting = false;
    _client.disconnect();
}

bool MqttManager::isConnected() const {
    return _client.connected();
}

void MqttManager::loop() {
    if (!_initialized || !_config.enabled) {
        return;
    }

    // Handle reconnection
    if (!isConnected() && !_connecting && WiFi.isConnected()) {
        uint32_t now = millis();
        if ((now - _last_reconnect_attempt) >= MQTT_RECONNECT_INTERVAL_MS) {
            _last_reconnect_attempt = now;

            if (_reconnect_attempts < MQTT_MAX_RECONNECT_ATTEMPTS) {
                _reconnect_attempts++;
                Serial.printf("[MQTT] Reconnect attempt %d/%d\n",
                              _reconnect_attempts, MQTT_MAX_RECONNECT_ATTEMPTS);
                connect();
            }
        }
    }
}

// ============ Home Assistant Discovery ============

void MqttManager::publishDiscovery() {
    if (!isConnected() || !_config.ha_discovery_enabled) {
        return;
    }

    Serial.println("[MQTT] Publishing HA discovery...");

    // Publish RSSI sensor for all devices
    publishRssiDiscovery();

    // Device-type specific discovery will be called by the device controller
}

void MqttManager::removeDiscovery() {
    // Publish empty payloads to remove discovery entries
    // This is called when unpairing or resetting
}

void MqttManager::publishMoistureDiscovery(uint8_t sensor_index, const char* sensor_name) {
    if (!isConnected() || !_config.ha_discovery_enabled) {
        return;
    }

    char entity[32];
    snprintf(entity, sizeof(entity), "moisture_%d", sensor_index + 1);

    String topic = getDiscoveryTopic("sensor", entity);
    String payload = buildMoistureDiscoveryPayload(sensor_index, sensor_name);

    publish(topic.c_str(), payload.c_str(), true);
}

void MqttManager::publishTemperatureDiscovery() {
    if (!isConnected() || !_config.ha_discovery_enabled) {
        return;
    }

    String topic = getDiscoveryTopic("sensor", "temperature");
    String payload = buildTemperatureDiscoveryPayload();

    publish(topic.c_str(), payload.c_str(), true);
}

void MqttManager::publishHumidityDiscovery() {
    if (!isConnected() || !_config.ha_discovery_enabled) {
        return;
    }

    String topic = getDiscoveryTopic("sensor", "humidity");
    String payload = buildHumidityDiscoveryPayload();

    publish(topic.c_str(), payload.c_str(), true);
}

void MqttManager::publishRelayDiscovery(uint8_t relay_index, const char* relay_name) {
    if (!isConnected() || !_config.ha_discovery_enabled) {
        return;
    }

    char entity[32];
    snprintf(entity, sizeof(entity), "relay_%d", relay_index + 1);

    String topic = getDiscoveryTopic("switch", entity);
    String payload = buildRelayDiscoveryPayload(relay_index, relay_name);

    publish(topic.c_str(), payload.c_str(), true);
}

void MqttManager::publishBatteryDiscovery() {
    if (!isConnected() || !_config.ha_discovery_enabled) {
        return;
    }

    // Battery percentage
    String topic = getDiscoveryTopic("sensor", "battery");
    String payload = buildBatteryDiscoveryPayload();
    publish(topic.c_str(), payload.c_str(), true);

    // Battery voltage
    topic = getDiscoveryTopic("sensor", "battery_voltage");
    payload = buildBatteryVoltageDiscoveryPayload();
    publish(topic.c_str(), payload.c_str(), true);
}

void MqttManager::publishRssiDiscovery() {
    if (!isConnected() || !_config.ha_discovery_enabled) {
        return;
    }

    String topic = getDiscoveryTopic("sensor", "rssi");
    String payload = buildRssiDiscoveryPayload();

    publish(topic.c_str(), payload.c_str(), true);
}

// ============ State Publishing ============

void MqttManager::publishState(const SensorReadings& readings) {
    if (!isConnected()) {
        return;
    }

    JsonDocument doc;

    // Moisture sensors
    for (uint8_t i = 0; i < readings.moisture_count; i++) {
        if (readings.moisture[i].valid) {
            char key[16];
            snprintf(key, sizeof(key), "moisture_%d", readings.moisture[i].index + 1);
            doc[key] = readings.moisture[i].percent;

            snprintf(key, sizeof(key), "moisture_%d_raw", readings.moisture[i].index + 1);
            doc[key] = readings.moisture[i].raw_value;
        }
    }

    // Environmental
    if (readings.has_environmental) {
        doc["temperature"] = serialized(String(readings.temperature_c, 1));
        doc["humidity"] = serialized(String(readings.humidity_percent, 1));
    }

    // Battery
    if (readings.has_battery) {
        doc["battery"] = readings.battery_percent;
        doc["battery_voltage"] = serialized(String(readings.battery_mv / 1000.0f, 2));
        doc["battery_charging"] = readings.battery_charging;
    }

    // Relay states
    for (uint8_t i = 0; i < readings.relay_count; i++) {
        if (readings.relays[i].valid) {
            char key[16];
            snprintf(key, sizeof(key), "relay_%d", readings.relays[i].index + 1);
            doc[key] = readings.relays[i].state ? "ON" : "OFF";
        }
    }

    // System info
    doc["rssi"] = readings.rssi;
    doc["uptime"] = readings.uptime_sec;
    doc["free_heap"] = readings.free_heap;

    String payload;
    serializeJson(doc, payload);

    publish(_state_topic.c_str(), payload.c_str(), false);
}

void MqttManager::publishAvailability(bool online) {
    if (!_initialized) {
        return;
    }

    // This might be called when disconnecting, so check carefully
    if (_client.connected()) {
        publish(_availability_topic.c_str(), online ? "online" : "offline", true, 1);
    }
}

void MqttManager::publishMoistureReading(uint8_t sensor_index, uint16_t raw_value, uint8_t percent) {
    if (!isConnected()) {
        return;
    }

    JsonDocument doc;

    char key[16];
    snprintf(key, sizeof(key), "moisture_%d", sensor_index + 1);
    doc[key] = percent;

    snprintf(key, sizeof(key), "moisture_%d_raw", sensor_index + 1);
    doc[key] = raw_value;

    String payload;
    serializeJson(doc, payload);

    publish(_state_topic.c_str(), payload.c_str(), false);
}

void MqttManager::publishEnvironmentalReading(float temperature_c, float humidity_percent) {
    if (!isConnected()) {
        return;
    }

    JsonDocument doc;
    doc["temperature"] = serialized(String(temperature_c, 1));
    doc["humidity"] = serialized(String(humidity_percent, 1));

    String payload;
    serializeJson(doc, payload);

    publish(_state_topic.c_str(), payload.c_str(), false);
}

void MqttManager::publishBatteryStatus(uint16_t voltage_mv, uint8_t percent, bool charging) {
    if (!isConnected()) {
        return;
    }

    JsonDocument doc;
    doc["battery"] = percent;
    doc["battery_voltage"] = serialized(String(voltage_mv / 1000.0f, 2));
    doc["battery_charging"] = charging;

    String payload;
    serializeJson(doc, payload);

    publish(_state_topic.c_str(), payload.c_str(), false);
}

void MqttManager::publishRelayState(uint8_t relay_index, bool state) {
    if (!isConnected()) {
        return;
    }

    JsonDocument doc;
    char key[16];
    snprintf(key, sizeof(key), "relay_%d", relay_index + 1);
    doc[key] = state ? "ON" : "OFF";

    String payload;
    serializeJson(doc, payload);

    publish(_state_topic.c_str(), payload.c_str(), false);
}

// ============ Raw Publish/Subscribe ============

bool MqttManager::publish(const char* topic, const char* payload, bool retain, uint8_t qos) {
    if (!isConnected()) {
        return false;
    }

    uint16_t packet_id = _client.publish(topic, qos, retain, payload);
    return packet_id > 0;
}

bool MqttManager::subscribe(const char* topic, uint8_t qos) {
    if (!isConnected()) {
        return false;
    }

    uint16_t packet_id = _client.subscribe(topic, qos);
    return packet_id > 0;
}

bool MqttManager::unsubscribe(const char* topic) {
    if (!isConnected()) {
        return false;
    }

    uint16_t packet_id = _client.unsubscribe(topic);
    return packet_id > 0;
}

// ============ Callbacks ============

void MqttManager::onConnect(MqttConnectCallback callback) {
    _connect_callback = callback;
}

void MqttManager::onDisconnect(MqttDisconnectCallback callback) {
    _disconnect_callback = callback;
}

void MqttManager::onMessage(MqttMessageCallback callback) {
    _message_callback = callback;
}

void MqttManager::onRelayCommand(RelayCommandCallback callback) {
    _relay_callback = callback;
}

// ============ Topic Builders ============

String MqttManager::getStateTopic() const {
    return _state_topic;
}

String MqttManager::getAvailabilityTopic() const {
    return _availability_topic;
}

String MqttManager::getCommandTopic(const char* entity) const {
    return _command_base_topic + "/" + entity;
}

String MqttManager::getDiscoveryTopic(const char* component, const char* entity) const {
    // Format: homeassistant/<component>/iwmp_<device_id>/<entity>/config
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/iwmp_%s/%s/config",
             _config.ha_discovery_prefix, component, _identity.device_id, entity);
    return String(topic);
}

// ============ Debug ============

void MqttManager::printDebugInfo() const {
    Serial.println("======== MQTT Debug Info ========");
    Serial.printf("Initialized: %s\n", _initialized ? "yes" : "no");
    Serial.printf("Enabled: %s\n", _config.enabled ? "yes" : "no");
    Serial.printf("Connected: %s\n", isConnected() ? "yes" : "no");
    Serial.printf("Broker: %s:%d\n", _config.broker, _config.port);
    Serial.printf("Base topic: %s\n", _config.base_topic);
    Serial.printf("HA discovery: %s\n", _config.ha_discovery_enabled ? "yes" : "no");
    Serial.printf("State topic: %s\n", _state_topic.c_str());
    Serial.printf("Availability topic: %s\n", _availability_topic.c_str());
    Serial.println("=================================");
}

// ============ Private Methods ============

void MqttManager::onMqttConnect(bool session_present) {
    _connecting = false;
    _reconnect_attempts = 0;

    Serial.printf("[MQTT] Connected (session: %s)\n", session_present ? "present" : "new");

    // Publish availability
    publishAvailability(true);

    // Subscribe to command topics
    subscribeToCommands();

    // Publish discovery if enabled
    if (_config.ha_discovery_enabled) {
        publishDiscovery();
    }

    // User callback
    if (_connect_callback) {
        _connect_callback(session_present);
    }
}

void MqttManager::onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    _connecting = false;

    const char* reason_str = "Unknown";
    switch (reason) {
        case AsyncMqttClientDisconnectReason::TCP_DISCONNECTED:
            reason_str = "TCP disconnected";
            break;
        case AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
            reason_str = "Unacceptable protocol version";
            break;
        case AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED:
            reason_str = "Identifier rejected";
            break;
        case AsyncMqttClientDisconnectReason::MQTT_SERVER_UNAVAILABLE:
            reason_str = "Server unavailable";
            break;
        case AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS:
            reason_str = "Malformed credentials";
            break;
        case AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED:
            reason_str = "Not authorized";
            break;
        default:
            break;
    }

    Serial.printf("[MQTT] Disconnected: %s\n", reason_str);

    if (_disconnect_callback) {
        _disconnect_callback(reason);
    }
}

void MqttManager::onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties,
                                 size_t len, size_t index, size_t total) {
    // Null-terminate payload
    char* payload_str = new char[len + 1];
    memcpy(payload_str, payload, len);
    payload_str[len] = '\0';

    // Check if it's a relay command
    String topic_str(topic);
    if (topic_str.startsWith(_command_base_topic + "/relay_")) {
        handleRelayCommand(payload_str, len);
    }

    // User callback
    if (_message_callback) {
        _message_callback(topic, payload_str, len);
    }

    delete[] payload_str;
}

void MqttManager::buildTopics() {
    // State topic: iwetmyplants/<device_id>/state
    _state_topic = String(_config.base_topic) + "/" + _identity.device_id + "/state";

    // Availability topic: iwetmyplants/<device_id>/status
    _availability_topic = String(_config.base_topic) + "/" + _identity.device_id + "/status";

    // Command base: iwetmyplants/<device_id>/cmd
    _command_base_topic = String(_config.base_topic) + "/" + _identity.device_id + "/cmd";
}

void MqttManager::setupLWT() {
    // Set Last Will and Testament
    _client.setWill(_availability_topic.c_str(), 1, true, "offline");
}

void MqttManager::subscribeToCommands() {
    // Subscribe to all command topics
    String cmd_wildcard = _command_base_topic + "/#";
    subscribe(cmd_wildcard.c_str());

    Serial.printf("[MQTT] Subscribed to %s\n", cmd_wildcard.c_str());
}

void MqttManager::handleRelayCommand(const char* payload, size_t len) {
    if (!_relay_callback) {
        return;
    }

    // Try to parse JSON payload
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);

    if (error) {
        // Simple ON/OFF payload
        bool state = (strcmp(payload, "ON") == 0 || strcmp(payload, "1") == 0 ||
                      strcmp(payload, "true") == 0);

        // Extract relay index from topic (handled elsewhere)
        // For now, assume relay 0
        _relay_callback(0, state, 0);
    } else {
        // JSON payload
        uint8_t relay = doc["relay"] | 0;
        bool state = doc["state"] | false;
        uint32_t duration = doc["duration"] | 0;

        _relay_callback(relay, state, duration);
    }
}

const char* MqttManager::getModelName() const {
    switch (_identity.device_type) {
        case 0: return HA_MODEL_HUB;
        case 1: return HA_MODEL_REMOTE;
        case 2: return HA_MODEL_GREENHOUSE;
        default: return "iWetMyPlants";
    }
}

String MqttManager::getUniqueId(const char* entity) const {
    char unique_id[64];
    snprintf(unique_id, sizeof(unique_id), "iwmp_%s_%s", _identity.device_id, entity);
    return String(unique_id);
}

String MqttManager::buildDeviceJson() const {
    JsonDocument device;

    // Identifiers
    JsonArray identifiers = device["ids"].to<JsonArray>();
    char id[32];
    snprintf(id, sizeof(id), "iwmp_%s", _identity.device_id);
    identifiers.add(id);

    // Device info
    device["name"] = _identity.device_name;
    device["mdl"] = getModelName();
    device["mf"] = HA_MANUFACTURER;
    device["sw"] = _identity.firmware_version;

    String result;
    serializeJson(device, result);
    return result;
}

String MqttManager::buildMoistureDiscoveryPayload(uint8_t sensor_index, const char* sensor_name) const {
    JsonDocument doc;

    char entity_name[64];
    snprintf(entity_name, sizeof(entity_name), "%s Moisture", sensor_name);
    doc["name"] = entity_name;

    char entity_id[32];
    snprintf(entity_id, sizeof(entity_id), "moisture_%d", sensor_index + 1);
    doc["uniq_id"] = getUniqueId(entity_id);

    doc["stat_t"] = _state_topic;

    char value_template[64];
    snprintf(value_template, sizeof(value_template), "{{ value_json.moisture_%d }}", sensor_index + 1);
    doc["val_tpl"] = value_template;

    doc["unit_of_meas"] = "%";
    doc["dev_cla"] = "moisture";
    doc["stat_cla"] = "measurement";
    doc["avty_t"] = _availability_topic;

    // Device info
    JsonObject device = doc["dev"].to<JsonObject>();
    JsonArray identifiers = device["ids"].to<JsonArray>();
    char id[32];
    snprintf(id, sizeof(id), "iwmp_%s", _identity.device_id);
    identifiers.add(id);
    device["name"] = _identity.device_name;
    device["mdl"] = getModelName();
    device["mf"] = HA_MANUFACTURER;
    device["sw"] = _identity.firmware_version;

    String result;
    serializeJson(doc, result);
    return result;
}

String MqttManager::buildTemperatureDiscoveryPayload() const {
    JsonDocument doc;

    doc["name"] = String(_identity.device_name) + " Temperature";
    doc["uniq_id"] = getUniqueId("temperature");
    doc["stat_t"] = _state_topic;
    doc["val_tpl"] = "{{ value_json.temperature }}";
    doc["unit_of_meas"] = "\u00B0C";
    doc["dev_cla"] = "temperature";
    doc["stat_cla"] = "measurement";
    doc["avty_t"] = _availability_topic;

    // Device info
    JsonObject device = doc["dev"].to<JsonObject>();
    JsonArray identifiers = device["ids"].to<JsonArray>();
    char id[32];
    snprintf(id, sizeof(id), "iwmp_%s", _identity.device_id);
    identifiers.add(id);
    device["name"] = _identity.device_name;
    device["mdl"] = getModelName();
    device["mf"] = HA_MANUFACTURER;
    device["sw"] = _identity.firmware_version;

    String result;
    serializeJson(doc, result);
    return result;
}

String MqttManager::buildHumidityDiscoveryPayload() const {
    JsonDocument doc;

    doc["name"] = String(_identity.device_name) + " Humidity";
    doc["uniq_id"] = getUniqueId("humidity");
    doc["stat_t"] = _state_topic;
    doc["val_tpl"] = "{{ value_json.humidity }}";
    doc["unit_of_meas"] = "%";
    doc["dev_cla"] = "humidity";
    doc["stat_cla"] = "measurement";
    doc["avty_t"] = _availability_topic;

    // Device info
    JsonObject device = doc["dev"].to<JsonObject>();
    JsonArray identifiers = device["ids"].to<JsonArray>();
    char id[32];
    snprintf(id, sizeof(id), "iwmp_%s", _identity.device_id);
    identifiers.add(id);
    device["name"] = _identity.device_name;
    device["mdl"] = getModelName();
    device["mf"] = HA_MANUFACTURER;
    device["sw"] = _identity.firmware_version;

    String result;
    serializeJson(doc, result);
    return result;
}

String MqttManager::buildRelayDiscoveryPayload(uint8_t relay_index, const char* relay_name) const {
    JsonDocument doc;

    doc["name"] = relay_name;

    char entity_id[32];
    snprintf(entity_id, sizeof(entity_id), "relay_%d", relay_index + 1);
    doc["uniq_id"] = getUniqueId(entity_id);

    doc["stat_t"] = _state_topic;

    char value_template[64];
    snprintf(value_template, sizeof(value_template), "{{ value_json.relay_%d }}", relay_index + 1);
    doc["val_tpl"] = value_template;

    // Command topic
    char cmd_topic[128];
    snprintf(cmd_topic, sizeof(cmd_topic), "%s/relay_%d", _command_base_topic.c_str(), relay_index + 1);
    doc["cmd_t"] = cmd_topic;

    doc["pl_on"] = "ON";
    doc["pl_off"] = "OFF";
    doc["avty_t"] = _availability_topic;

    // Device info
    JsonObject device = doc["dev"].to<JsonObject>();
    JsonArray identifiers = device["ids"].to<JsonArray>();
    char id[32];
    snprintf(id, sizeof(id), "iwmp_%s", _identity.device_id);
    identifiers.add(id);
    device["name"] = _identity.device_name;
    device["mdl"] = getModelName();
    device["mf"] = HA_MANUFACTURER;
    device["sw"] = _identity.firmware_version;

    String result;
    serializeJson(doc, result);
    return result;
}

String MqttManager::buildBatteryDiscoveryPayload() const {
    JsonDocument doc;

    doc["name"] = String(_identity.device_name) + " Battery";
    doc["uniq_id"] = getUniqueId("battery");
    doc["stat_t"] = _state_topic;
    doc["val_tpl"] = "{{ value_json.battery }}";
    doc["unit_of_meas"] = "%";
    doc["dev_cla"] = "battery";
    doc["stat_cla"] = "measurement";
    doc["avty_t"] = _availability_topic;

    // Device info
    JsonObject device = doc["dev"].to<JsonObject>();
    JsonArray identifiers = device["ids"].to<JsonArray>();
    char id[32];
    snprintf(id, sizeof(id), "iwmp_%s", _identity.device_id);
    identifiers.add(id);
    device["name"] = _identity.device_name;
    device["mdl"] = getModelName();
    device["mf"] = HA_MANUFACTURER;
    device["sw"] = _identity.firmware_version;

    String result;
    serializeJson(doc, result);
    return result;
}

String MqttManager::buildBatteryVoltageDiscoveryPayload() const {
    JsonDocument doc;

    doc["name"] = String(_identity.device_name) + " Battery Voltage";
    doc["uniq_id"] = getUniqueId("battery_voltage");
    doc["stat_t"] = _state_topic;
    doc["val_tpl"] = "{{ value_json.battery_voltage }}";
    doc["unit_of_meas"] = "V";
    doc["dev_cla"] = "voltage";
    doc["stat_cla"] = "measurement";
    doc["avty_t"] = _availability_topic;

    // Device info
    JsonObject device = doc["dev"].to<JsonObject>();
    JsonArray identifiers = device["ids"].to<JsonArray>();
    char id[32];
    snprintf(id, sizeof(id), "iwmp_%s", _identity.device_id);
    identifiers.add(id);
    device["name"] = _identity.device_name;
    device["mdl"] = getModelName();
    device["mf"] = HA_MANUFACTURER;
    device["sw"] = _identity.firmware_version;

    String result;
    serializeJson(doc, result);
    return result;
}

String MqttManager::buildRssiDiscoveryPayload() const {
    JsonDocument doc;

    doc["name"] = String(_identity.device_name) + " Signal Strength";
    doc["uniq_id"] = getUniqueId("rssi");
    doc["stat_t"] = _state_topic;
    doc["val_tpl"] = "{{ value_json.rssi }}";
    doc["unit_of_meas"] = "dBm";
    doc["dev_cla"] = "signal_strength";
    doc["stat_cla"] = "measurement";
    doc["ent_cat"] = "diagnostic";
    doc["avty_t"] = _availability_topic;

    // Device info
    JsonObject device = doc["dev"].to<JsonObject>();
    JsonArray identifiers = device["ids"].to<JsonArray>();
    char id[32];
    snprintf(id, sizeof(id), "iwmp_%s", _identity.device_id);
    identifiers.add(id);
    device["name"] = _identity.device_name;
    device["mdl"] = getModelName();
    device["mf"] = HA_MANUFACTURER;
    device["sw"] = _identity.firmware_version;

    String result;
    serializeJson(doc, result);
    return result;
}

} // namespace iwmp
