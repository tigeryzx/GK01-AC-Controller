#include <Arduino.h>

#include "app/app_context.h"
#include "app/mode_factory.h"
#include "core/pins.h"
#include "net/network_manager.h"
#include "config/config_store.h"
#include "hw/led_manager.h"
#include "hw/button_manager.h"
#include "ota/ota_manager.h"

static void onButtonShortPress() {
    Serial.println(F("[BTN] short (TBD: office preset)"));
}

static void onButtonLongPress() {
    Serial.println(F("[BTN] long → factory reset"));
    for (int i = 0; i < 10; i++) {
        leds.setYellow(true); delay(50);
        leds.setYellow(false); delay(50);
    }
    configStore.factoryResetWipe();
    delay(100);
    ESP.restart();
}

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println(F("[BOOT] IR Mini V105 — architecture overhaul"));

    pinMode(PIN_LED_BLUE,   OUTPUT);
    pinMode(PIN_LED_RED,    OUTPUT);
    pinMode(PIN_LED_YELLOW, OUTPUT);
    leds.setAll(false, false, false);
    leds.selfTest();
    leds.setAll(false, false, true);

    NetworkManager::setNoneSleep();

    ctx.begin();
    button.begin(onButtonShortPress, onButtonLongPress);

    DeviceModeId id = ctx.decideBootMode();
    Serial.printf("[BOOT] mode: %s\n", modeString(id));
    ctx.changeMode(id);
}

void loop() {
    ctx.loop();
}
