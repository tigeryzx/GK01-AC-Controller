#pragma once

#include "modes/device_mode.h"

class StaSlaveMode : public DeviceMode {
public:
    void onEnter() override;
    void loop() override;
    const char* name() const override { return "STA_SLAVE"; }
};
