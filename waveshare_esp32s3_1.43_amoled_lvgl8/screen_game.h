#pragma once
#include <Arduino.h>
#include "amoled.h"
#include <lvgl.h>

extern Amoled amoled;

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH  466
#define SCREEN_HEIGHT 466
#endif

// Game State External Variables
extern bool is_gaming;
extern bool is_jumping;
extern bool is_game_over;
extern bool dino_ready;
extern uint32_t game_timer;
extern uint32_t death_time;
extern int game_score;
extern int high_score;

extern float dino_y;
extern float current_sped;

// Game Mode Selection
enum ActiveGame { GAME_DINO, GAME_FLAPPY };
extern ActiveGame activeGame;

// Function Declarations
extern "C" {
    void run_raw_dino_frame();
    void run_raw_flappy_frame();
    void update_screen_game();
    void reset_obstacles();
    void reset_flappy_game();
    void flappy_flap();
    void stop_all_games();
    
    // Score label bridging
    extern lv_obj_t * ui_Uilabelnew21;
    extern lv_obj_t * ui_Label2; // Bridging the Game Over label
}
