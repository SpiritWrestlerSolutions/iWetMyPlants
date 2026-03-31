# Changelog

All notable changes to iWetMyPlants are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Note:** The changelog shown on the installer page is pulled from `version.json` and may contain more granular entries than this file. This file is the canonical human-readable record.

---

## [Unreleased]

### In Progress
- Multi-sensor support for Remote nodes (2–4 sensors via direct ADC)
- Remote + ADS1115 support for expanded sensor count
- Greenhouse Manager web interface improvements

---

## [1.0.0] — Initial Beta Release

### Added
- Hub firmware: WiFi provisioning, ESP-NOW coordinator, MQTT bridge, Home Assistant auto-discovery
- Remote firmware: deep-sleep battery sensor node, ESP-NOW reporting to Hub
- Greenhouse firmware: multi-sensor monitoring, relay control, built-in automation rules
- Web installer: browser-based flashing via ESP Web Tools (Chrome/Edge)
- OTA update support via the installer page
- Feedback/bug report form (Formspree)
- Full documentation suite:
 - Quick Start Guide
 - User Guide (Hub & Remote)
 - Greenhouse User Guide
 - Wiring Guide
 - Troubleshooting Guide

---

[Unreleased]: https://github.com/SpiritWrestlerSolutions/iWetMyPlants/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/SpiritWrestlerSolutions/iWetMyPlants/releases/tag/v1.0.0
