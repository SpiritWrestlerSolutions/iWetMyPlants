# iWetMyPlants - Claude Code Implementation Guide

## How to Use This Document with Claude Code

This guide explains how to effectively use the specification document with Claude Code in VS Code to implement the iWetMyPlants system while managing context limits efficiently.

---

## 1. Initial Project Setup Session

### Start with this prompt:

```
I'm implementing a plant monitoring system called iWetMyPlants. I have a detailed 
specification document. Let's start by setting up the project structure.

Please:
1. Create a PlatformIO project structure for ESP32
2. Set up the folder hierarchy from the spec (shared/, hub/, remote/, greenhouse/)
3. Create the platformio.ini with all three build environments
4. Create placeholder header files for the core components

Here's the key structure from my spec:
[Paste just the "2.1 Project Structure" section]
```

### Why this works:
- Gives Claude Code a focused task
- Provides only the relevant section
- Creates scaffolding for future work

---

## 2. Modular Implementation Strategy

### Break the work into focused sessions:

**Session 1: Configuration System**
```
I'm working on the iWetMyPlants config system. Here's the configuration schema:
[Paste section 2.2 Configuration Schema]

Please implement:
1. config_manager.h/.cpp - NVS-based storage with load/save
2. config_schema.h - The struct definitions
3. defaults.h - Default values for all fields
```

**Session 2: ESP-NOW Communication**
```
I'm implementing ESP-NOW for iWetMyPlants. Here's the message protocol:
[Paste section 2.3 ESP-NOW Message Protocol]

And the reliability requirements:
[Paste section 7.1 ESP-NOW Best Practices]

Please implement espnow_manager.h/.cpp with:
- Reliable send with ACK
- Message deduplication
- Statistics tracking
```

**Session 3: MQTT + HA Discovery**
```
I'm implementing MQTT with Home Assistant auto-discovery. Here's the spec:
[Paste section 2.4 Home Assistant MQTT Auto-Discovery]

Please implement mqtt_manager.h/.cpp with:
- Connection handling with reconnection
- Auto-discovery payload generation
- State publishing
- LWT for availability
```

**Session 4: Sensor Abstraction**
```
I need a sensor abstraction layer for moisture sensors. Requirements:
[Paste section 4.4 Remote ADC Configuration Options]

Please implement:
1. sensor_interface.h - Abstract interface
2. capacitive_moisture.h/.cpp - Direct ADC implementation
3. ads1115_moisture.h/.cpp - ADS1115 implementation
4. mux_moisture.h/.cpp - Multiplexer implementation
5. Factory function to create based on config
```

**Continue similarly for each component...**

---

## 3. Context Management Tips

### DO:
- Start each session with a brief context reminder
- Paste only the relevant spec sections
- Ask for one component at a time
- Save generated code to files before starting new components
- Use "continue from where we left off" with file paths

### DON'T:
- Paste the entire spec at once
- Ask for multiple unrelated components together
- Lose context by starting fresh without files

### Example continuation prompt:
```
Continuing iWetMyPlants development. We've completed:
- config_manager (in shared/config/)
- espnow_manager (in shared/communication/)

Now let's implement the Hub controller. Here's the spec:
[Paste section 3.3 Hub-Specific Code Structure]

The existing files are at:
- shared/config/config_manager.h
- shared/communication/espnow_manager.h

Please implement hub/src/hub_controller.h/.cpp that uses these existing components.
```

---

## 4. Testing During Development

### After each component, test it:

```
I've implemented espnow_manager. Before moving on, let's create a simple test:

1. Create a test sketch that initializes ESP-NOW
2. Sends a test message to broadcast
3. Prints statistics

Hardware: ESP32-WROOM DevKit
```

### Integration testing prompt:
```
I need to test the Hub receiving from a Remote.

Create two test sketches:
1. remote_test.cpp - Sends moisture readings every 5 seconds via ESP-NOW
2. hub_test.cpp - Receives and prints the readings

Use the existing espnow_manager and message_types from our implementation.
```

---

## 5. Debugging Sessions

### When something doesn't work:

```
The ESP-NOW messages aren't being received. Here's my setup:
- Hub: ESP32-WROOM on COM3
- Remote: ESP32-C3 on COM4
- Both using channel 1

Here's the relevant code:
[Paste just the problematic section]

Serial output from Hub:
[Paste serial output]

Serial output from Remote:
[Paste serial output]

What might be wrong?
```

### The more specific the context, the better the diagnosis.

---

## 6. Web Interface Development

### Static files approach:

```
I need the web UI for iWetMyPlants. Here's the API spec:
[Paste section 6.2 API Endpoints]

Please create:
1. web_ui/index.html - Dashboard with sensor readings
2. web_ui/settings.html - Configuration form
3. web_ui/style.css - Clean, mobile-friendly styling
4. api_endpoints.cpp - REST handlers

Use vanilla JavaScript, no frameworks. Embed the HTML/CSS in PROGMEM.
```

---

## 7. Final Integration

### Once components are done:

```
I've completed all iWetMyPlants components. Now I need to wire them together.

For the Hub firmware, please create main.cpp that:
1. Loads configuration from NVS
2. Starts WiFi (or AP mode if not configured)
3. Initializes MQTT with HA discovery
4. Initializes ESP-NOW for receiving
5. Starts the web server
6. In loop(), handles all managers

Use these existing components:
- ConfigManager
- WifiManager
- MqttManager
- EspNowManager
- WebServer

Reference the state machine from section 3.2 of my spec.
```

---

## 8. Common Prompts Reference

### "Explain the code":
```
Explain how the sendWithRetry() function in espnow_manager.cpp works, 
specifically the acknowledgment handling.
```

### "Add a feature":
```
Add battery voltage monitoring to the Remote. It should:
- Read GPIO 0 with voltage divider (max 4.2V mapped to 3.3V)
- Calculate percentage assuming 3.0V = 0%, 4.2V = 100%
- Include in the ESP-NOW message
- Trigger low battery warning at 20%
```

### "Fix a bug":
```
The moisture reading drifts over time. Looking at capacitive_moisture.cpp,
I suspect it's related to the averaging. Currently it uses a simple mean.
Please implement an exponential moving average instead.
```

### "Refactor":
```
The hub_controller.cpp has grown too large. Please split it into:
- hub_controller.cpp - Main orchestration
- sensor_handler.cpp - Sensor reading aggregation
- command_handler.cpp - MQTT command processing
```

---

## 9. Session Template

Start each coding session with:

```
# iWetMyPlants Development Session

## Context
Device: [Hub/Remote/Greenhouse]
Component: [What we're working on]
Dependencies: [What's already implemented]

## Today's Goal
[Specific deliverable]

## Relevant Spec
[Paste just the needed section]

## Existing Files
[List paths of files this component needs to interact with]
```

---

## 10. Checklist for Each Component

Before considering a component "done":

- [ ] Compiles without warnings
- [ ] Has header guard and includes
- [ ] Has brief doc comments
- [ ] Uses the shared types (from config_schema.h, message_types.h)
- [ ] Has error handling (return codes or exceptions)
- [ ] Logs appropriately (LOG_DEBUG, LOG_INFO, LOG_ERROR)
- [ ] Tested standalone if possible
- [ ] Added to platformio.ini build if needed

---

## Quick Reference: Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| IDE | PlatformIO in VS Code | Better dependency management than Arduino IDE |
| Framework | Arduino | Faster development than ESP-IDF raw |
| Config Storage | NVS | Native to ESP32, survives updates |
| Web Server | ESPAsyncWebServer | Non-blocking, WebSocket support |
| MQTT | AsyncMqttClient | Non-blocking, reconnect handling |
| JSON | ArduinoJson v7 | Industry standard, efficient |
| ADC | Recommend ADS1115 | ESP32 ADC is unreliable |

---

## Hardware Setup for Testing

Before starting implementation, have this ready:

1. **Hub Development**
   - ESP32-WROOM DevKit
   - USB cable
   - Optional: ADS1115 breakout + one moisture sensor

2. **Remote Development**
   - ESP32-C3 SuperMini
   - USB-C cable
   - One capacitive moisture sensor
   - Button for wake testing
   - Optional: Battery holder + 18650 cell

3. **Greenhouse Development** (after Hub works)
   - ESP32-WROOM DevKit
   - 4-channel relay module
   - DHT22 or SHT31 sensor
   - 12V pump (for testing)
   - Power supply

---

*Start small, test often, commit frequently!*