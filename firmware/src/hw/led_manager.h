#pragma once

#include <Arduino.h>
#include "core/pins.h"

class LedManager {
public:
    void begin();

    void setBlue(bool on);
    void setRed(bool on);
    void setYellow(bool on);
    void setAll(bool blue, bool red, bool yellow);

    void blink(uint8_t pin, uint8_t count, uint16_t onMs, uint16_t offMs);
    void blinkRestore(uint8_t pin, uint8_t count, uint16_t onMs, uint16_t offMs);

    void flashRed(uint16_t durationMs = 30);

    void selfTest();

    void loop();
};

extern LedManager leds;
