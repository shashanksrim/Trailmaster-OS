#pragma once
#ifndef LV_SIM_BUILD
#include <Arduino.h>
#endif
#include <math.h>
#include <lvgl.h>
#include "ui.h"
#include "ui_godzillaspeedometer.h"
#include "godzilla_speedo_ui.h"   // map_rpm_to_arc_value() etc. — shared inline functions

// Forward declarations for OBD variables mapped to UI
extern volatile int car_engine_temp;
extern volatile int car_engine_load; 
extern volatile float car_voltage;
extern volatile int car_rpm;
extern volatile int car_speed;

// Forward declarations for UI elements (from SquareLine)
extern "C" {
    extern lv_obj_t * ui_enginetemplabel;
    extern lv_obj_t * ui_oiltemplabel; 
    extern lv_obj_t * ui_batterylabel;
    extern lv_obj_t * ui_rpmlabel;
    extern lv_obj_t * ui_speedlabel;

    extern lv_obj_t * ui_rpmarc;
    extern lv_obj_t * ui_Engine_temp;
    extern lv_obj_t * ui_oiltemp;   

    // Godzilla Speedometer UI components
    extern lv_obj_t * ui_godzillaspeedometer;
    extern lv_obj_t * ui_godzilla_speed_label;
    extern lv_obj_t * ui_godzilla_rpm_arc;
}

// Forward declaration for dynamic color mapping
extern lv_color_t get_dynamic_color(float percent);

// RPM zones: amber (<=3000), hot (<=6000), redline (>6000) — same thresholds
// as the redline sector/tick boundary below and ui_uispeedometer.c's boot
// animation, so the dial reads consistently whether it's animating on
// screen load or tracking real OBD data.
static inline lv_color_t get_rpm_gauge_color(int rpm) {
    if (rpm <= 3000) {
        return lv_color_hex(0xFFB020); // amber
    } else if (rpm <= 6000) {
        return lv_color_hex(0xFFC54D); // hot
    } else {
        return lv_color_hex(0xFF3B1D); // redline
    }
}

// Explicit linkage to clean industrial numeric font provided by SquareLine assets
LV_FONT_DECLARE(ui_font_rajdhani1);

// Builds radial tick labels and markers onto the background screen
static inline void build_speedo_rpm_gauge(lv_obj_t * parent) {
    if (!ui_rpmarc) return;
    
    // 1. Modify existing arc visuals to match requested aesthetic
    lv_obj_clear_flag(ui_rpmarc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(ui_rpmarc, 466, 466); // Expand perfectly to the display glass edge
    lv_obj_align(ui_rpmarc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_range(ui_rpmarc, 0, 8000);
    lv_arc_set_bg_angles(ui_rpmarc, 135, 45); 
    
    lv_obj_set_style_arc_width(ui_rpmarc, 12, LV_PART_INDICATOR); // Thicker alignment with beefy ticks
    lv_obj_set_style_arc_width(ui_rpmarc, 12, LV_PART_MAIN);      // Match track width
    lv_obj_set_style_arc_color(ui_rpmarc, lv_color_hex(0x333333), LV_PART_MAIN); // Dark grey track
    lv_obj_set_style_arc_opa(ui_rpmarc, 150, LV_PART_MAIN);       // Visible background track
    
    // ── REDLINE CAUTION SECTOR ────────────────────────────────────────────────────
    // Static distinct red highlight tracking strictly along 6000-8000 interval
    lv_obj_t * redline = lv_arc_create(parent);
    lv_obj_set_size(redline, 420, 420); // Radius 210px, perfectly matching R_OUTER of ticks
    lv_obj_align(redline, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(redline, 338, 45); // 6000 to 8000 RPM radial sector
    lv_arc_set_value(redline, 0); // Static track mode only
    lv_obj_set_style_arc_color(redline, lv_color_hex(0xFF3B1D), LV_PART_MAIN);
    lv_obj_set_style_arc_width(redline, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(redline, 200, LV_PART_MAIN); // Slight translucency
    lv_obj_remove_style(redline, NULL, LV_PART_KNOB);
    lv_obj_remove_style(redline, NULL, LV_PART_INDICATOR);
    lv_obj_clear_flag(redline, LV_OBJ_FLAG_CLICKABLE);
    
    // 2. Build scale grid and number cluster system
    const int CX = 233, CY = 233;
    const float R_OUTER = 210;  // Offset inwards by 22px (arc width 12 + 10px gap)
    const float R_INNER = 160;  // Deeper inset to amplify typography space (moved in by 10)
    const float RAD = 3.14159265f / 180.0f;

    // Persistent point matrix to hold line geometry for 41 ticks (0 to 40)
    static lv_point_t scale_ticks[41][2]; 
    
    // Loop generating ticks every 200 RPM (Total 8000/200 = 40 intervals)
    for (int i = 0; i <= 40; i++) {
        float angle = 135.0f + (float)i * (270.0f / 40.0f);
        float a_rad = angle * RAD;
        float c = cosf(a_rad), s = sinf(a_rad);
        
        bool is_major = (i % 5 == 0); // Trigger every 1000 RPM (5 * 200)
        int tick_len = is_major ? 22 : 12; // Stretched visually to fill outer gap nicely
        int w = is_major ? 6 : 4; // Beefy thickness bump as requested
        
        // Calculate Line Start/End pairs
        scale_ticks[i][0] = {(lv_coord_t)(CX + (int)((R_OUTER-tick_len) * c)), (lv_coord_t)(CY + (int)((R_OUTER-tick_len) * s))};
        scale_ticks[i][1] = {(lv_coord_t)(CX + (int)(R_OUTER * c)), (lv_coord_t)(CY + (int)(R_OUTER * s))};
        
        // Render scale line
        lv_color_t line_color = (i >= 30) ? lv_color_hex(0xFF3B1D) : lv_color_hex(0xFFB020);
        lv_obj_t * ln = lv_line_create(parent);
        lv_line_set_points(ln, scale_ticks[i], 2);
        lv_obj_set_style_line_width(ln, w, 0);
        lv_obj_set_style_line_color(ln, line_color, 0); 
        lv_obj_set_style_line_opa(ln, is_major ? 255 : 180, 0);     // Subtly lower opacity on fine ticks
        
        // Handle Numeric Clusters only on 1000-RPM boundaries
        if (is_major) {
            int label_val = i / 5;
            // Redline labels (6/7/8) match the redline tick color; the rest
            // match the amber ticks.
            lv_color_t label_color = (label_val >= 6) ? lv_color_hex(0xFF3B1D) : lv_color_hex(0xFFB020);
            lv_obj_t * lbl = lv_label_create(parent);
            lv_label_set_text_fmt(lbl, "%d", label_val);
            lv_obj_set_style_text_font(lbl, &ui_font_rajdhani1, 0); // Use crisp industrial font
            lv_obj_set_style_text_color(lbl, label_color, 0);
            
            // Dynamically compute true centered offset to anchor visually
            int lx = CX + (int)(R_INNER * c) - 10;
            int ly = CY + (int)(R_INNER * s) - 14;
            lv_obj_set_pos(lbl, lx, ly);
        }
    }
    
    // Ensure the arc and important labels sit completely on top of all the generated tick lines
    lv_obj_move_foreground(ui_rpmarc);
    if (ui_speedlabel) lv_obj_move_foreground(ui_speedlabel);
    if (ui_rpmlabel) lv_obj_move_foreground(ui_rpmlabel);
    if (ui_visspeed) lv_obj_move_foreground(ui_visspeed);
    if (ui_visrpm) lv_obj_move_foreground(ui_visrpm);
}

// Live OBD->widget binding: uses millis()/Serial, so it's firmware-only. The
// sim drives the same widgets via its own minimal updater (sim_screens.cpp).
#ifndef LV_SIM_BUILD
inline void update_screen_ui() {
    static uint32_t ui_refresh_timer = 0;
    
    // ANTI-LAG CACHING: Store the last drawn values
    static int last_temp = -1, last_load = -1, last_rpm = -1, last_speed = -1;
    static float last_volt = -1.0;

    if (millis() - ui_refresh_timer > 100) { 
        
        // --- OBD SIMULATION OVERRIDE ---
        if (is_simulating_obd) {
            uint32_t elapsed_ms = millis() % 10000;
            float ratio;
            if (elapsed_ms < 5000) {
                ratio = elapsed_ms / 5000.0f;
            } else {
                ratio = (10000 - elapsed_ms) / 5000.0f;
            }
            car_rpm = (int)(8000.0f * ratio);
            car_speed = (int)(140.0f * ratio);
            
            // Mock additional variables for total visual dashboard sync
            car_engine_temp = 80 + (int)(20.0f * ratio);
            car_engine_load = (int)(100.0f * ratio);
            car_voltage = 13.5f + 0.8f * ratio;
        }
        
        // Engine Temp (Label + Color Arc)
        if (car_engine_temp != last_temp) {
            last_temp = car_engine_temp;
            if (ui_enginetemplabel) lv_label_set_text_fmt(ui_enginetemplabel, "%d°C", car_engine_temp);
            if (ui_Engine_temp) {
                float temp_pct = (float)(car_engine_temp - 40) / 80.0f;
                lv_arc_set_value(ui_Engine_temp, car_engine_temp);
                lv_obj_set_style_arc_color(ui_Engine_temp, get_dynamic_color(temp_pct), LV_PART_INDICATOR);
            }
        }

        // Engine Load (Label + Color Arc)
        if (car_engine_load != last_load) {
            last_load = car_engine_load;
            if (ui_oiltemplabel) lv_label_set_text_fmt(ui_oiltemplabel, "%d%%", car_engine_load);
            if (ui_oiltemp) {
                float load_pct = (float)car_engine_load / 100.0f;
                lv_arc_set_value(ui_oiltemp, car_engine_load);
                lv_obj_set_style_arc_color(ui_oiltemp, get_dynamic_color(load_pct), LV_PART_INDICATOR);
            }
        }

        // RPM (Label + Color Arc)
        if (car_rpm != last_rpm) {
            last_rpm = car_rpm;
            lv_color_t rpm_color = get_rpm_gauge_color(car_rpm);
            if (ui_rpmlabel && !is_standard_animating) {
                lv_label_set_text_fmt(ui_rpmlabel, "%d", car_rpm);
                lv_obj_set_style_text_color(ui_rpmlabel, rpm_color, LV_PART_MAIN);
            }
            if (ui_rpmarc && !is_standard_animating) {
                lv_arc_set_value(ui_rpmarc, car_rpm);
                lv_obj_set_style_arc_color(ui_rpmarc, rpm_color, LV_PART_INDICATOR);
            }
            // Speed and rpm values both follow the same amber/hot/redline
            // zone as the arc. Only the "rpm"/"km/h" unit captions stay
            // fixed amber.
            if (ui_speedlabel && !is_standard_animating) {
                lv_obj_set_style_text_color(ui_speedlabel, rpm_color, LV_PART_MAIN);
            }
        }

        // --- SPEEDOMETER WITH COLOR STATES ---
        if (car_speed != last_speed) {
            last_speed = car_speed;
            if (ui_speedlabel && !is_standard_animating) {
                lv_label_set_text_fmt(ui_speedlabel, "%d", car_speed);
            }
        }

        // Battery Voltage
        if (abs(car_voltage - last_volt) > 0.05f) { 
            last_volt = car_voltage;
            if (ui_batterylabel) {
                int v_whole = (int)car_voltage;
                int v_dec = (int)(car_voltage * 10) % 10;
                lv_label_set_text_fmt(ui_batterylabel, "%d.%dV", v_whole, v_dec);
            }
        }

        // --- GODZILLA SPEEDOMETER UPDATES (Dynamic GIF-Based) ---
        if (ui_godzillaspeedometer && lv_scr_act() == ui_godzillaspeedometer) {
            if (ui_godzilla_rpm_arc && !is_godzilla_animating) {
                lv_arc_set_value(ui_godzilla_rpm_arc, map_rpm_to_arc_value(car_rpm));
                
                // Dynamic colors based on RPM zone
                lv_color_t c;
                bool all_red = false;
                if (car_rpm < 1000) {
                    c = lv_color_hex(0x6E7384); // #6e7384 greyish
                } else if (car_rpm < 3000) {
                    float ratio = (car_rpm - 1000) / 2000.0f;
                    uint8_t r = 255;
                    uint8_t g = 255 - (uint8_t)(ratio * 127);
                    uint8_t b = 0;
                    c = lv_color_make(r, g, b);
                } else if (car_rpm < 6000) {
                    c = lv_color_hex(0x00E5FF); // bluish
                } else {
                    c = lv_color_hex(0xFF0000); // red
                    all_red = true;
                }
                
                lv_obj_set_style_arc_color(ui_godzilla_rpm_arc, c, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                if (ui_godzilla_speed_label && !is_godzilla_animating) {
                    // Use white instead of grey for 0-1000 RPM speed values
                    lv_color_t value_color = (car_rpm < 1000) ? lv_color_hex(0xFFFFFF) : c;
                    ui_godzilla_speed_update(car_speed, value_color);
                }
                
                // Recolor ticks/graduations dynamically
                update_godzilla_ticks_color(all_red);
            }
            // Update GIF State based on RPM (with Serial debug)
            static gif_state_t last_logged_state = GIF_STATE_NONE;
            gif_state_t new_state;
            if (car_rpm < 1000) {
                new_state = GIF_STATE_IDLE;
            } else if (car_rpm < 3000) {
                new_state = GIF_STATE_INCREASING;
            } else if (car_rpm < 6000) {
                new_state = GIF_STATE_MAX;
            } else {
                new_state = GIF_STATE_REDLINE;
            }
            if (new_state != last_logged_state) {
                const char* state_name = (new_state == GIF_STATE_IDLE) ? "IDLE" :
                                         (new_state == GIF_STATE_INCREASING) ? "INCREASING" :
                                         (new_state == GIF_STATE_MAX) ? "MAX" : "REDLINE";
                Serial.printf("[GIF] RPM=%d -> state=%s\n", car_rpm, state_name);
                last_logged_state = new_state;
            }
            set_gif_state(new_state);
        }
        
        ui_refresh_timer = millis();
    }
}
#endif // !LV_SIM_BUILD
