# iWetMyPlants 

**Free, open-source ESP32 plant monitoring and automated watering — no cloud, no subscriptions, no ongoing costs.**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![GitHub release](https://img.shields.io/github/v/release/SpiritWrestlerSolutions/iWetMyPlants)](https://github.com/SpiritWrestlerSolutions/iWetMyPlants/releases)
[![Project Status: Beta](https://img.shields.io/badge/status-beta-orange)](https://github.com/SpiritWrestlerSolutions/iWetMyPlants)
[![Made in Alberta](https://img.shields.io/badge/made%20in-Alberta%2C%20Canada-red)](https://github.com/SpiritWrestlerSolutions/iWetMyPlants)

---

![iWetMyPlants Banner](docs/banner.webp)

A complete, self-hosted plant monitoring network built around the ESP32. Monitor soil moisture, automate watering, and integrate with Home Assistant — all with hardware you can source for **under $20**.

** [Flash your device at spiritwrestler.ca/plants](https://spiritwrestler.ca/plants)**

---

## What is iWetMyPlants?

iWetMyPlants is a network of ESP32-based devices that monitor soil moisture and automate watering. No vendor lock-in, no monthly fees, no cloud required. Your plant data lives on your network.

The system has three device roles:

| Device | Role | Hardware | Power |
|--------|------|----------|-------|
| **Hub** | Central coordinator, MQTT/HA bridge | ESP32 WROOM or S3 | Mains |
| **Remote** | Battery-powered single-sensor node | ESP32-C3 SuperMini | Battery |
| **Greenhouse** | Multi-sensor + relay controller | ESP32 WROOM or S3 | Mains |

Remotes communicate with the Hub over **ESP-NOW** — a fast, low-power protocol that works independently of your WiFi router. The Hub bridges everything to **MQTT** and **Home Assistant** via auto-discovery.

---

## Features

- **Capacitive soil moisture sensors** — accurate and corrosion-resistant
- **ESP-NOW mesh** — sensors talk to the Hub without needing your WiFi router
- **Home Assistant integration** — auto-discovers on your network via MQTT
- **Deep sleep on Remotes** — months of battery life between charges
- **Greenhouse control** — up to 4 sensors + 4 relays with built-in automation
- **Web flasher** — flash firmware directly from your browser, no tools required
- **OTA updates** — update firmware over-the-air from the installer page
- **Under $20** for a full 8-sensor hub setup
- **Fully open source** — GPL v3, no proprietary components

---

## Quick Start

The fastest path to running hardware:

1. **Grab an ESP32** — see [Hardware Requirements](#hardware-requirements) below
2. **Open the web installer** at [spiritwrestler.ca/plants](https://spiritwrestler.ca/plants) in Chrome or Edge
3. **Select your device type** (Hub, Remote, or Greenhouse) and click **Connect & Install**
4. **Configure WiFi** when prompted
5. **Follow the [Quick Start Guide](https://spiritwrestler.ca/plants/documentation/quick-start.html)**

> **Early Beta** — This project is under active development. Bugs are expected and breaking changes may occur between releases.

---

## Hardware Requirements

### Hub / Greenhouse
- ESP32 WROOM-32 or ESP32-S3 development board
- USB cable (for initial flashing)
- Capacitive soil moisture sensor(s) — e.g., [this type](docs/images/sensor-capacitive.jpg)
- 5V power supply (after initial setup)

### Remote
- ESP32-C3 SuperMini
- 1× capacitive soil moisture sensor
- 3.7V LiPo battery (1000–2000 mAh recommended)
- USB-C cable (for initial flashing)

### Sensors
iWetMyPlants uses **capacitive** moisture sensors (not resistive). Resistive sensors corrode quickly in soil. The capacitive type typically have a black PCB and outputs an analog voltage. They're widely available for a few dollars each.

---

## Documentation

| Guide | Description |
|-------|-------------|
| [Quick Start Guide](https://spiritwrestler.ca/plants/documentation/quick-start.html) | Get up and running in 15 minutes |
| [User Guide — Hub & Remote](https://spiritwrestler.ca/plants/documentation/user-guide.html) | Full reference for Hub and Remote configuration |
| [Greenhouse User Guide](https://spiritwrestler.ca/plants/documentation/user-guide-greenhouse.html) | Greenhouse-specific setup and automation |
| [Wiring Guide](https://spiritwrestler.ca/plants/documentation/wiring-guide.html) | Pinouts, diagrams, and connection methods |
| [Troubleshooting Guide](https://spiritwrestler.ca/plants/documentation/troubleshooting.html) | Diagnosing common issues |

---

## Repository Structure

```
iWetMyPlants/
├── firmware/
│ ├── hub/ # Hub firmware source
│ ├── remote/ # Remote firmware source
│ └── greenhouse/ # Greenhouse firmware source
├── installer/
│ ├── index.html # Web flasher / installer page
│ └── documentation/ # User-facing HTML docs
├── hardware/
│ ├── schematics/ # Wiring diagrams and schematics
│ └── bom/ # Bills of materials
├── docs/
│ ├── images/ # Screenshots and photos used in docs
│ └── CHANGELOG.md # Version history
├── .github/
│ └── ISSUE_TEMPLATE/ # Bug report and feature request templates
├── CONTRIBUTING.md
├── CODE_OF_CONDUCT.md
└── README.md
```

---

## Home Assistant Integration

iWetMyPlants integrates with Home Assistant via MQTT with auto-discovery. Once your Hub is connected and MQTT is configured:

1. Open Home Assistant → **Settings → Devices & Services → MQTT**
2. The Hub and all paired sensors appear automatically
3. Moisture readings, battery levels, and relay states publish in real time

No YAML configuration required.

---

## Contributing

This is a one-person project built alongside a full-time job and a family. All contributions are genuinely welcome.

- **Bug reports** — Use the [issue tracker](https://github.com/SpiritWrestlerSolutions/iWetMyPlants/issues) or the feedback button on the installer page
- **Feature requests** — Open an issue with the `enhancement` label
- **Pull requests** — See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines
- **Documentation** — Corrections and improvements are always appreciated
- **Hardware testing** — Especially alternative ESP32 boards and sensor types

---

## Support the Project

iWetMyPlants is and will always be free. If it's saved your plants (or your sanity), consider:

- **Star this repo** — it helps others find the project
- **[Buy me a coffee on Ko-fi](https://ko-fi.com/iwetmyplants)** — hardware and hosting cost real money
- **File a bug report** — clear reports are genuinely valuable
- **Tell someone** — word of mouth is how open source projects grow

---

## License

iWetMyPlants is released under the **GNU General Public License v3.0**.

You are free to use, modify, and distribute this software under the terms of the GPL v3. See [LICENSE](LICENSE) for the full text.

---

*Made with in rural Alberta, Canada.*
*Built by a paramedic who got tired of watching his houseplants die.*
