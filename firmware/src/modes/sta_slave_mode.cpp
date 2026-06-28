#include "modes/sta_slave_mode.h"
#include "app/app_context.h"
#include "net/udp_mesh.h"
#include "hw/ir_service.h"
#include "hw/led_manager.h"
#include "config/config_store.h"

void StaSlaveMode::onEnter() {
    Serial.println(F("[STA_SLAVE] onEnter"));

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[STA_SLAVE] WiFi not connected, fallback to AP"));
        ctx.changeMode(DeviceModeId::ApMaster);
        return;
    }

    leds.setAll(true, false, true);
    ir.enableRx();
    udp.beginSlave();

    delay(200);
    String hello = "HELLO:" + WiFi.macAddress() + ":" +
                   String(configStore.cfg.device_name) + ":" +
                   String(configStore.cfg.device_icon) + ":" +
                   String(configStore.cfg.device_floor);
    udp.sendTo(IPAddress(10, 1, 1, 1), hello.c_str());

    Serial.println(F("=== Slave Ready ==="));
}

void StaSlaveMode::loop() {
    udp.loopSlave();
}
