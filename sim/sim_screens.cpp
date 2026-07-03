// Sim entry point + screen switcher. The OTA overlay and the RPM gauge
// builder are NOT duplicated here — they're #included straight from the
// firmware's shared headers (ota_overlay_ui.h, screen_ui.h), so there is
// exactly one copy of that UI code, compiled by both the device and the sim.
#define LV_SIM_BUILD   // tells OTAManager.h / screen_ui.h to skip Arduino.h
#include "lvgl.h"
#include "ui.h"                // SquareLine screens (ui_init, ui_uilauncher, ...)
#include "sim_ota_stub.h"
#include "ota_overlay_ui.h"    // shared: show_ota_update_overlay(), ota_overlay_close()
#include "screen_ui.h"         // shared: build_speedo_rpm_gauge(), get_rpm_gauge_color()
#include "godzilla_speedo_ui.h" // shared: build_godzilla_rpm_gauge(), godzilla_anim_cb(), open_speedo_settings_menu()
#include "godzilla_placeholder.h" // static frame standing in for the animated car GIF
#include "grid_launcher_ui.h"   // shared: build_grid_launcher() — the swipe-down app grid
#include <emscripten.h>

// OBD globals screen_ui.h expects (volatile, to match its extern declarations).
volatile int   car_engine_temp = 80;
volatile int   car_engine_load = 20;
volatile float car_voltage     = 13.8f;
volatile int   car_rpm         = 800;
volatile int   car_speed       = 0;

// Minimal updater driving the same widgets the firmware's update_screen_ui()
// does for the standard speedometer (that function itself is firmware-only —
// it also drives the godzilla/GIF speedometer, which the sim doesn't render).
static void sim_update_speedo(lv_timer_t * t) {
    (void)t;
    if (lv_scr_act() != ui_uispeedometer) return;
    lv_color_t col = get_rpm_gauge_color(car_rpm);
    if (ui_rpmlabel)  lv_label_set_text_fmt(ui_rpmlabel, "%d", car_rpm);
    if (ui_rpmarc)  { lv_arc_set_value(ui_rpmarc, car_rpm);
                      lv_obj_set_style_arc_color(ui_rpmarc, col, LV_PART_INDICATOR); }
    if (ui_speedlabel) { lv_label_set_text_fmt(ui_speedlabel, "%d", car_speed);
                         lv_obj_set_style_text_color(ui_speedlabel, col, LV_PART_MAIN); }
}

// ─── Godzilla speedometer: sim-only screen scaffold around the SHARED gauge ──
// Object creation/layout here is sim-specific (no PSRAM, no GIF) — but the
// gauge ticks, color zones, speed-label updates, and settings menu all come
// from godzilla_speedo_ui.h, so that visual logic is identical to firmware.
static void build_godzilla_screen_for_sim() {
    if (ui_godzillaspeedometer) return; // already built

    ui_godzillaspeedometer = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_godzillaspeedometer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_godzillaspeedometer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_godzillaspeedometer, 255, 0);

    ui_godzilla_rpm_arc = lv_arc_create(ui_godzillaspeedometer);
    lv_obj_set_size(ui_godzilla_rpm_arc, 466, 466);
    lv_obj_align(ui_godzilla_rpm_arc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_range(ui_godzilla_rpm_arc, 0, 8000);
    lv_arc_set_value(ui_godzilla_rpm_arc, 0);
    lv_arc_set_bg_angles(ui_godzilla_rpm_arc, 150, 330);
    lv_arc_set_mode(ui_godzilla_rpm_arc, LV_ARC_MODE_NORMAL);
    lv_obj_set_style_arc_color(ui_godzilla_rpm_arc, lv_color_hex(0x00E5FF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_godzilla_rpm_arc, 12, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(ui_godzilla_rpm_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_style(ui_godzilla_rpm_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ui_godzilla_rpm_arc, LV_OBJ_FLAG_CLICKABLE);

    // Static placeholder where the animated car GIF plays on the device.
    static lv_img_dsc_t placeholder_dsc = {
        .header = { .cf = LV_IMG_CF_TRUE_COLOR, .w = GODZILLA_PLACEHOLDER_W, .h = GODZILLA_PLACEHOLDER_H },
        .data_size = GODZILLA_PLACEHOLDER_W * GODZILLA_PLACEHOLDER_H * 2,
        .data = (const uint8_t *)godzilla_placeholder_rgb565,
    };
    ui_godzilla_canvas = lv_img_create(ui_godzillaspeedometer);
    lv_img_set_src(ui_godzilla_canvas, &placeholder_dsc);
    lv_obj_align(ui_godzilla_canvas, LV_ALIGN_BOTTOM_LEFT, 61, 42);
    lv_img_set_zoom(ui_godzilla_canvas, 384);
    lv_obj_move_background(ui_godzilla_canvas);

    ui_godzilla_speed_label = lv_label_create(ui_godzillaspeedometer);
    lv_obj_set_width(ui_godzilla_speed_label, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_godzilla_speed_label, LV_SIZE_CONTENT);
    lv_obj_align(ui_godzilla_speed_label, LV_ALIGN_RIGHT_MID, -20, 20);
    lv_label_set_text(ui_godzilla_speed_label, "0");
    lv_obj_set_style_text_color(ui_godzilla_speed_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_godzilla_speed_label, &ui_font_rajdhani200, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_move_foreground(ui_godzilla_speed_label);

    lv_obj_t * unit = lv_label_create(ui_godzillaspeedometer);
    lv_obj_set_width(unit, LV_SIZE_CONTENT);
    lv_obj_set_height(unit, LV_SIZE_CONTENT);
    lv_obj_align(unit, LV_ALIGN_RIGHT_MID, -30, 110);
    lv_label_set_text(unit, "km/h");
    lv_obj_set_style_text_color(unit, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(unit, &ui_font_rajdhani1, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_godzilla_unit_label = unit;
    lv_obj_move_foreground(unit);

    build_godzilla_rpm_gauge(ui_godzillaspeedometer);

    lv_obj_add_flag(ui_godzillaspeedometer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui_godzillaspeedometer, [](lv_event_t * e) {
        if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED)
            open_speedo_settings_menu(ui_godzillaspeedometer, true);
    }, LV_EVENT_LONG_PRESSED, NULL);
}

extern "C" {

EMSCRIPTEN_KEEPALIVE void sim_set_rpm(int rpm) {
    car_rpm = rpm;
    car_speed = rpm * 140 / 8000;   // derive a plausible speed for the demo
    if (ui_godzillaspeedometer && lv_scr_act() == ui_godzillaspeedometer)
        godzilla_anim_cb(NULL, rpm);  // live-drive the shared color-zone/gauge logic
}

// Called from the HTML control panel (Module.ccall) to jump between screens.
EMSCRIPTEN_KEEPALIVE void sim_show_screen(int idx) {
    ota_overlay_close();   // clear any open overlay before switching
    switch (idx) {
        case 0: lv_disp_load_scr(ui_uilauncher);
                if (ui_Panel1) lv_obj_add_flag(ui_Panel1, LV_OBJ_FLAG_HIDDEN); // grid mode hides the list view (mirrors firmware setup())
                build_grid_launcher();
                break;
        case 1: lv_disp_load_scr(ui_uispeedometer);
                { static bool built = false; if (!built) { build_speedo_rpm_gauge(ui_uispeedometer); built = true; } }
                break;
        case 2: lv_disp_load_scr(ui_uigauge);        break;
        case 3: lv_disp_load_scr(ui_uiinclinometer); break;
        case 4: lv_disp_load_scr(ui_uilauncher); show_ota_update_overlay(); break;
        case 5: build_godzilla_screen_for_sim(); lv_disp_load_scr(ui_godzillaspeedometer); break;
        default: break;
    }
}

void sim_build_ui(void) {
    ui_init();              // build all SquareLine screens
    lv_timer_create(sim_update_speedo, 100, NULL);  // live RPM/speed updates
    sim_show_screen(0);     // start on the launcher
}

} // extern "C"
