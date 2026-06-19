# ESP32 OTA Template (Quectel EC200U)

A reusable Over-The-Air (OTA) firmware update framework for ESP32, using a Quectel EC200U 4G LTE modem instead of WiFi. The device checks a remote server for new firmware, downloads it, flashes itself, and reboots — over cellular data.

Designed so future projects only ever need to touch three things: `appSetup()`, `appLoop()`, and `FW_VERSION`. Everything else (modem handling, version checks, downloading, flashing) is reusable OTA "engine" code.

## Features

- OTA updates over cellular (no WiFi required)
- HTTPS firmware delivery
- Checks for updates on boot and every 3 hours
- Minimal Python-based server for hosting firmware
- Clean separation between OTA logic and your application code

## Hardware Required

- ESP32 development board (38-pin)
- Quectel EC200U development board (Find the manual [here](https://robu-prod-media.s3.ap-south-1.amazonaws.com/uploads/2024/05/UserManual-TracX-1b-V1.3-_latest-edition.pdf).)
- Active SIM card with a data plan
- 5V power supply (3A recommended)

## Wiring

| ESP32 Pin | EC200U Pin |
|---|---|
| GPIO16 | TX |
| GPIO17 | RX |
| GND | GND |

**Power:** Power the EC200U board from the 5V supply, then power the ESP32 from the EC200U board's 5V rail. Both boards must share a common ground.

## Arduino IDE Setup

1. Install the ESP32 board package.
2. Select **Board:** `ESP32 Dev Module`
3. Set **Upload Speed:** `115200`
4. Set **Partition Scheme:** `Default 4MB with SPIFFS (1.2MB APP / 1.5MB SPIFFS)`
5. Set **Serial Monitor Baud:** `115200`

> **Why this partition scheme?** OTA needs two app partitions (one running, one being written to). Picking the wrong scheme either removes OTA support entirely or leaves too little space for your firmware — flashing will fail or the OTA write will fail mid-update.

## How It Works

```
Power On / Reset
  → setup()
    → modemInit()
    → checkForUpdates()
    → appSetup()
  → loop()
    → appLoop()
    → checkForUpdates() (every 3 hours)
```

An update check also runs on every reset, power cycle, or call to `ESP.restart()`, since `setup()` runs again each time.

### Version Check Logic

The device compares `FW_VERSION` to the server's `version.json` as plain strings — it does **not** understand semantic versioning.

| Device Version | Server Version | Result |
|---|---|---|
| 1.0.0 | 1.0.0 | No update |
| 1.0.0 | 1.0.1 | Update |
| 1.0.2 | 1.0.1 | Update (strings differ — even though 1.0.2 is "newer") |

**Always set a version on the server that's different from the device's current one**, regardless of whether it's numerically higher or lower.

## Server Setup

The server is just two static files served over HTTP/HTTPS.

```
ota_server/
├── firmware.bin
└── version.json
```

`version.json`:
```json
{ "version": "1.0.0" }
```

### Step 1: Start the local server

```bash
cd ota_server
python3 -m http.server 8000
```

### Step 2: Expose it publicly with ngrok

```bash
ngrok http 8000
```

ngrok is used here for quick development testing — it lets devices reach your laptop over the internet without you owning a public server or domain. Copy the `https://` URL ngrok gives you.

> **Important:** Free ngrok URLs change every time you restart ngrok. If you restart it, you must update the URLs in your firmware and reflash — devices in the field won't know the URL changed. For a permanent deployment, host on a real server with a fixed domain instead.

Your two endpoints will be:
- `https://your-ngrok-url/firmware.bin`
- `https://your-ngrok-url/version.json`

## First Flash Workflow

1. Create the `ota_server/` folder with `version.json` (`"version": "1.0.0"`) and a placeholder `firmware.bin`.
2. Start the local server (`python3 -m http.server 8000`).
3. Start ngrok (`ngrok http 8000`) and copy the public URL.
4. In the firmware, update the firmware URL and version URL to point to your ngrok URL.
5. Set `FW_VERSION` to `"1.0.0"`.
6. Flash the ESP32 over USB.
7. Open Serial Monitor and confirm the modem registers on the network.
8. Confirm the first OTA check runs and reports "no update" (since versions match).

## Firmware Structure

| Section | Contents | Edit for new projects? |
|---|---|---|
| CONFIG | `FW_VERSION`, pin definitions, OTA timing | Yes — update `FW_VERSION` per release |
| GLOBALS | Runtime variables, modem object | No |
| OTA STRUCTS | `FirmwareChunk` | No |
| **USER SETUP** | `appSetup()` | **Yes — your init code** |
| **USER LOOP** | `appLoop()` | **Yes — your main logic** |
| MODEM FUNCTIONS | `sendAT()`, `modemInit()` | No |
| OTA FUNCTIONS | `fetchVersion()`, `downloadFirmware()`, `performOTA()`, `checkForUpdates()` | No |
| SYSTEM SETUP/LOOP | `setup()`, `loop()` | No |

Add your sensors, motors, displays, or control logic inside `appSetup()` / `appLoop()` only. Leave everything else alone.

## Example Application

The template ships with a simple LED blink on GPIO19, used to visually confirm an OTA update succeeded:

- **v1.0.0:** blinks every 1000 ms
- **v1.0.1:** blinks every 200 ms

After flashing v1.0.1 over OTA, the faster blink rate confirms the new firmware is running.

## Releasing a New OTA Update

1. Modify `appSetup()` / `appLoop()` with your changes.
2. Update `FW_VERSION` to a new (different) string.
3. **Sketch → Export Compiled Binary** in Arduino IDE.
4. Rename the exported `project_name.ino.bin` to `firmware.bin`.
5. Replace `firmware.bin` on the server.
6. Update `version.json` to match the new `FW_VERSION`.
7. Devices will pick up the update on their next reset, power cycle, or 3-hour check.

> Only the compiled binary is uploaded to the server — never the source code.

## Testing an OTA Update

1. Change the blink interval and bump `FW_VERSION`.
2. Export the binary and replace `firmware.bin` + `version.json` on the server.
3. Press **RESET** on the ESP32.
4. Expected flow: check for update → download → flash → reboot → new blink rate visible.

## Troubleshooting

| Issue | Check |
|---|---|
| Modem not registering | SIM inserted correctly, valid data plan, signal strength |
| No SIM detected | SIM seated properly, correct SIM size/voltage for EC200U |
| Firmware download fails | Server running, ngrok URL current, URL matches firmware code |
| HTTP/HTTPS failures | Correct protocol in URL, ngrok tunnel still active |
| OTA partition errors | Partition scheme set to one that supports OTA |
| Firmware too large | Binary exceeds OTA partition size — check exported `.bin` size |
| Version mismatch confusion | Remember version check is string-only, not numeric |
| Reboot loops | Bad/corrupt firmware flashed, or insufficient power during flash |
| `Update.begin()` failures | Not enough free OTA partition space for incoming binary |

## Known Limitations / Future Improvements

- No semantic version comparison (string match only)
- No firmware checksum or signature validation
- Fixed `firmware.bin` filename (no per-version filenames)
- No rollback if a bad update is flashed
- Local server (ngrok) instead of permanent cloud hosting
- No MQTT-triggered updates or OTA progress reporting
- Modem handling is synchronous, not task/queue-based (FreeRTOS)
