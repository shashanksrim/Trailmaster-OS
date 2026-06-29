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

// Debug-only hook (this file is emulator-owned, not the firmware): set
// EMU_FORCE_SCREEN=gauge|inclinometer|launcher|settings|about to jump there
// after boot, for screenshot verification without simulating real touch
// gestures. The SquareLine screen objects/inits are declared extern "C" in
// the real headers (ui_uigauge.h etc.) — must match exactly, since the .c
// files defining them are compiled as plain C. build_settings_screen() and
// build_about_screen() are defined directly in the .ino with ordinary C++
// linkage, but the .ino's OWN forward declaration of them sits inside an
// extern "C" block (its line ~99-103) — that governs the actual link-time
// symbol names, so this declaration must match it (PLAN.md's note that no
// extern "C" was needed here was wrong; the linker disagreed).
extern "C" {
    extern lv_obj_t *ui_uigauge, *ui_uiinclinometer, *ui_uilauncher;
    void ui_uigauge_screen_init(void);
    void ui_uiinclinometer_screen_init(void);
    void ui_uilauncher_screen_init(void);
    void _ui_screen_change(lv_obj_t **target, lv_scr_load_anim_t fademode, int spd, int delay, void (*target_init)(void));
    void build_settings_screen();
    void build_about_screen();
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
}

int main(int, char**) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window   *window   = SDL_CreateWindow("Trailmaster Emulator",
                                 SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                 EMU_DISP_W, EMU_DISP_H, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Texture  *texture  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STREAMING, EMU_DISP_W, EMU_DISP_H);

    setup();
    emu_debug_force_screen();

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
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

        loop();

        SDL_UpdateTexture(texture, NULL, g_emu_framebuffer, EMU_DISP_W * sizeof(uint16_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        SDL_Delay(8); // avoid pegging a CPU core; LVGL paces its own redraws
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
