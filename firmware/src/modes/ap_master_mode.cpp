#include "modes/ap_master_mode.h"
#include "app/app_context.h"
#include "net/udp_mesh.h"
#include "web/web_server.h"
#include "hw/ir_service.h"
#include "hw/sensor_service.h"
#include "hw/led_manager.h"
#include "config/config_store.h"

void ApMasterMode::onEnter() {
    Serial.println(F("[AP_MASTER] onEnter"));

    leds.setAll(false, false, true);

    if (!network.beginAP(getApSsid(), getApPass())) {
        Serial.println(F("[AP_MASTER] start failed"));
        return;
    }

    web.beginAP();
    web.startCaptivePortal();
    web.registerApiRoutes();

    ir.enableRx();
    udp.beginMaster();
    sensors.begin();

    udp.broadcast("BOOT:");

    for (int i = 0; i < 3; i++) {
        leds.setBlue(true);  delay(100);
        leds.setBlue(false); delay(100);
    }
    leds.setAll(false, false, true);
    Serial.println(F("=== Master Ready ==="));
}

void ApMasterMode::loop() {
    web.loop();
    sensors.loop();
    udp.loopMaster();

    if (ctx.pairing.active()) {
        leds.setRed((millis() / 500) % 2 == 0);
    }

    ir.pollCapture();
}
