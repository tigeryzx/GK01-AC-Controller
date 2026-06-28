#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include "core/constants.h"

class WebServer {
public:
    void beginAP(uint16_t port = 80);
    void beginSTA(uint16_t port = 80);
    void loop();
    void stop();

    ESP8266WebServer& raw() { return server_; }

    void registerApiRoutes();
    void startCaptivePortal();
    void stopCaptivePortal();

private:
    ESP8266WebServer server_;
    DNSServer        dns_;
    bool             captiveActive_ = false;

    void handleRoot();
    void handleCaptiveRedirect();
};

extern WebServer web;
