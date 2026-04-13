#include "NeoPixelAdapter.h"

NeoPixelAdapter::NeoPixelAdapter(Adafruit_NeoPixel& strip)
    : _strip(strip) {}

void NeoPixelAdapter::setPixelColor(uint16_t index, uint32_t color) {
    _strip.setPixelColor(index, color);
}

void NeoPixelAdapter::show() {
    _strip.show();
}

void NeoPixelAdapter::setBrightness(uint8_t brightness) {
    _strip.setBrightness(brightness);
}

void NeoPixelAdapter::clear() {
    _strip.clear();
}
