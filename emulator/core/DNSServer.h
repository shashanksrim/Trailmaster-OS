#pragma once
// L2: fake DNSServer — see WebServer.h; no real captive-portal DNS happens
// since there's no real AP/socket in the emulator.
#include <cstdint>
#include "WiFi.h" // IPAddress

class DNSServer {
public:
    DNSServer() {}
    bool start(uint16_t, const char*, IPAddress) { return true; }
    void stop() {}
    void processNextRequest() {}
};
