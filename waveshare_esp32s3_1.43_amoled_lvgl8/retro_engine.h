#ifndef RETRO_ENGINE_H
#define RETRO_ENGINE_H

#include <Arduino.h>
#include "amoled.h"

// Retro Canvas Resolution (233x233 -> 2x Upscale to 466x466)
#define V_WIDTH 233
#define V_HEIGHT 233

class RetroEngine {
public:
    static bool begin();
    static void clear(uint16_t color = 0x0000);
    static void drawPixel(int x, int y, uint16_t color);
    static void drawSprite(int x, int y, int sw, int sh, const uint8_t *bits, uint16_t color, bool xbm = true);
    static void drawRect(int x, int y, int w, int h, uint16_t color);
    static void flush(); // High-performance burst to AMOLED

private:
    static uint16_t* vfb; // Virtual Framebuffer
    static uint16_t* line_scratch; // For scaling
};

#endif
