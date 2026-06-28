#pragma once

#include "modes/device_mode.h"

class ApMasterMode : public DeviceMode {
public:
    void onEnter() override;
    void loop() override;
    const char* name() const override { return "AP_MASTER"; }
};
