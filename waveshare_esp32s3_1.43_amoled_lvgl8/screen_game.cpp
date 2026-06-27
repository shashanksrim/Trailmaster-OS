#include <Arduino.h>
#include <lvgl.h>
#include "screen_game.h"
#include "retro_engine.h"

// --- DEFINITIVE DINO ENGINE (ORGANIC BIRDS & STABLE HUD) ---

static const uint8_t dino_bits[3][200] = {
{ 0x00,0x00,0xF8,0x3F,0x00,0x00,0x00,0xFC,0x7F,0x00,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xEE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x00,0x00,0x00,0xFE,0x01,0x00,0x00,0x00,0xFE,0x1F,0x00,0x01,0x00,0xFF,0x1F,0x00,0x03,0x80,0x7F,0x00,0x00,0x07,0xE0,0x7F,0x00,0x00,0x07,0xF8,0xFF,0x00,0x00,0x0F,0xFC,0xFF,0x03,0x00,0x1F,0xFE,0xFF,0x07,0x00,0x3F,0xFF,0x7F,0x0E,0x00,0xFE,0xFF,0x7F,0x0C,0x00,0xFE,0xFF,0x7F,0x00,0x00,0xFE,0xFF,0x7F,0x00,0x00,0xFC,0xFF,0x7F,0x00,0x00,0xF8,0xFF,0x3F,0x00,0x00,0xF0,0xFF,0x3F,0x00,0x00,0xE0,0xFF,0x0F,0x00,0x00,0xE0,0xFF,0x0F,0x00,0x00,0x80,0xFF,0x07,0x00,0x00,0x80,0xFF,0x03,0x00,0x00,0x00,0xBE,0x03,0x00,0x00,0x00,0x0E,0x03,0x00,0x00,0x00,0x0E,0x03,0x00,0x00,0x00,0x06,0x03,0x00,0x00,0x00,0x06,0x07,0x00,0x00,0x00,0x1E,0x0F,0x00,0x00 },
{ 0x00,0x00,0xF8,0x3F,0x00,0x00,0x00,0xFC,0x7F,0x00,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xEE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x00,0x00,0x00,0xFE,0x01,0x00,0x00,0x00,0xFE,0x1F,0x00,0x01,0x00,0xFF,0x1F,0x00,0x03,0x80,0x7F,0x00,0x00,0x07,0xE0,0x7F,0x00,0x00,0x07,0xF8,0xFF,0x00,0x00,0x0F,0xFC,0xFF,0x03,0x00,0x1F,0xFE,0xFF,0x07,0x00,0x3F,0xFF,0x7F,0x0E,0x00,0xFE,0xFF,0x7F,0x0C,0x00,0xFE,0xFF,0x7F,0x00,0x00,0xFE,0xFF,0x7F,0x00,0x00,0xFC,0xFF,0x7F,0x00,0x00,0xF8,0xFF,0x3F,0x00,0x00,0xF0,0xFF,0x3F,0x00,0x00,0xE0,0xFF,0x0F,0x00,0x00,0xE0,0xFF,0x0F,0x00,0x00,0x80,0xFF,0x07,0x00,0x00,0x80,0xFF,0x03,0x00,0x00,0x00,0xBE,0x03,0x00,0x00,0x00,0x0E,0x0F,0x00,0x00,0x00,0x0E,0x0F,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x1E,0x00,0x00,0x00 },
{ 0x00,0x00,0xF8,0x3F,0x00,0x00,0x00,0xFC,0x7F,0x00,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xEE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x01,0x00,0x00,0xFE,0xFF,0x00,0x00,0x00,0xFE,0x01,0x00,0x00,0x00,0xFE,0x1F,0x00,0x01,0x00,0xFF,0x1F,0x00,0x03,0x80,0x7F,0x00,0x00,0x07,0xE0,0x7F,0x00,0x00,0x07,0xF8,0xFF,0x00,0x00,0x0F,0xFC,0xFF,0x03,0x00,0x1F,0xFE,0xFF,0x07,0x00,0x3F,0xFF,0x7F,0x0E,0x00,0xFE,0xFF,0x7F,0x0C,0x00,0xFE,0xFF,0x7F,0x00,0x00,0xFE,0xFF,0x7F,0x00,0x00,0xFC,0xFF,0x7F,0x00,0x00,0xF8,0xFF,0x3F,0x00,0x00,0xF0,0xFF,0x3F,0x00,0x00,0xE0,0xFF,0x0F,0x00,0x00,0xE0,0xFF,0x0F,0x00,0x00,0x80,0xFF,0x07,0x00,0x00,0x80,0xFF,0x03,0x00,0x00,0x00,0xBE,0x03,0x00,0x00,0x00,0x0C,0x03,0x00,0x00,0x00,0x3C,0x03,0x00,0x00,0x00,0x3C,0x03,0x00,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x00,0x0F,0x00,0x00 }
};

static const uint8_t cactus_bits[] = { 0x00,0x03,0x00,0x80,0x07,0x00,0xC0,0x0F,0x00,0xC0,0x0F,0x00,0xC0,0x0F,0x00,0xC0,0x0F,0x00,0xC0,0x0F,0x00,0xC0,0x0F,0x00,0xC0,0x0F,0x00,0xC0,0x0F,0x00,0xC0,0x8F,0x01,0xC6,0xCF,0x03,0xCF,0xCF,0x03,0xCF,0xCF,0x03,0xCF,0xCF,0x03,0xCF,0xCF,0x03,0xCF,0xCF,0x03,0xCF,0xCF,0x03,0xCF,0xCF,0x03,0xCF,0xCF,0x03,0xCF,0xCF,0x03,0xCF,0xCF,0x03,0xFF,0xFF,0x03,0xFF,0xFF,0x01,0xFE,0xFF,0x00,0xFC,0x7F,0x00,0xF8,0x0F,0x00,0xC0,0x0F,0x00,0xC0,0x0F,0x00 };
static const uint8_t cloud_bits[] = { 0x00,0x00,0x7C,0x00,0x00,0x00,0x80,0xCF,0x01,0x00,0x00,0xE0,0x01,0x03,0x00,0x00,0x60,0x00,0x0F,0x00,0x00,0x38,0x00,0xFE,0x00,0x00,0x3C,0x30,0xA8,0x03,0xE0,0x0F,0x00,0x80,0x07,0x70,0x05,0x03,0x00,0x0E,0x18,0x00,0x00,0x02,0x08,0x0E,0x00,0x00,0x00,0x18,0xFF,0xFF,0xFF,0xFF,0x3F };

// --- ORGANIC PTERODACTYL BITMAPS (REPLACES ALIEN LOOK) ---
static const uint8_t bird_bits[2][180] = {
// Frame 1: Wings Arched UP
{ 0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x1E,0x00,0x00,0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00,0x00,0x7F,0x00,0x00,0x00,0x00,0x80,0xFF,0x00,0x00,0x00,0x00,0xC0,0xFF,0x01,0x00,0x00,0x00,0xE0,0xFF,0x03,0x00,0x00,0x00,0xF0,0xFF,0x0F,0x1F,0x00,0x00,0xF8,0xFF,0x1F,0x3E,0x00,0x00,0xFC,0xFF,0x3F,0x78,0x00,0x00,0xFE,0xFF,0x7E,0xF0,0x00,0x00,0xFF,0xFF,0xFC,0xE1,0x01,0x00,0xFF,0xFF,0xFF,0xC3,0x03,0x00,0xFF,0xFF,0xFF,0x87,0x07,0x00,0xFF,0xFF,0xFF,0x0F,0x0F,0x00,0xFF,0x7F,0xFE,0x1F,0x1E,0x00,0xFE,0x3F,0xFC,0x3F,0x1C,0x00,0xFC,0x1F,0x00,0x7F,0x00,0x00,0xF8,0x0F,0x00,0xFE,0x00,0x00,0xF0,0x07,0x00,0xFC,0x00,0x00,0xE0,0x03,0x00,0xF0,0x00,0x00,0x40,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
// Frame 2: Wings Arched DOWN
{ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x80,0x01,0x03,0x00,0x00,0x00,0xC0,0x03,0x07,0x1F,0x00,0x00,0xE0,0x07,0x0F,0x3E,0x00,0x00,0xF0,0x0F,0x1F,0x78,0x00,0x00,0xF8,0x1F,0x3F,0xF0,0x00,0x00,0xFC,0x3F,0x7E,0xC1,0x01,0x00,0xFE,0x7F,0xFC,0x83,0x03,0x00,0xFF,0xFF,0xFF,0x07,0x07,0x00,0xFF,0xFF,0xFF,0x0F,0x0F,0x00,0xFF,0xFF,0xFF,0x1F,0x1E,0x00,0xFF,0x7F,0xFE,0x3F,0x1C,0x00,0xFE,0x3F,0xFC,0x7F,0x00,0x00,0xFC,0x1F,0xF8,0xFF,0x00,0x00,0xF8,0x0F,0xF0,0xFE,0x01,0x00,0xF0,0x07,0xE0,0xFC,0x03,0x00,0xE0,0x03,0xC0,0xF0,0x07,0x00,0x40,0x01,0x80,0x80,0x0F,0x00,0x00,0x01,0x00,0x00,0x1F,0x00,0x00,0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00,0x00,0x7F,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x3C,0x00 },
};

static const uint8_t font_mini[26][5] = {
  {0x7C,0x12,0x11,0x12,0x7C}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
  {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
  {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
  {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
  {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
};

static const uint8_t digit_bits[10][5] = {
  {0x3E,0x41,0x41,0x41,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x62,0x51,0x49,0x49,0x46}, {0x22,0x49,0x49,0x49,0x36},
  {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}
};

// --- CORE ---
bool is_jumping = false, is_game_over = false;
bool dino_ready = true;
uint32_t game_timer = 0, death_time = 0;
float distance_ran = 0;
int game_score = 0, high_score = 0;
float dino_y = 157; 

void draw_outlined_text(int x, int y, const char* txt, int scale, uint16_t text_color);

void draw_text_mini(int x, int y, const char* txt, int scale, uint16_t color = 0x0000) {
    while (*txt) {
        char c = *txt;
        if (c >= 'A' && c <= 'Z') {
            const uint8_t* bits = font_mini[c - 'A'];
            for (int i = 0; i < 5; i++) {
                uint8_t col = bits[i];
                for (int j = 0; j < 7; j++) { if (col & (1 << j)) RetroEngine::drawPixel(x + (i * scale), y + (j * scale), color); }
            }
        } else if (c >= '0' && c <= '9') {
            const uint8_t* bits = digit_bits[c - '0'];
            for (int i = 0; i < 5; i++) {
                uint8_t col = bits[i];
                for (int j = 0; j < 7; j++) { if (col & (1 << j)) RetroEngine::drawPixel(x + (i * scale), y + (j * scale), color); }
            }
        } else if (c == ' ') { x += 2 * scale; }
        x += 6 * scale; txt++;
    }
}

void draw_hud_aesthetic() {
    int cx = 116; 
    char buf_full[32];
    sprintf(buf_full, "HI %05d  %05d", high_score, game_score);
    int total_w = 16 * 6;
    int start_x = cx - (total_w / 2);
    draw_text_mini(start_x, 30, buf_full, 1);
}

void draw_gameover_vfb() {
    int cx = 119; 
    RetroEngine::drawRect(cx-70, 80, 12, 4, 0x0000); RetroEngine::drawRect(cx-70, 80, 4, 15, 0x0000); RetroEngine::drawRect(cx-70, 91, 12, 4, 0x0000); RetroEngine::drawRect(cx-62, 86, 4, 9, 0x0000); RetroEngine::drawRect(cx-66, 86, 8, 4, 0x0000);
    RetroEngine::drawRect(cx-55, 80, 10, 4, 0x0000); RetroEngine::drawRect(cx-55, 80, 4, 15, 0x0000); RetroEngine::drawRect(cx-48, 80, 4, 15, 0x0000); RetroEngine::drawRect(cx-55, 87, 10, 4, 0x0000);
    RetroEngine::drawRect(cx-40, 80, 4, 15, 0x0000); RetroEngine::drawRect(cx-30, 80, 4, 15, 0x0000); RetroEngine::drawRect(cx-40, 80, 14, 4, 0x0000); RetroEngine::drawRect(cx-35, 80, 4, 7, 0x0000);
    RetroEngine::drawRect(cx-23, 80, 10, 4, 0x0000); RetroEngine::drawRect(cx-23, 86, 8, 4, 0x0000); RetroEngine::drawRect(cx-23, 91, 10, 4, 0x0000); RetroEngine::drawRect(cx-23, 80, 4, 15, 0x0000);
     RetroEngine::drawRect(cx+10, 80, 10, 4, 0x0000); RetroEngine::drawRect(cx+10, 91, 10, 4, 0x0000); RetroEngine::drawRect(cx+10, 80, 4, 15, 0x0000); RetroEngine::drawRect(cx+18, 80, 4, 15, 0x0000);
    RetroEngine::drawRect(cx+24, 80, 4, 15, 0x0000); RetroEngine::drawRect(cx+33, 80, 4, 15, 0x0000); RetroEngine::drawRect(cx+26, 91, 10, 4, 0x0000);
    RetroEngine::drawRect(cx+38, 80, 10, 4, 0x0000); RetroEngine::drawRect(cx+38, 86, 8, 4, 0x0000); RetroEngine::drawRect(cx+38, 91, 10, 4, 0x0000); RetroEngine::drawRect(cx+38, 80, 4, 15, 0x0000);
    RetroEngine::drawRect(cx+50, 80, 10, 4, 0x0000); RetroEngine::drawRect(cx+50, 80, 4, 15, 0x0000); RetroEngine::drawRect(cx+50, 86, 10, 4, 0x0000); RetroEngine::drawRect(cx+58, 80, 4, 7, 0x0000); RetroEngine::drawRect(cx+58, 88, 4, 7, 0x0000);

    draw_text_mini(cx - 50, 125, "TAP TO TRY AGAIN", 1);
}

float enemy_x = 240, cloud_x[3] = { 100, 240, 360 }, current_sped = 8.5, jump_vel = -8.1, gravity = 0.65;
int enemy_count = 1; 
int obstacle_type = 0; 
float bird_y = 150;
float ground_dots[15] = { 10, 30, 60, 100, 120, 150, 180, 200, 220, 240, 280, 320, 360, 400, 440 };

void run_raw_dino_frame() {
    if (is_game_over) {
        RetroEngine::clear(0xFFFF); 
        RetroEngine::drawRect(0, 192, 233, 1, 0x0000);
        draw_hud_aesthetic();
        draw_gameover_vfb();
        RetroEngine::flush();
        return;
    }

    // Dino auto mode: no jumps are needed as obstacles are locked off-screen.

    if (is_jumping) { dino_y += jump_vel; jump_vel += gravity; if (dino_y >= 157) { dino_y = 157; is_jumping = false; } }

    if (!dino_ready) {
        enemy_x -= current_sped; 
        if (enemy_x < -100) { 
            enemy_x = 240 + random(0, 150); 
            int r = random(0, 100);
            if (r < 75) { 
                obstacle_type = 0;
                if (r < 50) enemy_count = 1; else if (r < 65) enemy_count = 2; else enemy_count = 3;
            } else {
                obstacle_type = 1; enemy_count = 1;
                bird_y = (random(0, 2) == 0) ? 120 : 155; 
            }
        }
    } else {
        enemy_x = 240; // Keep obstacles off-screen during ready state
    }
    
    // Freeze score progression in ready state
    if (!dino_ready) {
        distance_ran += current_sped * 0.15; 
        game_score = (int)distance_ran;
    }

    for(int i=0; i<3; i++) { cloud_x[i] -= 1.2; if (cloud_x[i] < -40) cloud_x[i] = 240 + random(0, 100); }
    for(int i=0; i<15; i++) { ground_dots[i] -= current_sped; if(ground_dots[i] < -20) ground_dots[i] = 240 + random(0,40); }

    // Bypass collisions in ready state so autonomous preview is bulletproof
    if (!dino_ready) {
        for (int i = 0; i < enemy_count; i++) {
            float ex = enemy_x + (i * 20); 
            float ey = (obstacle_type == 0) ? 156 : bird_y;
            float ew = (obstacle_type == 0) ? 18 : 42;
            float eh = (obstacle_type == 0) ? 38 : 30;
            if (ex < 55 && ex + ew > 25) { 
                if (dino_y + 35 > ey && dino_y < ey + eh) { 
                    is_game_over = true; 
                    death_time = millis(); 
                    if (game_score > high_score) high_score = game_score; 
                } 
            }
        }
    }

    RetroEngine::clear(0xFFFF); 
    RetroEngine::drawRect(0, 192, 233, 1, 0x0000);
    for(int i=0; i<15; i++) { RetroEngine::drawRect(ground_dots[i], 193 + (i%5), 3, 1, 0x0000); } 

    draw_hud_aesthetic();
    for(int i=0; i<3; i++) RetroEngine::drawSprite(cloud_x[i], 60 + (i*15), 38, 11, cloud_bits, 0x0000, true);
    
    if (obstacle_type == 0) {
        for (int i = 0; i < enemy_count; i++) { RetroEngine::drawSprite(enemy_x + (i * 20), 156, 18, 38, cactus_bits, 0x0000, true); }
    } else {
        int b_frame = (millis() / 250) % 2;
        RetroEngine::drawSprite(enemy_x, bird_y, 42, 30, bird_bits[b_frame], 0x0000, true);
    }
    
    int frame = is_jumping ? 0 : ((millis() / 80) % 2 + 1);
    RetroEngine::drawSprite(30, dino_y, 33, 35, dino_bits[frame], 0x0000, true);

    // Ready screen overlays
    if (dino_ready) {
        int cx = 119;
        draw_outlined_text(cx - 36, 75, "GET READY", 1, 0xFFFF);
        draw_outlined_text(cx - 40, 115, "TAP TO JUMP", 1, 0xFFFF);
    }

    RetroEngine::flush();
}

ActiveGame activeGame = GAME_DINO;

// Flappy Bird Variables
float flappy_y = 96.0f;
float flappy_vy = 0.0f;
float flappy_gravity = 0.52f; // Snappy retro gravity
float flappy_jump = -5.2f;    // Crisp tight jump impulse
int flappy_score = 0;
int flappy_high_score = 0;
float pipes_x[2] = { 240.0f, 360.0f };
int pipes_gap_y[2] = { 90, 110 };
bool pipes_passed[2] = { false, false };
const int pipes_gap_h = 66; // Slightly larger vertical opening for playability
bool flappy_ready = true;

// Parallax scrolling offset tracking
static float flappy_bg_x = 0.0f;
static float flappy_trees_x = 0.0f;
static float flappy_ground_x = 0.0f;
float jimny_x = 300.0f; // Jimny easter egg horizontal position

// Float / scroll variables from Dino game reused/referenced
extern float enemy_x;
extern float cloud_x[3];
extern float ground_dots[15];

void drawCircle(int cx, int cy, int r, uint16_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) {
                RetroEngine::drawPixel(cx + x, cy + y, color);
            }
        }
    }
}

// 8-way black outlined text helper for high fidelity overlays
void draw_outlined_text(int x, int y, const char* txt, int scale, uint16_t text_color) {
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx != 0 || dy != 0) {
                draw_text_mini(x + dx, y + dy, txt, scale, 0x0000);
            }
        }
    }
    draw_text_mini(x, y, txt, scale, text_color);
}

// Large white score digits with thick solid black outlines
void draw_flappy_score(int cx, int y, int score) {
    char buf[16];
    sprintf(buf, "%d", score);
    int len = strlen(buf);
    int char_w = 6 * 2; // character width under scale 2
    int total_w = len * char_w - 2;
    int start_x = cx - total_w / 2;
    
    // 8-way black outline
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx != 0 || dy != 0) {
                int lx = start_x + dx;
                for (int c_idx = 0; c_idx < len; c_idx++) {
                    char c = buf[c_idx];
                    const uint8_t* bits = digit_bits[c - '0'];
                    for (int i = 0; i < 5; i++) {
                        uint8_t col = bits[i];
                        for (int j = 0; j < 7; j++) {
                            if (col & (1 << j)) {
                                for (int sx = 0; sx < 2; sx++) {
                                    for (int sy = 0; sy < 2; sy++) {
                                        RetroEngine::drawPixel(lx + (i * 2) + sx, y + dy + (j * 2) + sy, 0x0000);
                                    }
                                }
                            }
                        }
                    }
                    lx += 6 * 2;
                }
            }
        }
    }
    
    // White body
    int lx = start_x;
    for (int c_idx = 0; c_idx < len; c_idx++) {
        char c = buf[c_idx];
        const uint8_t* bits = digit_bits[c - '0'];
        for (int i = 0; i < 5; i++) {
            uint8_t col = bits[i];
            for (int j = 0; j < 7; j++) {
                if (col & (1 << j)) {
                    for (int sx = 0; sx < 2; sx++) {
                        for (int sy = 0; sy < 2; sy++) {
                            RetroEngine::drawPixel(lx + (i * 2) + sx, y + (j * 2) + sy, 0xFFFF);
                        }
                    }
                }
            }
        }
        lx += 6 * 2;
    }
}

// Procedural background Skyline Buildings (slowest parallax layer)
void draw_skyline_buildings(float bg_x) {
    static const int building_w[12] = { 18, 14, 22, 16, 20, 15, 24, 18, 16, 20, 22, 14 };
    static const int building_h[12] = { 45, 30, 55, 38, 50, 32, 60, 42, 35, 48, 52, 28 };
    
    uint16_t building_color = 0x7EB9; // #7bd4dc
    uint16_t sky_color = 0x4DF9; // Sky blue
    
    float cur_x = bg_x;
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 12; i++) {
            int w = building_w[i];
            int h = building_h[i];
            int bx = (int)cur_x;
            int by = 192 - h;
            
            if (bx + w >= 0 && bx < 233) {
                RetroEngine::drawRect(bx, by, w, h, building_color);
                
                // Building shading outlines
                RetroEngine::drawRect(bx, by, 1, h, 0x6E56);
                RetroEngine::drawRect(bx + w - 1, by, 1, h, 0x5DCE);
                RetroEngine::drawRect(bx, by, w, 1, 0x6E56);
                
                // Square windows column
                if (w > 15 && h > 35) {
                    for (int wx = bx + 4; wx < bx + w - 4; wx += 5) {
                        for (int wy = by + 6; wy < 192 - 6; wy += 8) {
                            RetroEngine::drawRect(wx, wy, 2, 3, sky_color);
                        }
                    }
                }
            }
            cur_x += w + 4;
        }
    }
}

// Midground organic bushes (medium speed parallax layer)
void draw_midground_bushes(float trees_x) {
    static const int bush_w[10] = { 20, 28, 24, 32, 22, 26, 30, 24, 28, 20 };
    static const int bush_h[10] = { 14, 18, 12, 22, 15, 16, 20, 13, 17, 12 };
    
    uint16_t bush_color = 0x3DF0;      // Medium green (#40b060)
    uint16_t bush_highlight = 0x5E80;  // Lighter green rounded top
    
    float cur_x = trees_x;
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < 10; i++) {
            int w = bush_w[i];
            int h = bush_h[i];
            int bx = (int)cur_x;
            int by = 192 - h;
            
            if (bx + w >= 0 && bx < 233) {
                RetroEngine::drawRect(bx, by, w, h, bush_color);
                
                // Simulating a rounded top
                RetroEngine::drawRect(bx + 2, by, w - 4, 3, bush_highlight);
                RetroEngine::drawRect(bx + 4, by - 2, w - 8, 2, bush_highlight);
                
                RetroEngine::drawRect(bx + w - 2, by + 4, 2, h - 4, 0x24C0);
            }
            cur_x += w - 3;
        }
    }
}

// Parallax scrolling green-stripe stripe ground with beige base
void draw_parallax_ground(float ground_x) {
    uint16_t green_stripe_color = 0x7608;
    uint16_t dark_green_line = 0x5464;
    uint16_t beige_ground_color = 0xDED1;
    
    RetroEngine::drawRect(0, 192, 233, 10, green_stripe_color);
    
    int offset_x = (int)ground_x;
    for (int x = -15; x < 233 + 15; x += 12) {
        int start_x = x + offset_x;
        for (int dy = 0; dy < 10; dy++) {
            RetroEngine::drawRect(start_x + dy, 192 + dy, 3, 1, dark_green_line);
        }
    }
    
    RetroEngine::drawRect(0, 202, 233, 1, 0x0000);
    RetroEngine::drawRect(0, 203, 233, 30, beige_ground_color);
}

// Micro pixel-art Jimny car easter egg!
void draw_jimny_easter_egg(int jx, int jy) {
    uint16_t body_color = 0xBE00;  // Signature Kinetic Yellow / lime-green (#c7df00)
    uint16_t window_color = 0x0000;// Dark tinted box windows
    uint16_t wheel_color = 0x2104; // Rugged dark wheels
    
    // 1. Boxy Cab
    RetroEngine::drawRect(jx + 2, jy, 9, 5, body_color);
    // 2. Tinted windows
    RetroEngine::drawRect(jx + 3, jy + 1, 3, 3, window_color); // Front windshield
    RetroEngine::drawRect(jx + 7, jy + 1, 3, 3, window_color); // Side/rear window
    
    // 3. Rugged lower body
    RetroEngine::drawRect(jx, jy + 5, 13, 3, body_color);
    
    // 4. Front bumper / headlight
    RetroEngine::drawRect(jx, jy + 6, 1, 2, 0x0000);
    RetroEngine::drawPixel(jx, jy + 5, 0xFFFF); // Glow-in-the-dark round headlight!
    
    // 5. Back spare tire holder
    RetroEngine::drawRect(jx + 12, jy + 2, 2, 5, 0x0000);
    
    // 6. Dual heavy wheels
    drawCircle(jx + 3, jy + 8, 2, 0x0000);
    drawCircle(jx + 3, jy + 8, 1, wheel_color);
    drawCircle(jx + 9, jy + 8, 2, 0x0000);
    drawCircle(jx + 9, jy + 8, 1, wheel_color);
}

// Crisp round bird in official Flappy Bird colors with dynamic nose-dive and tap-climb tilting!
void draw_flappy_bird(int bx, int by) {
    int eye_dx = 3, eye_dy = -3;
    int pupil_dx = 4, pupil_dy = -3;
    int beak_dx = 5, beak_dy = 0;
    int wing_dx = -4, wing_dy = 0;
    
    // High-frequency flutter on tap upward thrust (vy < 0) and absolute stillness when gliding/falling
    int wing_offset = (flappy_ready || flappy_vy >= 0) ? 1 : ((millis() / 35) % 2 == 0 ? 2 : -3);
    
    if (!flappy_ready) {
        if (flappy_vy < -1.0f) {
            // Tilting UP (Rising / Climbing tap)
            eye_dx = 2; eye_dy = -5;
            pupil_dx = 3; pupil_dy = -5;
            beak_dx = 6; beak_dy = -3;
            wing_dx = -5; wing_dy = 2;
        } else if (flappy_vy > 1.5f) {
            // Nose-diving (Falling rapidly)
            eye_dx = 4; eye_dy = 0;
            pupil_dx = 5; pupil_dy = 0;
            beak_dx = 3; beak_dy = 4;
            wing_dx = -3; wing_dy = -3;
        }
    }

    // 1. Black outline for body
    drawCircle(bx, by, 9, 0x0000);
    // 2. Bright golden-yellow body
    drawCircle(bx, by, 8, 0xF586); 
    // 3. Cute white belly on bottom-left
    drawCircle(bx - 3, by + 3, 5, 0xFFFF);
    
    // 4. Black outline for wing
    drawCircle(bx + wing_dx, by + wing_dy + wing_offset, 5, 0x0000);
    // 5. White wing
    drawCircle(bx + wing_dx, by + wing_dy + wing_offset, 4, 0xFFFF);
    
    // 6. Beak outline (thick black lips)
    RetroEngine::drawRect(bx + beak_dx - 1, by + beak_dy - 1, 9, 7, 0x0000);
    // 7. Split orange/red beak (Flappy Bird style)
    RetroEngine::drawRect(bx + beak_dx, by + beak_dy, 7, 2, 0xFBE0); // Bright orange top beak
    RetroEngine::drawRect(bx + beak_dx, by + beak_dy + 3, 7, 2, 0xE100); // Red bottom beak
    // Beak split line (middle separator)
    RetroEngine::drawRect(bx + beak_dx, by + beak_dy + 2, 7, 1, 0x0000);
    
    // 8. Large eye outline & white
    drawCircle(bx + eye_dx, by + eye_dy, 4, 0x0000);
    drawCircle(bx + eye_dx, by + eye_dy, 3, 0xFFFF);
    // 9. Pupil
    drawCircle(bx + pupil_dx, by + pupil_dy, 1, 0x0000);
}

// Authentic retro 3D green shaded tubes
void draw_flappy_pipe(int px, int gap_y, int gap_h) {
    int top_pipe_end = gap_y - gap_h / 2;
    int bottom_pipe_start = gap_y + gap_h / 2;
    int pw = 24; // stem width
    int lip_w = 28; // lip width
    int lip_h = 10; // lip height
    
    uint16_t pipe_highlight = 0xDFFB; // Light white/mint highlight band
    uint16_t pipe_light = 0x9F2A;     // Light green
    uint16_t pipe_green = 0x75E5;     // Medium base green (#73bf2e)
    uint16_t pipe_shadow = 0x5404;    // Dark green (#558022)
    uint16_t pipe_dark_shadow = 0x2200;// Very dark forest green shadow
    
    // --- TOP PIPE ---
    int top_h = top_pipe_end - lip_h;
    if (top_h > 0) {
        RetroEngine::drawRect(px, 0, 2, top_h, pipe_highlight);
        RetroEngine::drawRect(px + 2, 0, 4, top_h, pipe_light);
        RetroEngine::drawRect(px + 6, 0, 10, top_h, pipe_green);
        RetroEngine::drawRect(px + 16, 0, 5, top_h, pipe_shadow);
        RetroEngine::drawRect(px + 21, 0, 3, top_h, pipe_dark_shadow);
        
        RetroEngine::drawRect(px - 1, 0, 1, top_h, 0x0000);
        RetroEngine::drawRect(px + pw, 0, 1, top_h, 0x0000);
    }
    
    // Lip
    int lx = px - 2;
    int ly = top_pipe_end - lip_h;
    RetroEngine::drawRect(lx - 1, ly - 1, lip_w + 2, lip_h + 2, 0x0000);
    RetroEngine::drawRect(lx, ly, 2, lip_h, pipe_highlight);
    RetroEngine::drawRect(lx + 2, ly, 4, lip_h, pipe_light);
    RetroEngine::drawRect(lx + 6, ly, 12, lip_h, pipe_green);
    RetroEngine::drawRect(lx + 18, ly, 6, lip_h, pipe_shadow);
    RetroEngine::drawRect(lx + 24, ly, 4, lip_h, pipe_dark_shadow);
    
    // --- BOTTOM PIPE ---
    int bottom_h = 192 - (bottom_pipe_start + lip_h);
    int b_stem_y = bottom_pipe_start + lip_h;
    if (bottom_h > 0) {
        RetroEngine::drawRect(px, b_stem_y, 2, bottom_h, pipe_highlight);
        RetroEngine::drawRect(px + 2, b_stem_y, 4, bottom_h, pipe_light);
        RetroEngine::drawRect(px + 6, b_stem_y, 10, bottom_h, pipe_green);
        RetroEngine::drawRect(px + 16, b_stem_y, 5, bottom_h, pipe_shadow);
        RetroEngine::drawRect(px + 21, b_stem_y, 3, bottom_h, pipe_dark_shadow);
        
        RetroEngine::drawRect(px - 1, b_stem_y, 1, bottom_h, 0x0000);
        RetroEngine::drawRect(px + pw, b_stem_y, 1, bottom_h, 0x0000);
    }
    
    // Lip
    ly = bottom_pipe_start;
    RetroEngine::drawRect(lx - 1, ly - 1, lip_w + 2, lip_h + 2, 0x0000);
    RetroEngine::drawRect(lx, ly, 2, lip_h, pipe_highlight);
    RetroEngine::drawRect(lx + 2, ly, 4, lip_h, pipe_light);
    RetroEngine::drawRect(lx + 6, ly, 12, lip_h, pipe_green);
    RetroEngine::drawRect(lx + 18, ly, 6, lip_h, pipe_shadow);
    RetroEngine::drawRect(lx + 24, ly, 4, lip_h, pipe_dark_shadow);
}

void draw_flappy_gameover() {
    int cx = 119;
    draw_outlined_text(cx - 32, 80, "GAME OVER", 1, 0xFFFF);
    draw_outlined_text(cx - 50, 125, "TAP TO TRY AGAIN", 1, 0xFFFF);
}

void flappy_flap() {
    if (flappy_ready) flappy_ready = false;
    flappy_vy = flappy_jump;
}

void reset_flappy_game() {
    flappy_y = 96.0f;
    flappy_vy = 0.0f;
    flappy_score = 0;
    is_game_over = false;
    flappy_ready = true;
    pipes_x[0] = 240.0f;
    pipes_gap_y[0] = 90;
    pipes_passed[0] = false;
    pipes_x[1] = 240.0f + 100.0f; // Reduced horizontal distance between subsequent pipes
    pipes_gap_y[1] = 110;
    pipes_passed[1] = false;

    // Reset parallax scroll offsets
    flappy_bg_x = 0.0f;
    flappy_trees_x = 0.0f;
    flappy_ground_x = 0.0f;
    
    // Spawn Jimny easter egg far off-screen initially
    jimny_x = 240.0f + random(300, 800);
}

void run_raw_flappy_frame() {
    // 1. Physics & scroll calculations
    if (!is_game_over && !flappy_ready) {
        flappy_vy += flappy_gravity;
        if (flappy_vy > 7.0f) flappy_vy = 7.0f; // Snappy falling cap
        flappy_y += flappy_vy;

        // Authentic foreground scroll (ground and pipes locked at identical fast speed)
        float foreground_speed = 2.2f; 
        for (int i = 0; i < 2; i++) {
            pipes_x[i] -= foreground_speed;
            if (pipes_x[i] < -40) {
                // Organic gap randomization: reset relative to the other pipe's position!
                int other = 1 - i;
                pipes_x[i] = pipes_x[other] + 95.0f + random(0, 30.0f);
                pipes_gap_y[i] = random(50, 130);
                pipes_passed[i] = false;
            }
            
            // Score tracking
            if (!pipes_passed[i] && pipes_x[i] + 12 < 45) {
                flappy_score++;
                pipes_passed[i] = true;
            }
        }

        // Scroll backgrounds at slower speeds to achieve rich depth parallax!
        flappy_bg_x -= 0.15f;
        if (flappy_bg_x < -267.0f) flappy_bg_x += 267.0f;

        flappy_trees_x -= 0.6f;
        if (flappy_trees_x < -224.0f) flappy_trees_x += 224.0f;

        flappy_ground_x -= foreground_speed; // Perfectly locked with pipes!
        if (flappy_ground_x < -12.0f) flappy_ground_x += 12.0f;

        // Update Jimny easter egg position
        jimny_x -= foreground_speed;
        if (jimny_x < -30) {
            jimny_x = 240.0f + random(600, 1800); // Drives by every 5 to 15 seconds randomly!
        }

        // Clouds scroll speed
        for (int i = 0; i < 3; i++) {
            cloud_x[i] -= 0.3f;
            if (cloud_x[i] < -40) cloud_x[i] = 240 + random(0, 100);
        }

        // Collision Checks
        // A. Ceiling & Ground
        if (flappy_y - 7 <= 0 || flappy_y + 7 >= 192) {
            is_game_over = true;
            death_time = millis();
            if (flappy_score > flappy_high_score) flappy_high_score = flappy_score;
        }

        // B. Pipes
        float bx1 = 45 - 7, bx2 = 45 + 7;
        float by1 = flappy_y - 7, by2 = flappy_y + 7;
        for (int i = 0; i < 2; i++) {
            float px1 = pipes_x[i];
            float px2 = pipes_x[i] + 24;
            float gap_top = pipes_gap_y[i] - pipes_gap_h / 2.0f;
            float gap_bottom = pipes_gap_y[i] + pipes_gap_h / 2.0f;

            if (!(bx2 < px1 || bx1 > px2 || by2 < 0 || by1 > gap_top)) {
                is_game_over = true;
                death_time = millis();
                if (flappy_score > flappy_high_score) flappy_high_score = flappy_score;
            }
            if (!(bx2 < px1 || bx1 > px2 || by2 < gap_bottom || by1 > 192)) {
                is_game_over = true;
                death_time = millis();
                if (flappy_score > flappy_high_score) flappy_high_score = flappy_score;
            }
        }
    } else if (flappy_ready) {
        // Floating hover on Ready screen
        flappy_y = 96.0f + 6.0f * sin(millis() / 150.0f);
        flappy_vy = 0;
        
        flappy_bg_x -= 0.08f;
        if (flappy_bg_x < -267.0f) flappy_bg_x += 267.0f;

        flappy_trees_x -= 0.3f;
        if (flappy_trees_x < -224.0f) flappy_trees_x += 224.0f;

        flappy_ground_x -= 1.2f;
        if (flappy_ground_x < -12.0f) flappy_ground_x += 12.0f;

        jimny_x -= 1.2f;
        if (jimny_x < -30) {
            jimny_x = 240.0f + random(600, 1800);
        }

        for (int i = 0; i < 3; i++) {
            cloud_x[i] -= 0.15f;
            if (cloud_x[i] < -40) cloud_x[i] = 240 + random(0, 100);
        }
    }

    // 2. Render Environment
    // Clear to official Flappy Bird Sky Blue (#4ec0ca -> 0x4DF9)
    RetroEngine::clear(0x4DF9);

    // Distant background Skyline
    draw_skyline_buildings(flappy_bg_x);

    // Midground green bushes
    draw_midground_bushes(flappy_trees_x);

    // Solid white clouds
    for(int i=0; i<3; i++) {
        RetroEngine::drawSprite(cloud_x[i], 45 + (i*12), 38, 11, cloud_bits, 0xFFFF, true);
    }

    // Pipes (only rendered when game running or dead)
    if (!flappy_ready) {
        for (int i = 0; i < 2; i++) {
            draw_flappy_pipe(pipes_x[i], pipes_gap_y[i], pipes_gap_h);
        }
    }

    // Fast Parallax Ground
    draw_parallax_ground(flappy_ground_x);

    // Draw Jimny easter egg driving on the sand road!
    if (jimny_x > -20 && jimny_x < 233) {
        draw_jimny_easter_egg((int)jimny_x, 204);
    }

    // Bird
    draw_flappy_bird(45, flappy_y);

    // 3. Draw HUD & Panels
    if (is_game_over) {
        draw_flappy_gameover();
        
        // Draw score panel values
        char hi_str[32];
        sprintf(hi_str, "BEST %03d", flappy_high_score);
        draw_outlined_text(119 - 22, 50, hi_str, 1, 0xFFFF);
        
        draw_flappy_score(119, 24, flappy_score);
    } else if (flappy_ready) {
        int cx = 119;
        draw_outlined_text(cx - 36, 75, "GET READY", 1, 0xFFFF);
        draw_outlined_text(cx - 40, 115, "TAP TO FLAP", 1, 0xFFFF);
    } else {
        // Center live score
        draw_flappy_score(119, 20, flappy_score);
    }

    RetroEngine::flush();
}

void update_screen_game() {
    if (activeGame == GAME_DINO) {
        if (millis() - game_timer > 16) { run_raw_dino_frame(); game_timer = millis(); yield(); }
    } else if (activeGame == GAME_FLAPPY) {
        if (millis() - game_timer > 16) { run_raw_flappy_frame(); game_timer = millis(); yield(); }
    }
}

void reset_obstacles() {
    enemy_x = 240; enemy_count = 1; obstacle_type = 0; distance_ran = 0; game_score = 0; is_game_over = false; is_jumping = false; jump_vel = -8.1; dino_y = 157; 
    dino_ready = true;
}

void stop_all_games() {
    is_jumping = false;
    is_game_over = false;
    dino_ready = true;
    flappy_ready = true;
    flappy_y = 96.0f;
    flappy_vy = 0.0f;
    reset_obstacles();
    reset_flappy_game();
}
