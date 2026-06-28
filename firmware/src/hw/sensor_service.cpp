#include "hw/sensor_service.h"

SensorService sensors;

void SensorService::begin() {
    initialized_ = true;

    pinMode(PIN_SENSOR_PIR, INPUT);
    pirDetected_ = (digitalRead(PIN_SENSOR_PIR) == HIGH);

    dallas_.begin();
    present_ = (dallas_.getDeviceCount() > 0);
    if (present_) {
        dallas_.setResolution(12);
        dallas_.requestTemperatures();
        Serial.printf("[SENSOR] DS18B20 found, count=%u\n", dallas_.getDeviceCount());
    } else {
        Serial.println(F("[SENSOR] No DS18B20 detected"));
    }
    Serial.println(F("[MODULE] sensor_service real impl loaded"));
}

void SensorService::loop() {
    if (!initialized_) begin();
    if (initialized_ && !present_) {
        dallas_.begin();
        present_ = (dallas_.getDeviceCount() > 0);
        if (present_) dallas_.setResolution(12);
    }

    uint32_t now = millis();
    if ((uint32_t)(now - lastReadMs_) < SENSOR_INTERVAL_MS) return;
    lastReadMs_ = now;

    pirDetected_ = (digitalRead(PIN_SENSOR_PIR) == HIGH);

    if (present_) {
        dallas_.requestTemperatures();
        float t = dallas_.getTempCByIndex(0);
        if (t > SENSOR_TEMP_INVALID && t < SENSOR_TEMP_MAX) {
            roomTempC_ = t;
        }
    }
}
