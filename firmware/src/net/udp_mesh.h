#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include "core/constants.h"

class UdpMesh {
public:
    void beginMaster();
    void beginSlave();

    void loopMaster();
    void loopSlave();

    void broadcast(const char* msg);
    void sendTo(IPAddress ip, const char* msg);

private:
    WiFiUDP udp_;
    char    rcvBuf_[1024];
    size_t  rcvLen_ = 0;
    IPAddress remoteIp_;

    bool   receivePacket();
};

extern UdpMesh udp;
