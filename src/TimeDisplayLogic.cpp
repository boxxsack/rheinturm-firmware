#include "TimeDisplayLogic.h"

#include <algorithm>
#include <iterator>

namespace TimeDisplayLogic {
namespace {

constexpr uint8_t kSeparatorModeMax = static_cast<uint8_t>(SeparatorMode::Blink);
constexpr uint8_t kSeparatorIntervalMin = 1;
constexpr uint8_t kSeparatorIntervalMax = 60;

struct BcdSegment {
    uint8_t startIndex;
    uint8_t maxValue;
};

constexpr BcdSegment kSegments[6] = {
    {40, 9}, // seconds ones
    {31, 5}, // seconds tens
    {25, 9}, // minutes ones
    {16, 5}, // minutes tens
    {10, 9}, // hours ones
    {1, 2},  // hours tens
};

} // namespace

bool decodeSchedulePayload(const uint8_t* payload, size_t len, Schedule& schedule) {
    if (payload == nullptr || len < 5 || payload[1] > 23 || payload[3] > 23 ||
        payload[2] > 59 || payload[4] > 59) {
        return false;
    }

    schedule.enabled = payload[0] ? 1 : 0;
    schedule.onHour = payload[1];
    schedule.onMinute = payload[2];
    schedule.offHour = payload[3];
    schedule.offMinute = payload[4];
    return true;
}

bool decodeSeparatorPayload(const uint8_t* payload, size_t len, SeparatorConfig& config) {
    if (payload == nullptr || len < 2 || payload[0] > kSeparatorModeMax ||
        payload[1] < kSeparatorIntervalMin || payload[1] > kSeparatorIntervalMax) {
        return false;
    }

    config.mode = payload[0];
    config.intervalSeconds = payload[1];
    return true;
}

bool isScheduleActive(const Schedule& schedule, const tm& time) {
    if (!schedule.enabled) return true;

    int now = time.tm_hour * 60 + time.tm_min;
    int on = schedule.onHour * 60 + schedule.onMinute;
    int off = schedule.offHour * 60 + schedule.offMinute;
    if (on == off) return false;
    if (on < off) return now >= on && now < off;
    return now >= on || now < off;
}

bool isSeparatorOn(const SeparatorConfig& config, uint32_t nowMs) {
    switch (static_cast<SeparatorMode>(config.mode)) {
    case SeparatorMode::Off:
        return false;
    case SeparatorMode::On:
        return true;
    case SeparatorMode::Blink:
    default: {
        uint32_t periodMs = static_cast<uint32_t>(config.intervalSeconds) * 1000u;
        if (periodMs == 0) return true;
        return (nowMs % periodMs) >= (periodMs / 2u);
    }
    }
}

bool takePendingBrightness(BrightnessState& state, uint8_t& appliedBrightness) {
    if (!state.changed) return false;

    state.current = state.pending;
    state.changed = false;
    appliedBrightness = state.current;
    return true;
}

void clear(Frame& frame) {
    std::fill(std::begin(frame.pixels), std::end(frame.pixels), kColorOff);
}

void renderTime(const Schedule& schedule,
                const SeparatorConfig& separator,
                const tm& time,
                uint32_t nowMs,
                Frame& frame) {
    clear(frame);
    if (!isScheduleActive(schedule, time)) return;

    const uint8_t digits[6] = {
        static_cast<uint8_t>(time.tm_sec % 10),
        static_cast<uint8_t>(time.tm_sec / 10),
        static_cast<uint8_t>(time.tm_min % 10),
        static_cast<uint8_t>(time.tm_min / 10),
        static_cast<uint8_t>(time.tm_hour % 10),
        static_cast<uint8_t>(time.tm_hour / 10),
    };

    for (size_t segment = 0; segment < 6; ++segment) {
        uint8_t count = std::min(digits[segment], kSegments[segment].maxValue);
        for (uint8_t i = 0; i < count; ++i) {
            frame.pixels[kSegments[segment].startIndex - i] = kColorWarmWhite;
        }
    }

    const uint32_t separatorColor = isSeparatorOn(separator, nowMs) ? kColorRed : kColorOff;
    frame.pixels[kSeparatorA] = separatorColor;
    frame.pixels[kSeparatorB] = separatorColor;
}

void renderOtaProgress(uint8_t percent, Frame& frame) {
    uint8_t ledsToFill = (static_cast<uint16_t>(percent) * kLedCount) / 100;
    for (uint8_t i = 0; i < kLedCount; ++i) {
        frame.pixels[i] = i < ledsToFill ? kColorTeal : kColorOff;
    }
}

} // namespace TimeDisplayLogic
