#pragma once

#include <Arduino.h>
#include "core/constants.h"

class ButtonManager {
public:
    using ShortPressCallback    = void (*)();
    using LongPressCallback     = void (*)();

    void begin(ShortPressCallback onShort, LongPressCallback onLong);

    void loop();

private:
    ShortPressCallback onShort_ = nullptr;
    LongPressCallback  onLong_  = nullptr;

    bool     btnPressed_      = false;
    bool     btnLongHandled_  = false;
    uint32_t btnPressStart_   = 0;
    uint32_t lastCheckMs_     = 0;
};

extern ButtonManager button;
