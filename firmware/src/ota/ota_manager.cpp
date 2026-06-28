#include "ota/ota_manager.h"
#include "config/config_store.h"
#include <LittleFS.h>
#include <spi_flash.h>
#include "rboot.h"
#include <string.h>

OtaManager ota;

namespace {

inline constexpr uint32_t OTA_STATE_MAGIC       = 0x4F544153;
inline constexpr uint8_t  OTA_STATE_VERSION     = 0x01;
inline constexpr uint32_t OTA_SLOT_MAX_SIZE     = 0xFE000;
inline constexpr uint32_t OTA_APP_FIRST_SECTION = 0x40202010UL;
inline constexpr uint8_t  OTA_FLASH_MODE_DOUT   = 0x03;
inline constexpr uint8_t  OTA_RBOOT_MAGIC_NEW1  = 0xEA;
inline constexpr uint8_t  OTA_RBOOT_MAGIC_NEW2  = 0x04;
inline constexpr uint32_t OTA_IROM_BASE         = 0x40200000UL;
inline constexpr uint32_t OTA_IROM_LIMIT        = 0x40300000UL;
inline constexpr size_t   OTA_MANIFEST_SIZE         = 64;
inline constexpr uint8_t  OTA_MANIFEST_VERSION      = 0x01;
inline constexpr uint8_t  OTA_MANIFEST_FORMAT_EA04  = 0x01;
inline constexpr uint8_t  OTA_MANIFEST_TARGET_ANY   = 0xFF;
inline constexpr size_t   OTA_MANIFEST_CRC_OFFSET   = 16;
inline constexpr size_t   OTA_BOARD_FIELD_SIZE      = 20;
inline constexpr size_t   OTA_VERSION_FIELD_SIZE    = 20;
const uint8_t OTA_MANIFEST_MAGIC[8] = {'I','R','A','C','O','T','A','1'};
const char OTA_BOARD_ID[] = "GK01_IR_MINI_V105";

uint32_t readLe32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool rangeWithin(uint32_t offset, uint32_t len, uint32_t imageSize) {
    return offset <= imageSize && len <= imageSize - offset;
}

bool isIromAddress(uint32_t addr) {
    return addr >= OTA_IROM_BASE && addr < OTA_IROM_LIMIT;
}

void verifyReason(char* reason, size_t reasonLen, const char* msg) {
    if (!reason || reasonLen == 0) return;
    strncpy(reason, msg, reasonLen - 1);
    reason[reasonLen - 1] = '\0';
}

bool flashReadBytes(uint32_t addr, uint8_t* out, size_t len) {
    if (!out && len > 0) return false;
    while (len > 0) {
        uint32_t alignedAddr = addr & ~0x03UL;
        uint32_t word = 0xFFFFFFFF;
        noInterrupts();
        bool ok = (spi_flash_read(alignedAddr, &word, sizeof(word)) == SPI_FLASH_RESULT_OK);
        interrupts();
        if (!ok) return false;
        uint8_t off = (uint8_t)(addr & 0x03);
        size_t copy = min((size_t)(4 - off), len);
        memcpy(out, ((uint8_t*)&word) + off, copy);
        out += copy; addr += copy; len -= copy;
    }
    return true;
}

uint32_t crc32UpdateByte(uint32_t crc, uint8_t value) {
    crc ^= value;
    for (uint8_t i = 0; i < 8; i++) {
        crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }
    return crc;
}

bool flashCrc32WithZeroField(uint32_t addr, uint32_t len,
                             uint32_t zeroOffset, uint32_t zeroLen,
                             uint32_t& outCrc) {
    uint8_t buf[256];
    uint32_t pos = 0;
    uint32_t crc = 0xFFFFFFFFUL;
    while (pos < len) {
        size_t chunk = (size_t)min((uint32_t)sizeof(buf), len - pos);
        if (!flashReadBytes(addr + pos, buf, chunk)) return false;
        for (size_t i = 0; i < chunk; i++) {
            uint32_t off = pos + i;
            uint8_t value = (off >= zeroOffset && off < zeroOffset + zeroLen) ? 0 : buf[i];
            crc = crc32UpdateByte(crc, value);
        }
        pos += chunk;
        yield();
    }
    outCrc = ~crc;
    return true;
}

bool fixedFieldEquals(const uint8_t* field, size_t fieldLen, const char* expected) {
    size_t expectedLen = strlen(expected);
    if (expectedLen >= fieldLen) return false;
    if (memcmp(field, expected, expectedLen) != 0) return false;
    for (size_t i = expectedLen; i < fieldLen; i++) {
        if (field[i] != '\0') return false;
    }
    return true;
}

void copyFixedField(char* dst, size_t dstLen, const uint8_t* field, size_t fieldLen) {
    if (!dst || dstLen == 0) return;
    size_t n = min(dstLen - 1, fieldLen);
    size_t i = 0;
    for (; i < n && field[i] != '\0'; i++) dst[i] = (char)field[i];
    dst[i] = '\0';
}

bool validateRbootImage(uint32_t baseAddr, uint32_t imageSize,
                        char* reason, size_t reasonLen) {
    if (imageSize < 16 || imageSize > OTA_SLOT_MAX_SIZE) {
        verifyReason(reason, reasonLen, "invalid image size");
        return false;
    }
    uint8_t hdr[16];
    if (!flashReadBytes(baseAddr, hdr, sizeof(hdr))) {
        verifyReason(reason, reasonLen, "flash read failed");
        return false;
    }
    if (hdr[0] != OTA_RBOOT_MAGIC_NEW1 || hdr[1] != OTA_RBOOT_MAGIC_NEW2) {
        verifyReason(reason, reasonLen, "invalid rboot image magic");
        return false;
    }
    if (hdr[2] != OTA_FLASH_MODE_DOUT) {
        verifyReason(reason, reasonLen, "invalid flash mode");
        return false;
    }
    uint32_t entry = readLe32(hdr + 4);
    uint32_t iromAdd = readLe32(hdr + 8);
    uint32_t iromLen = readLe32(hdr + 12);
    if (iromAdd != 0 || iromLen == 0 || !rangeWithin(16, iromLen, imageSize)) {
        verifyReason(reason, reasonLen, "invalid IROM layout");
        return false;
    }
    uint32_t normalOff = 16 + iromLen;
    if (!rangeWithin(normalOff, 8, imageSize)) {
        verifyReason(reason, reasonLen, "missing RAM image header");
        return false;
    }
    uint8_t appHdr[8];
    if (!flashReadBytes(baseAddr + normalOff, appHdr, sizeof(appHdr))) {
        verifyReason(reason, reasonLen, "RAM header read failed");
        return false;
    }
    if (appHdr[0] != 0xE9 || appHdr[2] != OTA_FLASH_MODE_DOUT ||
        readLe32(appHdr + 4) != entry) {
        verifyReason(reason, reasonLen, "invalid RAM image header");
        return false;
    }
    return true;
}

bool validateManifest(uint32_t baseAddr, uint32_t imageSize,
                      uint8_t targetRom, OtaManager::ManifestInfo& info,
                      char* reason, size_t reasonLen) {
    uint8_t hdr[16];
    if (!flashReadBytes(baseAddr, hdr, sizeof(hdr))) {
        verifyReason(reason, reasonLen, "manifest header read failed");
        return false;
    }
    uint32_t iromLen = readLe32(hdr + 12);
    if (iromLen < OTA_MANIFEST_SIZE) {
        verifyReason(reason, reasonLen, "missing OTA manifest");
        return false;
    }
    uint32_t manifestOffset = 16 + iromLen - OTA_MANIFEST_SIZE;
    if (!rangeWithin(manifestOffset, OTA_MANIFEST_SIZE, imageSize)) {
        verifyReason(reason, reasonLen, "invalid OTA manifest offset");
        return false;
    }
    uint8_t manifest[OTA_MANIFEST_SIZE];
    if (!flashReadBytes(baseAddr + manifestOffset, manifest, sizeof(manifest))) {
        verifyReason(reason, reasonLen, "manifest read failed");
        return false;
    }
    if (memcmp(manifest, OTA_MANIFEST_MAGIC, sizeof(OTA_MANIFEST_MAGIC)) != 0) {
        verifyReason(reason, reasonLen, "missing IRACOTA1 manifest");
        return false;
    }
    uint8_t manifestVersion = manifest[8];
    uint8_t format = manifest[9];
    uint8_t target = manifest[10];
    uint8_t flashMode = manifest[11];
    uint32_t manifestImageSize = readLe32(manifest + 12);
    uint32_t manifestCrc32 = readLe32(manifest + 16);
    if (manifestVersion != OTA_MANIFEST_VERSION || format != OTA_MANIFEST_FORMAT_EA04) {
        verifyReason(reason, reasonLen, "unsupported OTA manifest version");
        return false;
    }
    if (flashMode != OTA_FLASH_MODE_DOUT) {
        verifyReason(reason, reasonLen, "manifest flash mode mismatch");
        return false;
    }
    if (manifestImageSize != imageSize) {
        verifyReason(reason, reasonLen, "manifest image size mismatch");
        return false;
    }
    if (target != OTA_MANIFEST_TARGET_ANY && target != targetRom) {
        verifyReason(reason, reasonLen, "OTA target ROM mismatch");
        return false;
    }
    if (!fixedFieldEquals(manifest + 20, OTA_BOARD_FIELD_SIZE, OTA_BOARD_ID)) {
        verifyReason(reason, reasonLen, "OTA board mismatch");
        return false;
    }
    uint32_t actualCrc32 = 0;
    uint32_t crcFieldOffset = manifestOffset + OTA_MANIFEST_CRC_OFFSET;
    if (!flashCrc32WithZeroField(baseAddr, imageSize, crcFieldOffset, 4, actualCrc32)) {
        verifyReason(reason, reasonLen, "OTA CRC read failed");
        return false;
    }
    if (actualCrc32 != manifestCrc32) {
        verifyReason(reason, reasonLen, "OTA CRC mismatch");
        return false;
    }
    info.manifestOffset = manifestOffset;
    info.imageSize      = manifestImageSize;
    info.imageCrc32     = manifestCrc32;
    info.target         = target;
    copyFixedField(info.version, sizeof(info.version),
                   manifest + 40, OTA_VERSION_FIELD_SIZE);
    return true;
}

bool imageHeaderValid(const uint8_t* data, size_t len, char* reason, size_t reasonLen) {
    if (!data || len < 16) {
        verifyReason(reason, reasonLen, "image header too short");
        return false;
    }
    if (data[0] != OTA_RBOOT_MAGIC_NEW1 || data[1] != OTA_RBOOT_MAGIC_NEW2) {
        verifyReason(reason, reasonLen, "invalid rboot OTA image magic");
        return false;
    }
    uint8_t flashMode = data[2];
    uint32_t iromAdd = readLe32(data + 8);
    uint32_t iromLen = readLe32(data + 12);
    if (iromAdd != 0 || iromLen == 0 || iromLen >= OTA_SLOT_MAX_SIZE - 16) {
        verifyReason(reason, reasonLen, "invalid rboot OTA image layout");
        return false;
    }
    if (flashMode != OTA_FLASH_MODE_DOUT) {
        verifyReason(reason, reasonLen, "invalid flash mode; this board requires DOUT images");
        return false;
    }
    return true;
}

}

void OtaManager::begin() {
    loadState();
    Serial.println(F("[MODULE] ota_manager real impl loaded"));
}

void OtaManager::loadState() {
    state_ = {OTA_STATE_MAGIC, OTA_STATE_VERSION, false, 0, 0};
    if (!ensureFSMounted(false)) return;
    File f = LittleFS.open(FS_OTA_STATE, "r");
    if (f && f.size() == sizeof(state_)) {
        f.read((uint8_t*)&state_, sizeof(state_));
        f.close();
        if (state_.magic != OTA_STATE_MAGIC || state_.version != OTA_STATE_VERSION) {
            state_ = {OTA_STATE_MAGIC, OTA_STATE_VERSION, false, 0, 0};
        }
    } else {
        if (f) f.close();
    }
}

void OtaManager::clearState() {
    state_ = {OTA_STATE_MAGIC, OTA_STATE_VERSION, false, 0, 0};
    if (!ensureFSMounted(false)) return;
    File f = LittleFS.open(FS_OTA_STATE, "w");
    if (f) {
        f.write((uint8_t*)&state_, sizeof(state_));
        f.close();
    }
}

void OtaManager::reject(const char* reason) {
    writeOk_ = false;
    strncpy(rejectReason_, reason, sizeof(rejectReason_) - 1);
    rejectReason_[sizeof(rejectReason_) - 1] = '\0';
    Serial.printf("[OTA] Reject: %s\n", rejectReason_);
}

bool OtaManager::verifyInFlash(uint32_t baseAddr, uint32_t imageSize,
                                uint8_t targetRom, ManifestInfo& info,
                                char* reason, size_t reasonLen) {
    if (!validateRbootImage(baseAddr, imageSize, reason, reasonLen)) return false;
    return validateManifest(baseAddr, imageSize, targetRom, info, reason, reasonLen);
}

void OtaManager::onUploadStart() {
    uploadSize_    = 0;
    accLen_        = 0;
    writeOk_       = true;
    headerChecked_ = false;
    rejectReason_[0] = '\0';
#if IRAC_RBOOT_OTA
    uint8_t target = rboot_get_target_rom();
    targetAddr_    = rboot_get_rom_address(target);
    if (targetAddr_ == 0) {
        reject("invalid OTA target partition");
        return;
    }
    Serial.printf("[OTA] start ROM %u at 0x%06X\n", target, targetAddr_);
#else
    reject("rboot OTA disabled in factory firmware");
#endif
}

bool OtaManager::onUploadChunk(const uint8_t* data, size_t len) {
    if (!writeOk_) return false;
#if IRAC_RBOOT_OTA
    if (uploadSize_ + accLen_ + len > OTA_SLOT_MAX_SIZE) {
        reject("firmware image is too large for OTA slot");
        return false;
    }
    if (!headerChecked_ && accLen_ >= 16) {
        headerChecked_ = true;
        if (!imageHeaderValid(accBuf_, accLen_, rejectReason_, sizeof(rejectReason_))) {
            writeOk_ = false;
            return false;
        }
    }
    if (accLen_ + len > sizeof(accBuf_)) {
        if (!rboot_write_flash(targetAddr_ + uploadSize_, accBuf_, accLen_)) {
            reject("flash write failed");
            return false;
        }
        uploadSize_ += accLen_;
        accLen_ = 0;
    }
    memcpy(accBuf_ + accLen_, data, len);
    accLen_ += len;
#else
    (void)data; (void)len;
#endif
    return true;
}

void OtaManager::onUploadEnd() {
#if IRAC_RBOOT_OTA
    if (!headerChecked_ && writeOk_) {
        headerChecked_ = true;
        imageHeaderValid(accBuf_, accLen_, rejectReason_, sizeof(rejectReason_));
        if (rejectReason_[0] != '\0') writeOk_ = false;
    }
    if (accLen_ > 0 && writeOk_) {
        if (!rboot_write_flash(targetAddr_ + uploadSize_, accBuf_, accLen_)) {
            reject("final flash write failed");
            return;
        }
        uploadSize_ += accLen_;
        accLen_ = 0;
    }
    Serial.printf("[OTA] upload done: %u bytes, %s\n",
                  (unsigned)uploadSize_, writeOk_ ? "OK" : "FAILED");
#endif
}

bool OtaManager::rollbackIfPending(const char* reason) {
#if !IRAC_RBOOT_OTA
    (void)reason;
    return false;
#endif
    loadState();
    if (!state_.pending) return false;
    Serial.printf("[OTA] Rollback: %s\n", reason);
    uint8_t prev = state_.previous_rom;
    clearState();
    if (ensureFSMounted(false)) LittleFS.remove(FS_OTA_BOOTS);
#if IRAC_RBOOT_OTA
    rboot_set_current_rom(prev);
    delay(100);
    ESP.restart();
#endif
    return true;
}

void OtaManager::markConfirmReady() {
    if (!state_.pending) return;
    confirmAt_ = millis() + OTA_CONFIRM_DELAY_MS;
    Serial.printf("[OTA] basic check passed, will confirm after %us stable\n",
                  (unsigned)(OTA_CONFIRM_DELAY_MS / 1000));
}

void OtaManager::confirmIfReady() {
    if (confirmAt_ == 0 || !state_.pending) return;
    if ((long)(millis() - confirmAt_) < 0) return;
    Serial.println(F("[OTA] stable run reached, update confirmed"));
    clearState();
    if (ensureFSMounted(false)) LittleFS.remove(FS_OTA_BOOTS);
    confirmAt_ = 0;
}

void OtaManager::switchToTargetRom() {
#if IRAC_RBOOT_OTA
    state_.magic = OTA_STATE_MAGIC;
    state_.version = OTA_STATE_VERSION;
    state_.pending = true;
    state_.previous_rom = rboot_get_current_rom();
    state_.timestamp = millis();
    if (ensureFSMounted(true)) {
        File f = LittleFS.open(FS_OTA_STATE, "w");
        if (f) {
            f.write((uint8_t*)&state_, sizeof(state_));
            f.close();
        }
    }
    uint8_t target = rboot_get_target_rom();
    Serial.printf("[OTA] switching to ROM %u, rebooting\n", target);
    rboot_set_current_rom(target);
#endif
}
