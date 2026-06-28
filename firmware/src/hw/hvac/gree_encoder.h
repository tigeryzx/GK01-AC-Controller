#pragma once

#include "hw/hvac/hvac_encoder.h"

class GreeEncoder : public hvac::IEncoder {
public:
    const char* vendor() const override { return "GREE"; }
    int  minTemp() const override       { return 16; }
    int  maxTemp() const override       { return 30; }
    bool send(const hvac::Command& cmd) override;
};
