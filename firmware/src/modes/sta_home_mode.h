#pragma once

#include "modes/device_mode.h"

class StaHomeMode : public DeviceMode {
public:
    void onEnter() override;
    void loop() override;
    const char* name() const override { return "STA_HOME"; }
};
