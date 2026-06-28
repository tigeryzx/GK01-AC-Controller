#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include "core/constants.h"

using MqttCallback = void (*)(char*, uint8_t*, unsigned int);

class MqttService {
public:
    void begin();
    bool connect();
    void disconnect();
    void loop();

    bool enabled() const { return enabled_; }
    bool connected() { return mqtt_.connected(); }

    void publishState();
    void publishDiscovery();
    void publishAvailability(bool online);
    void publishMotion(bool detected);

    void onHvacCommandReceived();

    void setCallback(MqttCallback cb);

    const String& topicBase() const { return topicBase_; }

private:
    WiFiClient     net_;
    PubSubClient   mqtt_;
    bool           enabled_      = false;
    uint32_t       lastReconnect_ = 0;
    String         topicBase_;
};

extern MqttService mqtt;
