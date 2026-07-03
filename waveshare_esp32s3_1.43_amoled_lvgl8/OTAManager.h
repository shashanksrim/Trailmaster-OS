#pragma once
#ifndef LV_SIM_BUILD
#include <Arduino.h>
#endif

// ── OTA Status ────────────────────────────────────────────────────────────────
enum OTAState {
    OTA_IDLE,
    OTA_SCANNING_WIFI,
    OTA_CONNECTING_WIFI,
    OTA_CHECKING_VERSION,
    OTA_UPDATE_AVAILABLE,   // paused, waiting for user to call ota_install()
    OTA_UP_TO_DATE,
    OTA_DOWNLOADING_FW,
    OTA_DOWNLOADING_SD,
    OTA_REBOOTING,
    OTA_FAILED_NO_WIFI,
    OTA_FAILED_SERVER,
    OTA_FAILED_FLASH,
};

struct OTAStatus {
    OTAState state;
    int      progress;          // 0-100 %
    char     status_text[96];   // human-readable message for the UI
    char     available_version[16];
    char     changelog[256];
};

// ── Public API ────────────────────────────────────────────────────────────────
void ota_init();
void ota_add_network(const char* ssid, const char* password);
void ota_remove_network(const char* ssid);
int  ota_list_networks(char ssids[][33], int max_count); // returns count

// Starts check+install flow on a background FreeRTOS task.
// If an update is found, state becomes OTA_UPDATE_AVAILABLE.
// Call ota_install() from the UI to proceed with download.
void ota_check_for_update();
void ota_install();   // called after user confirms

const OTAStatus* ota_get_status();

// Version string of currently running firmware (from version.h)
const char* ota_current_version();
