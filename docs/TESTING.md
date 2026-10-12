# Hardware checks

The logic tests (`pio test -e native`) run in CI on every push. These checks need real boards.
Run each milestone's list before starting the next one, and the whole file before tagging a release.

## Flashing a development build

```
pio run -e esp32 -t upload          # ESP32 DevKit
pio run -e esp32c3 -t upload        # ESP32-C3 SuperMini
pio device monitor -e esp32c3       # serial output (115200)
```

If the C3 doesn't show up as a port: hold **BOOT**, tap **RESET**, release **BOOT**, then upload.
To start from a blank slate: `pio run -e esp32c3 -t erase` before uploading.

## M0 + M1: boot, setup network, WiFi, updates

Do these on both boards.

- [ ] **Boot banner.** Serial shows `iWetMyPlants 2.0.0+N (hash) on ESP32…, no settings yet`, then
      `Setup network "iWetMyPlants-xxxxxx" is open`.
- [ ] **Setup network.** On a phone, join `iWetMyPlants-xxxxxx`. The setup page opens by itself
      (if not, browse to `http://192.168.4.1/`). Settings lists nearby networks.
- [ ] **Join WiFi.** Tap your network, enter the password, save. Put the phone back on your home WiFi
      and open `http://iwmp-xxxxxx.local/` (or the IP printed on serial). Home says **Online**.
- [ ] **Wrong password.** In Settings, save a wrong WiFi password. About 2 minutes later the setup
      network appears again. Join it and fix the password.
- [ ] **Setup network times out.** With the WiFi password right but the router off, the unit opens its
      setup network after 2 minutes, then goes back to trying WiFi 5 minutes later (if no phone joined).
      Turn the router back on: it rejoins.
- [ ] **Admin password.** Set one and save. Press Restart: the browser asks for it (user name `admin`).
      A wrong password is refused.
- [ ] **Other websites are refused.** On any other website, open the browser console (F12) and run
      `fetch('http://iwmp-xxxxxx.local/api/reboot', {method: 'POST', mode: 'no-cors'})`.
      The unit must **not** restart.
- [ ] **OTA, twice in a row.** Build (`pio run -e esp32c3`), then System → Update firmware →
      `.pio/build/esp32c3/firmware.bin`. The unit restarts and System shows the new build. Repeat.
- [ ] **OTA rollback.** Temporarily add `abort();` as the first line of `loop()`, build, and upload it
      through System → Update firmware. The unit crashes once, then comes back by itself on the previous
      firmware (System shows the old build). Remove the `abort();`.
- [ ] **Factory reset button.** Hold BOOT for 10 seconds while the unit runs. Serial shows
      `factory reset` and the setup network appears.
- [ ] **C3 range.** The C3 transmits at 8.5 dBm (SuperMini antennas misbehave at full power). Note how
      far from the router it still connects reliably.

## M2: sensors and calibration

Wire sensors to the S ports as in [HARDWARE.md](HARDWARE.md). Serial prints a line each time the
sensors are read (`S1 1834 mV 42%`), which helps while testing.

- [ ] **Turn sensors on.** Sensors page: tick each port with a sensor, name it, save. Home shows one
      card per sensor.
- [ ] **Readings settle.** With "Read every 10 seconds (testing)", a sensor sitting still reads within
      about ±30 mV from one read to the next.
- [ ] **Calibrate.** Calibrate → hold the sensor in dry air, Set dry → stand it in water, Set wet.
      The percentage goes to about 0% in air and about 100% in water, and follows the sensor as you
      move it between the two. Home stops saying "Not calibrated yet".
- [ ] **Calibration survives a restart.** System → Restart. Same dry/wet points afterwards.
- [ ] **Bad calibration is refused.** Press Set wet while the sensor is still dry: the page says the
      points are too close, and nothing changes.
- [ ] **Unplugged sensor.** Unplug a calibrated sensor. Note what Home shows within a read or two.
      ("Check the sensor" is the hope; input-only pins can float, so record what actually happens.)
- [ ] **Needs water.** Set "Needs water below" above the current reading: the card turns amber and
      says "Needs water".
- [ ] **SHT (if fitted).** Pick SHT3x or SHT4x, save. Home shows an Air card with plausible
      temperature and humidity. Breathe on it: humidity rises.
- [ ] **Sensor power (if SPWR fitted).** Set the sensor power pin to 13 and save. The switched rail
      is on only around each read, and readings match the always-on ones.
