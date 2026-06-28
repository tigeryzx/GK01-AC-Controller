#pragma once

#include <Arduino.h>
#include "hw/hvac/hvac_encoder.h"
#include "hw/ir_service.h"

class HvacRegistry {
public:
    static const size_t MAX_ENCODERS = 4;

    void begin(IrService& ir);

    void registerEncoder(hvac::IEncoder* enc);

    bool send(hvac::Command& cmd);
    bool isKnownVendor(const String& vendor) const;
    int  minTempFor(const String& vendor) const;
    int  maxTempFor(const String& vendor) const;

private:
    hvac::IEncoder* encoders_[MAX_ENCODERS] = {nullptr};
    size_t   count_ = 0;
    IrService* ir_  = nullptr;

    hvac::IEncoder* find(const String& vendor) const;
};

extern HvacRegistry hvacRegistry;
