#include "net/network_manager.h"
#include "config/config_store.h"
#include "app/app_context.h"
#include "core/string_utils.h"

NetworkManager network;

extern "C" {
#include <user_interface.h>
}

void NetworkManager::setNoneSleep() {
    wifi_set_sleep_type(NONE_SLEEP_T);
}

void NetworkManager::beginSTA(const char* ssid, const char* pass) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
}

bool NetworkManager::connectSTABlocking(const char* ssid, const char* pass, uint32_t timeoutMs) {
    Serial.printf("[NET] STA connecting to %s\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
        leds.setBlue((millis() / 500) % 2 == 0);
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[NET] STA connected, IP=%s RSSI=%d\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }
    Serial.println(F("\n[NET] STA connect failed"));
    return false;
}

bool NetworkManager::beginAP(const char* ssid, const char* pass) {
    WiFi.mode(WIFI_AP);
    IPAddress ip(10, 1, 1, 1);
    IPAddress gw(10, 1, 1, 1);
    IPAddress sn(255, 255, 255, 0);
    if (!WiFi.softAPConfig(ip, gw, sn)) {
        Serial.println(F("[NET] softAPConfig failed"));
    }

    bool ok = WiFi.softAP(ssid, pass, 1);

    if (!ok && strlen(configStore.cfg.ap_pass) > 0) {
        Serial.println(F("[NET] AP start failed, retry without pass"));
        configStore.cfg.ap_pass[0] = '\0';
        configStore.save();
        ok = WiFi.softAP(ssid, AP_DEFAULT_PASS, 1);
    }
    if (!ok && strlen(configStore.cfg.ap_ssid) > 0) {
        Serial.println(F("[NET] AP start failed, retry with default SSID"));
        configStore.cfg.ap_ssid[0] = '\0';
        configStore.save();
        ok = WiFi.softAP(AP_DEFAULT_SSID, AP_DEFAULT_PASS, 1);
    }

    if (!ok) {
        Serial.println(F("[NET] AP start failed"));
        leds.blink(PIN_LED_RED, 8, 80, 80);
        return false;
    }

    Serial.printf("[NET] AP: %s  http://%s\n", ssid, WiFi.softAPIP().toString().c_str());
    return true;
}

bool NetworkManager::reconnect() {
    Serial.println(F("[NET] WiFi.reconnect()"));
    return WiFi.reconnect();
}

void NetworkManager::disconnect() {
    WiFi.disconnect();
}

void NetworkManager::maintain() {
    if (WiFi.status() != WL_CONNECTED) {
        leds.setBlue(false);
        static uint32_t lastReconnect = 0;
        if ((uint32_t)(millis() - lastReconnect) >= WIFI_RECONNECT_INTERVAL_MS) {
            lastReconnect = millis();
            Serial.println(F("[NET] WiFi lost, reconnecting..."));
            WiFi.reconnect();
        }
    } else {
        leds.setBlue(true);
    }
}

NetworkManager::State NetworkManager::state() const {
    if (WiFi.status() == WL_CONNECTED) return State::Connected;
    return State::Disconnected;
}

bool NetworkManager::scanForMaster(const char* ssid, String& outSsid,
                                    uint8_t outBssid[6], int32_t& outChannel) {
    Serial.println(F("[NET] scanning for master..."));
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int n = WiFi.scanNetworks();
    bool hasCustomSsid = strlen(configStore.cfg.ap_ssid) > 0;
    uint8_t pairedBssid[6] = {0};
    bool hasPaired = str::parseMacBytes(configStore.cfg.paired_master_bssid, pairedBssid);

    bool foundMaster = false;
    bool foundPaired = false;
    bool targetBssidSet = false;
    int bestRssi = -1000;
    uint8_t tmpBssid[6] = {0};

    for (int i = 0; i < n; i++) {
        String scanSsid = WiFi.SSID(i);
        bool ssidMatches = hasCustomSsid ? (scanSsid == configStore.cfg.ap_ssid)
                                          : (scanSsid == ssid);
        if (!ssidMatches) continue;

        uint8_t* scanBssid = WiFi.BSSID(i);
        bool bssidMatches = hasPaired && scanBssid &&
                            memcmp(scanBssid, pairedBssid, sizeof(pairedBssid)) == 0;
        if (bssidMatches || (!foundPaired && WiFi.RSSI(i) > bestRssi)) {
            foundMaster   = true;
            foundPaired   = bssidMatches;
            bestRssi      = WiFi.RSSI(i);
            outSsid       = scanSsid;
            outChannel    = WiFi.channel(i);
            if (scanBssid) {
                memcpy(tmpBssid, scanBssid, sizeof(tmpBssid));
                targetBssidSet = true;
            } else {
                targetBssidSet = false;
            }
            Serial.printf("[NET] candidate AP: %s RSSI=%d%s\n",
                          outSsid.c_str(), bestRssi, bssidMatches ? " paired" : "");
        }
    }
    WiFi.scanDelete();

    if (foundMaster && targetBssidSet) {
        memcpy(outBssid, tmpBssid, 6);
    }
    return foundMaster;
}
