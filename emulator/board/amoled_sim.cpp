// L1: Amoled board-driver substitute. Implements the SAME public class
// declared in the real (unmodified) amoled.h, writing into a plain RGB565
// framebuffer instead of pushing QSPI panel commands. L3 reads the
// framebuffer once per frame and pushes it to the SDL texture.
//
// The real amoled.cpp is never compiled in the emulator — this file's
// symbols are linked in its place.
#include "amoled.h"
#include "emu_state.h"
#include <cstring>

uint16_t g_emu_framebuffer[EMU_DISP_W * EMU_DISP_H];
Amoled amoled;

Amoled::Amoled() {}
Amoled::~Amoled() {}
uint8_t Amoled::ID() { return SH8601_ID; }
char *Amoled::name() { static char n[] = "AMOLED-SIM"; return n; }

bool Amoled::begin() {
    std::memset(g_emu_framebuffer, 0, sizeof(g_emu_framebuffer));
    return true;
}

bool Amoled::drawBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) {
    return drawArea(x, y, x + w - 1, y + h - 1, bitmap);
}

bool Amoled::drawArea(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint16_t *bitmap) {
    // my_disp_flush() byte-swaps each pixel before calling this (the real
    // QSPI panel wants swapped bytes); undo that so the framebuffer holds
    // native RGB565 the SDL texture (SDL_PIXELFORMAT_RGB565) expects.
    int w = (int)x2 - (int)x1 + 1;
    int h = (int)y2 - (int)y1 + 1;
    if (w <= 0 || h <= 0) return false;
    for (int row = 0; row < h; row++) {
        uint32_t fy = y1 + row;
        if (fy >= EMU_DISP_H) break;
        for (int col = 0; col < w; col++) {
            uint32_t fx = x1 + col;
            if (fx >= EMU_DISP_W) continue;
            uint16_t px = bitmap[row * w + col];
            px = (uint16_t)((px << 8) | (px >> 8));
            g_emu_framebuffer[fy * EMU_DISP_W + fx] = px;
        }
    }
    return true;
}

bool Amoled::fillScreen(uint16_t color565) {
    for (int i = 0; i < EMU_DISP_W * EMU_DISP_H; i++) g_emu_framebuffer[i] = color565;
    return true;
}

bool Amoled::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color565) {
    for (int row = 0; row < h; row++) {
        int fy = y + row;
        if (fy < 0 || fy >= EMU_DISP_H) continue;
        for (int col = 0; col < w; col++) {
            int fx = x + col;
            if (fx < 0 || fx >= EMU_DISP_W) continue;
            g_emu_framebuffer[fy * EMU_DISP_W + fx] = color565;
        }
    }
    return true;
}

bool Amoled::invertColor(bool) { return true; }
bool Amoled::setBrightness(uint8_t) { return true; }
