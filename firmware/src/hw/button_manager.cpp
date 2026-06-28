#include "hw/button_manager.h"
#include "core/pins.h"

ButtonManager button;

void ButtonManager::begin(ShortPressCallback onShort, LongPressCallback onLong) {
    onShort_       = onShort;
    onLong_        = onLong;
    btnPressed_     = false;
    btnLongHandled_ = false;
    btnPressStart_  = 0;
    lastCheckMs_    = 0;
    Serial.println(F("[MODULE] button_manager real impl loaded"));
}

void ButtonManager::loop() {
    uint32_t now = millis();
    if ((uint32_t)(now - lastCheckMs_) < BTN_CHECK_INTERVAL_MS) return;
    lastCheckMs_ = now;

    bool ledWasOn = (digitalRead(PIN_LED_YELLOW) == LED_LEVEL_ON);
    pinMode(PIN_LED_YELLOW, INPUT_PULLUP);
    delayMicroseconds(100);
    bool btnState = digitalRead(PIN_LED_YELLOW);
    pinMode(PIN_LED_YELLOW, OUTPUT);
    if (ledWasOn) ledOn(PIN_LED_YELLOW); else ledOff(PIN_LED_YELLOW);

    if (btnState == LED_LEVEL_ON) {
        if (!btnPressed_) {
            btnPressed_     = true;
            btnLongHandled_ = false;
            btnPressStart_  = millis();
            Serial.println(F("[BTN] Pressed"));
        } else {
            unsigned long elapsed = millis() - btnPressStart_;
            if (elapsed >= BTN_LONG_PRESS_MS && !btnLongHandled_) {
                btnLongHandled_ = true;
                Serial.println(F("[BTN] Long press → onLong callback"));
                if (onLong_) onLong_();
            } else if (elapsed >= 2000 && !btnLongHandled_) {
                if ((millis() / 200) % 2 == 0) ledOn(PIN_LED_YELLOW);
                else ledOff(PIN_LED_YELLOW);
            }
        }
    } else {
        if (btnPressed_) {
            unsigned long elapsed = millis() - btnPressStart_;
            if (!btnLongHandled_ && elapsed >= BTN_SHORT_PRESS_MIN_MS &&
                elapsed <= BTN_SHORT_PRESS_MAX_MS) {
                if (onShort_) onShort_();
            } else if (!btnLongHandled_ && elapsed > BTN_SHORT_PRESS_MAX_MS) {
                Serial.println(F("[BTN] Release ignored (hold too long for shortcut)"));
            }
        }
        btnPressed_     = false;
        btnLongHandled_ = false;
    }
}
