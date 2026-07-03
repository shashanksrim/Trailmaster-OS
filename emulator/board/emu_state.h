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

// L3 sets this once at startup, before calling setup(), to a function that
// pumps SDL events and presents g_emu_framebuffer to the real window. L1's
// Amoled shim calls it after every write so the window updates in real time
// — not just once per loop() iteration. This matters because some real
// firmware code (e.g. the boot splash) calls lv_timer_handler() in a
// blocking loop INSIDE setup(), before L3's own event/present loop ever
// runs; on real hardware that's fine since the flush callback IS the
// display, but without this hook the emulator would show nothing until
// setup() returns and the splash (or any other setup()-time animation)
// would be invisible. Kept as a plain function pointer (not a direct SDL
// call from L1) to keep L1 free of SDL dependencies, per the file header.
extern void (*g_emu_present)();
