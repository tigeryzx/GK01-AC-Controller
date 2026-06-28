#include "modes/sta_home_mode.h"
#include "app/app_context.h"
#include "net/mqtt_service.h"
#include "web/web_server.h"
#include "hw/ir_service.h"
#include "hw/sensor_service.h"
#include "hw/led_manager.h"
#include "config/config_store.h"

void StaHomeMode::onEnter() {
    Serial.println(F("[STA_HOME] onEnter"));

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[STA_HOME] WiFi not connected, fallback to AP"));
        ctx.changeMode(DeviceModeId::ApMaster);
        return;
    }

    leds.setAll(true, false, true);

    web.beginSTA();
    web.registerApiRoutes();

    ir.enableRx();
    sensors.begin();

    if (mqtt.enabled()) {
        mqtt.connect();
    }

    Serial.println(F("=== STA Home Ready ==="));
}

void StaHomeMode::loop() {
    web.loop();
    sensors.loop();
    mqtt.loop();
    network.maintain();
    ir.pollCapture();
}
