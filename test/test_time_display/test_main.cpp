// Host-side tests for the pure display decisions and the injected TimeDisplay
// adapters. Run with `pio test -e native`.
#include <unity.h>

#include <array>
#include <cstdint>
#include <initializer_list>

#include "TimeDisplay.h"
#include "TimeDisplayLogic.h"

using namespace TimeDisplayLogic;

namespace {

tm at(int hour, int minute, int second = 0) {
    tm value = {};
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_sec = second;
    return value;
}

Frame expectedFrame(std::initializer_list<uint8_t> warmPixels,
                    std::initializer_list<uint8_t> redPixels = {}) {
    Frame frame;
    for (uint8_t index : warmPixels) frame.pixels[index] = kColorWarmWhite;
    for (uint8_t index : redPixels) frame.pixels[index] = kColorRed;
    return frame;
}

void assertFrameEquals(const Frame& actual, const Frame& expected) {
    for (uint8_t i = 0; i < kLedCount; ++i) {
        TEST_ASSERT_EQUAL_UINT32(expected.pixels[i], actual.pixels[i]);
    }
}

struct BcdCase {
    tm time;
    Frame expected;
};

void test_bcd_table_has_exact_pixels(void) {
    const Schedule schedule{};
    const SeparatorConfig separator{static_cast<uint8_t>(SeparatorMode::On), 1};
    const BcdCase cases[] = {
        {at(0, 0, 0), expectedFrame({}, {kSeparatorA, kSeparatorB})},
        {at(23, 59, 59),
         expectedFrame({0, 1, 8, 9, 10,
                        12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                        27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40},
                       {kSeparatorA, kSeparatorB})},
        {at(12, 34, 56),
         expectedFrame({1, 9, 10, 14, 15, 16, 22, 23, 24, 25,
                        27, 28, 29, 30, 31, 35, 36, 37, 38, 39, 40},
                       {kSeparatorA, kSeparatorB})},
    };

    for (const BcdCase& testCase : cases) {
        Frame actual;
        renderTime(schedule, separator, testCase.time, 0, actual);
        assertFrameEquals(actual, testCase.expected);
    }
}

void test_schedule_window_table(void) {
    struct ScheduleCase {
        Schedule schedule;
        tm time;
        bool active;
    };
    const ScheduleCase cases[] = {
        {{0, 8, 0, 23, 0}, at(3, 0), true},
        {{1, 8, 0, 23, 0}, at(8, 0), true},
        {{1, 8, 0, 23, 0}, at(12, 30), true},
        {{1, 8, 0, 23, 0}, at(23, 0), false},
        {{1, 22, 0, 6, 0}, at(22, 0), true},
        {{1, 22, 0, 6, 0}, at(2, 0), true},
        {{1, 22, 0, 6, 0}, at(6, 0), false},
        {{1, 22, 0, 6, 0}, at(12, 0), false},
        {{1, 8, 0, 8, 0}, at(7, 59), false},
        {{1, 8, 0, 8, 0}, at(8, 0), false},
    };

    for (const ScheduleCase& testCase : cases) {
        TEST_ASSERT_EQUAL(testCase.active, isScheduleActive(testCase.schedule, testCase.time));
    }
}

void test_schedule_payload_validation(void) {
    Schedule schedule{1, 9, 30, 18, 45};
    const uint8_t valid[] = {1, 7, 5, 22, 59, 99};
    TEST_ASSERT_TRUE(decodeSchedulePayload(valid, sizeof(valid), schedule));
    TEST_ASSERT_EQUAL_UINT8(1, schedule.enabled);
    TEST_ASSERT_EQUAL_UINT8(7, schedule.onHour);
    TEST_ASSERT_EQUAL_UINT8(5, schedule.onMinute);
    TEST_ASSERT_EQUAL_UINT8(22, schedule.offHour);
    TEST_ASSERT_EQUAL_UINT8(59, schedule.offMinute);

    const Schedule unchanged = schedule;
    const uint8_t invalid[] = {1, 24, 0, 23, 0};
    TEST_ASSERT_FALSE(decodeSchedulePayload(invalid, sizeof(invalid), schedule));
    TEST_ASSERT_EQUAL_UINT8(unchanged.onHour, schedule.onHour);
    TEST_ASSERT_FALSE(decodeSchedulePayload(nullptr, 5, schedule));
    TEST_ASSERT_FALSE(decodeSchedulePayload(valid, 4, schedule));
}

void test_separator_modes(void) {
    const struct {
        uint8_t mode;
        uint32_t nowMs;
        bool on;
    } cases[] = {
        {static_cast<uint8_t>(SeparatorMode::Off), 999999, false},
        {static_cast<uint8_t>(SeparatorMode::On), 0, true},
        {static_cast<uint8_t>(SeparatorMode::On), 999999, true},
        {static_cast<uint8_t>(SeparatorMode::Blink), 0, false},
    };
    for (const auto& testCase : cases) {
        TEST_ASSERT_EQUAL(testCase.on, isSeparatorOn({testCase.mode, 1}, testCase.nowMs));
    }
}

void test_separator_blink_phase_boundaries_and_intervals(void) {
    const SeparatorConfig oneSecond{static_cast<uint8_t>(SeparatorMode::Blink), 1};
    const SeparatorConfig sixtySeconds{static_cast<uint8_t>(SeparatorMode::Blink), 60};
    const struct {
        SeparatorConfig config;
        uint32_t nowMs;
        bool on;
    } cases[] = {
        {oneSecond, 499, false},
        {oneSecond, 500, true},
        {oneSecond, 999, true},
        {oneSecond, 1000, false},
        {sixtySeconds, 29999, false},
        {sixtySeconds, 30000, true},
        {sixtySeconds, 59999, true},
        {sixtySeconds, 60000, false},
    };
    for (const auto& testCase : cases) {
        TEST_ASSERT_EQUAL(testCase.on, isSeparatorOn(testCase.config, testCase.nowMs));
    }
}

void test_separator_timer_wraparound_preserves_modulo_phase(void) {
    const SeparatorConfig blink{static_cast<uint8_t>(SeparatorMode::Blink), 1};
    // millis() is an unsigned 32-bit counter. The decision remains defined at
    // both sides of wrap and follows the existing absolute-millis phase.
    TEST_ASSERT_FALSE(isSeparatorOn(blink, UINT32_MAX - 800));
    TEST_ASSERT_TRUE(isSeparatorOn(blink, UINT32_MAX - 500));
    TEST_ASSERT_FALSE(isSeparatorOn(blink, 0));
}

void test_separator_payload_validation(void) {
    SeparatorConfig config{static_cast<uint8_t>(SeparatorMode::Off), 60};
    const uint8_t valid[] = {2, 60, 99};
    TEST_ASSERT_TRUE(decodeSeparatorPayload(valid, sizeof(valid), config));
    TEST_ASSERT_EQUAL_UINT8(2, config.mode);
    TEST_ASSERT_EQUAL_UINT8(60, config.intervalSeconds);

    const SeparatorConfig unchanged = config;
    const uint8_t invalidMode[] = {3, 1};
    const uint8_t invalidInterval[] = {1, 0};
    TEST_ASSERT_FALSE(decodeSeparatorPayload(invalidMode, sizeof(invalidMode), config));
    TEST_ASSERT_FALSE(decodeSeparatorPayload(invalidInterval, sizeof(invalidInterval), config));
    TEST_ASSERT_EQUAL_UINT8(unchanged.mode, config.mode);
    TEST_ASSERT_FALSE(decodeSeparatorPayload(valid, 1, config));
}

void test_ota_progress_pixel_table(void) {
    const struct {
        uint8_t percent;
        uint8_t filled;
    } cases[] = {{0, 0}, {1, 0}, {50, 20}, {99, 40}, {100, 41}, {255, 41}};

    for (const auto& testCase : cases) {
        Frame frame;
        renderOtaProgress(testCase.percent, frame);
        for (uint8_t i = 0; i < kLedCount; ++i) {
            const uint32_t expected = i < testCase.filled ? kColorTeal : kColorOff;
            TEST_ASSERT_EQUAL_UINT32(expected, frame.pixels[i]);
        }
    }
}

void test_pending_brightness_decision(void) {
    BrightnessState state;
    uint8_t applied = 0;
    TEST_ASSERT_FALSE(takePendingBrightness(state, applied));
    state.pending = 42;
    state.changed = true;
    TEST_ASSERT_TRUE(takePendingBrightness(state, applied));
    TEST_ASSERT_EQUAL_UINT8(42, applied);
    TEST_ASSERT_EQUAL_UINT8(42, state.current);
    TEST_ASSERT_FALSE(state.changed);
}

struct FakeStrip : ILedStrip {
    std::array<uint32_t, kLedCount> pixels{};
    uint8_t brightness = 0;
    int showCalls = 0;
    int brightnessCalls = 0;

    void setPixelColor(uint16_t index, uint32_t color) override { pixels[index] = color; }
    void show() override { ++showCalls; }
    void setBrightness(uint8_t value) override {
        brightness = value;
        ++brightnessCalls;
    }
    void clear() override { pixels.fill(kColorOff); }
};

struct FakeClock : IMonotonicClock {
    uint32_t now = 0;
    uint32_t nowMs() const override { return now; }
    void delayMs(uint32_t durationMs) override { now += durationMs; }
};

struct FakeSettings : IDisplaySettingsStore {
    Schedule persistedSchedule{};
    SeparatorConfig persistedSeparator{};
    bool scheduleLoaded = false;
    bool separatorLoaded = false;
    int scheduleSaves = 0;
    int separatorSaves = 0;

    void loadSchedule(Schedule& schedule) override {
        schedule = persistedSchedule;
        scheduleLoaded = true;
    }
    void saveSchedule(const Schedule& schedule) override {
        persistedSchedule = schedule;
        ++scheduleSaves;
    }
    void loadSeparator(SeparatorConfig& config) override {
        config = persistedSeparator;
        separatorLoaded = true;
    }
    void saveSeparator(const SeparatorConfig& config) override {
        persistedSeparator = config;
        ++separatorSaves;
    }
};

void test_time_display_loads_persisted_values_from_fake_store(void) {
    FakeStrip strip;
    FakeClock clock;
    FakeSettings settings;
    settings.persistedSchedule = {1, 20, 15, 21, 45};
    settings.persistedSeparator = {static_cast<uint8_t>(SeparatorMode::On), 60};
    TimeDisplay display(strip, clock, settings);

    display.loadSchedule();
    display.loadSeparatorConfig();
    uint8_t scheduleBytes[5] = {};
    uint8_t separatorBytes[2] = {};
    display.getScheduleBytes(scheduleBytes);
    display.getSeparatorConfigBytes(separatorBytes);
    TEST_ASSERT_TRUE(settings.scheduleLoaded);
    TEST_ASSERT_TRUE(settings.separatorLoaded);
    const uint8_t expectedSchedule[] = {1, 20, 15, 21, 45};
    const uint8_t expectedSeparator[] = {1, 60};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedSchedule, scheduleBytes, 5);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedSeparator, separatorBytes, 2);

    display.update(at(20, 15, 0));
    TEST_ASSERT_EQUAL_UINT32(kColorRed, strip.pixels[kSeparatorA]);
    TEST_ASSERT_EQUAL_UINT32(kColorRed, strip.pixels[kSeparatorB]);
    TEST_ASSERT_EQUAL_UINT32(kColorWarmWhite, strip.pixels[1]);
}

void test_time_display_blanks_and_restores_brightness_without_persistence_side_effect(void) {
    FakeStrip strip;
    FakeClock clock;
    FakeSettings settings;
    TimeDisplay display(strip, clock, settings);
    const uint8_t schedule[] = {1, 8, 0, 18, 0};
    display.setSchedule(schedule, sizeof(schedule));
    display.setBrightness(42);

    display.update(at(19, 0));
    TEST_ASSERT_EQUAL_UINT8(42, strip.brightness);
    TEST_ASSERT_EQUAL_INT(1, strip.brightnessCalls);
    for (uint32_t pixel : strip.pixels) TEST_ASSERT_EQUAL_UINT32(kColorOff, pixel);

    display.update(at(8, 0));
    TEST_ASSERT_EQUAL_UINT32(kColorWarmWhite, strip.pixels[10]);
    TEST_ASSERT_EQUAL_UINT8(42, strip.brightness);
    TEST_ASSERT_EQUAL_INT(1, strip.brightnessCalls);
    TEST_ASSERT_EQUAL_INT(1, settings.scheduleSaves);
}

void test_time_display_persists_only_valid_payloads(void) {
    FakeStrip strip;
    FakeClock clock;
    FakeSettings settings;
    TimeDisplay display(strip, clock, settings);
    const uint8_t invalidSchedule[] = {1, 24, 0, 23, 0};
    const uint8_t invalidSeparator[] = {3, 1};
    display.setSchedule(invalidSchedule, sizeof(invalidSchedule));
    display.setSeparatorConfig(invalidSeparator, sizeof(invalidSeparator));
    TEST_ASSERT_EQUAL_INT(0, settings.scheduleSaves);
    TEST_ASSERT_EQUAL_INT(0, settings.separatorSaves);
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_bcd_table_has_exact_pixels);
    RUN_TEST(test_schedule_window_table);
    RUN_TEST(test_schedule_payload_validation);
    RUN_TEST(test_separator_modes);
    RUN_TEST(test_separator_blink_phase_boundaries_and_intervals);
    RUN_TEST(test_separator_timer_wraparound_preserves_modulo_phase);
    RUN_TEST(test_separator_payload_validation);
    RUN_TEST(test_ota_progress_pixel_table);
    RUN_TEST(test_pending_brightness_decision);
    RUN_TEST(test_time_display_loads_persisted_values_from_fake_store);
    RUN_TEST(test_time_display_blanks_and_restores_brightness_without_persistence_side_effect);
    RUN_TEST(test_time_display_persists_only_valid_payloads);
    return UNITY_END();
}
