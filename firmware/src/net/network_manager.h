#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "core/constants.h"

class NetworkManager {
public:
    enum class State { Disconnected, Connecting, Connected };

    void beginSTA(const char* ssid, const char* pass);
    bool connectSTABlocking(const char* ssid, const char* pass,
                            uint32_t timeoutMs = WIFI_CONNECT_TIMEOUT_MS);
    bool beginAP(const char* ssid, const char* pass);
    bool reconnect();
    void disconnect();

    void maintain();

    State   state() const;
    bool    isConnected() const { return WiFi.status() == WL_CONNECTED; }
    int32_t rssi() const { return WiFi.RSSI(); }
    bool    scanForMaster(const char* ssid, String& outSsid,
                          uint8_t outBssid[6], int32_t& outChannel);

    static void setNoneSleep();
};

extern NetworkManager network;
