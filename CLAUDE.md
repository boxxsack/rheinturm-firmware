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

All firmware logic is in a single file: `src/ESP32_BLE_WIFI_SCAN_FEAT.cpp`.

### Major Components

- **LED Display**: 41 NeoPixel LEDs on GPIO 5 showing hours/minutes/seconds in BCD. LEDs at indices 11 and 26 are red separator blinkers. Active LEDs use warm white (255, 243, 170).
- **BLE Interface**: Device advertises as "Rheinturm" with service UUID `4fafc201-...`. Six characteristics handle WiFi config (SSID, password), WiFi scanning, connection state, and brightness control. Scan results are chunked into 20-byte BLE packets.
- **WiFi/NTP**: Connects using credentials stored in NVS via Arduino Preferences (key: "credentials"). Syncs time from `de.pool.ntp.org` with CET/CEST timezone. Re-syncs hourly.
- **Rainbow animation**: Triggers at midnight (00:00:00) for 60 seconds.

### Flow

`setup()` initializes BLE, loads WiFi credentials from NVS, starts NeoPixel strip. `loop()` runs at ~10Hz, reconnects WiFi if needed, syncs NTP, and updates the LED display.

## Key Details

- German variable names and comments throughout (e.g., `blinkgeschwindigkeit`, `Stunden`)
- Partition scheme: `huge_app.csv` (needed for BLE + WiFi stack size)
- Dependency: Adafruit NeoPixel v1.12.3
- `#define DEBUG` enables serial debug output
