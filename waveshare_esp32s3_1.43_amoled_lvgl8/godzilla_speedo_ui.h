#pragma once
// Shared Godzilla speedometer visuals — RPM-to-arc mapping, the gauge/tick
// layout, color-zone logic, and the settings menu. Included by BOTH the
// firmware (ui_godzillaspeedometer.cpp) and the simulator (sim/), so there is
// exactly one copy of this code.
//
// NOT shared (stay firmware-only in ui_godzillaspeedometer.cpp): the screen
// init/destroy (PSRAM canvas + GIF resource lifecycle) and the screen event
// handler (GIF playback state machine, touch gestures). Those are deeply
// tied to AnimatedGIF/PSRAM/SD, which the sim doesn't run — the sim builds
// its own screen using the shared functions below plus a static placeholder
// image in place of the animated car GIF.
#include "ui.h"
#include "ui_godzillaspeedometer.h"
#include <math.h>

extern volatile int car_rpm;
extern volatile int car_speed;

static lv_point_t godzilla_ticks[41][2];

inline uint16_t map_rpm_to_arc_value(uint16_t rpm) {
    if (rpm < 1000) {
        // Map 0-1000 RPM to 0-1583 arc value (represents 150 to 185.625 degrees)
        return (uint16_t)((float)rpm * 1.5833f);
    } else {
        // Map 1000-8000 RPM to 1583-8000 arc value
        return (uint16_t)(1583.3f + ((float)(rpm - 1000) * 6416.7f / 7000.0f));
    }
}

inline void ui_godzilla_speed_update(int speed, lv_color_t color) {
    if (ui_godzilla_speed_label) {
        lv_label_set_text_fmt(ui_godzilla_speed_label, "%d", speed);
        lv_obj_set_style_text_color(ui_godzilla_speed_label, color, LV_PART_MAIN | LV_STATE_DEFAULT);
        for (int i = 0; i < 8; i++) {
            if (ui_godzilla_speed_label_shadows[i]) {
                lv_label_set_text_fmt(ui_godzilla_speed_label_shadows[i], "%d", speed);
            }
        }
    }
    if (ui_godzilla_unit_label) {
        lv_obj_set_style_text_color(ui_godzilla_unit_label, color, LV_PART_MAIN | LV_STATE_DEFAULT);
        for (int i = 0; i < 8; i++) {
            if (ui_godzilla_unit_label_shadows[i]) {
                lv_obj_set_style_text_color(ui_godzilla_unit_label_shadows[i], lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
    }
}

inline void build_godzilla_rpm_gauge(lv_obj_t * parent) {
    const int CX = 233, CY = 233;
    const float R_OUTER = 232;
    const float RAD = 3.14159265f / 180.0f;

    for (int i = 0; i <= 40; i++) {
        // Clockwise sweep: angle increases
        // Maintain position of 1 onwards the same, only 0 moves to 8 o'clock (150 degrees)
        // 0 to 1 will be longer (non-linear).
        float angle;
        if (i < 5) {
            angle = 150.0f + (float)i * 7.125f; // Interpolate 150.0f (0) to 185.625f (1)
        } else {
            angle = 165.0f + (float)i * 4.125f; // Follow original for 1 onwards
        }
        if (angle >= 360.0f) angle -= 360.0f;
        float a_rad = angle * RAD;
        float c = cosf(a_rad), s = sinf(a_rad);

        int is_major = (i % 5 == 0);
        int tick_len = is_major ? 22 : 12;
        int w = is_major ? 5 : 3;

        int current_num = i / 5;
        uint32_t element_color = (current_num >= 6) ? 0xFF0000 : 0xFFFFFF;

        godzilla_ticks[i][0].x = (lv_coord_t)(CX + (int)((R_OUTER - tick_len) * c));
        godzilla_ticks[i][0].y = (lv_coord_t)(CY + (int)((R_OUTER - tick_len) * s));
        godzilla_ticks[i][1].x = (lv_coord_t)(CX + (int)(R_OUTER * c));
        godzilla_ticks[i][1].y = (lv_coord_t)(CY + (int)(R_OUTER * s));

        lv_obj_t * ln = lv_line_create(parent);
        lv_line_set_points(ln, godzilla_ticks[i], 2);
        lv_obj_set_style_line_width(ln, w, 0);
        lv_obj_set_style_line_color(ln, lv_color_hex(element_color), 0);
        lv_obj_set_style_line_opa(ln, is_major ? 220 : 130, 0);
        lv_obj_clear_flag(ln, LV_OBJ_FLAG_CLICKABLE);

        godzilla_tick_lines[i] = ln; // Store line pointer

        if (is_major && current_num > 0) {
            lv_obj_t * lbl = lv_label_create(parent);
            lv_label_set_text_fmt(lbl, "%d", current_num);
            lv_obj_set_style_text_font(lbl, &ui_font_rajdhani1, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(element_color), 0);
            lv_obj_set_style_text_opa(lbl, 255, 0);

            float lbl_r = R_OUTER - tick_len - 15;
            lv_obj_align(lbl, LV_ALIGN_CENTER, (int)(lbl_r * c), (int)(lbl_r * s));

            godzilla_tick_labels[current_num] = lbl; // Store label pointer

            if (current_num == 8) {
                ui_godzilla_x1000_label = lv_label_create(parent);
                lv_label_set_text(ui_godzilla_x1000_label, "X1000");
                lv_obj_set_style_text_font(ui_godzilla_x1000_label, &lv_font_montserrat_12, 0);
                lv_obj_set_style_text_color(ui_godzilla_x1000_label, lv_color_hex(0xFF0000), 0);
                // Dynamically align beautiful X1000 label to the left of the 8 label
                lv_obj_align_to(ui_godzilla_x1000_label, lbl, LV_ALIGN_OUT_LEFT_MID, -8, 2);
            }
        }
    }
}

inline void update_godzilla_ticks_color(bool all_red) {
    for (int i = 0; i <= 40; i++) {
        if (godzilla_tick_lines[i]) {
            int current_num = i / 5;
            uint32_t color = (all_red || current_num >= 6) ? 0xFF0000 : 0xFFFFFF;
            lv_obj_set_style_line_color(godzilla_tick_lines[i], lv_color_hex(color), 0);
        }
    }
    for (int i = 0; i <= 8; i++) {
        if (godzilla_tick_labels[i]) {
            uint32_t color = (all_red || i >= 6) ? 0xFF0000 : 0xFFFFFF;
            lv_obj_set_style_text_color(godzilla_tick_labels[i], lv_color_hex(color), 0);
        }
    }
    if (ui_godzilla_x1000_label) {
        lv_obj_set_style_text_color(ui_godzilla_x1000_label, lv_color_hex(0xFF0000), 0);
    }
}

inline void godzilla_anim_cb(void * var, int32_t v) {
    (void)var;
    if (ui_godzilla_rpm_arc) {
        lv_arc_set_value(ui_godzilla_rpm_arc, map_rpm_to_arc_value(v));

        lv_color_t c;
        bool all_red = false;
        if (v < 1000) {
            c = lv_color_hex(0xB0B8C0); // light grey
        } else if (v < 3000) {
            float ratio = (v - 1000) / 2000.0f;
            uint8_t r = 255;
            uint8_t g = 255 - (uint8_t)(ratio * 127);
            uint8_t b = 0;
            c = lv_color_make(r, g, b);
        } else if (v < 6000) {
            c = lv_color_hex(0x00E5FF); // bluish
        } else {
            c = lv_color_hex(0xFF0000); // red
            all_red = true;
        }

        lv_obj_set_style_arc_color(ui_godzilla_rpm_arc, c, LV_PART_INDICATOR | LV_STATE_DEFAULT);

        if (ui_godzilla_speed_label) {
            int32_t speed = (v * 140) / 8000;
            // Use white instead of grey for 0-1000 RPM speed values
            lv_color_t value_color = (v < 1000) ? lv_color_hex(0xFFFFFF) : c;
            ui_godzilla_speed_update((int)speed, value_color);
        }

        // Sweep color updates also apply to graduation lines and labels!
        update_godzilla_ticks_color(all_red);
    }
}

inline void godzilla_anim_ready_cb(lv_anim_t * a) {
    (void)a;
    is_godzilla_animating = false;
}

inline void open_speedo_settings_menu(lv_obj_t * parent_screen, bool is_godzilla) {
    static uint32_t menu_opened_time = 0;
    menu_opened_time = lv_tick_get();
    lv_indev_wait_release(lv_indev_get_act()); // Wait for finger release to prevent instant triggers

    lv_obj_t * overlay = lv_obj_create(parent_screen);
    lv_obj_set_size(overlay, 466, 466);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, 210, 0); // Translucent glassmorphic background
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(overlay);

    // Close button
    lv_obj_t * btn_close = lv_btn_create(overlay);
    lv_obj_set_size(btn_close, 75, 75);
    lv_obj_set_style_radius(btn_close, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0x444444), 0);
    lv_obj_align(btn_close, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_ext_click_area(btn_close, 30); // larger invisible tap target beyond the visible circle
    lv_obj_t * lbl_x = lv_label_create(btn_close);
    lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(lbl_x, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl_x);

    lv_obj_add_event_cb(btn_close, [](lv_event_t * ev) {
        lv_event_code_t c = lv_event_get_code(ev);
        if (c != LV_EVENT_CLICKED && c != LV_EVENT_RELEASED) return;
        if (lv_tick_elaps(menu_opened_time) < 200) return;
        lv_obj_t * ov = (lv_obj_t *)lv_event_get_user_data(ev);
        lv_obj_del(ov);
    }, LV_EVENT_ALL, overlay);

    // Container for flex layout — Settings-style dark cards instead of the
    // old solid blue/green/red buttons, so this matches build_settings_screen().
    lv_obj_t * cont = lv_obj_create(overlay);
    lv_obj_set_size(cont, 420, 320);
    lv_obj_set_style_bg_opa(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0); // default theme padding was shrinking
                                          // the usable width below the 400px
                                          // rows, triggering a horizontal scrollbar
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE); // this container never needs to scroll
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, 14, 0);
    lv_obj_center(cont);

    // cont's bounding box overlaps btn_close's lower edge (cont spans down
    // from y~73, btn_close spans to y~100) — without this, cont (created
    // after btn_close) sits on top in z-order and silently eats clicks in
    // that overlap band, which is exactly what made the close button feel
    // unresponsive near its bottom half.
    lv_obj_move_foreground(btn_close);

    // Row 1: "Default speedometer" — tap to make this screen the one that
    // boots by default; shows a checkmark instead once it already is.
    lv_obj_t * row_default = lv_obj_create(cont);
    lv_obj_set_size(row_default, 400, 80);
    lv_obj_set_style_bg_color(row_default, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row_default, 255, 0);
    lv_obj_set_style_border_width(row_default, 1, 0);
    lv_obj_set_style_border_color(row_default, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row_default, 12, 0);
    lv_obj_clear_flag(row_default, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl_default_title = lv_label_create(row_default);
    lv_label_set_text(lbl_default_title, "Default speedometer");
    lv_obj_set_style_text_font(lbl_default_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_default_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_default_title, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t * lbl_default = lv_label_create(row_default);
    lv_obj_set_style_text_font(lbl_default, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_default, LV_ALIGN_RIGHT_MID, -16, 0);

    bool is_currently_default = (is_godzilla && default_speedometer == 1) || (!is_godzilla && default_speedometer == 0);
    if (is_currently_default) {
        lv_label_set_text(lbl_default, LV_SYMBOL_OK " Default");
        lv_obj_set_style_text_color(lbl_default, lv_color_hex(0xFFB020), 0);
    } else {
        lv_label_set_text(lbl_default, "Set as default");
        lv_obj_set_style_text_color(lbl_default, lv_color_hex(0x888888), 0);
        lv_obj_add_flag(row_default, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_add_event_cb(row_default, [](lv_event_t * ev) {
            if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
            if (lv_tick_elaps(menu_opened_time) < 200) return;
            bool is_godz_btn = (bool)(uintptr_t)lv_event_get_user_data(ev);
            save_speedo_preferences(is_godz_btn ? 1 : 0);

            // Close menu — the new default takes effect next time this
            // screen (or the other speedometer) loads.
            lv_obj_t * row = lv_event_get_target(ev);
            lv_obj_t * cont = lv_obj_get_parent(row);
            lv_obj_t * overlay = lv_obj_get_parent(cont);
            lv_obj_del(overlay);
        }, LV_EVENT_ALL, (void*)(uintptr_t)is_godzilla);
    }

    // Row 2: "Simulate OBD data" — a real switch, not a button whose own
    // label doubled as a status readout (the old "Simulate OBD ON/OFF"
    // button text read like a status display, not an action, and looked
    // inverted: red coloring on "OFF" suggested danger/stop rather than
    // "currently inactive"). A switch makes current state unambiguous.
    lv_obj_t * row_sim = lv_obj_create(cont);
    lv_obj_set_size(row_sim, 400, 80);
    lv_obj_set_style_bg_color(row_sim, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(row_sim, 255, 0);
    lv_obj_set_style_border_width(row_sim, 1, 0);
    lv_obj_set_style_border_color(row_sim, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(row_sim, 12, 0);
    lv_obj_clear_flag(row_sim, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl_sim_title = lv_label_create(row_sim);
    lv_label_set_text(lbl_sim_title, "Simulate OBD data");
    lv_obj_set_style_text_font(lbl_sim_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_sim_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_sim_title, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t * sw_sim = lv_switch_create(row_sim);
    lv_obj_align(sw_sim, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_size(sw_sim, 60, 30);
    lv_obj_set_style_bg_color(sw_sim, lv_color_hex(0xFF6A00), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (is_simulating_obd) lv_obj_add_state(sw_sim, LV_STATE_CHECKED);

    lv_obj_add_event_cb(sw_sim, [](lv_event_t * ev) {
        if (lv_event_get_code(ev) != LV_EVENT_VALUE_CHANGED) return;
        lv_obj_t * sw = lv_event_get_target(ev);
        is_simulating_obd = lv_obj_has_state(sw, LV_STATE_CHECKED);
        if (!is_simulating_obd) {
            // Immediately reset simulation values to zero
            car_rpm = 0;
            car_speed = 0;
        }
    }, LV_EVENT_VALUE_CHANGED, NULL);
}
