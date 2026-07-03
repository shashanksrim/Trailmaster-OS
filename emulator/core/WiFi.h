#pragma once
// L2: fake WiFi — never actually connects (no network access from the
// emulator), but satisfies the .ino's OBD-worker WiFi calls so that loop
// runs harmlessly (status() always reports disconnected, so it just retries
// with delays forever, matching real-device behavior with no WiFi configured).
#include <cstdint>
#include <cstdio>
#include <string>

enum wl_status_t { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL, WL_CONNECTED, WL_CONNECT_FAILED, WL_DISCONNECTED = 6 };
enum wifi_mode_t { WIFI_OFF = 0, WIFI_STA, WIFI_AP, WIFI_AP_STA };

class IPAddress {
public:
    IPAddress() {}
    IPAddress(uint8_t, uint8_t, uint8_t, uint8_t) {}
    std::string toString() const { return "0.0.0.0"; }
};
static const IPAddress INADDR_NONE;

class WiFiClass {
public:
    void disconnect(bool = false, bool = false) {}
    void mode(wifi_mode_t) {}
    void setAutoReconnect(bool) {}
    bool begin(const char* /*ssid*/, const char* /*pass*/ = nullptr) { return false; }
    wl_status_t status() { return WL_DISCONNECTED; }
    void config(IPAddress, IPAddress, IPAddress) {}
    IPAddress localIP() { return IPAddress(); }
    int scanNetworks(bool = false, bool = false) { return 0; }
    std::string SSID(int) { return ""; }
    int RSSI(int) { return 0; }
    int channel(int) { return 0; }
    bool softAP(const char*, const char* = nullptr, int = 1, int = 0, int = 4) { return false; }
    bool softAPdisconnect(bool = false) { return true; }
    IPAddress softAPIP() { return IPAddress(); }
    bool softAPConfig(IPAddress, IPAddress, IPAddress) { return true; }
};
extern WiFiClass WiFi;

class WiFiClient {
public:
    bool connected() { return false; }
    void stop() {}
    bool connect(const char*, uint16_t) { return false; }
    int available() { return 0; }
    int read() { return -1; }
    void print(const char*) {}
};
