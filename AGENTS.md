# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32 firmware for a Rheinturm (Rhine Tower) clock replica that displays time using NeoPixel LEDs in BCD format, configurable via Bluetooth Low Energy (BLE). Built with PlatformIO and Arduino framework.

The companion Flutter app ([boxxsack/rheinturm-app](https://github.com/boxxsack/rheinturm-app)) connects to this firmware over BLE to provide WiFi credentials and control LED brightness.

## Build Commands

```bash
platformio run                    # Build firmware
platformio run --target upload    # Build and flash to device
platformio device monitor         # Serial monitor (115200 baud)
```

No test infrastructure exists for the ESP32 target itself. There is one host-side (`native`)
PlatformIO test env for the pure-logic modules (`OtaImageVerifier`, `BleAuthFailurePolicy`); its
`build_src_filter` excludes the Arduino-dependent sources, so new pure modules are picked up automatically:

```bash
brew install mbedtls               # or: apt install libmbedtls-dev (Linux)
platformio test -e native
```

## Architecture

The firmware is split into five modules with a thin orchestrator:

### Modules

- **TimeDisplay** (`include/TimeDisplay.h`, `src/TimeDisplay.cpp`) — Owns BCD time-to-LED conversion, separator blink logic, non-blocking rainbow animation, and OTA progress display. Depends on `ILedStrip` interface for hardware abstraction. 5 public methods: `update()`, `setBrightness()`, `playRainbow()`, `cancelRainbow()`, `showOtaProgress()`.

- **ConnectivityManager** (`include/ConnectivityManager.h`, `src/ConnectivityManager.cpp`) — Owns WiFi connection lifecycle (non-blocking reconnect with 30s cooldown), NTP time sync (background SNTP polling), credential persistence (NVS), and async WiFi scanning. 8 public methods: `begin()`, `tick()`, `applyCredentials()`, `clearCredentials()`, `getState()`, `hasValidTime()`, `startAsyncScan()`, `checkScanResults()`.

- **BLEConfigInterface** (`include/BLEConfigInterface.h`, `src/BLEConfigInterface.cpp`) — Owns the entire BLE stack: server, service, 12 characteristics (confState, SSID, password, scanState, scanList, brightness, firmwareVersion, otaControl, rainbow, schedule, separatorConfig, wifiReset), callback dispatch. BLE callbacks stage values in private fields; `tick()` dispatches to ConnectivityManager and TimeDisplay (cross-task safe). Non-blocking scan state machine with chunked 20-byte notifications. OTA update support: receives download URL over BLE, deinits BLE to free ~60-80KB heap for TLS, resolves the download URL and its signature (`_resolveRedirect`), fetches the signature (`_fetchOtaSignature`), streams the firmware image in one pass writing it to flash and hashing it simultaneously (`_downloadFlashAndHash`, using `Update.h` directly rather than the higher-level `HTTPUpdate`), then only activates the image (`Update.end()`) if `OtaImageVerifier::verify()` passes — otherwise `Update.abort()`. Shows progress on LEDs, then restarts. 3 public methods: `begin()`, `tick()`, `isClientConnected()`.

- **ILedStrip / NeoPixelAdapter** (`include/ILedStrip.h`, `include/NeoPixelAdapter.h`, `src/NeoPixelAdapter.cpp`) — Abstract 3-method LED strip interface (`setPixelColor`, `show`, `setBrightness`) with Adafruit NeoPixel adapter. Enables testing without hardware.

- **OtaImageVerifier** (`include/OtaImageVerifier.h`, `src/OtaImageVerifier.cpp`) — Pure function, no Arduino/ESP-IDF dependency beyond mbedtls: verifies an RSA-2048 RSASSA-PKCS1-v1.5/SHA-256 signature over a firmware SHA-256 digest against an embedded public key (`include/OtaSigningKey.h`). Host-testable — see `test/test_ota_image_verifier`.

### Shared Types

- **ConnectivityState** (`include/ConnectivityState.h`) — Enum shared by ConnectivityManager and BLEConfigInterface: `DISCONNECTED`, `CONNECTING`, `CONNECTED_NO_TIME`, `CONNECTED_WITH_TIME`.

### Orchestrator

`src/ESP32_BLE_WIFI_SCAN_FEAT.cpp` — Thin setup/loop that constructs all modules and calls `tick()` on each. Handles midnight rainbow trigger. ~80 lines.

### Dependency Graph

```
BLEConfigInterface → OtaImageVerifier (signature check before flashing)
BLEConfigInterface → ConnectivityManager (credentials, scan, state)
BLEConfigInterface → TimeDisplay (brightness)
TimeDisplay → ILedStrip
ConnectivityManager and TimeDisplay have no knowledge of BLE.
```

### Flow

`setup()` initializes the LED strip, then calls `begin()` on ConnectivityManager and BLEConfigInterface. `loop()` runs at ~10Hz: `connectivity.tick()` drives WiFi/NTP, `ble.tick()` drives BLE dispatch and scan delivery, `display.update()` renders the current time. All operations are non-blocking.

## Key Details

- 41 NeoPixel LEDs on GPIO 5, BCD layout with separators at indices 11 and 26
- BLE device name: "Rheinturm", service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- BLE security: link encryption + bonding required ("Just Works" pairing, `ESP_LE_AUTH_REQ_SC_BOND`). The SSID, password, otaControl and wifiReset characteristics carry `ESP_GATT_PERM_*_ENCRYPTED` permissions, so a client must pair before provisioning or triggering OTA. See `SECURITY_REVIEW.md`.
- BLE bond self-heal: `SecurityCallbacks` in `src/BLEConfigInterface.cpp` logs every `ESP_GAP_BLE_AUTH_CMPL_EVT` (peer address + fail reason) and, per `BleAuthFailurePolicy`, removes the ESP32's bond and disconnects on auth failure so the next connect re-pairs. Bluedroid (IDF 4.4) already clears NVS keys on most SMP failures itself; this is an explicit, stack-version-independent layer plus diagnostics. It cannot heal an iOS-side stale key (ESP32 has no bond) - the user must forget the device in iOS Settings. Registering the callbacks must not add `setEncryptionLevel()` (would cause a second pairing dialog).
- BLE notifications: the five NOTIFY characteristics (confState, scanList, brightness, otaControl, wifiReset) carry explicit CCCD (0x2902) descriptors — Bluedroid does not auto-create one from the NOTIFY property bit, and iOS rejects subscription attempts without it. `notify()` only delivers to clients that subscribed via the CCCD. The CCCDs are deliberately unencrypted; the notified values are not secrets.
- OTA TLS: the firmware download verifies GitHub's certificate chain against pinned roots in `include/GitHubRootCerts.h` (Sectigo E46/R46 + ISRG X1/X2) via `setCACert()` — no `setInsecure()`. Fails closed if validation fails. Update the header if GitHub rotates roots.
- OTA image signing (defense-in-depth, independent of the TLS check above): `_downloadFlashAndHash` streams `firmware.bin` in a single HTTP GET, feeding each chunk to both `Update.write()` (flash) and a running SHA-256 hash — the same bytes are hashed and written, so there is no gap between "verified" and "flashed" content. `Update.end()` (which activates the image) is only called after `OtaImageVerifier::verify()` passes against the public key embedded in `include/OtaSigningKey.h`; on failure `Update.abort()` leaves the previous firmware bootable. The signature itself is fetched separately from `<firmware-url>.sig` (small, not the flashed content, so no TOCTOU risk there). Signing happens in `.github/workflows/release.yml` via `scripts/sign_firmware.py`, using the `OTA_SIGNING_PRIVATE_KEY` GitHub Actions secret (never committed). See `SECURITY_REVIEW.md` Finding 3 and `test/test_ota_image_verifier`.
- Releases are built and signed by `.github/workflows/release.yml` on `v*.*.*` tag push — do not publish `firmware.bin` manually; an unsigned binary won't be accepted by devices running this firmware or later.
- WiFi credentials stored in NVS via Preferences (namespace: "credentials")
- NTP server: `de.pool.ntp.org`, timezone: CET/CEST
- Partition scheme: `partitions_ota.csv` (dual OTA partitions — `ota_0` and `ota_1` at ~1.8MB each, enables over-the-air updates)
- Flash budget is tight (~95% of the OTA partition used as of this writing — check `pio run` output). The OTA image-signing addition (OtaImageVerifier + embedded public key + single-fetch hash-while-flash in `_downloadFlashAndHash`) net *reduced* flash usage by ~7.8KB versus pre-#17: it replaced `HTTPUpdate` with direct `Update.h` calls (dropping the now-unused `HTTPUpdate` library), and mbedtls's PK/RSA/hash code was already linked in via `WiFiClientSecure`'s TLS stack, so RSA verification itself added only ~3.3KB gross. Re-measure before adding more code near this ceiling.
- Dependencies: Adafruit NeoPixel v1.12.3, Update.h (included in ESP32 Arduino framework; OTA no longer uses the higher-level `HTTPUpdate` wrapper — see the OTA image signing bullet above)
- `#define DEBUG` enables serial debug output
- `#define FIRMWARE_VERSION "2.7.0"` in main file — published releases on GitHub Releases
- Separator config persisted in NVS via Preferences (namespace: "separator", keys: "mode", "ival") — modes: 0=off, 1=on, 2=blink; intervalSeconds 1–60
- WiFi reset: app writes `"reset-wifi"` to `wifiReset` characteristic (`4fafff0c-...`); firmware erases NVS credentials, disconnects WiFi, notifies `"reset-ok"` (or `"reset-fail:<msg>"` on error). BLE stays alive.

## Maintaining this file

Keep this file for knowledge useful to almost every future agent session in this project.
Do not repeat what the codebase already shows; point to the authoritative file or command instead.
Prefer rewriting or pruning existing entries over appending new ones.
When updating this file, preserve this bar for all agents and keep entries concise.
