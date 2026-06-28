#pragma once

#include "hw/hvac/hvac_encoder.h"

class WahinEncoder : public hvac::IEncoder {
public:
    const char* vendor() const override { return "WAHIN"; }
    int  minTemp() const override       { return 17; }
    int  maxTemp() const override       { return 30; }
    bool send(const hvac::Command& cmd) override;
};
