// main.cpp - boot order and the main loop. Everything runs here, one thing at a time.
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include "app.h"

Config cfg;

// Stop the core from marking a freshly flashed image good by itself. loop() does it once the
// unit has run for a while, so an update that crashes early rolls back on the next restart.
extern "C" bool verifyRollbackLater() { return true; }

namespace {
constexpr uint32_t CONFIRM_AFTER_MS = 30000;

void confirmFirmwareOnce() {
  static bool done;
  if (done || millis() < CONFIRM_AFTER_MS) return;
  esp_ota_mark_app_valid_cancel_rollback();   // no-op unless this image is fresh from an update
  done = true;
}

// Hold BOOT for 10 s while the unit is running: erase everything and restart.
// (Holding it during power-up instead puts the chip into flashing mode.)
void factoryResetButton() {
  static uint32_t pressedAt;
  if (digitalRead(BOOT_PIN) == HIGH) {
    pressedAt = 0;
  } else if (!pressedAt) {
    pressedAt = millis() | 1;
  } else if (millis() - pressedAt > 10000) {
    Serial.println("BOOT held for 10 s: factory reset");
    configErase();
    ESP.restart();
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  bool saved = configLoad(cfg);
  Serial.printf("\niWetMyPlants %s (%s%s) on %s, %s\n", IWMP_VERSION, IWMP_BUILD_HASH,
                IWMP_BUILD_DIRTY ? "*" : "", ESP.getChipModel(), saved ? "settings loaded" : "no settings yet");
  esp_task_wdt_init(30, true);   // ponytail: IDF 4.4 signature; Arduino core 3.x needs esp_task_wdt_reconfigure()
  enableLoopWDT();
  pinMode(BOOT_PIN, INPUT_PULLUP);
  wifiBegin();
  webBegin();
}

void loop() {
  wifiLoop();
  webLoop();
  factoryResetButton();
  confirmFirmwareOnce();
}
