// mqtt.cpp - publishes readings to MQTT and Home Assistant, and listens to the other units for
// the overview. esp-mqtt runs in its own task; its event handler only sets flags and queues
// incoming messages. Everything else happens in loop().
#include <Arduino.h>
#include <WiFi.h>
#include <mqtt_client.h>
#include <esp_system.h>
#include "app.h"
#include "logic.h"

namespace {
constexpr int OV_UNITS = 16;    // ponytail: fixed-size overview; raise if anyone runs more units
constexpr int OV_SENSORS = 6;   // the most any chip has
constexpr uint32_t MIN_GAP_MS = 10000;

esp_mqtt_client_handle_t client;
volatile bool up, freshConnect;
char id[13];   // the 12-hex WiFi MAC, as v1 used, so existing Home Assistant entities carry over
char stateTopic[40], statusTopic[40], uri[90];
uint32_t lastPublish, publishedAt;   // publishedAt = rd.at of the last reading sent

struct Msg { char topic[64]; char payload[1024]; };
QueueHandle_t inbox;

struct Unit {
  char id[13], name[32];
  int8_t m[OV_SENSORS];             // -2 = sensor off, -1 = no reading
  char names[OV_SENSORS][24];
  bool low[OV_SENSORS];
  int8_t bat;
  float temp;
  uint32_t at;                      // millis() when last heard; 0 = empty slot
};
Unit units[OV_UNITS];

void onEvent(void*, esp_event_base_t, int32_t event, void* data) {
  auto* e = static_cast<esp_mqtt_event_handle_t>(data);
  if (event == MQTT_EVENT_CONNECTED) {
    up = true;
    freshConnect = true;
  } else if (event == MQTT_EVENT_DISCONNECTED) {
    up = false;
  } else if (event == MQTT_EVENT_DATA && e->data_len == e->total_data_len &&
             e->topic_len < int(sizeof Msg::topic) && e->data_len < int(sizeof Msg::payload)) {
    // ponytail: messages that arrive in pieces (over 2 KB) are skipped; a unit's state is ~0.5 KB
    static Msg m;   // only the MQTT task runs this, so one scratch buffer is enough
    memcpy(m.topic, e->topic, e->topic_len);
    m.topic[e->topic_len] = 0;
    memcpy(m.payload, e->data, e->data_len);
    m.payload[e->data_len] = 0;
    xQueueSend(inbox, &m, 0);   // queue full: drop it, another update comes soon
  }
}

// Queued and sent by the MQTT task, so loop() never waits on the network.
void send(const char* topic, const char* payload, bool retain) {
  esp_mqtt_client_enqueue(client, topic, payload, 0, 0, retain, true);
}

// One Home Assistant entity. Entities that are off get an empty retained message, which
// removes them from Home Assistant.
template <typename F> void entity(const char* component, const char* key, bool on, F fill) {
  char topic[96];
  snprintf(topic, sizeof topic, "homeassistant/%s/iwmp_%s/%s/config", component, id, key);
  if (!on || !cfg.ha_discovery) return send(topic, "", true);

  JsonDocument d;
  char uid[48];
  snprintf(uid, sizeof uid, "iwmp_%s_%s", id, key);
  d["uniq_id"] = uid;
  d["stat_t"] = stateTopic;
  if (cfg.sleep) d["exp_aft"] = cfg.sleep_min * 180 + 60;   // asleep between reports: stale after 3 missed
  else d["avty_t"] = statusTopic;
  JsonObject dev = d["dev"].to<JsonObject>();
  dev["ids"].add(String("iwmp_") + id);
  dev["name"] = cfg.name[0] ? cfg.name : deviceId();
  dev["mf"] = "iWetMyPlants";
  dev["mdl"] = ESP.getChipModel();
  dev["sw"] = IWMP_VERSION;
  if (wifiConnected()) dev["cu"] = "http://" + WiFi.localIP().toString() + "/";
  fill(d);
  String out;
  serializeJson(d, out);
  send(topic, out.c_str(), true);
}

void measurement(JsonDocument& d, const char* name, const char* tpl, const char* unit, const char* cls) {
  d["name"] = name;
  d["val_tpl"] = tpl;
  d["unit_of_meas"] = unit;
  d["dev_cla"] = cls;
  d["stat_cla"] = "measurement";
}

void publishDiscovery() {
  for (int i = 0; i < MAX_SENSORS; i++) {
    char key[16], tpl[32];
    snprintf(key, sizeof key, "moisture_%d", i + 1);
    snprintf(tpl, sizeof tpl, "{{ value_json.m[%d] }}", i);
    entity("sensor", key, cfg.s[i].en, [&](JsonDocument& d) { measurement(d, cfg.s[i].name, tpl, "%", "moisture"); });
  }
  entity("sensor", "temperature", cfg.sht, [](JsonDocument& d) { measurement(d, "Temperature", "{{ value_json.temp }}", "°C", "temperature"); });
  entity("sensor", "humidity", cfg.sht, [](JsonDocument& d) { measurement(d, "Humidity", "{{ value_json.rh }}", "%", "humidity"); });
  entity("sensor", "battery", cfg.bat_pin >= 0, [](JsonDocument& d) { measurement(d, "Battery", "{{ value_json.bat }}", "%", "battery"); });
  entity("sensor", "battery_voltage", cfg.bat_pin >= 0, [](JsonDocument& d) {
    measurement(d, "Battery voltage", "{{ value_json.bat_v }}", "V", "voltage");
    d["ent_cat"] = "diagnostic";
  });
  entity("sensor", "rssi", true, [](JsonDocument& d) {
    measurement(d, "WiFi signal", "{{ value_json.rssi }}", "dBm", "signal_strength");
    d["ent_cat"] = "diagnostic";
  });
}

// One retained JSON per unit. Slot-aligned arrays: m[0] is S1. null = off or no trustworthy reading.
void publishState() {
  JsonDocument d;
  d["v"] = 2;
  d["name"] = cfg.name[0] ? cfg.name : deviceId();
  JsonArray m = d["m"].to<JsonArray>(), names = d["names"].to<JsonArray>(), low = d["low"].to<JsonArray>();
  for (int i = 0; i < MAX_SENSORS; i++) {
    int pct = sensorPct(i);
    if (pct >= 0) m.add(pct);
    else m.add(nullptr);
    if (cfg.s[i].en) names.add(cfg.s[i].name);
    else names.add(nullptr);
    low.add(pct >= 0 && pct < cfg.s[i].warn_pct);
  }
  if (cfg.sht) {
    if (isnan(rd.tempC)) {
      d["temp"] = nullptr;
      d["rh"] = nullptr;
    } else {
      d["temp"] = serialized(String(rd.tempC, 1));
      d["rh"] = serialized(String(rd.rh, 0));
    }
  }
  if (rd.batMv >= 0) {
    d["bat"] = batteryPct(rd.batMv);
    d["bat_v"] = serialized(String(rd.batMv / 1000.0f, 2));
  }
  d["rssi"] = WiFi.RSSI();
  d["fw"] = IWMP_VERSION;
  String out;
  serializeJson(d, out);
  send(stateTopic, out.c_str(), true);
  publishedAt = rd.at;
  lastPublish = millis();
}

// Another unit's state, for the overview.
void absorb(const Msg& msg) {
  const char* other = msg.topic + strlen("iwetmyplants/");
  if (strncmp(msg.topic, "iwetmyplants/", 13) || strlen(other) != 18 || strcmp(other + 12, "/state") ||
      !strncmp(other, id, 12))
    return;
  JsonDocument d;
  if (deserializeJson(d, msg.payload) || d["v"] != 2) return;   // v1 units and removed units

  Unit* u = nullptr;
  for (Unit& x : units)
    if (!strncmp(x.id, other, 12)) u = &x;
  if (!u) {   // a free slot, or the one heard from longest ago
    u = &units[0];
    for (Unit& x : units)
      if (!x.at || (u->at && millis() - x.at > millis() - u->at)) u = &x;
  }
  memcpy(u->id, other, 12);
  u->id[12] = 0;
  strlcpy(u->name, d["name"] | "", sizeof u->name);
  for (int i = 0; i < OV_SENSORS; i++) {
    bool on = !d["names"][i].isNull();
    strlcpy(u->names[i], d["names"][i] | "", sizeof u->names[i]);
    u->m[i] = !on ? -2 : d["m"][i].is<int>() ? d["m"][i].as<int>() : -1;
    u->low[i] = d["low"][i] | false;
  }
  u->bat = d["bat"] | -1;
  u->temp = d["temp"].is<float>() ? d["temp"].as<float>() : NAN;
  u->at = millis() | 1;
}

void start() {
  snprintf(uri, sizeof uri, "mqtt://%s:%u", cfg.mqtt_host, cfg.mqtt_port);
  esp_mqtt_client_config_t mc = {};
  mc.uri = uri;
  mc.client_id = deviceId();
  if (cfg.mqtt_user[0]) {
    mc.username = cfg.mqtt_user;
    mc.password = cfg.mqtt_pass;
  }
  if (!cfg.sleep) {   // sleeping units would look offline every night; they use exp_aft instead
    mc.lwt_topic = statusTopic;
    mc.lwt_msg = "offline";
    mc.lwt_qos = 1;
    mc.lwt_retain = 1;
  }
  mc.keepalive = 30;
  mc.buffer_size = 2048;
  client = esp_mqtt_client_init(&mc);
  esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, onEvent, nullptr);
  esp_mqtt_client_start(client);
  Serial.printf("MQTT: connecting to %s\n", uri);
}
}  // namespace

void mqttBegin() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(id, sizeof id, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  snprintf(stateTopic, sizeof stateTopic, "iwetmyplants/%s/state", id);
  snprintf(statusTopic, sizeof statusTopic, "iwetmyplants/%s/status", id);
  inbox = xQueueCreate(6, sizeof(Msg));
}

void mqttLoop() {
  if (!client) {
    if (cfg.mqtt_host[0] && wifiConnected()) start();   // esp-mqtt needs the network up first
    return;
  }
  static bool wasUp;
  if (up != wasUp) {
    wasUp = up;
    Serial.println(up ? "MQTT connected" : "MQTT disconnected");
  }
  if (freshConnect) {
    freshConnect = false;
    if (!cfg.sleep) {
      send(statusTopic, "online", true);
      esp_mqtt_client_subscribe(client, "iwetmyplants/+/state", 0);
    }
    publishDiscovery();   // every connect: repairs a broker that lost its retained messages
    publishedAt = 0;      // and send the state straight away
    lastPublish = millis() - MIN_GAP_MS;
  }
  if (up && rd.at && rd.at != publishedAt && millis() - lastPublish >= MIN_GAP_MS) publishState();

  static Msg m;   // 1 KB, kept off the loop task's stack
  while (xQueueReceive(inbox, &m, 0)) absorb(m);
}

void mqttJson(JsonObject out) {
  out["mqtt"] = !cfg.mqtt_host[0] ? "off" : up ? "connected" : "connecting";
  out["topic"] = stateTopic;
  JsonArray list = out["units"].to<JsonArray>();
  for (const Unit& u : units) {
    if (!u.at) continue;
    JsonObject o = list.add<JsonObject>();
    o["name"] = u.name[0] ? u.name : u.id;
    o["age"] = (millis() - u.at) / 1000;
    JsonArray s = o["sensors"].to<JsonArray>();
    for (int i = 0; i < OV_SENSORS; i++) {
      if (u.m[i] == -2) continue;
      JsonObject x = s.add<JsonObject>();
      x["n"] = u.names[i];
      if (u.m[i] >= 0) x["m"] = u.m[i];
      x["low"] = u.low[i];
    }
    if (u.bat >= 0) o["bat"] = u.bat;
    if (!isnan(u.temp)) o["temp"] = serialized(String(u.temp, 1));
  }
}
