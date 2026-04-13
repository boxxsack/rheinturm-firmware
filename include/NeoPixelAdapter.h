#pragma once

#include "ILedStrip.h"
#include <Adafruit_NeoPixel.h>

class NeoPixelAdapter : public ILedStrip {
public:
    explicit NeoPixelAdapter(Adafruit_NeoPixel& strip);

    void setPixelColor(uint16_t index, uint32_t color) override;
    void show() override;
    void setBrightness(uint8_t brightness) override;
    void clear() override;

private:
    Adafruit_NeoPixel& _strip;
};
