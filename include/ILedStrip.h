#pragma once

#include <cstdint>

class ILedStrip {
public:
    virtual ~ILedStrip() = default;
    virtual void setPixelColor(uint16_t index, uint32_t color) = 0;
    virtual void show() = 0;
    virtual void setBrightness(uint8_t brightness) = 0;
};
