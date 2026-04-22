# iWetMyPlants — UI Revision Prompts

**Purpose:** These are specific, actionable changes to the web UI codebase identified during user manual documentation review. Each item describes what needs to change and why. Pass these to Claude Code or implement them manually.

---

## Priority 1 — Relay Configuration Needs Its Own Page (Greenhouse)

**Current state:** Relay enable/disable and per-relay settings (name, GPIO pin, max on time, min off time, cooldown) are buried inside the MQTT settings section on the Greenhouse Settings page.

**Problem:** Users looking to enable a relay have no reason to look in MQTT settings. The manual currently has to say "Go to Settings → WiFi/MQTT (relay config is in the MQTT settings section for now)" which is confusing and suggests the product isn't finished.

**Requested change:** Create a dedicated **Relays** configuration page (or a distinct section/tab on the existing Settings page) for the Greenhouse Controller that includes:
- Per-relay enable/disable toggle
- GPIO pin assignment
- Friendly name field
- Max on time (seconds)
- Min off time (seconds)  
- Cooldown (seconds)
- Active-low toggle (default: on)

This page should be accessible from the main navigation alongside Dashboard, Relays (control), and Settings. The existing Relay Control page (the one with ON/OFF buttons and timed-ON) should remain separate — that's the "use it" page; this new page is the "set it up" page.

**API:** The existing `POST /api/config/relays` endpoint should continue to work. The new page just needs a UI that reads from `GET /api/config/relays` and writes back to `POST /api/config/relays`.

---

## Priority 2 — Environmental Sensor Type Selection Needs Its Own Section (Greenhouse)

**Current state:** The environmental sensor type (DHT11, DHT22, SHT30, SHT31, SHT40, SHT41, or None) is configured somewhere in the MQTT settings section.

**Problem:** Same issue as relay config — a user who just wired a DHT22 and wants to tell the firmware about it shouldn't have to navigate to MQTT settings. This is a hardware configuration concern, not a communication one.

**Requested change:** Add an **Environmental Sensor** configuration section to the Greenhouse settings (could be on the proposed Relays config page, or a separate "Hardware" settings tab). It should include:
- Sensor type dropdown (None / DHT11 / DHT22 / SHT30 / SHT31 / SHT40 / SHT41)
- GPIO pin field (for DHT types — default GPIO 4)
- I2C address field (for SHT types — default 0x44)
- Enable/disable toggle
- Current reading display showing live temp/humidity once configured (confirming it works)

**API:** Use the existing `GET/POST /api/config/env` or equivalent endpoint from `ApiEndpoints`.

---

## Priority 3 — Sensor Calibration UI Clarity Improvements (Hub)

**Current state:** The calibration flow works (WebSocket live readout, Set Dry Point, Set Wet Point, Save), but the UI could better guide a novice user.

**Requested changes:**
1. Add a brief inline instruction blurb at the top of the calibration modal/page: "Hold the sensor in open air for the dry point, then submerge the probe in water for the wet point. Wait 2–3 seconds for the reading to stabilize before clicking each button."
2. Show the current raw value, the current dry cal value, and the current wet cal value side by side so the user can see what they're changing.
3. After both points are set, show a preview of what the calculated moisture percentage would be at the current raw reading before the user clicks Save. This gives immediate confirmation that the calibration makes sense.
4. Add a "Reset to Defaults" button that restores the factory calibration values (3500/1500 for Direct ADC, 45000/18000 for ADS1115).

---

## Priority 4 — Hub Dashboard Should Show Remote Battery Status

**Current state:** The Hub Dashboard shows moisture readings from Remote nodes that have reported in, and the Devices page shows which Remotes are online.

**Problem:** Battery-powered Remotes send battery voltage and percentage in their reports (`BATTERY_STATUS` message type, and battery data is included in `MULTI_SENSOR_READING`). This data is cached in `DeviceRegistry` (`battery_percent` field in `PairedDeviceInfo`). However, there's no visible battery indicator on the Dashboard or Devices page for the end user.

**Requested change:** On the Hub's Devices page, add a battery indicator for each device that has reported battery data:
- Show battery percentage with a simple icon (full/three-quarter/half/quarter/empty/charging)
- If `charging` flag is true, show a charging indicator
- If battery percent < 20%, use a warning color (red/orange)

This is purely a display change — the data already exists in the device registry cache.

---

## Priority 5 — WiFi Signal Strength Display

**Current state:** RSSI values are transmitted in ESP-NOW messages (in the `HEARTBEAT` and `ANNOUNCE` message types) and stored in `PairedDeviceInfo`. The Hub's own WiFi RSSI is available from the WiFi stack.

**Requested change:** 
- On the Hub Dashboard or Settings page, show the Hub's own WiFi signal strength (bars or dBm).
- On the Devices page, show the last-reported RSSI for each paired device. This helps users troubleshoot range issues with Remote nodes.

---

## Priority 6 — Navigation Consistency Across Device Types

**Current state:** The Hub has Dashboard / Sensors / Devices / Settings. The Greenhouse has Dashboard / Relays / Settings. The Remote has a completely different lightweight web UI (`RemoteWeb`).

**Requested changes:**
- Ensure the Greenhouse navigation order is: **Dashboard → Sensors → Relays → Settings** (add a Sensors page to the Greenhouse if one doesn't exist, since the Greenhouse supports up to 4 moisture sensors).
- On the Hub and Greenhouse, add a small "System Info" link or section (firmware version, device ID, uptime, free heap, WiFi RSSI) accessible from the Settings page. This information is already available via `GET /api/system/info` — it just needs a UI element.
- The Remote's `RemoteWeb` is intentionally lightweight, so no structural changes are needed there. But if feasible, add the firmware version string somewhere visible on the Remote's status page.

---

## Priority 7 — Static IP Configuration UI

**Current state:** The BETA_GUIDE recommends setting a static IP in "Settings → WiFi Settings" so the Hub's address doesn't change, but it's unclear whether this field is actually exposed in the current WiFi settings UI.

**Requested change:** Verify that the WiFi settings page includes fields for:
- Static IP toggle (enable/disable — default: off/DHCP)
- IP address
- Subnet mask
- Gateway
- DNS server

These fields exist in `WifiConfig` in the config schema. If they're not yet in the UI, add them. If they are, no action needed — just confirm.

---

## Low Priority — Cosmetic / Polish

### Favicon
Add a simple favicon to the web UI. A small plant/leaf icon in green would be on-brand. Even a simple SVG inline favicon works.

### Page Title
Set the browser tab title to include the device type and name: e.g., "iWetMyPlants Hub — Dashboard" or "IWMP-GH-A1B2C3 — Relays". Currently it may just show "iWetMyPlants" or the IP address.

### Sensor Names in Dashboard Cards
If a sensor has been given a custom name (e.g., "Kitchen Basil"), display that name prominently on the Dashboard card rather than "Plant 1" or "Sensor 1". Fall back to the slot number only if no name is set.

---

## Notes for Implementation

- The architecture doc confirms that all `/api/config/{section}` endpoints already exist and accept GET/POST. Most of these UI changes are purely frontend work — adding forms that read/write to existing API endpoints.
- The Greenhouse no longer hosts a local automation engine (rescoped 2026-04). Relay decisions originate from Hub via ESP-NOW or Home Assistant via MQTT. No threshold-binding UI is needed on the Greenhouse — the existing relay-state controls are sufficient.
- The `RemoteWeb` is intentionally minimal (~10KB PROGMEM HTML) to fit on the ESP32-C3. Any changes to the Remote's UI should be mindful of flash budget.
