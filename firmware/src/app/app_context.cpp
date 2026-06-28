#include "app/app_context.h"
#include "app/mode_factory.h"
#include "core/string_utils.h"
#include "hw/sensor_service.h"
#include "hw/hvac/hvac_registry.h"
#include "net/mqtt_service.h"
#include "ota/ota_manager.h"

extern "C" {
#include <user_interface.h>
}

AppContext ctx;

void AppContext::begin() {
    chipId_  = String(ESP.getChipId(), HEX);
    mac_     = WiFi.macAddress();
    slaveId_ = str::slaveIdFromMac(mac_);
    Serial.printf("[CTX] chipId=%s mac=%s slaveId=%s\n",
                  chipId_.c_str(), mac_.c_str(), slaveId_.c_str());

    configStore.begin(false);
    configStore.load();
    slaves.load();
    hvacState.loadFrom(configStore.cfg);

    ir.begin();
    hvacRegistry.begin(ir);
    sensors.begin();
    mqtt.begin();
    ota.begin();
}

void AppContext::loop() {
    leds.loop();
    button.loop();
    pairing.loop();
    configStore.saveIfDue();
    ota.confirmIfReady();

    if (mode) mode->loop();

    yield();
}

void AppContext::changeMode(DeviceModeId id) {
    if (mode) {
        mode = nullptr;
    }
    modeId_ = id;
    auto owned = createMode(id);
    mode = owned.release();
    if (mode) {
        Serial.printf("[CTX] enter mode: %s\n", mode->name());
        mode->onEnter();
    } else {
        Serial.println(F("[CTX] createMode returned nullptr"));
    }
}

void AppContext::reboot(uint32_t delayMs) {
    Serial.printf("[CTX] reboot in %u ms\n", (unsigned)delayMs);
    delay(delayMs);
    ESP.restart();
}

DeviceModeId AppContext::decideBootMode() {
    Config& cfg = configStore.cfg;
    bool hasSTA = strlen(cfg.sta_ssid) > 0;

    if (cfg.force_mode == FORCE_MODE_AP) {
        Serial.println(F("[BOOT] force_mode=AP"));
        return DeviceModeId::ApMaster;
    }

    if (cfg.force_mode == FORCE_MODE_HOME) {
        if (hasSTA) {
            Serial.println(F("[BOOT] force_mode=HOME, connecting STA"));
            leds.setAll(true, false, false);
            if (network.connectSTABlocking(cfg.sta_ssid, cfg.sta_pass)) {
                return DeviceModeId::StaHome;
            }
            Serial.println(F("[BOOT] HOME STA failed, fallback AP"));
        } else {
            Serial.println(F("[BOOT] force_mode=HOME without STA, revert AUTO"));
            cfg.force_mode = FORCE_MODE_AUTO;
            configStore.save();
        }
        return DeviceModeId::ApMaster;
    }

    if (hasSTA && cfg.force_mode != FORCE_MODE_SLAVE) {
        Serial.println(F("[BOOT] AUTO, trying STA Home"));
        leds.setAll(true, false, false);
        if (network.connectSTABlocking(cfg.sta_ssid, cfg.sta_pass)) {
            return DeviceModeId::StaHome;
        }
        Serial.println(F("[BOOT] STA failed, trying slave scan"));
    }

    Serial.println(F("[BOOT] scanning for master"));
    leds.setAll(false, false, true);
    String foundSsid;
    uint8_t bssid[6] = {0};
    int32_t channel = 0;
    if (network.scanForMaster(getApSsid(), foundSsid, bssid, channel)) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(foundSsid.c_str(), getApPass(), channel, bssid);
        Serial.printf("[BOOT] slave connecting to %s...\n", foundSsid.c_str());

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 40) {
            leds.setBlue(attempts % 2 == 0);
            leds.setYellow(attempts % 2 != 0);
            delay(500);
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[BOOT] slave connected, IP=%s\n", WiFi.localIP().toString().c_str());
            return DeviceModeId::StaSlave;
        }
        Serial.println(F("[BOOT] slave connect failed"));
    }

    Serial.println(F("[BOOT] fallback to AP Master"));
    return DeviceModeId::ApMaster;
}

const char* AppContext::modeName() const {
    return modeString(modeId_);
}

const char* modeString(DeviceModeId id) {
    switch (id) {
        case DeviceModeId::ApMaster: return "ap";
        case DeviceModeId::StaSlave: return "slave";
        case DeviceModeId::StaHome:  return "sta";
    }
    return "unknown";
}

const char* getApSsid() {
    if (strlen(configStore.cfg.ap_ssid) > 0) return configStore.cfg.ap_ssid;
    return AP_DEFAULT_SSID;
}

const char* getApPass() {
    if (strlen(configStore.cfg.ap_pass) > 0) return configStore.cfg.ap_pass;
    return AP_DEFAULT_PASS;
}

void HvacState::applyFrom(const hvac::Command& cmd) {
    vendor = cmd.vendor;
    power  = cmd.power;
    mode   = cmd.mode;
    temp   = cmd.temp;
    fan    = cmd.fan;
}

void HvacState::persistTo(Config& cfg) const {
    str::copyTo(cfg.last_vendor, sizeof(cfg.last_vendor), vendor);
    str::copyTo(cfg.last_mode,   sizeof(cfg.last_mode),   mode);
    cfg.last_temp  = (uint8_t)temp;
    str::copyTo(cfg.last_fan,    sizeof(cfg.last_fan),    fan);
    cfg.last_power = power;
}

void HvacState::loadFrom(const Config& cfg) {
    if (strlen(cfg.last_vendor) > 0) vendor = cfg.last_vendor;
    power = cfg.last_power;
    if (strlen(cfg.last_mode) > 0) mode = cfg.last_mode;
    temp = constrain((int)cfg.last_temp, HVAC_MIN_TEMP_DEFAULT, HVAC_MAX_TEMP_DEFAULT);
    if (strlen(cfg.last_fan) > 0) fan = cfg.last_fan;
}

void SlaveRegistry::load()  {}
void SlaveRegistry::save()  {}
void SlaveRegistry::clear() {}

int SlaveRegistry::findByMac(const char* mac) const {
    for (size_t i = 0; i < CAPACITY; i++) {
        if (slaves_[i].mac[0] != '\0' && strcmp(slaves_[i].mac, mac) == 0) return (int)i;
    }
    return -1;
}

int SlaveRegistry::findById(const String& id) const {
    for (size_t i = 0; i < CAPACITY; i++) {
        if (slaves_[i].mac[0] != '\0' && str::slaveIdFromMac(slaves_[i].mac) == id) return (int)i;
    }
    return -1;
}

int SlaveRegistry::findFree() const {
    for (size_t i = 0; i < CAPACITY; i++) {
        if (slaves_[i].mac[0] == '\0') return (int)i;
    }
    return -1;
}

void SlaveRegistry::clearSlot(size_t idx) {
    if (idx < CAPACITY) slaves_[idx] = SlaveInfo{};
}

void SlaveRegistry::pruneOffline() {
    uint32_t now = millis();
    for (size_t i = 0; i < CAPACITY; i++) {
        if (slaves_[i].mac[0] != '\0' && slaves_[i].lastSeen != 0 &&
            (uint32_t)(now - slaves_[i].lastSeen) >= SLAVE_OFFLINE_TIMEOUT_MS) {
            Serial.printf("[SLAVES] prune offline %s\n", slaves_[i].mac);
            slaves_[i] = SlaveInfo{};
        }
    }
}

void SlaveRegistry::refreshFromApStations() {
    struct station_info *stat_info = wifi_softap_get_station_info();
    while (stat_info) {
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 stat_info->bssid[0], stat_info->bssid[1], stat_info->bssid[2],
                 stat_info->bssid[3], stat_info->bssid[4], stat_info->bssid[5]);
        int slot = findByMac(mac);
        if (slot >= 0) {
            slaves_[slot].ip       = stat_info->ip.addr;
            slaves_[slot].lastSeen = millis();
        }
        stat_info = STAILQ_NEXT(stat_info, next);
    }
    wifi_softap_free_station_info();
}

uint32_t PairingWindow::remainingMs() const {
    if (!active_) return 0;
    uint32_t now = millis();
    return (until_ > now) ? (until_ - now) : 0;
}

void PairingWindow::start() {
    active_ = true;
    until_  = millis() + PAIRING_DURATION_MS;
    Serial.println(F("[PAIR] window opened"));
}

void PairingWindow::stop() {
    active_ = false;
    Serial.println(F("[PAIR] window closed"));
}

void PairingWindow::loop() {
    if (active_ && (long)(millis() - until_) >= 0) {
        active_ = false;
        Serial.println(F("[PAIR] window expired"));
    }
}
