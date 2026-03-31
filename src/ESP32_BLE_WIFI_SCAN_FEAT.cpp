#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define FIRMWARE_VERSION "2.0.0"

#include "NeoPixelAdapter.h"
#include "TimeDisplay.h"
#include "ConnectivityManager.h"
#include "BLEConfigInterface.h"

// #define DEBUG

#define LED_PIN 5
#define LED_COUNT 41

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
NeoPixelAdapter stripAdapter(strip);
TimeDisplay display(stripAdapter);
ConnectivityManager connectivity;
BLEConfigInterface ble(connectivity, display);

static time_t lastDisplayUpdate = 0;
static bool midnightRainbowTriggered = false;

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(100);
    }
    Serial.println("*****SETUP START*****");
    Serial.println("Firmware version: " FIRMWARE_VERSION);

    // Initialize LED strip
    strip.begin();
    strip.show();
    strip.setBrightness(100);

    // Initialize modules
    connectivity.begin();
    ble.begin("Rheinturm");

    Serial.println("*****SETUP END*****");
}

void loop() {
    delay(100);

    // Tick all modules
    tm currentTime;
    bool hasTime = connectivity.tick(currentTime);

    ble.tick();

    if (hasTime) {
        time_t now = mktime(&currentTime);

        // Only update display when time changes
        if (now != lastDisplayUpdate) {
            lastDisplayUpdate = now;
            display.update(currentTime);

            // Midnight rainbow
            if (currentTime.tm_hour == 0 && currentTime.tm_min == 0 && currentTime.tm_sec == 0) {
                if (!midnightRainbowTriggered) {
                    display.playRainbow();
                    midnightRainbowTriggered = true;
                }
            } else {
                midnightRainbowTriggered = false;
            }

#ifdef DEBUG
            char timeStr[30];
            strftime(timeStr, sizeof(timeStr), "%a %d-%m-%y %T", &currentTime);
            Serial.println(timeStr);
#endif
        }
    } else {
        // No valid time yet — still update display for rainbow/separator animation
        tm placeholder = {};
        display.update(placeholder);
    }
}
