#include "retro_engine.h"

uint16_t* RetroEngine::vfb = nullptr;
uint16_t* RetroEngine::line_scratch = nullptr;
extern Amoled amoled;

bool RetroEngine::begin() {
    vfb = (uint16_t*)heap_caps_malloc(V_WIDTH * V_HEIGHT * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!vfb) vfb = (uint16_t*)malloc(V_WIDTH * V_HEIGHT * 2);
    line_scratch = (uint16_t*)malloc(466 * 2);
    if (vfb) memset(vfb, 0, V_WIDTH * V_HEIGHT * 2);
    return (vfb != nullptr);
}

void RetroEngine::clear(uint16_t color) {
    if (!vfb) return;
    // Byte swap for AMOLED
    uint16_t be_color = (color << 8) | (color >> 8);
    for (int i = 0; i < V_WIDTH * V_HEIGHT; i++) vfb[i] = be_color;
}

void RetroEngine::drawPixel(int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || x >= V_WIDTH || y >= V_HEIGHT) return;
    // Byte swap for AMOLED
    uint16_t be_color = (color << 8) | (color >> 8);
    vfb[y * V_WIDTH + x] = be_color;
}

void RetroEngine::drawRect(int x, int y, int w, int h, uint16_t color) {
    if (!vfb) return;
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            drawPixel(x + j, y + i, color);
        }
    }
}

void RetroEngine::drawSprite(int x, int y, int sw, int sh, const uint8_t *bits, uint16_t color, bool xbm) {
    if (!vfb) return;
    int bw = (sw + 7) / 8;
    for (int j = 0; j < sh; j++) {
        for (int i = 0; i < sw; i++) {
            bool pixel = false;
            if (xbm) pixel = (bits[j * bw + i / 8] & (1 << (i & 7)));
            else pixel = (bits[j * bw + i / 8] & (0x80 >> (i & 7)));
            
            if (pixel) drawPixel(x + i, y + j, color);
        }
    }
}

void RetroEngine::flush() {
    if (!vfb || !line_scratch) return;
    // Pushing bytes exactly as they are in VFB (already swapped)
    for (int y = 0; y < V_HEIGHT; y++) {
        uint16_t* src = &vfb[y * V_WIDTH];
        for (int x = 0; x < V_WIDTH; x++) {
            uint16_t p = src[x];
            line_scratch[x * 2] = p;
            line_scratch[x * 2 + 1] = p;
        }
        amoled.drawArea(0, y * 2, 465, y * 2, line_scratch);
        amoled.drawArea(0, y * 2 + 1, 465, y * 2 + 1, line_scratch);
    }
}
