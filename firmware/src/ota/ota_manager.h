#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "core/constants.h"

class OtaManager {
public:
    struct ManifestInfo {
        uint32_t manifestOffset;
        uint32_t imageSize;
        uint32_t imageCrc32;
        uint8_t  target;
        char     version[24];
    };

    void begin();

    bool rollbackIfPending(const char* reason);

    void onUploadStart();
    bool onUploadChunk(const uint8_t* data, size_t len);
    void onUploadEnd();

    bool verifyInFlash(uint32_t baseAddr, uint32_t imageSize,
                       uint8_t targetRom, ManifestInfo& info,
                       char* reason, size_t reasonLen);

    bool isPending() const { return state_.pending; }
    void markConfirmReady();
    void confirmIfReady();

    uint32_t  lastUploadSize() const     { return uploadSize_; }
    bool      lastUploadOk() const        { return writeOk_; }
    const char* lastRejectReason() const { return rejectReason_; }
    void switchToTargetRom();

private:
    struct State {
        uint32_t magic;
        uint8_t  version;
        bool     pending;
        uint8_t  previous_rom;
        uint32_t timestamp;
    } state_;

    size_t    uploadSize_     = 0;
    uint32_t  targetAddr_     = 0;
    bool      writeOk_        = true;
    bool      headerChecked_  = false;
    char      rejectReason_[96];
    uint8_t   accBuf_[4096];
    size_t    accLen_         = 0;
    uint32_t  confirmAt_      = 0;

    void loadState();
    void clearState();
    void reject(const char* reason);
};

extern OtaManager ota;
