// L3: runtime bootstrap. Owns the SDL window/event pump; calls the REAL
// sketch's setup() once then loop() forever — the same two functions the
// real Arduino-ESP32 core would call. The sketch's own loop() already calls
// lv_tick_inc()/lv_timer_handler() itself (confirmed in the .ino), so this
// file doesn't touch LVGL directly at all — it only feeds L1's shims
// (g_emu_input) and displays what L1's Amoled shim wrote (g_emu_framebuffer).
#include <SDL2/SDL.h>
#include "emu_state.h"

// Provided by the sketch (compiled as-is, unmodified).
extern void setup();
extern void loop();

int main(int, char**) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window   *window   = SDL_CreateWindow("Trailmaster Emulator",
                                 SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                 EMU_DISP_W, EMU_DISP_H, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Texture  *texture  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STREAMING, EMU_DISP_W, EMU_DISP_H);

    setup();

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
