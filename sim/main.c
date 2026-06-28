// Trailmaster-OS LVGL simulator — SDL2 + LVGL 8.3.11, compiled to WASM via emscripten.
// Renders the real LVGL UI on a 466x466 canvas so visuals can be checked in a browser.
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#include <emscripten.h>
#include "lvgl.h"

#define HRES 466
#define VRES 466

static lv_disp_draw_buf_t draw_buf;
static lv_color_t lvbuf[HRES * VRES];
static lv_disp_drv_t  disp_drv;
static lv_indev_drv_t indev_drv;

static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;

static bool mouse_pressed = false;
static int  mouse_x = 0, mouse_y = 0;
static uint32_t last_tick;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    SDL_Rect r = { area->x1, area->y1, w, h };
    SDL_UpdateTexture(texture, &r, color_p, w * sizeof(lv_color_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    lv_disp_flush_ready(drv);
}

static void mouse_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    data->point.x = mouse_x;
    data->point.y = mouse_y;
    data->state = mouse_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

// Provided by sim_screens.c — builds the screen(s) to preview.
extern void sim_build_ui(void);

static void loop(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_MOUSEBUTTONDOWN) { mouse_pressed = true;  mouse_x = e.button.x; mouse_y = e.button.y; }
        else if (e.type == SDL_MOUSEBUTTONUP) { mouse_pressed = false; mouse_x = e.button.x; mouse_y = e.button.y; }
        else if (e.type == SDL_MOUSEMOTION) { mouse_x = e.motion.x; mouse_y = e.motion.y; }
    }
    uint32_t now = SDL_GetTicks();
    lv_tick_inc(now - last_tick);
    last_tick = now;
    lv_timer_handler();
}

int main(void) {
    SDL_Init(SDL_INIT_VIDEO);
    window   = SDL_CreateWindow("Trailmaster", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, HRES, VRES, 0);
    renderer = SDL_CreateRenderer(window, -1, 0);
    texture  = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, HRES, VRES);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, lvbuf, NULL, HRES * VRES);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = HRES;
    disp_drv.ver_res  = VRES;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = mouse_read;
    lv_indev_drv_register(&indev_drv);

    last_tick = SDL_GetTicks();
    sim_build_ui();

    emscripten_set_main_loop(loop, 0, 1);
    return 0;
}
