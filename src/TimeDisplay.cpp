#include "TimeDisplay.h"
#include <Arduino.h>
#include <Preferences.h>

constexpr TimeDisplay::BcdSegment TimeDisplay::SEGMENTS[6];

TimeDisplay::TimeDisplay(ILedStrip& strip, uint16_t blinkRateMs)
    : _strip(strip)
    , _brightness(100)
    , _pendingBrightness(100)
    , _brightnessChanged(false)
    , _blinkRateMs(blinkRateMs)
    , _rainbowCancelled(false)
{
    memset(_ledState, 0, sizeof(_ledState));
    _ledState[SEP_A] = 3;
    _ledState[SEP_B] = 3;
}

void TimeDisplay::update(const tm& time) {
    // Apply pending brightness
    if (_brightnessChanged) {
        _brightness = _pendingBrightness;
        _strip.setBrightness(_brightness);
        _brightnessChanged = false;
    }

    // If outside the scheduled on-window, blank the display without altering
    // the stored brightness so re-entering the window restores it automatically.
    if (!_isScheduleActive(time)) {
        _strip.clear();
        _strip.show();
        return;
    }

    _computeBcd(time);
    _renderTime();
}

void TimeDisplay::setBrightness(uint8_t brightness) {
    _pendingBrightness = brightness;
    _brightnessChanged = true;
}

void TimeDisplay::playRainbow(uint32_t durationMs, void (*onTick)()) {
    _rainbowCancelled = false;
    uint32_t startMs = millis();
    while (millis() - startMs < durationMs && !_rainbowCancelled) {
        if (onTick) onTick();
        for (uint16_t j = 0; j < 256 && !_rainbowCancelled; j++) {
            if (_brightnessChanged) {
                _brightness = _pendingBrightness;
                _strip.setBrightness(_brightness);
                _brightnessChanged = false;
            }
            for (uint8_t i = 0; i < LED_COUNT; i++) {
                if (i == SEP_A || i == SEP_B) {
                    _strip.setPixelColor(i, COLOR_OFF);
                } else {
                    _strip.setPixelColor(i, _colorWheel((i + j) & 255));
                }
            }
            _strip.show();
            delay(10);
        }
    }
}

void TimeDisplay::cancelRainbow() {
    _rainbowCancelled = true;
}

void TimeDisplay::showOtaProgress(uint8_t percent) {
    uint8_t ledsToFill = (static_cast<uint16_t>(percent) * LED_COUNT) / 100;
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        _strip.setPixelColor(i, i < ledsToFill ? COLOR_TEAL : COLOR_OFF);
    }
    _strip.show();
}

void TimeDisplay::_computeBcd(const tm& time) {
    // Reset all LEDs
    memset(_ledState, 0, sizeof(_ledState));
    _ledState[SEP_A] = 3;  // separator marker
    _ledState[SEP_B] = 3;

    // Extract digit values
    uint8_t digits[6] = {
        static_cast<uint8_t>(time.tm_sec % 10),   // seconds ones
        static_cast<uint8_t>(time.tm_sec / 10),    // seconds tens
        static_cast<uint8_t>(time.tm_min % 10),    // minutes ones
        static_cast<uint8_t>(time.tm_min / 10),    // minutes tens
        static_cast<uint8_t>(time.tm_hour % 10),   // hours ones
        static_cast<uint8_t>(time.tm_hour / 10),   // hours tens
    };

    // Fill BCD segments
    for (int seg = 0; seg < 6; seg++) {
        for (int i = 0; i < digits[seg]; i++) {
            _ledState[SEGMENTS[seg].startIndex - i] = 1;
        }
    }
}

void TimeDisplay::_renderTime() {
    uint32_t now = millis();
    bool separatorOn = (now % _blinkRateMs) >= (_blinkRateMs / 2);

    for (uint8_t i = 0; i < LED_COUNT; i++) {
        switch (_ledState[i]) {
        case 0:
            _strip.setPixelColor(i, COLOR_OFF);
            break;
        case 1:
            _strip.setPixelColor(i, COLOR_WARM_WHITE);
            break;
        case 3:
            _strip.setPixelColor(i, separatorOn ? COLOR_RED : COLOR_OFF);
            break;
        default:
            _strip.setPixelColor(i, COLOR_OFF);
            break;
        }
    }
    _strip.show();
}

void TimeDisplay::setSchedule(const uint8_t* payload, size_t len) {
    if (len < 5) return;
    if (payload[1] > 23 || payload[3] > 23 || payload[2] > 59 || payload[4] > 59) return;
    _schedEnabled = payload[0] ? 1 : 0;
    _schedOnH     = payload[1];
    _schedOnM     = payload[2];
    _schedOffH    = payload[3];
    _schedOffM    = payload[4];
    _persistSchedule();
    Serial.printf("Schedule set: enabled=%d on=%02d:%02d off=%02d:%02d\n",
        _schedEnabled, _schedOnH, _schedOnM, _schedOffH, _schedOffM);
}

void TimeDisplay::loadSchedule() {
    Preferences prefs;
    prefs.begin("schedule", true);
    _schedEnabled = prefs.getUChar("en",   0);
    _schedOnH     = prefs.getUChar("onH",  8);
    _schedOnM     = prefs.getUChar("onM",  0);
    _schedOffH    = prefs.getUChar("offH", 23);
    _schedOffM    = prefs.getUChar("offM", 0);
    prefs.end();
    Serial.printf("Schedule loaded: enabled=%d on=%02d:%02d off=%02d:%02d\n",
        _schedEnabled, _schedOnH, _schedOnM, _schedOffH, _schedOffM);
}

void TimeDisplay::getScheduleBytes(uint8_t out[5]) const {
    out[0] = _schedEnabled;
    out[1] = _schedOnH;
    out[2] = _schedOnM;
    out[3] = _schedOffH;
    out[4] = _schedOffM;
}

bool TimeDisplay::_isScheduleActive(const tm& time) const {
    if (!_schedEnabled) return true;
    int now = time.tm_hour * 60 + time.tm_min;
    int on  = _schedOnH  * 60 + _schedOnM;
    int off = _schedOffH * 60 + _schedOffM;
    if (on == off)  return false;          // degenerate window → always off
    if (on < off)   return now >= on && now < off;  // same-day window
    return now >= on || now < off;         // cross-midnight window
}

void TimeDisplay::_persistSchedule() const {
    Preferences prefs;
    prefs.begin("schedule", false);
    prefs.putUChar("en",   _schedEnabled);
    prefs.putUChar("onH",  _schedOnH);
    prefs.putUChar("onM",  _schedOnM);
    prefs.putUChar("offH", _schedOffH);
    prefs.putUChar("offM", _schedOffM);
    prefs.end();
}

uint32_t TimeDisplay::_colorWheel(uint8_t position) {
    position = 255 - position;
    if (position < 85) {
        return ((255 - position * 3) << 16) | (position * 3);
    }
    if (position < 170) {
        position -= 85;
        return ((position * 3) << 8) | (255 - position * 3);
    }
    position -= 170;
    return ((position * 3) << 16) | ((255 - position * 3) << 8);
}
