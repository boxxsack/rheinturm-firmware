# Rheinturm firmware

ESP32 firmware for a Rheinturm clock replica. It displays the time on NeoPixel LEDs and is configured over Bluetooth Low Energy.

## Build

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html), then build the default `esp32dev` environment:

```bash
pio run
```

The default command only builds the firmware. Flashing hardware is an explicit separate action:

```bash
pio run --target upload
```

## Host tests

The native test environment exercises the pure `OtaImageVerifier` and `BleAuthFailurePolicy` modules without ESP32 hardware. Install mbedTLS first:

```bash
brew install mbedtls              # macOS
sudo apt install libmbedtls-dev   # Debian or Ubuntu
```

Then run both test suites:

```bash
pio test -e native
```

Pull requests and pushes to `main` run the native tests and firmware build in CI. Version-tag releases also run the native tests before signing and publishing firmware.
