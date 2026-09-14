#include "TimeDisplayPlatform.h"

#include <Arduino.h>
#include <Preferences.h>

uint32_t ArduinoMonotonicClock::nowMs() const {
    return millis();
}

void ArduinoMonotonicClock::delayMs(uint32_t durationMs) {
    delay(durationMs);
}

void PreferencesDisplaySettings::loadSchedule(TimeDisplayLogic::Schedule& schedule) {
    Preferences prefs;
    prefs.begin("schedule", true);
    schedule.enabled = prefs.getUChar("en", schedule.enabled);
    schedule.onHour = prefs.getUChar("onH", schedule.onHour);
    schedule.onMinute = prefs.getUChar("onM", schedule.onMinute);
    schedule.offHour = prefs.getUChar("offH", schedule.offHour);
    schedule.offMinute = prefs.getUChar("offM", schedule.offMinute);
    prefs.end();
    Serial.printf("Schedule loaded: enabled=%d on=%02d:%02d off=%02d:%02d\n",
                  schedule.enabled, schedule.onHour, schedule.onMinute,
                  schedule.offHour, schedule.offMinute);
}

void PreferencesDisplaySettings::saveSchedule(const TimeDisplayLogic::Schedule& schedule) {
    Preferences prefs;
    prefs.begin("schedule", false);
    prefs.putUChar("en", schedule.enabled);
    prefs.putUChar("onH", schedule.onHour);
    prefs.putUChar("onM", schedule.onMinute);
    prefs.putUChar("offH", schedule.offHour);
    prefs.putUChar("offM", schedule.offMinute);
    prefs.end();
}

void PreferencesDisplaySettings::loadSeparator(TimeDisplayLogic::SeparatorConfig& config) {
    Preferences prefs;
    prefs.begin("separator", true);
    config.mode = prefs.getUChar("mode", config.mode);
    config.intervalSeconds = prefs.getUChar("ival", config.intervalSeconds);
    prefs.end();
    Serial.printf("Separator loaded: mode=%u interval=%us\n", config.mode, config.intervalSeconds);
}

void PreferencesDisplaySettings::saveSeparator(const TimeDisplayLogic::SeparatorConfig& config) {
    Preferences prefs;
    prefs.begin("separator", false);
    prefs.putUChar("mode", config.mode);
    prefs.putUChar("ival", config.intervalSeconds);
    prefs.end();
}
