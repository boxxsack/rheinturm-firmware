#pragma once

#include "ILedStrip.h"
#include <cstdint>
#include <time.h>

class TimeDisplay {
public:
    explicit TimeDisplay(ILedStrip& strip, uint16_t blinkRateMs = 500);

    // Call every loop tick. Renders BCD time display, or advances rainbow if active.
    // Applies pending brightness.
    void update(const tm& time);

    // Stores brightness — applied on next update() call.
    void setBrightness(uint8_t brightness);

    // Blocking rainbow animation (original style). Accepts an optional callback
    // invoked each color cycle (~2.5 s) so the caller can keep BLE dispatch alive.
    void playRainbow(uint32_t durationMs = 60000, void (*onTick)() = nullptr);

    // Cancel an active rainbow — the blocking loop exits on the next cycle.
    void cancelRainbow();

    // Show OTA update progress on LEDs (0-100%). Fills LEDs proportionally in teal.
    void showOtaProgress(uint8_t percent);

private:
    ILedStrip& _strip;

    // Brightness
    uint8_t _brightness;
    uint8_t _pendingBrightness;
    bool _brightnessChanged;

    // Rainbow cancellation (set from BLE task, checked in blocking loop)
    volatile bool _rainbowCancelled;

    // Separator blink
    uint16_t _blinkRateMs;

    // LED state array
    static constexpr uint8_t LED_COUNT = 41;
    uint8_t _ledState[LED_COUNT];

    // LED layout constants
    static constexpr uint8_t SEP_A = 11;
    static constexpr uint8_t SEP_B = 26;

    // BCD segment ranges: {startIndex, direction} — LEDs fill from startIndex downward
    // Seconds ones:  indices 40 down to 32 (max 9)
    // Seconds tens:  indices 31 down to 27 (max 5)
    // Minutes ones:  indices 25 down to 17 (max 9)
    // Minutes tens:  indices 16 down to 12 (max 5)
    // Hours ones:    indices 10 down to 2  (max 9)
    // Hours tens:    indices 1  down to 0  (max 2)
    struct BcdSegment {
        uint8_t startIndex;
        uint8_t maxValue;
    };
    static constexpr BcdSegment SEGMENTS[6] = {
        {40, 9},  // seconds ones
        {31, 5},  // seconds tens
        {25, 9},  // minutes ones
        {16, 5},  // minutes tens
        {10, 9},  // hours ones
        { 1, 2},  // hours tens
    };

    // Colors (packed as 0x00RRGGBB)
    static constexpr uint32_t COLOR_WARM_WHITE = (255u << 16) | (243u << 8) | 170u;
    static constexpr uint32_t COLOR_RED        = (255u << 16);
    static constexpr uint32_t COLOR_OFF        = 0;
    static constexpr uint32_t COLOR_TEAL       = (4u << 16) | (94u << 8) | 135u;

    // Internal methods
    void _computeBcd(const tm& time);
    void _renderTime();
    static uint32_t _colorWheel(uint8_t position);
};
