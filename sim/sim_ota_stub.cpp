// Simulated OTA state machine — advances one step per ota_get_status() call
// (the overlay polls it every 400 ms), so the overlay animates through the flow.
#include "sim_ota_stub.h"
#include <string.h>

static OTAStatus s;
static int phase = 0;   // 0 = idle, 1 = checking, 2 = installing
static int step  = 0;

static void setst(OTAState state, int prog, const char *txt) {
    s.state = state;
    s.progress = prog;
    strncpy(s.status_text, txt, sizeof(s.status_text) - 1);
    s.status_text[sizeof(s.status_text) - 1] = '\0';
}

void ota_check_for_update(void) { phase = 1; step = 0; }
void ota_install(void)          { phase = 2; step = 0; }

const OTAStatus *ota_get_status(void) {
    if (phase == 1) {
        if      (step == 0) setst(OTA_SCANNING_WIFI,   0,  "Scanning for WiFi networks...");
        else if (step == 1) setst(OTA_CONNECTING_WIFI, 10, "Connecting to Pixel10_shnk...");
        else if (step == 2) setst(OTA_CHECKING_VERSION,20, "WiFi connected, checking GitHub...");
        else {
            setst(OTA_UPDATE_AVAILABLE, 30, "v3.9 available! Tap Install.");
            strncpy(s.available_version, "3.9", sizeof(s.available_version) - 1);
            phase = 0;
        }
        step++;
    } else if (phase == 2) {
        int p = step * 15;
        if (p < 100)      setst(OTA_DOWNLOADING_FW, p, "Downloading firmware...");
        else if (step < 9) setst(OTA_DOWNLOADING_SD, 100, "Updating SD files 8/16");
        else { setst(OTA_REBOOTING, 100, "Update complete! Rebooting..."); phase = 0; }
        step++;
    }
    return &s;
}
