#pragma once
// L2: fake AnimatedGIF — just enough for the .ino (and ui_godzillaspeedometer.cpp,
// if compiled later) to parse. Real GIF decode/playback is a deliberate gap
// for now (matches sim/'s existing static-placeholder precedent) — Day-3
// stretch goal: try compiling the real AnimatedGIF library, since it's
// portable C/C++, not ESP-specific.
#include <cstdint>
struct GIFDRAW {
    int iX, iY, y, iWidth;
    uint8_t* pPixels;
    uint16_t* pPalette;
    uint8_t ucHasTransparency, ucTransparent;
};
#define LITTLE_ENDIAN_PIXELS 0

class AnimatedGIF {
public:
    void begin(int) {}
    bool open(uint8_t*, int, void (*)(GIFDRAW*)) { return false; }
    bool playFrame(bool, int*) { return false; }
    void close() {}
    void reset() {}
};
