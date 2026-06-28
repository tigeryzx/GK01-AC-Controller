#include "hw/hvac/hvac_registry.h"
#include "hw/hvac/wahin_encoder.h"
#include "hw/hvac/gree_encoder.h"
#include "hw/hvac/irac_encoder.h"
#include "core/constants.h"
#include <IRutils.h>

HvacRegistry hvacRegistry;

namespace {
WahinEncoder wahinEnc;
GreeEncoder greeEnc;
IracEncoder  iracEnc;
}

void HvacRegistry::begin(IrService& irRef) {
    ir_ = &irRef;
    count_ = 0;
    registerEncoder(&wahinEnc);
    registerEncoder(&greeEnc);
    Serial.println(F("[MODULE] hvac_registry real impl loaded"));
}

void HvacRegistry::registerEncoder(hvac::IEncoder* enc) {
    if (enc && count_ < MAX_ENCODERS) {
        encoders_[count_++] = enc;
    }
}

hvac::IEncoder* HvacRegistry::find(const String& vendor) const {
    for (size_t i = 0; i < count_; i++) {
        if (encoders_[i] && vendor == encoders_[i]->vendor()) {
            return encoders_[i];
        }
    }
    return nullptr;
}

bool HvacRegistry::isKnownVendor(const String& vendor) const {
    if (find(vendor)) return true;
    decode_type_t proto = strToDecodeType(vendor.c_str());
    return proto != decode_type_t::UNKNOWN;
}

int HvacRegistry::minTempFor(const String& vendor) const {
    hvac::IEncoder* enc = find(vendor);
    if (enc) return enc->minTemp();
    return HVAC_MIN_TEMP_DEFAULT;
}

int HvacRegistry::maxTempFor(const String& vendor) const {
    hvac::IEncoder* enc = find(vendor);
    if (enc) return enc->maxTemp();
    return HVAC_MAX_TEMP_DEFAULT;
}

bool HvacRegistry::send(hvac::Command& cmd) {
    hvac::IEncoder* enc = find(cmd.vendor);
    if (enc) return enc->send(cmd);

    Serial.printf("[HVAC] no dedicated encoder for %s, fallback IRac\n", cmd.vendor.c_str());
    return iracEnc.send(cmd);
}
