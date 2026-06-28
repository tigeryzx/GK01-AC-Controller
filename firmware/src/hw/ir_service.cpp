#include "hw/ir_service.h"

IrService ir;

void IrService::begin() {
    irSend_.begin();
    irRecv_.enableIRIn();
    Serial.println(F("[MODULE] ir_service real impl loaded"));
}

void IrService::disableRx() { irRecv_.disableIRIn(); }
void IrService::enableRx()  { irRecv_.enableIRIn(); }

void IrService::sendRaw(const uint16_t* timings, size_t len) {
    irRecv_.disableIRIn();
    irSend_.sendRaw(timings, len, 38);
    irRecv_.enableIRIn();
}

bool IrService::sendAc(const stdAc::state_t& state) {
    irRecv_.disableIRIn();
    bool ok = ac_.sendAc(state);
    irRecv_.enableIRIn();
    return ok;
}

void IrService::pollCapture() {
    decode_results results;
    if (!irRecv_.decode(&results)) return;

    irRecv_.disableIRIn();

    captureLen_ = 0;
    for (uint16_t i = 1; i < results.rawlen; i++) {
        if (captureLen_ + 8 >= CAPTURE_BUF_SIZE) break;
        if (i > 1) captureBuf_[captureLen_++] = ',';
        int n = snprintf(captureBuf_ + captureLen_, CAPTURE_BUF_SIZE - captureLen_,
                         "%u", (unsigned)(results.rawbuf[i] * kRawTick));
        if (n <= 0) break;
        captureLen_ += (size_t)n;
    }
    captureBuf_[captureLen_] = '\0';

    String proto = typeToString(results.decode_type);
    strncpy(captureProto_, proto.c_str(), sizeof(captureProto_) - 1);
    captureProto_[sizeof(captureProto_) - 1] = '\0';
    captureBits_ = results.bits;
    hasCapture_  = true;

    Serial.printf("[IR] %s %dbit len=%u\n", captureProto_, captureBits_, (unsigned)captureLen_);

    irRecv_.resume();
    irRecv_.enableIRIn();
}

bool IrService::consumeCapture(Capture& out) {
    if (!hasCapture_) return false;
    strncpy(out.proto, captureProto_, sizeof(out.proto) - 1);
    out.proto[sizeof(out.proto) - 1] = '\0';
    out.bits   = captureBits_;
    out.raw    = captureBuf_;
    out.rawLen = captureLen_;
    hasCapture_ = false;
    return true;
}

void IrService::discardCapture() {
    hasCapture_ = false;
}
