// L4 integration glue: stubs for the firmware files this emulator pass
// deliberately doesn't compile — OTAManager.cpp (real network OTA: real
// HTTPS calls to GitHub + ESP32 flash-partition writes, neither of which
// has a desktop equivalent worth faking, so this keeps the sim/-precedent
// animated-state-machine stub instead) and the NES/SMS game engines.
// PhotoFrameApp.cpp and ui_godzillaspeedometer.cpp are REAL now — see
// build.sh — so their globals/functions are defined there, not here.
#include "Arduino.h"
#include "FFat.h"
#include "lvgl.h"
#define LV_SIM_BUILD
#include "OTAManager.h"
#include <cstring>
#include <cstdio>

// ── SD card / FFat ────────────────────────────────────────────────────────────
// sd_card_bsp.h declares this with default C++ linkage (no extern "C").
void SD_card_Init(void) {
    std::printf("[emulator] SD_card_Init (no-op — /sd_card redirects to %s)\n", EMU_SDCARD_ROOT);
}
FFatClass FFat;

// NOTE: switch_to_launcher, force_full_ui_redraw, app_about,
// app_dino_jump_trigger, app_imageframe, app_settings, app_start_dino_game,
// app_zero_inclinometer, build_rom_menu, build_settings_screen,
// build_about_screen, and stop_all_games are all REAL — defined directly in
// the .ino (or screen_game.cpp) — so they're intentionally NOT stubbed here.

// ── OTAManager (real network OTA) — same animated-state-machine stub used by
// sim/sim_ota_stub.cpp, so the "Check for Update" overlay still animates ───
static OTAStatus s_ota;
static int phase = 0, step = 0;

static void setst(OTAState state, int prog, const char* txt) {
    s_ota.state = state; s_ota.progress = prog;
    std::strncpy(s_ota.status_text, txt, sizeof(s_ota.status_text) - 1);
}

void ota_init() { std::memset(&s_ota, 0, sizeof(s_ota)); s_ota.state = OTA_IDLE; }
void ota_add_network(const char*, const char*) {}
void ota_remove_network(const char*) {}
int  ota_list_networks(char[][33], int) { return 0; }

void ota_check_for_update() { phase = 1; step = 0; }
void ota_install() { phase = 2; step = 0; }

const OTAStatus* ota_get_status() {
    if (phase == 1) {
        if      (step == 0) setst(OTA_SCANNING_WIFI,   0,  "Scanning for WiFi networks...");
        else if (step == 1) setst(OTA_CONNECTING_WIFI, 10, "Connecting...");
        else if (step == 2) setst(OTA_CHECKING_VERSION,20, "Checking GitHub...");
        else { setst(OTA_UP_TO_DATE, 100, "Up to date (emulator)"); phase = 0; }
        step++;
    } else if (phase == 2) {
        int p = step * 20;
        if (p < 100) setst(OTA_DOWNLOADING_FW, p, "Downloading firmware...");
        else { setst(OTA_REBOOTING, 100, "Update complete! Rebooting..."); phase = 0; }
        step++;
    }
    return &s_ota;
}
const char* ota_current_version() { return "emulator"; }

// ── Game engines (NES/SMS emulation) — out of scope for this pass ────────────
#include "retro_engine.h"
#include "NesEngine.h"
uint16_t* RetroEngine::vfb = nullptr;
uint16_t* RetroEngine::line_scratch = nullptr;
bool RetroEngine::begin() { return true; }
void RetroEngine::clear(uint16_t) {}
void RetroEngine::drawPixel(int, int, uint16_t) {}
void RetroEngine::drawSprite(int, int, int, int, const uint8_t*, uint16_t, bool) {}
void RetroEngine::drawRect(int, int, int, int, uint16_t) {}
void RetroEngine::flush() {}

bool NesEngine::is_running = false;
bool NesEngine::loadROM(const char*) { return false; }
void NesEngine::update() {}
extern "C" bool nes_is_running() { return false; }
extern "C" void nes_set_running(bool) {}
