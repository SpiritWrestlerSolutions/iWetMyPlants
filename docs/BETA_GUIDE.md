# iWetMyPlants v2.0 — Beta Guide

> **Version 1.0.0** · Web installer: https://spiritwrestler.ca/plants/

---

## Contents

1. [Shopping List](#1-shopping-list)
2. [Hardware Guide](#2-hardware-guide)
   - [Hub wiring](#21-hub)
   - [Remote wiring](#22-remote-sensor-node)
   - [Greenhouse Controller wiring](#23-greenhouse-controller)
3. [User Guide](#3-user-guide)
   - [First flash](#31-flashing-firmware)
   - [Hub setup](#32-hub-setup)
   - [Remote setup](#33-remote-sensor-node-setup)
   - [Greenhouse setup](#34-greenhouse-controller-setup)
   - [Home Assistant / MQTT](#35-home-assistant--mqtt)
   - [Calibrating sensors](#36-calibrating-moisture-sensors)
4. [Troubleshooting & FAQ](#4-troubleshooting--faq)

---

## 1. Shopping List

### Required — Hub (the brains)

| Item | Notes |
|---|---|
| ESP32-WROOM-32 dev board | Any standard 38-pin "ESP32 DevKit" works |
| USB-A to Micro-USB cable | For flashing and power |
| 5V USB power supply | Phone charger is fine |
| Capacitive soil moisture sensor(s) | Capacitive preferred over resistive — they last longer |

> **One Hub runs your whole network.** It connects to WiFi, talks to Home Assistant via MQTT, and receives readings from Remote nodes.

---

### Optional — Remote Sensor Nodes *(one per distant plant)*

| Item | Notes |
|---|---|
| ESP32-C3 SuperMini | **Must be SuperMini** — the native USB matters |
| Capacitive soil moisture sensor | One per Remote |
| USB-C cable | SuperMini uses USB-C |
| 5V USB power supply | Or a small LiPo battery (see below) |
| 3.7V LiPo battery | 400–1000 mAh. Enables battery mode + deep sleep |
| TP4056 charging module | If running on battery |

> **Remotes are optional.** If all your plants are near the Hub, you don't need them — just wire sensors directly to the Hub.

---

### Optional — Greenhouse Controller *(automated watering/venting)*

| Item | Notes |
|---|---|
| ESP32-WROOM-32 dev board | Same as Hub |
| Relay module, 4- or 8-channel, 5V | **Must be active-low trigger** (virtually all common modules are) |
| DHT22 sensor | Temperature + humidity. DHT11 also works, less accurate |
| *or* SHT31 breakout board | Better accuracy than DHT, uses I2C |
| Water pump or solenoid valve(s) | 12V DC pumps work well with a relay |
| 12V power supply | For pumps (separate from the ESP32 supply) |
| Tubing + fittings | Match your pump outlet diameter |

---

### Recommended Extras (any setup)

| Item | Notes |
|---|---|
| ADS1115 ADC module | 16-bit external ADC; gives much more stable readings than the ESP32's built-in ADC |
| Dupont jumper wires (F-F, M-F) | For connecting sensors to the board |
| Breadboard or prototyping board | Helps keep things tidy during setup |
| Project box / enclosure | Protect from moisture in a real deployment |
| Electrical tape or heat shrink | Insulate any bare connections near water |

---

## 2. Hardware Guide

### A note on moisture sensors

The firmware supports three ways to read moisture sensors:

| Method | How it works | Best for |
|---|---|---|
| **Direct ADC** | Sensor → ESP32 GPIO pin | 1–8 sensors on Hub, 1 on Remote |
| **ADS1115** | Sensor → ADS1115 → I2C → ESP32 | More stable readings; up to 4 per module, up to 4 modules |
| **MUX (CD74HC4067)** | Sensor → MUX → one ADC pin | Up to 16 sensors on one Hub ADC input |

For a basic setup, **Direct ADC is simplest** — no extra hardware. If you want more reliable readings or more than 8 sensors, add an ADS1115.

All capacitive moisture sensors have three wires: **VCC (3.3 V or 5 V)**, **GND**, and **AOUT (analog signal)**. The AOUT wire is what you connect to an ADC input.

---

### 2.1 Hub

**Board:** ESP32-WROOM-32 (38-pin DevKit)

#### Power
Connect to a 5V USB power supply via the Micro-USB port. The board's onboard regulator supplies 3.3 V to sensors.

#### Moisture sensors — Direct ADC (up to 8)

| Sensor | Sensor pin | ESP32 pin | Notes |
|---|---|---|---|
| Sensor 1 | AOUT | GPIO 32 | ADC1_CH4 |
| Sensor 2 | AOUT | GPIO 33 | ADC1_CH5 |
| Sensor 3 | AOUT | GPIO 34 | ADC1_CH6 — **input only** |
| Sensor 4 | AOUT | GPIO 35 | ADC1_CH7 — **input only** |
| Sensor 5 | AOUT | GPIO 36 | ADC1_CH0 — **input only** |
| Sensor 6 | AOUT | GPIO 37 | ADC1_CH1 — **input only** |
| Sensor 7 | AOUT | GPIO 38 | ADC1_CH2 — **input only** |
| Sensor 8 | AOUT | GPIO 39 | ADC1_CH3 — **input only** |
| All sensors | VCC | 3.3V | |
| All sensors | GND | GND | |

> Pins 34–39 are **input-only** — they have no internal pull-up/down and cannot be driven as outputs. This is fine for analog sensor signals.

#### Moisture sensors — ADS1115 (sensors 9–16, or as a replacement for Direct ADC)

Wire the ADS1115 module to the Hub's I2C bus:

| ADS1115 pin | ESP32 pin |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SCL | GPIO 22 |
| SDA | GPIO 21 |
| ADDR | GND (address 0x48) |

Then connect sensor AOUT wires to ADS1115's **A0–A3** inputs.

To add a second ADS1115 (sensors 13–16), tie its ADDR pin to VCC (gives address 0x49) and connect it to the same I2C bus.

#### MUX (CD74HC4067) — 16 sensors via one ADC pin

| MUX pin | ESP32 pin |
|---|---|
| SIG / COM | GPIO 33 (ADC) |
| S0 | GPIO 14 |
| S1 | GPIO 27 |
| S2 | GPIO 26 |
| S3 | GPIO 25 |
| EN | GND (always enabled) |
| VCC | 3.3V |
| GND | GND |

Connect sensor AOUT wires to MUX channels **C0–C15**.

---

### 2.2 Remote Sensor Node

**Board:** ESP32-C3 SuperMini

The Remote is a single-sensor node. It connects to your WiFi and reports to the Hub.

#### Moisture sensor (single, direct ADC)

| Sensor pin | ESP32-C3 pin |
|---|---|
| AOUT | GPIO 0 |
| VCC | 3.3V |
| GND | GND |

#### Battery operation (optional)

The Remote can run on a 3.7V LiPo battery for cord-free placement.

| Connection | Detail |
|---|---|
| Battery +/- | To TP4056 BAT+/BAT- |
| TP4056 OUT+ | To SuperMini 5V pin |
| TP4056 OUT- | To SuperMini GND |
| Power-detect wire | GPIO 6 — connect HIGH (+3.3V) when USB is plugged in |

> When USB power is detected on GPIO 6, the Remote stays awake continuously. On battery, it deep-sleeps between readings.

To wake the Remote manually (e.g. to enter config mode): press the button on **GPIO 5**, or connect a momentary switch between GPIO 5 and GND.

---

### 2.3 Greenhouse Controller

**Board:** ESP32-WROOM-32 (38-pin DevKit)

#### Relay module

Most 4- and 8-channel relay modules have this layout per channel:

```
[IN1] [IN2] [IN3] [IN4]    ← signal wires from ESP32
[VCC] [GND]                ← 5V power for relay coils
[JD-VCC / VCC jumper]      ← leave jumper in place for shared power
```

| Relay module pin | ESP32 pin | Notes |
|---|---|---|
| IN1 | GPIO 16 | Relay 1 |
| IN2 | GPIO 17 | Relay 2 |
| IN3 | GPIO 18 | Relay 3 |
| IN4 | GPIO 19 | Relay 4 |
| IN5 | GPIO 23 | Relay 5 |
| IN6 | GPIO 25 | Relay 6 |
| IN7 | GPIO 26 | Relay 7 |
| IN8 | GPIO 27 | Relay 8 |
| VCC | 5V (USB) | Relay coils need 5V |
| GND | GND | |

> The firmware defaults to **active-low** relay control, which is correct for virtually all common relay modules (the relay closes when the input is pulled LOW).

#### Pump / valve wiring

```
[12V supply +] → [Relay COM]
[Relay NO]     → [Pump/valve +]
[Pump/valve -] → [12V supply -]
```

When the relay closes, current flows through the pump. **Never run mains voltage (120V/240V AC) through a relay unless you know exactly what you're doing — use low-voltage DC pumps for a safe first build.**

#### Environmental sensor — DHT22 (temperature + humidity)

| DHT22 pin | ESP32 pin |
|---|---|
| VCC | 3.3V |
| DATA | GPIO 4 |
| GND | GND |

> Add a 10 kΩ pull-up resistor between DATA and VCC if readings are unstable. Many DHT22 breakout boards include this already.

#### Environmental sensor — SHT31 (alternative, I2C)

| SHT31 pin | ESP32 pin |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SCL | GPIO 22 |
| SDA | GPIO 21 |

The SHT31 has I2C address **0x44** by default (ADDR pin floating or tied to GND).

---

## 3. User Guide

### 3.1 Flashing Firmware

No programming tools required. The web installer handles everything.

1. Open **Google Chrome** or **Microsoft Edge** (Firefox and Safari do not support Web Serial)
2. Go to **https://spiritwrestler.ca/plants/**
3. Plug your ESP32 board into a USB port
4. Click the tab for your device type (Hub, Remote, or Greenhouse)
5. Click **Install**, select your COM port when prompted, and wait for the flash to complete (~30 seconds)
6. The device will reboot automatically when done

> **ESP32-C3 SuperMini note:** If the port doesn't appear, hold the BOOT button on the board, press and release RESET, then release BOOT. This forces it into download mode.

---

### 3.2 Hub Setup

#### First boot

On first boot (or after a factory reset) the Hub has no WiFi credentials. It will:
- Create a WiFi hotspot called `IWMP-Hub-XXXXXX` (where XXXXXX is part of its MAC address)
- Open a captive portal automatically on most phones

Connect to that network. Your phone should redirect you to the setup page. If it doesn't, open a browser and go to **http://192.168.4.1/settings/wifi**.

Enter your home WiFi SSID and password, tap Save. The Hub will reboot, connect to your network, and get an IP from your router. Find that IP in your router's client list, or check the serial monitor at 115200 baud.

#### Web interface

Once connected, open **http://[hub-ip]/** in any browser.

| Page | What it does |
|---|---|
| **Dashboard** | Live moisture readings for all enabled sensors; Remote device status |
| **Sensors** | Enable/disable sensors, set input type, configure ADS1115 channel, set warning threshold |
| **Devices** | Remote nodes that have reported to this Hub |
| **Settings** | WiFi, MQTT, sensor polling interval, Remote pairing config export |

#### Adding sensors

1. Go to **Sensors**
2. Click a sensor slot (Plant 1, Plant 2, etc.)
3. Set **Input Type**: Direct ADC, ADS1115, or MUX
4. Set the GPIO pin / ADS channel / MUX channel to match your wiring
5. Give it a name
6. Enable it
7. Save

#### Sensor polling

The Hub reads sensors on a background timer — it does **not** block while reading, and it averages 5 samples per sensor. The default interval is 60 seconds. You can change this in **Settings → Sensor Polling**, from 5 seconds (testing) up to 24 hours.

Click **Take Reading** on the Dashboard or in Settings to trigger an immediate read.

---

### 3.3 Remote Sensor Node Setup

#### First boot

The Remote also creates a hotspot on first boot: `IWMP-Remote-XXXXXX`. Connect to it. The captive portal will ask for:

- **WiFi network** — your home WiFi
- **Hub Address** — the IP address of your Hub (e.g. `192.168.1.100`)

After saving, the Remote reboots, joins your WiFi, and starts reporting moisture readings to the Hub every 60 seconds (configurable).

#### Accessing the Remote after setup

Once on WiFi, the Remote is reachable at **`http://iwmp-remote-XXXXXX.local`** (mDNS). Replace XXXXXX with the same suffix as the hotspot name.

If mDNS doesn't work on your network, check your router's client list for the IP.

#### Battery mode

To enable battery operation:
1. Open the Remote's web interface
2. Go to **Settings**
3. Enable **Battery Powered**
4. Set **Deep Sleep Duration** (default: 5 minutes)
5. Save and reboot

In battery mode, the Remote wakes up, takes a reading, sends it to the Hub, and goes back to sleep. Each wake cycle takes about 2–3 seconds.

---

### 3.4 Greenhouse Controller Setup

#### First boot

Same process as the Hub — connects to `IWMP-GH-XXXXXX` hotspot, configure WiFi via captive portal.

#### Web interface

| Page | What it does |
|---|---|
| **Dashboard** | Temperature, humidity, relay states with ON/OFF buttons, automation toggle |
| **Relays** | Per-relay control with timed-ON, emergency stop |
| **Settings** | WiFi, MQTT, reboot/reset |

#### Enabling relays

Relays are disabled by default. To enable one:
1. Go to **Settings → WiFi/MQTT** (relay config is in the MQTT settings section for now — TODO: dedicated page)
2. Or use the API: `POST /api/config/relays` with the relay config JSON

For a first test, use the **Relay Control** page to manually turn relays on and off.

#### Safety limits (all relays)

| Setting | Default | Purpose |
|---|---|---|
| Max on time | 5 minutes | Automatically turns off if left on too long |
| Min off time | 1 minute | Prevents immediate re-activation |
| Cooldown | 5 minutes | Minimum gap between activations |

These prevent a pump from running dry or a valve from flooding a plant.

#### Environmental sensor

After wiring your DHT22 or SHT31:
1. The Greenhouse dashboard will show **--** until the sensor is configured
2. Set the sensor type in **Settings → MQTT Settings** (sensor type field) — TODO: this will move to a dedicated env sensor config page

---

### 3.5 Home Assistant / MQTT

The Hub and Greenhouse both support MQTT with **Home Assistant auto-discovery**. When enabled, entities appear in HA automatically — no manual YAML needed.

#### Setup

1. In the Hub or Greenhouse web interface, go to **Settings → MQTT Settings**
2. Enter your broker's hostname or IP, port (default 1883), and credentials if required
3. Leave **HA Discovery** enabled
4. Save — the device will connect and publish discovery messages within ~10 seconds

#### What appears in Home Assistant

**Hub:**
- One `sensor` entity per enabled moisture sensor (moisture %, raw value)
- Device tracker / availability sensor

**Greenhouse:**
- Temperature sensor (°C)
- Humidity sensor (%)
- One `switch` entity per enabled relay
- Relay state updates in real-time

#### MQTT topic structure

```
iwetmyplants/{device_id}/sensors/{n}/moisture    ← moisture %
iwetmyplants/{device_id}/relays/{n}/state        ← ON/OFF
iwetmyplants/{device_id}/environment/temperature
iwetmyplants/{device_id}/environment/humidity
iwetmyplants/{device_id}/status                  ← online/offline
```

---

### 3.6 Calibrating Moisture Sensors

Raw ADC readings vary between sensors, soil types, and pots. Calibration maps "dry air" and "fully saturated soil" to 0% and 100%.

**How to calibrate:**

1. On the Hub, go to **Sensors**, find the sensor you want to calibrate, click **Calibrate**
2. A live readout starts updating every second (WebSocket)
3. Hold the sensor **in open air** — click **Set Dry Point**
4. Submerge the sensor tip fully in water — click **Set Wet Point**
5. Click **Save** — calibration is stored in flash and survives reboots

**Tips:**
- Let the reading stabilize (2–3 seconds) before setting each point
- For best accuracy: set the wet point in moist soil at field capacity, not in standing water
- Re-calibrate if you change the sensor or move to a very different soil type

**Default calibration values (if you skip calibration):**

| Input type | Dry value | Wet value |
|---|---|---|
| Direct ADC (12-bit) | 3500 | 1500 |
| ADS1115 (16-bit) | 45000 | 18000 |

---

## 4. Troubleshooting & FAQ

### Q: The installer says "Failed to initialize" or the port doesn't appear

**Check:**
- You're using Chrome or Edge (not Firefox or Safari)
- The USB cable supports data, not just charging (swap cables and try again)
- On Windows: check Device Manager for a "USB Serial" or "Silicon Labs CP210x" device. If it shows with a yellow warning, install the [CP2102 driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
- For **ESP32-C3 SuperMini**: hold BOOT, tap RESET, release BOOT to force download mode

---

### Q: The Hub hotspot appeared but my phone won't open the portal

- On Android: tap the notification that says "Sign in to network"
- On iOS: a Safari popup should appear automatically within ~5 seconds. If not, open Safari and go to **http://192.168.4.1**
- On Windows: click the notification in the system tray
- If nothing works, connect manually and navigate to **http://192.168.4.1/settings/wifi**

---

### Q: The Hub connected to WiFi but I can't find its IP

- Check your router's "connected devices" or "DHCP leases" list — look for a device named `iWetMyPlants Hub` or with a MAC starting with the same prefix as the hotspot name
- Open a serial monitor (115200 baud) right after boot — the Hub prints its IP to the serial console
- Set a static IP in **Settings → WiFi Settings** so it's always the same

---

### Q: Sensor readings look wrong or jump around a lot

- **Capacitive sensors on GPIO 34–39 (input-only pins):** these pins float when the sensor isn't plugged in. That's normal — the reading becomes meaningful once a sensor is connected
- **Very noisy readings:** try an ADS1115 instead of direct ADC. The ESP32's onboard ADC is notoriously noisy and non-linear above ~3V
- **Reading stuck at 0 or max:** check the AOUT wire is connected, and that the sensor has power (LED should be lit on most modules)
- **Sensor reads 100% in dry soil:** calibration values are inverted — the dry value is lower than the wet value. Re-calibrate (dry in air first, then wet in water)

---

### Q: The Greenhouse relay won't turn on

- Check that the relay is **enabled** in the relay configuration (disabled by default)
- Confirm the GPIO pin matches your wiring
- The relay module needs **5V on its VCC pin**, not 3.3V — the signal wires are 3.3V (which is fine), but the coils need 5V
- Most modules have a **JD-VCC jumper** that bridges coil power to the signal power rail. Leave it in for a basic setup

---

### Q: The Hub reboots every few hours

- This was a known issue (heap fragmentation from WebSocket allocations) — fixed in v1.0.0
- If still occurring: check free heap on the Dashboard. Below ~20 KB is a warning sign. Try reducing the number of enabled sensors or increasing the poll interval

---

### Q: Remote isn't showing up on the Hub's Devices page

- Confirm both Hub and Remote are on the same WiFi network and that the Remote's **Hub Address** setting matches the Hub's IP
- The Remote reports every 5 minutes by default — wait a full cycle after configuring
- Check the Remote's web interface to confirm it shows "Connected" status

---

### Q: Home Assistant entities appear but show "unavailable"

- The device publishes an **availability topic** — if it goes offline (power cut, WiFi drop), HA marks entities unavailable. This is correct behaviour
- If entities never become available: check the MQTT broker is reachable, and that the base topic in both HA's MQTT integration and the device match (`iwetmyplants` by default)
- Enable MQTT logging in HA (`logger: mqtt: debug` in `configuration.yaml`) to see what's being published

---

### Q: Can I run multiple Hubs?

Yes. Each Hub is independent. Remotes report to one Hub at a time (set by **Hub Address** in the Remote's config). Each Hub publishes to MQTT under its own device ID, so they won't conflict.

---

### Q: Is my WiFi password stored securely?

Credentials are stored in the ESP32's **NVS (Non-Volatile Storage)** — a dedicated flash partition. It's not encrypted at rest (ESP32 secure boot/flash encryption is not enabled in this firmware). For home use this is standard practice. Don't deploy in a shared/public environment without understanding this.

---

### Q: How do I factory reset a device?

- **Via web UI:** Settings → Factory Reset (the only method — there is no hardware button hold for factory reset)

---

*Photos and wiring diagrams will be added to the HTML version of this guide.*
