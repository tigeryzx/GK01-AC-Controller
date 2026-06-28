#pragma once

#include <Arduino.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRac.h>
#include <IRutils.h>
#include "core/pins.h"

class IrService {
public:
    static const size_t CAPTURE_BUF_SIZE = 1200;

    struct Capture {
        char     proto[16];
        int      bits;
        const char* raw;     // 指向内部 captureBuf_，调用者不应保留
        size_t   rawLen;
    };

    void begin();

    void sendRaw(const uint16_t* timings, size_t len);
    bool sendAc(const stdAc::state_t& state);

    void disableRx();
    void enableRx();
    void pollCapture();

    IRsend& rawSender() { return irSend_; }

    bool hasCapture() const { return hasCapture_; }
    bool consumeCapture(Capture& out);
    void discardCapture();

private:
    IRsend irSend_{PIN_IR_TX};
    IRrecv irRecv_{PIN_IR_RX, 1024, 50, false};
    IRac   ac_{PIN_IR_TX};

    char     captureBuf_[CAPTURE_BUF_SIZE];
    size_t   captureLen_  = 0;
    char     captureProto_[16];
    int      captureBits_ = 0;
    bool     hasCapture_  = false;
};

extern IrService ir;
