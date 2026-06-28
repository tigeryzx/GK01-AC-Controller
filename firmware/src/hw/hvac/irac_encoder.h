#pragma once

#include "hw/hvac/hvac_encoder.h"
#include "core/constants.h"

class IracEncoder : public hvac::IEncoder {
public:
    const char* vendor() const override { return "IRAC_GENERIC"; }
    int  minTemp() const override       { return HVAC_MIN_TEMP_DEFAULT; }
    int  maxTemp() const override       { return HVAC_MAX_TEMP_DEFAULT; }
    bool send(const hvac::Command& cmd) override;
};
