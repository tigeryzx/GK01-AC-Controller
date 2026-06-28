#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "core/pins.h"
#include "core/constants.h"

class SensorService {
public:
    void begin();
    void loop();

    bool   present() const { return present_; }
    float  temperatureC() const { return roomTempC_; }
    bool   motionDetected() const { return pirDetected_; }

private:
    OneWire           oneWire_{PIN_SENSOR_TEMP};
    DallasTemperature dallas_{&oneWire_};
    bool     initialized_    = false;
    bool     present_        = false;
    float    roomTempC_      = SENSOR_TEMP_INVALID;
    bool     pirDetected_    = false;
    uint32_t lastReadMs_     = 0;
};

extern SensorService sensors;
