// L4 integration glue: stubs for the firmware files this emulator pass
// deliberately doesn't compile — PhotoFrameApp.cpp (WiFi portal + photo
// carousel), OTAManager.cpp (real network OTA), and the GIF/PSRAM-heavy
// parts of ui_godzillaspeedometer.cpp. Same approach as sim/sim_stubs.c.
// Everything else in the real .ino compiles and runs unmodified.
#include "Arduino.h"
#include "FFat.h"
#include "lvgl.h"
#include "ui_godzillaspeedometer.h"
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

// ── Godzilla speedometer globals (real screen init/destroy/event handler are
// stubbed below; the shared godzilla_speedo_ui.h functions still reference
// these, so they must exist even though the screen is never built) ──────────
lv_obj_t *ui_godzillaspeedometer = nullptr;
lv_obj_t *ui_godzilla_canvas = nullptr;
lv_obj_t *ui_godzilla_speed_label = nullptr;
lv_obj_t *ui_godzilla_speed_label_shadows[8] = {nullptr};
lv_obj_t *ui_godzilla_unit_label = nullptr;
lv_obj_t *ui_godzilla_unit_label_shadows[8] = {nullptr};
lv_obj_t *ui_godzilla_rpm_arc = nullptr;
lv_obj_t *godzilla_tick_lines[41] = {nullptr};
lv_obj_t *godzilla_tick_labels[9] = {nullptr};
lv_obj_t *ui_godzilla_x1000_label = nullptr;
bool is_godzilla_animating = false;
int default_speedometer = 0;
bool is_simulating_obd = false;

void ui_godzillaspeedometer_screen_init(void) {}
void ui_godzillaspeedometer_screen_destroy(void) {}
void ui_event_godzillaspeedometer(lv_event_t*) {}
void load_speedo_preferences(void) {}
void save_speedo_preferences(int val) { default_speedometer = val; }
void set_gif_state(gif_state_t) {}

// ── PhotoFrameApp (WiFi portal + photo carousel) ──────────────────────────────
bool wifi_ap_running = false;
bool pf_autostart_wifi = false;
lv_obj_t * photoframe_screen = nullptr;

// PhotoFrameApp.h declares these with default C++ linkage (NOT extern "C").
void build_photoframe_screen() {}
void switch_to_photoframe() {}
void photoframe_setup() {}
void photoframe_loop_handler() {}
void start_photoframe_wifi() {}
void stop_photoframe_wifi() {}
lv_obj_t * show_boot_splash() { return nullptr; }
int  get_custom_boot_time() { return 3; }
void set_custom_boot_time(int) {}

// pf_show_upload_overlay is the ONLY one of PhotoFrameApp.h's "launcher
// bridge" trio not also defined for real in the .ino itself (switch_to_launcher
// and force_full_ui_redraw ARE — see below, they'd be duplicate symbols).
extern "C" {
    void pf_show_upload_overlay(void) {}
}

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

// ── Force-emit godzilla_speedo_ui.h's inline open_speedo_settings_menu ───────
// Its only callers in this reduced build are .c files (ui_uispeedometer.c)
// that can't include the C++ header; without a real call site in a compiled
// C++ TU, the compiler elides the unused inline body and the linker can't
// find it. Taking its address here forces emission.
#include "godzilla_speedo_ui.h"
// A non-static, externally-linked global: the compiler can't prove nothing
// else references it, so it can't dead-code-eliminate this (or the function
// whose address it holds) the way it did with a throwaway local variable.
void* g_emu_keep_godzilla_funcs[] = { (void*)&open_speedo_settings_menu };
