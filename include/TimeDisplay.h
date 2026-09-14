#pragma once

#include "ILedStrip.h"
#include "TimeDisplayLogic.h"

#include <cstddef>
#include <cstdint>
#include <time.h>

class IMonotonicClock {
public:
    virtual ~IMonotonicClock() = default;
    virtual uint32_t nowMs() const = 0;
    virtual void delayMs(uint32_t durationMs) = 0;
};

class IDisplaySettingsStore {
public:
    virtual ~IDisplaySettingsStore() = default;
    virtual void loadSchedule(TimeDisplayLogic::Schedule& schedule) = 0;
    virtual void saveSchedule(const TimeDisplayLogic::Schedule& schedule) = 0;
    virtual void loadSeparator(TimeDisplayLogic::SeparatorConfig& config) = 0;
    virtual void saveSeparator(const TimeDisplayLogic::SeparatorConfig& config) = 0;
};

class TimeDisplay {
public:
    TimeDisplay(ILedStrip& strip, IMonotonicClock& clock, IDisplaySettingsStore& settings);

    // Call every loop tick. Renders BCD time display, or advances rainbow if active.
    // Applies pending brightness. Blanks LEDs (without touching stored brightness)
    // when the current time falls outside the configured on/off window.
    void update(const tm& time);

    // Stores brightness - applied on next update() call.
    void setBrightness(uint8_t brightness);

    // Applies and persists a 5-byte schedule payload:
    //   [enabled, onHour, onMinute, offHour, offMinute]
    // Silently ignores malformed payloads (length < 5, values out of range).
    void setSchedule(const uint8_t* payload, size_t len);

    // Loads the persisted schedule from the injected settings store. Call once in setup().
    void loadSchedule();
    void getScheduleBytes(uint8_t out[5]) const;

    // Applies and persists a 2-byte separator config payload:
    //   [mode, intervalSeconds] mode: 0=off, 1=on, 2=blink
    //   intervalSeconds: 1-60 (full on/off cycle length when mode==blink)
    // Silently ignores malformed payloads.
    void setSeparatorConfig(const uint8_t* payload, size_t len);

    // Loads the persisted separator config from the injected settings store. Call once in setup().
    void loadSeparatorConfig();
    void getSeparatorConfigBytes(uint8_t out[2]) const;

    // Blocking rainbow animation (original style). Accepts an optional callback
    // invoked each color cycle (~2.5 s) so the caller can keep BLE dispatch alive.
    void playRainbow(uint32_t durationMs = 60000, void (*onTick)() = nullptr);
    void cancelRainbow();

    // Show OTA update progress on LEDs (0-100%). Fills LEDs proportionally in teal.
    void showOtaProgress(uint8_t percent);

    bool isScheduleActive(const tm& time) const;

private:
    ILedStrip& _strip;
    IMonotonicClock& _clock;
    IDisplaySettingsStore& _settings;
    TimeDisplayLogic::BrightnessState _brightness;
    volatile bool _rainbowCancelled;
    TimeDisplayLogic::SeparatorConfig _separator;
    TimeDisplayLogic::Schedule _schedule;

    static uint32_t _colorWheel(uint8_t position);
    void _showFrame(const TimeDisplayLogic::Frame& frame);
};
