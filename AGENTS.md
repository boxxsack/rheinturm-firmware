# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32 firmware for a Rheinturm (Rhine Tower) clock replica that displays time using NeoPixel LEDs in BCD format, configurable via Bluetooth Low Energy (BLE). Built with PlatformIO and Arduino framework.

The companion Flutter app ([boxxsack/rheinturm-app](https://github.com/boxxsack/rheinturm-app)) connects to this firmware over BLE to provide WiFi credentials and control LED brightness.

## Build Commands

```bash
pio run                    # Build firmware (esp32dev is the default environment)
pio run --target upload    # Build and flash to device
pio device monitor         # Serial monitor (115200 baud)
```

The OTA app slots are sized by `partitions_ota.csv`. After an `esp32dev` build,
check the published image with `python3 scripts/check_flash_budget.py`.
The check measures `.pio/build/esp32dev/firmware.bin`, the artifact that must
fit the OTA slot, and derives the capacity from the `ota_0` app row. It warns
at 96% and fails at 98%; override either threshold with
`FLASH_BUDGET_WARNING_PERCENT` or `FLASH_BUDGET_FAIL_PERCENT` (or the matching
script arguments). CI and release run this check before release signing.

No test infrastructure exists for the ESP32 target itself. There is one host-side (`native`)
PlatformIO test env for the pure-logic modules (`OtaImageVerifier`, `BleAuthFailurePolicy`,
`OtaUpdater`); its
pre-build configuration explicitly selects the source needed by each suite, so hardware-dependent
sources cannot silently enter the native build:

```bash
brew install mbedtls              # macOS
sudo apt install libmbedtls-dev   # Debian or Ubuntu
pio test -e native
```

Pull requests and pushes to `main` run native tests and an `esp32dev` build in
`.github/workflows/ci.yml`. Tag releases run the native tests before signing or publishing.

## Architecture

The firmware is split into seven modules with a thin orchestrator:

### Modules

- **TimeDisplayLogic / TimeDisplay** (`include/TimeDisplayLogic.h`, `src/TimeDisplayLogic.cpp`, `include/TimeDisplay.h`, `src/TimeDisplay.cpp`) — `TimeDisplayLogic` is the Arduino-free, host-tested decision layer for BCD frames, schedule windows and payload validation, separator phases, brightness decisions, blanking, and OTA progress frames. `TimeDisplay` is the thin LED-facing adapter; it receives `IMonotonicClock` and `IDisplaySettingsStore` implementations so timing and NVS stay outside the logic. ESP32 implementations are in `TimeDisplayPlatform.cpp`, preserving the `schedule` and `separator` namespaces and existing keys. Rainbow animation remains on `TimeDisplay`. Public methods remain `update()`, `setBrightness()`, `playRainbow()`, `cancelRainbow()`, and `showOtaProgress()`, plus the existing configuration accessors.

- **ConnectivityManager** (`include/ConnectivityManager.h`, `src/ConnectivityManager.cpp`) — Owns WiFi connection lifecycle (non-blocking reconnect with 30s cooldown), NTP time sync (background SNTP polling), credential persistence (NVS), and async WiFi scanning. 8 public methods: `begin()`, `tick()`, `applyCredentials()`, `clearCredentials()`, `getState()`, `hasValidTime()`, `startAsyncScan()`, `checkScanResults()`.

- **BLEConfigInterface** (`include/BLEConfigInterface.h`, `src/BLEConfigInterface.cpp`) — Owns the entire BLE stack: server, service, 12 characteristics (confState, SSID, password, scanState, scanList, brightness, firmwareVersion, otaControl, rainbow, schedule, separatorConfig, wifiReset), callback dispatch. BLE callbacks stage values in private fields; `tick()` dispatches to ConnectivityManager and TimeDisplay (cross-task safe). Non-blocking scan state machine with chunked 20-byte notifications. For OTA, it tears down BLE to free ~60-80KB heap for TLS, then drives `OtaUpdater` through real WiFi, flash, mbedTLS, display, and restart adapters. 3 public methods: `begin()`, `tick()`, `isClientConnected()`.

- **ILedStrip / NeoPixelAdapter** (`include/ILedStrip.h`, `include/NeoPixelAdapter.h`, `src/NeoPixelAdapter.cpp`) — Abstract LED strip interface (`setPixelColor`, `show`, `setBrightness`, `clear`) with Adafruit NeoPixel adapter. Enables testing without hardware.

- **OtaImageVerifier** (`include/OtaImageVerifier.h`, `src/OtaImageVerifier.cpp`) — Pure function, no Arduino/ESP-IDF dependency beyond mbedtls: verifies an RSA-2048 RSASSA-PKCS1-v1.5/SHA-256 signature over a firmware SHA-256 digest against an embedded public key (`include/OtaSigningKey.h`). Host-testable — see `test/test_ota_image_verifier`.

- **OtaUpdater** (`include/OtaUpdater.h`, `src/OtaUpdater.cpp`) — Host-testable fail-closed OTA transaction. Injects HTTP transport, update sink, SHA-256 hashing, signature verification, progress, and restart; resolves both URLs, fetches the fixed-size signature, writes and hashes the image in one pass, and calls `end()` only after verification. Host coverage for every terminal branch is in `test/test_ota_updater`.

- **BleAuthFailurePolicy** (`include/BleAuthFailurePolicy.h`, `src/BleAuthFailurePolicy.cpp`) — Pure function, no Arduino/ESP-IDF dependency: decides whether a failed `ESP_GAP_BLE_AUTH_CMPL_EVT` should remove the ESP32's bond and disconnect the peer (see the BLE bond self-heal key detail below). Host-testable — see `test/test_ble_auth_failure_policy`.

### Shared Types

- **ConnectivityState** (`include/ConnectivityState.h`) — Enum shared by ConnectivityManager and BLEConfigInterface: `DISCONNECTED`, `CONNECTING`, `CONNECTED_NO_TIME`, `CONNECTED_WITH_TIME`.

### Orchestrator

`src/ESP32_BLE_WIFI_SCAN_FEAT.cpp` — Thin setup/loop that constructs all modules and calls `tick()` on each. Handles midnight rainbow trigger. ~80 lines.

### Dependency Graph

```
BLEConfigInterface → OtaUpdater (real transport, flash, hash, verifier, display, and restart adapters)
BLEConfigInterface → OtaImageVerifier (production verifier adapter)
OtaUpdater → injected HTTP/update/hash/verifier/progress/restart interfaces
BLEConfigInterface → BleAuthFailurePolicy (bond-removal decision on auth failure)
BLEConfigInterface → ConnectivityManager (credentials, scan, state)
BLEConfigInterface → TimeDisplay (brightness)
TimeDisplay → TimeDisplayLogic
TimeDisplay → ILedStrip
TimeDisplay → IMonotonicClock / IDisplaySettingsStore
TimeDisplayPlatform → Arduino / Preferences
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
- OTA image signing (defense-in-depth, independent of the TLS check above): `OtaUpdater` streams `firmware.bin` in a single HTTP GET, feeding each chunk to both `Update.write()` (flash) and a running SHA-256 hash - the same bytes are hashed and written, so there is no gap between "verified" and "flashed" content. `Update.end()` (which activates the image) is only called after the production adapter delegates to `OtaImageVerifier::verify()` against the public key embedded in `include/OtaSigningKey.h`; on failure `Update.abort()` leaves the previous firmware bootable. The signature itself is fetched separately from `<firmware-url>.sig` (small, not the flashed content, so no TOCTOU risk there). The transaction tests in `test/test_ota_updater` prove the fail-closed ordering and restart behavior; `test/test_ota_image_verifier` also checks that the embedded key parses as RSA-2048. Signing happens in `.github/workflows/release.yml` via `scripts/sign_firmware.py`, using the `OTA_SIGNING_PRIVATE_KEY` GitHub Actions secret (never committed). See `SECURITY_REVIEW.md` Finding 3.
- Releases are built and signed by `.github/workflows/release.yml` on `v*.*.*` tag push — do not publish `firmware.bin` manually; an unsigned binary won't be accepted by devices running this firmware or later.
- WiFi credentials stored in NVS via Preferences (namespace: "credentials")
- NTP server: `de.pool.ntp.org`, timezone: CET/CEST
- Partition scheme: `partitions_ota.csv` (dual OTA partitions — `ota_0` and `ota_1` at ~1.8MB each, enables over-the-air updates)
- Flash budget is tight (~95% of the OTA partition used as of this writing — see `scripts/check_flash_budget.py` under Build Commands above). The OTA image-signing addition (OtaImageVerifier + embedded public key + single-fetch hash-while-flash, now in `OtaUpdater::perform()`) net *reduced* flash usage by ~7.8KB versus pre-#17: it replaced `HTTPUpdate` with direct `Update.h` calls (dropping the now-unused `HTTPUpdate` library), and mbedtls's PK/RSA/hash code was already linked in via `WiFiClientSecure`'s TLS stack, so RSA verification itself added only ~3.3KB gross. Re-measure before adding more code near this ceiling.
- Dependencies: Adafruit NeoPixel v1.12.3, Update.h (included in ESP32 Arduino framework; OTA no longer uses the higher-level `HTTPUpdate` wrapper — see the OTA image signing bullet above)
- `#define DEBUG` enables serial debug output
- `FIRMWARE_VERSION` is defined at the top of `src/ESP32_BLE_WIFI_SCAN_FEAT.cpp` (single source of truth) - published releases on GitHub Releases
- Separator config persisted in NVS via Preferences (namespace: "separator", keys: "mode", "ival") — modes: 0=off, 1=on, 2=blink; intervalSeconds 1–60
- WiFi reset: app writes `"reset-wifi"` to `wifiReset` characteristic (`4fafff0c-...`); firmware erases NVS credentials, disconnects WiFi, notifies `"reset-ok"` (or `"reset-fail:<msg>"` on error). BLE stays alive.

## Maintaining this file

Keep this file for knowledge useful to almost every future agent session in this project.
Do not repeat what the codebase already shows; point to the authoritative file or command instead.
Prefer rewriting or pruning existing entries over appending new ones.
When updating this file, preserve this bar for all agents and keep entries concise.
