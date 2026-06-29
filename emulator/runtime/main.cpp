// L3: runtime bootstrap. Owns the SDL window/event pump; calls the REAL
// sketch's setup() once then loop() forever — the same two functions the
// real Arduino-ESP32 core would call. The sketch's own loop() already calls
// lv_tick_inc()/lv_timer_handler() itself (confirmed in the .ino), so this
// file doesn't touch LVGL directly at all — it only feeds L1's shims
// (g_emu_input) and displays what L1's Amoled shim wrote (g_emu_framebuffer).
#include <SDL2/SDL.h>
#include <cstdlib>
#include <cstring>
#include "emu_state.h"
#include "lvgl.h"

// Provided by the sketch (compiled as-is, unmodified).
extern void setup();
extern void loop();

static SDL_Window   *g_window   = nullptr;
static SDL_Renderer *g_renderer = nullptr;
static SDL_Texture  *g_texture  = nullptr;
static bool           g_running = true;

// Registered into g_emu_present (see emu_state.h) so L1's Amoled shim can
// push a frame to the real window the instant the firmware flushes one —
// including from INSIDE setup(), before this file's own while(running) loop
// below ever runs. Without this, anything that renders during setup() (the
// boot splash plays a GIF/static logo via a blocking lv_timer_handler()
// loop there) would be entirely invisible: real hardware shows it because
// the flush callback IS the display, but the emulator would otherwise only
// start presenting frames once setup() returns.
static void emu_present() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_running = false;
        else if (e.type == SDL_MOUSEBUTTONDOWN) {
            g_emu_input.pressed = true;
            g_emu_input.x = (uint16_t)e.button.x;
            g_emu_input.y = (uint16_t)e.button.y;
        } else if (e.type == SDL_MOUSEBUTTONUP) {
            g_emu_input.pressed = false;
        } else if (e.type == SDL_MOUSEMOTION) {
            g_emu_input.x = (uint16_t)e.motion.x;
            g_emu_input.y = (uint16_t)e.motion.y;
        }
    }
    SDL_UpdateTexture(g_texture, NULL, g_emu_framebuffer, EMU_DISP_W * sizeof(uint16_t));
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, NULL, NULL);
    SDL_RenderPresent(g_renderer);
}

// Debug-only hook (this file is emulator-owned, not the firmware): set
// EMU_FORCE_SCREEN=gauge|inclinometer|launcher|settings|about|godzilla to
// jump there after boot, for screenshot verification without simulating
// real touch gestures. The SquareLine screen objects/inits are declared
// extern "C" in the real headers (ui_uigauge.h etc.) — must match exactly,
// since the .c files defining them are compiled as plain C.
// build_settings_screen()/build_about_screen() are defined directly in the
// .ino with ordinary C++ linkage, but the .ino's OWN forward declaration of
// them sits inside an extern "C" block (its line ~99-103) — that governs
// the actual link-time symbol names, so this declaration must match it
// (PLAN.md's note that no extern "C" was needed here was wrong; the linker
// disagreed). ui_godzillaspeedometer_screen_init is declared extern "C" in
// ui_godzillaspeedometer.h, matching the rest of this block.
extern "C" {
    extern lv_obj_t *ui_uigauge, *ui_uiinclinometer, *ui_uilauncher;
    void ui_uigauge_screen_init(void);
    void ui_uiinclinometer_screen_init(void);
    void ui_uilauncher_screen_init(void);
    void _ui_screen_change(lv_obj_t **target, lv_scr_load_anim_t fademode, int spd, int delay, void (*target_init)(void));
    void build_settings_screen();
    void build_about_screen();
    extern lv_obj_t *ui_godzillaspeedometer;
    void ui_godzillaspeedometer_screen_init(void);
    void app_imageframe(lv_event_t * e);
}

static void emu_debug_force_screen() {
    const char *which = std::getenv("EMU_FORCE_SCREEN");
    if (!which) return;
    if (std::strcmp(which, "gauge") == 0)
        _ui_screen_change(&ui_uigauge, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_uigauge_screen_init);
    else if (std::strcmp(which, "inclinometer") == 0)
        _ui_screen_change(&ui_uiinclinometer, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_uiinclinometer_screen_init);
    else if (std::strcmp(which, "launcher") == 0)
        _ui_screen_change(&ui_uilauncher, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_uilauncher_screen_init);
    else if (std::strcmp(which, "settings") == 0)
        build_settings_screen();
    else if (std::strcmp(which, "about") == 0)
        build_about_screen();
    else if (std::strcmp(which, "godzilla") == 0)
        _ui_screen_change(&ui_godzillaspeedometer, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_godzillaspeedometer_screen_init);
    else if (std::strcmp(which, "imageframe") == 0)
        app_imageframe(nullptr);
}

int main(int, char**) {
    SDL_Init(SDL_INIT_VIDEO);
    g_window   = SDL_CreateWindow("Trailmaster Emulator",
                     SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                     EMU_DISP_W, EMU_DISP_H, 0);
    g_renderer = SDL_CreateRenderer(g_window, -1, 0);
    g_texture  = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGB565,
                     SDL_TEXTUREACCESS_STREAMING, EMU_DISP_W, EMU_DISP_H);

    g_emu_present = &emu_present;

    setup();
    emu_debug_force_screen();

    while (g_running) {
        loop();
        emu_present();
        SDL_Delay(8); // avoid pegging a CPU core; LVGL paces its own redraws
    }

    SDL_DestroyTexture(g_texture);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
