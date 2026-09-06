# ESP32 Standalone Version (v3.0.0) — Fully Offline, No WiFi/Server Needed

This is the final Tier 1 form: the ESP32 needs nothing outside itself. No
home WiFi, no laptop, no internet. The user connects their phone directly
to the ESP32's own WiFi network to see the dashboard.

## `data/` folder — unchanged

`index.html`, `app.js`, `style.css` are **reused exactly as they are** from
the previous (Station mode) version — no changes needed. Only the `.ino`
firmware changed.

## What changed (v2 Station mode → v3 SoftAP standalone)

| | v2 — Station mode | v3 — SoftAP standalone |
|---|---|---|
| WiFi role | ESP32 joins the home WiFi | ESP32 **creates its own** WiFi network (`VP-Farm-zone1`) |
| How to access dashboard | Phone must be on the same home WiFi | Phone connects **directly** to the ESP32's WiFi |
| Internet/router needed | Yes (same network as ESP32) | **No** — fully offline |
| DNS / captive portal | Not present | Added: connecting to the WiFi can auto-pop the dashboard, like a hotel WiFi login page |
| Model inference | On-device (`model.h`) — unchanged | On-device (`model.h`) — unchanged |
| Crop config, fail-safe logic | Unchanged | Unchanged |
| Fixed AP IP | — | `192.168.4.1` (ESP32's standard SoftAP gateway address) |

Only the WiFi setup and route registration in `setup()`/`loop()` changed:
- `WiFi.mode(WIFI_STA)` + `WiFi.begin(ssid, pass)` → `WiFi.mode(WIFI_AP)` +
  `WiFi.softAP(ap_ssid, ap_pass)`.
- Added `DNSServer` + `dns.start(...)` so any domain the phone tries to
  reach resolves back to the ESP32 (captive portal behaviour).
- `server.onNotFound(...)` redirects any unmatched path back to `/`.
- Route order fix carried over from the previous debugging session: the `/`
  handler serving `index.html` is registered **before** `serveStatic("/", ...)`,
  otherwise `serveStatic` claims `/` first and the custom handler never runs
  (this was the earlier "Not Found: /" bug).

## Test steps

1. **Upload the sketch** (`.ino`) via Arduino IDE.
2. **Upload LittleFS data** (Tools → "Upload LittleFS Data") — re-upload even
   if `data/` files didn't change, since a sketch upload can affect the
   filesystem partition.
3. **Reset the board** (EN/RST button, or unplug/replug USB).
4. On your **phone's WiFi settings**, look for network **`VP-Farm-zone1`**
   and connect using password **`vpfarm1234`**.
5. The dashboard should **auto-open** (captive portal). If it doesn't, open
   a browser and go to **`http://192.168.4.1`**.
6. Confirm the dashboard loads, shows live soil/temp readings, and the crop
   dropdown works (select a crop, see the "saved" confirmation).
7. Check Serial Monitor (115200 baud) for `AP IP: 192.168.4.1` and sensor
   sample logs to confirm the board booted correctly.

## Trade-off to know

Only one device can be usefully connected to the ESP32's WiFi at a time in
practice (some routers limit SoftAP client count) — this is fine for a
single-farmer, single-phone use case, but doesn't scale to multiple viewers
at once the way the home-WiFi version could.
