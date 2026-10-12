# Hardware and wiring

## Port labels

Every board, doc, silkscreen and the web page use the same names. Wire to these defaults and the
unit works without changing a single pin in Settings.

| Label | What it is | Firmware slot |
|---|---|---|
| **S1–S6** | Moisture sensor inputs (analog) | Sensor 1–6 |
| **R1–R8** | Relay outputs | Relay 1–8 |
| **SDA / SCL** | Temperature/humidity sensor (SHT3x or SHT4x, I2C) | "Temperature sensor" |
| **SPWR** | Switched power for the moisture sensors (optional) | "Sensor power pin" |
| **BAT** | Battery voltage input (battery units only) | "Battery pin" |

## ESP32 DevKit (WROOM-32)

### Pin map

| Label | GPIO | Notes |
|---|---|---|
| S1 | 36 (VP) | ADC1_CH0, input only |
| S2 | 39 (VN) | ADC1_CH3, input only |
| S3 | 34 | ADC1_CH6, input only |
| S4 | 35 | ADC1_CH7, input only |
| S5 | 32 | ADC1_CH4. Can be a relay or SPWR pin instead if S5 isn't used |
| S6 | 33 | ADC1_CH5. Can be a relay or SPWR pin instead if S6 isn't used |
| R1 | 16 | |
| R2 | 17 | |
| R3 | 18 | |
| R4 | 19 | |
| R5 | 23 | |
| R6 | 25 | |
| R7 | 26 | |
| R8 | 27 | |
| SDA | 21 | |
| SCL | 22 | |
| SPWR | 13 | Optional; set "Sensor power pin" to 13 in Settings |
| spare | 4 | Free output |
| BOOT | 0 | On-board button. Hold 10 s while running to factory reset |
| BAT | — | Not available: all six analog pins are sensor ports |

**Never wire to:** 1 and 3 (USB serial), 6–11 (flash), 2, 5, 12, 14, 15 (boot-mode pins, or they
toggle during boot). GPIO 16/17 are fine on WROOM-32 but not on WROVER boards.

### Where the pins are

38-pin ESP32-DevKitC, antenna at the top, USB at the bottom. The common 30-pin DOIT board has every
pin used here in the same order. Clone boards sometimes differ, so check yours against its
silkscreen before laying out a PCB.

```
                        ┌───────────────┐
                        │    antenna    │
          3V3 ●─────────┤               ├─────────● GND
           EN ●         │               │         ● 23   R5
   S1  VP/36  ●─────────┤               ├─────────● 22   SCL
   S2  VN/39  ●─────────┤               │         ● 1    (USB serial)
   S3     34  ●─────────┤               │         ● 3    (USB serial)
   S4     35  ●─────────┤     ESP32     ├─────────● 21   SDA
   S5     32  ●─────────┤   WROOM-32    ├─────────● GND
   S6     33  ●─────────┤               ├─────────● 19   R4
   R6     25  ●─────────┤               ├─────────● 18   R3
   R7     26  ●─────────┤               │         ● 5
   R8     27  ●─────────┤               ├─────────● 17   R2
          14  ●         │               ├─────────● 16   R1
          12  ●         │               │         ● 4    spare
          GND ●─────────┤               │         ● 0    BOOT
   SPWR   13  ●─────────┤               │         ● 2
              ●         │               │         ● 15
              ●         │               │         ●
              ●         │               │         ●
           5V ●─────────┤     [USB]     │         ●
                        └───────────────┘
```

The six sensor inputs sit together at the top left, so the sensor ports can be one straight row.

### Sensor ports S1–S6

One 3-pin port per sensor. Match the order to your sensor's connector so the cable runs straight
through (check the sensor's silkscreen; most read `AOUT VCC GND`).

```
  port pin   to
  SIG   ───┬───── GPIO (S1 = 36 … S6 = 33)
           ┴ 100 nF ceramic, close to the ESP32 pin (Espressif's advice for quiet ADC readings)
           ┬
  GND   ───┴───── GND
  V+    ───────── 3V3 (or the SPWR rail, below)
```

- Power capacitive sensors from **3V3, never 5V**, so their output can't exceed what the pin takes.
- For a PCB, add an empty footprint for a pull-down resistor from SIG to GND on each port. It may
  help the firmware spot an unplugged sensor; that gets tested in M2 before it's fitted.

### Relay outputs R1–R8

One 10-pin header in the same order as a 4- or 8-channel relay module's input header, so a
straight ribbon cable connects them:

```
  header:   GND  R1  R2  R3  R4  R5  R6  R7  R8  VCC
  ESP32:    GND  16  17  18  19  23  25  26  27  3V3
```

On the relay module:
- **VCC → 3V3** (the input side), **JD-VCC → 5V** (the coils), and **remove the JD-VCC jumper**.
  With the jumper in and VCC at 5V, the ESP32's 3.3V "off" can leave a relay half on.
- The firmware treats relays as **active-low** by default, which suits these modules.
- Each coil draws about 70 mA. The DevKit's 5V pin (USB power) can carry one or two at a time; for
  more, give JD-VCC its own 5V supply.
- Pumps and valves get their own supply. Never route their current through the ESP32 board.

**For the test rig, LEDs can stand in for relays.** Same active-low behaviour: the LED lights when the
firmware switches that relay on.

```
  3V3 ──[1 kΩ]──▶|── GPIO (R1 = 16 … R8 = 27)        (LED: anode toward 3V3)
```

For a PCB, add 10 kΩ pull-ups from each R line to 3V3, so every relay input reads "off" from power-on
until the firmware takes over.

### Temperature/humidity port

4 pins in the Qwiic / STEMMA QT order, so SparkFun and Adafruit SHT4x boards plug straight in:

```
  1 GND   ── GND
  2 3V3   ── 3V3
  3 SDA   ── GPIO 21
  4 SCL   ── GPIO 22
```

Most breakout boards already have the 4.7 kΩ pull-ups on SDA and SCL. Add them only if yours
doesn't. Keep the cable under about 30 cm; I2C isn't made for long runs.

### SPWR: switched sensor power (optional)

Mains units can leave the sensors on 3V3 all the time. SPWR is for powering them only while reading.
Six sensors draw about 30 mA, more than one GPIO should carry, so it switches a P-MOSFET:

| Part | Connections |
|---|---|
| Q1 AO3401A (P-channel) | Source → 3V3, Drain → sensor V+ rail, Gate → net PG |
| R1 100 kΩ | PG → 3V3 (keeps Q1 off) |
| Q2 2N7002 (N-channel) | Drain → PG, Source → GND, Gate → GPIO 13 |
| R2 100 kΩ | GPIO 13 → GND (keeps Q2 off while the ESP32 boots or sleeps) |

GPIO 13 high powers the sensors. Low or floating, they're off.

### Test rig parts list

- ESP32-DevKitC (WROOM-32), 38-pin
- 6 × 3-pin headers and 6 × 100 nF ceramic capacitors (sensor ports)
- 1 × 10-pin header for a relay module, **or** 8 LEDs and 8 × 1 kΩ resistors
- 1 × 4-pin header (temperature/humidity)
- Optional SPWR: AO3401A, 2N7002, 2 × 100 kΩ
