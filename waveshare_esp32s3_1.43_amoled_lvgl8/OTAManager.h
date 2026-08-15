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
// Saved networks WITH passwords, for the on-device settings page. Returns count.
int  ota_list_networks_full(char ssids[][33], char passes[][65], int max_count);

// Join the first reachable saved network in pure STA mode. Same tested path the
// updater uses, minus the set_status() calls — so a caller that just needs WiFi
// (convoy) does not make the OTA overlay announce a fake update check. Skips the
// diagnostic scan too, which costs seconds and only ever fed a log line.
// The caller must already own the radio (see convoy_radio_mode in the .ino).
bool ota_wifi_connect_saved();

// Pull photo-frame images listed in the manifest the web app publishes, onto the
// SD card. Reuses the updater's network path, which prefers the strongest saved
// network — normally home Wi-Fi, so photos are not round-tripped through the
// phone's cellular twice. Files already on the card are skipped.
void ota_sync_photos();
// Same, on its own task. Use this from anything the user is looking at — the
// synchronous form blocks for seconds per image.
void ota_sync_photos_async();
// Set true when a sync actually brought files down; the photo frame clears it
// after rescanning. Not a count: the frame only needs "something changed".
extern volatile bool ota_photos_changed;

// Starts check+install flow on a background FreeRTOS task.
// If an update is found, state becomes OTA_UPDATE_AVAILABLE.
// Call ota_install() from the UI to proceed with download.
void ota_check_for_update();
void ota_install();   // called after user confirms

const OTAStatus* ota_get_status();

// Version string of currently running firmware (from version.h)
const char* ota_current_version();
