#pragma once
// Shared state between L3 (runtime bootstrap, owns the SDL window/mouse) and
// L1 (board BSP shims). Keeps L1 free of direct SDL dependencies.
#include <cstdint>

#define EMU_DISP_W 466
#define EMU_DISP_H 466

// L3 writes these every frame from SDL events; L1's FT3168 shim reads them.
struct EmuInput {
    bool     pressed = false;
    uint16_t x = 0, y = 0;
};
extern EmuInput g_emu_input;

// L1's Amoled shim writes finished frames here; L3 reads it once per frame
// and pushes it to the SDL texture. RGB565, native (un-swapped) byte order.
extern uint16_t g_emu_framebuffer[EMU_DISP_W * EMU_DISP_H];
