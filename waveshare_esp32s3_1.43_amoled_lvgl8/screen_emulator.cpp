#include <Arduino.h>
#include <lvgl.h>
#include "ui.h"
#include "SmsEngine.h"
#include <dirent.h>

lv_obj_t * ui_emulator_screen = nullptr;
lv_obj_t * ui_game_list = nullptr;
bool emulator_active = false;

void start_sms_emulator() {
    if (!ui_emulator_screen) {
        ui_emulator_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(ui_emulator_screen, lv_color_hex(0x000000), 0);
        
        ui_game_list = lv_list_create(ui_emulator_screen);
        lv_obj_set_size(ui_game_list, 400, 350);
        lv_obj_center(ui_game_list);
        lv_obj_set_style_bg_color(ui_game_list, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_style_text_color(ui_game_list, lv_color_hex(0xFFFFFF), 0);
        
        lv_obj_t * lbl = lv_label_create(ui_emulator_screen);
        lv_label_set_text(lbl, "SELECT SMS GAME");
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_set_style_text_font(lbl, &ui_font_rajdhani1, 0);
    }
    
    // Scan SD card for .sms files
    lv_obj_clean(ui_game_list);
    const char* folders[] = {"/sd_card/sms", "/sd_card"};
    bool found = false;
    for (int i = 0; i < 2; i++) {
        Serial.print("Scanning folder: "); Serial.println(folders[i]);
        DIR *dir = opendir(folders[i]);
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != NULL) {
                String fname = ent->d_name;
                if (fname.endsWith(".sms")) {
                    found = true;
                    Serial.print("Found SMS game: "); Serial.println(fname);
                    lv_obj_t * btn = lv_list_add_btn(ui_game_list, LV_SYMBOL_FILE, ent->d_name);
                    char* full_path = (char*)malloc(128);
                    snprintf(full_path, 128, "%s/%s", folders[i], ent->d_name);
                    lv_obj_add_event_cb(btn, [](lv_event_t * e) {
                        char * path = (char*)lv_event_get_user_data(e);
                        if (SmsEngine::loadROM(path)) {
                            lv_obj_add_flag(ui_game_list, LV_OBJ_FLAG_HIDDEN);
                            emulator_active = true;
                        } else {
                            Serial.println("Failed to load ROM!");
                        }
                    }, LV_EVENT_CLICKED, full_path);
                }
            }
            closedir(dir);
        } else {
            Serial.print("Folder not found: "); Serial.println(folders[i]);
        }
    }
    if (!found) {
        lv_list_add_text(ui_game_list, "No .sms files found");
        Serial.println("No SMS files found on SD card.");
    }

    lv_scr_load_anim(ui_emulator_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

void update_sms_emulator() {
    if (emulator_active) {
        SmsEngine::update();
    }
}

// Map touch to Joypad
void sms_handle_touch(int start_y, int end_y, int start_x, int end_x, bool released) {
    if (!emulator_active) return;
    
    uint8_t input = 0;
    if (!released) {
        int dy = end_y - start_y;
        int dx = end_x - start_x;
        
        if (abs(dy) > abs(dx)) {
            if (dy > 50) input |= J_DOWN;
            else if (dy < -50) input |= J_UP;
        } else {
            if (dx > 50) input |= J_RIGHT;
            else if (dx < -50) input |= J_LEFT;
        }
        
        // Tap zones for buttons
        if (end_x > 350) input |= J_BUTTON1;
        if (end_x < 110) input |= J_BUTTON2;
    }
    
    SmsEngine::setInput(input);
}
