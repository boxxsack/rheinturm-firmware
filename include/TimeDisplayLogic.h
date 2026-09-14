#pragma once

#include <cstddef>
#include <cstdint>
#include <time.h>

namespace TimeDisplayLogic {

constexpr uint8_t kLedCount = 41;
constexpr uint8_t kSeparatorA = 11;
constexpr uint8_t kSeparatorB = 26;

constexpr uint32_t kColorWarmWhite = (255u << 16) | (243u << 8) | 170u;
constexpr uint32_t kColorRed = (255u << 16);
constexpr uint32_t kColorOff = 0;
constexpr uint32_t kColorTeal = (4u << 16) | (94u << 8) | 135u;

enum class SeparatorMode : uint8_t {
    Off = 0,
    On = 1,
    Blink = 2,
};

struct Schedule {
    uint8_t enabled = 0;
    uint8_t onHour = 8;
    uint8_t onMinute = 0;
    uint8_t offHour = 23;
    uint8_t offMinute = 0;
};

struct SeparatorConfig {
    uint8_t mode = static_cast<uint8_t>(SeparatorMode::Blink);
    uint8_t intervalSeconds = 1;
};

struct Frame {
    uint32_t pixels[kLedCount] = {};
};

struct BrightnessState {
    uint8_t current = 100;
    uint8_t pending = 100;
    bool changed = false;
};

bool decodeSchedulePayload(const uint8_t* payload, size_t len, Schedule& schedule);
bool decodeSeparatorPayload(const uint8_t* payload, size_t len, SeparatorConfig& config);

bool isScheduleActive(const Schedule& schedule, const tm& time);
bool isSeparatorOn(const SeparatorConfig& config, uint32_t nowMs);

// Returns true and copies the new value to appliedBrightness when a pending
// brightness value was consumed. This keeps brightness restoration independent
// from whether the current schedule causes the frame to be blank.
bool takePendingBrightness(BrightnessState& state, uint8_t& appliedBrightness);

void renderTime(const Schedule& schedule,
                const SeparatorConfig& separator,
                const tm& time,
                uint32_t nowMs,
                Frame& frame);
void renderOtaProgress(uint8_t percent, Frame& frame);
void clear(Frame& frame);

} // namespace TimeDisplayLogic
