// L1: FT3168 touch-driver substitute. Implements the same 2-function API
// declared in the real (unmodified) FT3168.h, reading mouse state that L3
// sets from SDL events instead of polling the real I2C touch chip.
#include "FT3168.h"
#include "emu_state.h"

EmuInput g_emu_input;

void Touch_Init(void) {}

uint8_t getTouch(uint16_t *x, uint16_t *y) {
    if (!g_emu_input.pressed) return 0;
    *x = g_emu_input.x;
    *y = g_emu_input.y;
    return 1;
}
