// main.cpp - M0 skeleton: proves both chips build, boot and report their version.
#include <Arduino.h>

void setup() { Serial.begin(115200); }

void loop() {
  // Repeats so the banner shows whenever a monitor attaches (the C3's USB port re-enumerates on reset).
  Serial.printf("iWetMyPlants %s (%s%s)\n", IWMP_VERSION, IWMP_BUILD_HASH, IWMP_BUILD_DIRTY ? "*" : "");
  delay(5000);
}
