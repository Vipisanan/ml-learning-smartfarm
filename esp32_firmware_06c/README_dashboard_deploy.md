# ESP32 Dashboard — Setup, Upload & the "Not Found" Fix

## How it works

The ESP32 runs two things at once:

1. **On-device model** (`model.h`) — reads sensors, predicts irrigation
   decision, no server/internet needed.
2. **Web server** (`WebServer` library) — serves the dashboard (HTML/CSS/JS)
   from LittleFS (the ESP32's onboard flash filesystem) and exposes JSON
   endpoints (`/api/data`, `/api/config`, etc.) that the dashboard's
   JavaScript polls.

Two separate uploads are required, because they go to two different flash regions:

- **Sketch upload** → the compiled `.ino` code (the logic).
- **LittleFS upload** → the `data/` folder's files (HTML/CSS/JS), as a raw filesystem image.

## The "Not Found: /" bug — cause and fix

**Cause:** route registration order. The original code had:

```cpp
server.serveStatic("/", LittleFS, "/");   // registered FIRST
server.on("/", []() { ... });             // registered SECOND, never reached for "/"
```

`serveStatic("/", ...)` claims the `/` path for itself first. Once a route is
claimed, `WebServer` doesn't fall through to a later handler for the same
path — so the custom `/` handler serving `index.html` was dead code, and any
request to `/` had no matching file to serve directly, hence 404.

**Fix:** register the explicit `/` handler **before** `serveStatic`, and let
`serveStatic` only catch everything else (`/style.css`, `/app.js`, etc.):

```cpp
server.on("/", HTTP_GET, []() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) { server.send(404, "text/plain", "index.html not found"); return; }
  server.streamFile(file, "text/html");
  file.close();
});
server.serveStatic("/", LittleFS, "/");   // now only handles the remaining static files
```

Now `http://<esp32-ip>/` and `http://<esp32-ip>/index.html` both work.

## Full deployment steps (every time you change code or dashboard files)

1. **Folder layout** (all in one sketch folder):

   ```
   esp32_firmware_06c/
   ├── esp32_firmware.ino
   ├── model.h
   └── data/
       ├── index.html
       ├── app.js
       └── style.css
   ```

2. **Upload the sketch** (Arduino IDE → Upload button). This flashes the code
   logic only — LittleFS data is untouched by this step, but can sometimes be
   affected by a changed partition layout, so re-upload LittleFS after any
   sketch change just to be safe.

3. **Upload the filesystem** (Tools → "Upload LittleFS Data" / the
   `arduino-littlefs-upload` plugin). This flashes `data/`'s contents to
   LittleFS. Do this **after** the sketch upload, every time.

4. **Reset the board** (EN/RST button, or unplug/replug USB).

5. **Get the IP** from Serial Monitor (115200 baud):

   ```
   Connected! ESP32 IP: 192.168.8.126
   ```

6. **Connect your phone/laptop to the same WiFi**, then open:
   ```
   http://192.168.8.126/
   http://192.168.8.126/index.html
   ```

## Quick troubleshooting

| Symptom                                   | Likely cause                                                                                                        |
| ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| "Not Found: /"                            | Route order bug above, OR LittleFS upload wasn't done/redone after a sketch upload                                  |
| "LittleFS mount failed" in Serial Monitor | Filesystem corrupted/never formatted — `LittleFS.begin(true)` should auto-format, re-upload LittleFS data afterward |
| IP shown but page won't load              | Phone/laptop not on the same WiFi network as the ESP32                                                              |
| Dashboard loads but shows "offline"       | `/api/data` not responding — check Serial Monitor for crashes/reboots                                               |
