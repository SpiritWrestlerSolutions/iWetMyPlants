# Next Session

## Priority: Greenhouse Settings — Relay Config Page

The greenhouse Settings page currently has no way to configure relays (enable/disable, GPIO pin, name, safety timings). Relays are disabled by default so a fresh Greenhouse device shows no relays at all.

**What's needed:**
- A "Relay Configuration" card in the Greenhouse Settings page (or a dedicated `/settings/relays` sub-page)
- Per-relay fields: Name, Enabled toggle, GPIO pin, Active Low toggle, Max On Time, Min Off Time, Cooldown
- Save via `POST /api/config/relays` (endpoint already exists)
- `GET /api/config/relays` already returns the current config

**Also still needed (noted in BETA_GUIDE.md):**
- Environmental sensor type selection in Greenhouse Settings (DHT11/DHT22/SHT30/SHT31/None, pin/address)
- These are currently only configurable via raw API calls

## Also for next session: docs → HTML
- Convert docs/BETA_GUIDE.md into HTML pages (Shopping List, Hardware Guide, User Guide separate pages)
- Add photo placeholders for wiring diagrams

## Resume
"Let's add relay config to the greenhouse settings page"
