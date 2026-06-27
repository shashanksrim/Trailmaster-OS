#include <Arduino.h>
#include "hw_config.h"
#include "FT3168.h"
#include "nofrendo.h"
#include "NesEngine.h"

extern "C" void nes_poweroff(void);
extern "C" void main_quit(void);

extern "C" {
    #include "nesinput.h"
}

// Button Masks from nesinput.h
#define HW_MASK_A      INP_PAD_A
#define HW_MASK_B      INP_PAD_B
#define HW_MASK_SELECT INP_PAD_SELECT
#define HW_MASK_START  INP_PAD_START
#define HW_MASK_UP     INP_PAD_UP
#define HW_MASK_DOWN   INP_PAD_DOWN
#define HW_MASK_LEFT   INP_PAD_LEFT
#define HW_MASK_RIGHT  INP_PAD_RIGHT

static volatile uint16_t start_x, start_y;
static volatile unsigned long touch_start_ms = 0;
static volatile unsigned long game_launch_ms = 0;
static volatile bool is_touching = false;
static volatile bool exit_requested = false;
static volatile bool ignition_triggered = false;
static volatile unsigned long ignition_start_ms = 0;

static volatile bool initial_touch_released = false;
static volatile bool first_frame_passed = false;

extern "C" void reset_nes_controller() {
    exit_requested = false;
    ignition_triggered = false;
    ignition_start_ms = 0;
    game_launch_ms = millis();
    is_touching = false;
    initial_touch_released = false;
    first_frame_passed = false;
    printf("Controller: Smart Arcade Mode (Tap to Ignite Active)\n");
}

void setup_controller() {
    Touch_Init();
    reset_nes_controller();
}

extern "C" void nes_notify_first_frame_rendered() {
    if (!first_frame_passed) {
        first_frame_passed = true;
        game_launch_ms = millis(); // Reset clock EXACTLY when the screen draws!
    }
}

extern "C" int nes_get_gamepad_state() {
    if (nes_is_running() == false) return 0;
    
    int state = 0;
    unsigned long elapsed = millis() - game_launch_ms;
    
    uint16_t tx, ty;
    bool touching = getTouch(&tx, &ty);
    if (!touching) initial_touch_released = true;

    // 1. SMART IGNITION: Triggered after game has fully loaded
    // Flappy Bird has a very long initial load (7-8s) before the title is interactive.
    // All other games are ready within ~1.2s of first frame.
    extern char current_rom_path[128];
    bool is_flappy = (strcasestr(current_rom_path, "flappy") != NULL);
    unsigned long ignition_delay = is_flappy ? 8500 : 1200;

    if (!ignition_triggered) {
        if (elapsed > ignition_delay) {
            printf("[INPUT] Virtual Ignition Triggered! Elapsed: %lu ms (delay=%lu)\n", elapsed, ignition_delay);
            ignition_triggered = true;
            ignition_start_ms = millis();
        }
        return 0; // Don't allow steering until ignited
    }

    // 2. IGNITION SEQUENCE (Once triggered)
    // Multi-pulse "Gatling" sequence to punch through various title screens
    unsigned long ignition_elapsed = millis() - ignition_start_ms;
    
    // Flappy Bird needs a longer Gatling window to load but users want quick entry.
    // Adjusted to 8s as requested. Other games only need 1.5s.
    unsigned long max_ignition = is_flappy ? 8000 : 1500;
    
    if (ignition_elapsed < max_ignition) {
        int cycle = (ignition_elapsed / 250) % 4; // 250ms each state
        if (cycle == 0) state |= HW_MASK_START;
        else if (cycle == 1) state = 0;
        else if (cycle == 2) state |= HW_MASK_A;
        else if (cycle == 3) state = 0;
    } else {
        // 3. AUTO-GAS: Maintain A (For driving games). Disabled for Flappy Bird to allow user flaps.
        if (!is_flappy) state |= HW_MASK_A;
    }

    if (touching) {
        // Touch Maps to Button A directly so tapping works intuitively for Flappy Bird and jump games
        state |= HW_MASK_A; 
        if (!is_touching) {
            start_x = tx; start_y = ty;
            touch_start_ms = millis();
            is_touching = true;
        }
        
        int dy = (int)ty - (int)start_y;
        
        // 4. CLEAN EXIT: Nuclear Memory Wipe (NO REBOOT)
        if (dy > 200) {
            printf("[SYSTEM] CLEAN EXIT: Purging Emulator State...\n");
            main_quit();     // Breaks the outer main_loop
            nes_poweroff();  // Breaks the inner nes_emulate loop
            return 0;
        }
        // 4. SENSITIVE STEERING
        int dx = (int)tx - (int)start_x;
        if (abs(dx) > 10) {
            state |= (dx > 0) ? HW_MASK_RIGHT : HW_MASK_LEFT;
        }
    } else {
        is_touching = false;
    }
    
    return state;
}
