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

No test infrastructure is set up.

## Architecture

The firmware is split into four modules with a thin orchestrator:

### Modules

- **TimeDisplay** (`include/TimeDisplay.h`, `src/TimeDisplay.cpp`) — Owns BCD time-to-LED conversion, separator blink logic, and non-blocking rainbow animation. Depends on `ILedStrip` interface for hardware abstraction. 4 public methods: `update()`, `setBrightness()`, `playRainbow()`, `cancelRainbow()`.

- **ConnectivityManager** (`include/ConnectivityManager.h`, `src/ConnectivityManager.cpp`) — Owns WiFi connection lifecycle (non-blocking reconnect with 30s cooldown), NTP time sync (background SNTP polling), credential persistence (NVS), and async WiFi scanning. 7 public methods: `begin()`, `tick()`, `applyCredentials()`, `getState()`, `hasValidTime()`, `startAsyncScan()`, `checkScanResults()`.

- **BLEConfigInterface** (`include/BLEConfigInterface.h`, `src/BLEConfigInterface.cpp`) — Owns the entire BLE stack: server, service, 6 characteristics, callback dispatch. BLE callbacks stage values in private fields; `tick()` dispatches to ConnectivityManager and TimeDisplay (cross-task safe). Non-blocking scan state machine with chunked 20-byte notifications. 3 public methods: `begin()`, `tick()`, `isClientConnected()`.

- **ILedStrip / NeoPixelAdapter** (`include/ILedStrip.h`, `include/NeoPixelAdapter.h`, `src/NeoPixelAdapter.cpp`) — Abstract 3-method LED strip interface (`setPixelColor`, `show`, `setBrightness`) with Adafruit NeoPixel adapter. Enables testing without hardware.

### Shared Types

- **ConnectivityState** (`include/ConnectivityState.h`) — Enum shared by ConnectivityManager and BLEConfigInterface: `DISCONNECTED`, `CONNECTING`, `CONNECTED_NO_TIME`, `CONNECTED_WITH_TIME`.

### Orchestrator

`src/ESP32_BLE_WIFI_SCAN_FEAT.cpp` — Thin setup/loop that constructs all modules and calls `tick()` on each. Handles midnight rainbow trigger. ~80 lines.

### Dependency Graph

```
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
- WiFi credentials stored in NVS via Preferences (namespace: "credentials")
- NTP server: `de.pool.ntp.org`, timezone: CET/CEST
- Partition scheme: `huge_app.csv` (needed for BLE + WiFi stack size)
- Dependency: Adafruit NeoPixel v1.12.3
- `#define DEBUG` enables serial debug output
- `#define FIRMWARE_VERSION "2.0.0"` in main file
