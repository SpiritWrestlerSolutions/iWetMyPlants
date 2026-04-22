# Changelog

All notable changes to iWetMyPlants are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and this project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Note:** The changelog shown on the installer page is pulled from `version.json` and may contain more granular entries than this file. This file is the canonical human-readable record.

---

## [Unreleased]

### Security
- **Captive portal: WiFi SSID XSS hardened.** Network-list JS rewritten to use DOM construction (`createElement` + `textContent`) instead of `innerHTML` interpolation, blocking malicious AP names from executing as HTML. Applied to the Hub/Greenhouse WiFi settings page, the Remote captive portal, and the dormant `CaptivePortal` class.
- **`/api/espnow/export` no longer leaks the WiFi password by default.** The dashboard's "Copy Remote Config" button now opts in via `?include_secrets=1`; drive-by readers get only Hub IP/MAC/SSID. The new Devices-page pairing card never requests the secret.

### Stability
- **MQTT and WiFi reconnect with exponential backoff (5/10/20/40/60s ceiling), no give-up.** Replaces the previous 10-attempt cap on MQTT and the flat 5s WiFi retry — a broker or AP that comes back after hours of downtime now recovers automatically.
- **MQTT heap fragmentation fixed in callbacks and hot publishers.** `onMqttMessage` uses a stack buffer (heap fallback only for >256-byte payloads) instead of `new char[len+1]` per message; five hot-path publishers (`publishState`, `publishMoistureReading`, `publishEnvironmentalReading`, `publishBatteryStatus`, `publishRelayState`) use stack buffers instead of `String`. Same class of fix as the prior RapidRead/HubController conversions.
- **ESP-NOW RX mutex bounded at 50 ms** (was `portMAX_DELAY` — could deadlock the WiFi RX task).
- **`clearAllPeers()` capped at 32 iterations** to prevent runaway loops if the underlying iterator misbehaves.
- **Watchdog `setTimeout()` safe under feed-from-other-task races** (new `_changing_timeout` guard during deinit/reinit).
- **Config mutable accessors no longer cross-contaminate via shared dummy on out-of-range access** — dummies re-zero on each invalid call and log loudly.

### Greenhouse rescope (finalized)
- Greenhouse is now an explicit "pro add-on": environmental sensor reporter + relay executor only. No local moisture sensing, no autonomous automation. Relay decisions arrive from Hub via ESP-NOW or Home Assistant via MQTT.
- Deleted `automation_engine.{h,cpp}`. Removed dead relay-safety setters and the `_daily_limit[]` tracking. `IWMP_HAS_AUTOMATION` removed; `IWMP_MAX_BINDINGS=8` retained for NVS-layout compat.
- HA discovery actively removes legacy moisture entities at MQTT connect.

### UI / UX
- **New Devices page pairing card** — shows Hub IP and MAC with copy-to-clipboard buttons so users can bootstrap a new Remote without hunting for values.
- **Per-device "Unpair" button** with confirmation, wired to existing `DELETE /api/devices/{idx}`.
- **Sensor cards on the Hub dashboard now show an age pill** ("2m ago", red if >180s) and `valid: false` readings render as "no data" instead of misleadingly showing 0%. Backed by new `valid` and `age_sec` fields on `/api/sensors`.
- **Dashboard polling pauses while the tab is hidden** (one-line `setInterval` interceptor in the embedded JS).
- **Captive portal polish**: signal bars instead of raw RSSI, scan timeout with retry button, "Open network — leave blank" hint, success-page 30s countdown with `window.close()` attempt.

### Installer
- **Ping result tags the device version** with `↑ upgrade available` / `↓ newer than published` / `✓ up to date` so users can tell at a glance whether OTA-flashing the bundled `.bin` would be an upgrade, downgrade, or no-op.

### Developer hygiene
- `static_assert` on every packed config struct's size — silent ABI drift now fails the build instead of bricking in-field NVS blobs.
- Per-channel ADS1115 re-probe skip counter (was a `static` local, so one bad channel delayed re-probes on healthy siblings).
- Relay GPIO `digitalWrite` skipped if the cached state matches — no more re-asserting the line every loop tick.
- WiFi MAC-address string cached at first access (efuse read on every log line was waste).

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
