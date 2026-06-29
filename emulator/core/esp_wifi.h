#pragma once
// L2: fake esp_wifi.h — just the constants/function the .ino's OBD worker calls.
#include <cstdint>
typedef int esp_err_t;
typedef enum { WIFI_IF_STA = 0, WIFI_IF_AP = 1 } wifi_interface_t;
#define WIFI_PROTOCOL_11B 0x01
#define WIFI_PROTOCOL_11G 0x02
#define WIFI_PROTOCOL_11N 0x04
#define WIFI_PROTOCOL_LR  0x08
inline esp_err_t esp_wifi_set_protocol(wifi_interface_t, uint8_t) { return 0; }
