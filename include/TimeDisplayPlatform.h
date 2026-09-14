#pragma once

#include "TimeDisplay.h"

#include <cstdint>

// ESP32 adapters keep Arduino timing and NVS details out of TimeDisplay and
// its host-testable logic.
class ArduinoMonotonicClock final : public IMonotonicClock {
public:
    uint32_t nowMs() const override;
    void delayMs(uint32_t durationMs) override;
};

class PreferencesDisplaySettings final : public IDisplaySettingsStore {
public:
    void loadSchedule(TimeDisplayLogic::Schedule& schedule) override;
    void saveSchedule(const TimeDisplayLogic::Schedule& schedule) override;
    void loadSeparator(TimeDisplayLogic::SeparatorConfig& config) override;
    void saveSeparator(const TimeDisplayLogic::SeparatorConfig& config) override;
};
