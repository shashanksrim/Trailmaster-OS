#pragma once
// Shared grid launcher — the app-icon grid shown on the launcher screen
// (swipe down from any app to reveal it). Included by BOTH the firmware
// (.ino) and the simulator (sim/), so there is exactly one copy of this UI.
//
// Pure LVGL + screen-dispatch calls (_ui_screen_change, app_imageframe, etc,
// all already extern elsewhere) — no direct hardware calls.
#include <lvgl.h>
#include "ui.h"
#include "grid_icons.h"
#include "ui_godzillaspeedometer.h"  // ui_godzillaspeedometer, default_speedometer

extern lv_obj_t * grid_container;
extern bool ignore_until_lift;

extern "C" {
    void build_rom_menu();
    void build_settings_screen();
    void build_about_screen();
    void app_imageframe(lv_event_t * e);
}

inline void btn_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ignore_until_lift) return;
    int id = (int)(intptr_t)lv_event_get_user_data(e);

    switch (id) {
        case 0: // SPEED
            if (default_speedometer == 1) {
                _ui_screen_change(&ui_godzillaspeedometer, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_godzillaspeedometer_screen_init);
            } else {
                _ui_screen_change(&ui_uispeedometer,  LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_uispeedometer_screen_init);
            }
            break;
        case 1: // GAUGES
            _ui_screen_change(&ui_uigauge, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_uigauge_screen_init);
            break;
        case 2: // INCLINE
            _ui_screen_change(&ui_uiinclinometer, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, &ui_uiinclinometer_screen_init);
            break;
        case 3: // IMAGE FRAME
            app_imageframe(NULL);
            break;
        case 4: // GAMES
            build_rom_menu();
            break;
        case 5: // SYSTEM
            build_settings_screen();
            break;
        case 6: // ABOUT
            build_about_screen();
            break;
    }
}

inline void build_grid_launcher() {
    if (grid_container != NULL) {
        lv_obj_del(grid_container);
        grid_container = NULL;
    }

    grid_container = lv_obj_create(ui_uilauncher);
    lv_obj_set_size(grid_container, 390, 374);
    // Shifted grid container up by 20 more pixels (from 108 to 88)
    lv_obj_set_pos(grid_container, 38, 88);
    lv_obj_set_style_bg_opa(grid_container, 0, 0); // fully transparent wrapper
    lv_obj_set_style_border_width(grid_container, 0, 0);
    lv_obj_set_style_pad_left(grid_container, 0, 0);
    lv_obj_set_style_pad_right(grid_container, 0, 0);
    lv_obj_set_style_pad_top(grid_container, 40, 0); // Starts the scrollable content 40px below the container top
    lv_obj_set_style_pad_bottom(grid_container, 0, 0);
    lv_obj_set_style_pad_row(grid_container, 16, 0); // row gap
    lv_obj_set_style_pad_column(grid_container, 14, 0); // column gap

    // Enable vertical scrolling
    lv_obj_add_flag(grid_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(grid_container, LV_DIR_VER);

    // Align grid to the top-center (rows start vertically at top of container, items centered in row)
    lv_obj_set_flex_flow(grid_container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    // Keep scrolling functionality active but make the scrollbar fully invisible
    lv_obj_set_scrollbar_mode(grid_container, LV_SCROLLBAR_MODE_OFF);

    // 7 items following the exact original list sequence: Speed, Gauges, Incline, Image, Games, Settings, About
    const lv_img_dsc_t* imgs[] = {&icon_speedo, &icon_gauges, &icon_incline, &icon_image, &icon_games, &icon_settings, &icon_about};
    const char* labels[] = {"SPEEDO", "GAUGES", "INCLINE", "IMAGE", "GAMES", "SYSTEM", "ABOUT"};
    const lv_color_t colors[] = {
        lv_color_hex(0xFF0000), // Speedo (Red)
        lv_color_hex(0x4ade80), // Gauges (Green)
        lv_color_hex(0xFF0000), // Incline (Red)
        lv_color_hex(0xa78bfa), // Image (Purple)
        lv_color_hex(0xf87171), // Games (Red)
        lv_color_hex(0x5a7060), // System (Gray-green)
        lv_color_hex(0x38bdf8)  // About (Blue)
    };

    for (int i = 0; i < 7; i++) {
        lv_obj_t * btn = lv_btn_create(grid_container);
        lv_obj_set_size(btn, 112, 112); // Extra spacious 112x112 cells!

        // Brushed Steel Border Logic
        bool is_center = (i == 1 || i == 4 || i == 6);
        lv_color_t steel_color = is_center ? lv_color_hex(0xE0E0E0) : lv_color_hex(0x9A9A9A); // Brighter sides

        // Base styling (Normal State)
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A1A), 0); // dark charcoal grey background ~80% black
        lv_obj_set_style_bg_opa(btn, 255, 0); // fully opaque so no bleed-through
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, steel_color, 0);
        lv_obj_set_style_border_opa(btn, 200, 0); // strong metallic border
        lv_obj_set_style_radius(btn, 20, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);

        // Pressed State (Tactile visual feedback)
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x505050), LV_STATE_PRESSED); // Standardised grey shade
        lv_obj_set_style_bg_opa(btn, 255, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED); // flash white on press
        lv_obj_set_style_border_opa(btn, 255, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 3, LV_STATE_PRESSED); // Thicker border

        // Disable scroll on buttons so swipe gestures scroll the parent grid container smoothly
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * img = lv_img_create(btn);
        lv_img_set_src(img, imgs[i]);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 10); // larger 48px icon moved closer to top
        // Tint the icons with the steel theme
        lv_obj_set_style_img_recolor(img, steel_color, 0);
        lv_obj_set_style_img_recolor_opa(img, 255, 0);
        lv_obj_set_style_img_recolor(img, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
        lv_obj_set_style_img_recolor_opa(img, 255, LV_STATE_PRESSED);

        lv_obj_t * lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0); // 14px size for perfect readability
        lv_obj_set_style_text_color(lbl, steel_color, 0); // Text color matches steel box border
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED); // white highlight when clicked
        // dropped text closer to very bottom, expanding internal cell gap to a spacious 34 pixels
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -6);

        lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    // Force grid to render above the black header patch (ui_Container2)
    lv_obj_move_foreground(grid_container);
    // Explicitly lock scroll position at the very top (0px) initially
    lv_obj_scroll_to_y(grid_container, 0, LV_ANIM_OFF);
}
