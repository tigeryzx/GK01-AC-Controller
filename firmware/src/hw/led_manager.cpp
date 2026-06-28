#include "hw/led_manager.h"

LedManager leds;

namespace {
unsigned long ledOffTime_  = 0;
bool          ledBlinking_ = false;
}

void LedManager::begin() {
    setAll(false, false, false);
    Serial.println(F("[MODULE] led_manager real impl loaded"));
}

void LedManager::setBlue(bool on)   { on ? ledOn(PIN_LED_BLUE)   : ledOff(PIN_LED_BLUE); }
void LedManager::setRed(bool on)    { on ? ledOn(PIN_LED_RED)    : ledOff(PIN_LED_RED); }
void LedManager::setYellow(bool on) { on ? ledOn(PIN_LED_YELLOW) : ledOff(PIN_LED_YELLOW); }

void LedManager::setAll(bool blue, bool red, bool yellow) {
    setBlue(blue); setRed(red); setYellow(yellow);
}

void LedManager::blink(uint8_t pin, uint8_t count, uint16_t onMs, uint16_t offMs) {
    for (uint8_t i = 0; i < count; i++) {
        ledOn(pin);  delay(onMs);
        ledOff(pin); delay(offMs);
        yield();
    }
}

void LedManager::blinkRestore(uint8_t pin, uint8_t count, uint16_t onMs, uint16_t offMs) {
    bool wasOn = (digitalRead(pin) == LED_LEVEL_ON);
    blink(pin, count, onMs, offMs);
    if (wasOn) ledOn(pin); else ledOff(pin);
}

void LedManager::flashRed(uint16_t durationMs) {
    ledOn(PIN_LED_RED);
    ledOffTime_  = millis() + durationMs;
    ledBlinking_ = true;
}

void LedManager::selfTest() {
    setAll(true, true, true);
    delay(300);
    setAll(false, false, false);
    delay(120);
}

void LedManager::loop() {
    if (ledBlinking_ && (long)(millis() - ledOffTime_) >= 0) {
        ledOff(PIN_LED_RED);
        ledBlinking_ = false;
    }
}
