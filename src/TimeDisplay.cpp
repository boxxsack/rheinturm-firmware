#include "TimeDisplay.h"

#include <algorithm>

TimeDisplay::TimeDisplay(ILedStrip& strip, IMonotonicClock& clock, IDisplaySettingsStore& settings)
    : _strip(strip), _clock(clock), _settings(settings), _brightness(), _rainbowCancelled(false) {}

void TimeDisplay::update(const tm& time) {
    uint8_t appliedBrightness = 0;
    if (TimeDisplayLogic::takePendingBrightness(_brightness, appliedBrightness)) {
        _strip.setBrightness(appliedBrightness);
    }

    TimeDisplayLogic::Frame frame;
    TimeDisplayLogic::renderTime(_schedule, _separator, time, _clock.nowMs(), frame);
    _showFrame(frame);
}

void TimeDisplay::setBrightness(uint8_t brightness) {
    _brightness.pending = brightness;
    _brightness.changed = true;
}

void TimeDisplay::playRainbow(uint32_t durationMs, void (*onTick)()) {
    _rainbowCancelled = false;
    uint32_t startMs = _clock.nowMs();
    while (_clock.nowMs() - startMs < durationMs && !_rainbowCancelled) {
        if (onTick) onTick();
        for (uint16_t j = 0; j < 256 && !_rainbowCancelled; ++j) {
            uint8_t appliedBrightness = 0;
            if (TimeDisplayLogic::takePendingBrightness(_brightness, appliedBrightness)) {
                _strip.setBrightness(appliedBrightness);
            }
            for (uint8_t i = 0; i < TimeDisplayLogic::kLedCount; ++i) {
                _strip.setPixelColor(i, (i == TimeDisplayLogic::kSeparatorA || i == TimeDisplayLogic::kSeparatorB)
                                            ? TimeDisplayLogic::kColorOff
                                            : _colorWheel((i + j) & 255));
            }
            _strip.show();
            _clock.delayMs(10);
        }
    }
}

void TimeDisplay::cancelRainbow() {
    _rainbowCancelled = true;
}

void TimeDisplay::showOtaProgress(uint8_t percent) {
    TimeDisplayLogic::Frame frame;
    TimeDisplayLogic::renderOtaProgress(percent, frame);
    _showFrame(frame);
}

void TimeDisplay::setSchedule(const uint8_t* payload, size_t len) {
    TimeDisplayLogic::Schedule schedule = _schedule;
    if (!TimeDisplayLogic::decodeSchedulePayload(payload, len, schedule)) return;
    _schedule = schedule;
    _settings.saveSchedule(_schedule);
}

void TimeDisplay::loadSchedule() {
    _settings.loadSchedule(_schedule);
}

void TimeDisplay::getScheduleBytes(uint8_t out[5]) const {
    out[0] = _schedule.enabled;
    out[1] = _schedule.onHour;
    out[2] = _schedule.onMinute;
    out[3] = _schedule.offHour;
    out[4] = _schedule.offMinute;
}

bool TimeDisplay::isScheduleActive(const tm& time) const {
    return TimeDisplayLogic::isScheduleActive(_schedule, time);
}

void TimeDisplay::setSeparatorConfig(const uint8_t* payload, size_t len) {
    TimeDisplayLogic::SeparatorConfig separator = _separator;
    if (!TimeDisplayLogic::decodeSeparatorPayload(payload, len, separator)) return;
    _separator = separator;
    _settings.saveSeparator(_separator);
}

void TimeDisplay::loadSeparatorConfig() {
    _settings.loadSeparator(_separator);
    if (_separator.mode > static_cast<uint8_t>(TimeDisplayLogic::SeparatorMode::Blink)) {
        _separator.mode = static_cast<uint8_t>(TimeDisplayLogic::SeparatorMode::Blink);
    }
    if (_separator.intervalSeconds < 1 || _separator.intervalSeconds > 60) {
        _separator.intervalSeconds = 1;
    }
}

void TimeDisplay::getSeparatorConfigBytes(uint8_t out[2]) const {
    out[0] = _separator.mode;
    out[1] = _separator.intervalSeconds;
}

void TimeDisplay::_showFrame(const TimeDisplayLogic::Frame& frame) {
    for (uint8_t i = 0; i < TimeDisplayLogic::kLedCount; ++i) {
        _strip.setPixelColor(i, frame.pixels[i]);
    }
    _strip.show();
}

uint32_t TimeDisplay::_colorWheel(uint8_t position) {
    position = 255 - position;
    if (position < 85) return ((255 - position * 3) << 16) | (position * 3);
    if (position < 170) {
        position -= 85;
        return ((position * 3) << 8) | (255 - position * 3);
    }
    position -= 170;
    return ((position * 3) << 16) | ((255 - position * 3) << 8);
}
