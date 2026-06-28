#pragma once
// Shared OTA update overlay — included by BOTH the firmware (.ino) and the
// browser simulator (sim/), so there is exactly one copy of this UI code.
// Editing this file changes what both the device and the sim render; there is
// no separate "sim version" to keep in sync.
//
// Depends only on LVGL + OTAManager's public API (ota_get_status/_check_/_install),
// no direct hardware calls, so it compiles unmodified for both targets.
#include <lvgl.h>
#include "OTAManager.h"

extern "C" {
    extern const lv_font_t ui_font_rajdhani1;
}

// Full-screen overlay that shows update status/progress (instead of a label
// under the button). Opening it kicks off the version check automatically.
static lv_obj_t * ota_overlay     = NULL;
static lv_obj_t * ota_ov_status   = NULL;
static lv_obj_t * ota_ov_bar      = NULL;
static lv_obj_t * ota_ov_btn      = NULL;
static lv_obj_t * ota_ov_btn_lbl  = NULL;
static lv_timer_t * ota_ov_timer  = NULL;

static void ota_overlay_close() {
    if (ota_overlay && lv_obj_is_valid(ota_overlay)) lv_obj_del(ota_overlay); // DELETE cb clears the rest
}

static void ota_overlay_refresh(lv_timer_t * t) {
    const OTAStatus* st = ota_get_status();
    // Single, consistent palette: neutral text (red only on failure), grey
    // progress bar, orange action button — no per-state colour switching.
    if (ota_ov_status && lv_obj_is_valid(ota_ov_status)) {
        lv_label_set_text(ota_ov_status, st->status_text);
        bool failed = (st->state == OTA_FAILED_NO_WIFI ||
                       st->state == OTA_FAILED_SERVER ||
                       st->state == OTA_FAILED_FLASH);
        lv_obj_set_style_text_color(ota_ov_status, lv_color_hex(failed ? 0xFF3333 : 0xCCCCCC), 0);
    }
    if (ota_ov_bar && lv_obj_is_valid(ota_ov_bar))
        lv_bar_set_value(ota_ov_bar, st->progress, LV_ANIM_ON);

    if (ota_ov_btn && lv_obj_is_valid(ota_ov_btn) && ota_ov_btn_lbl) {
        bool busy = (st->state == OTA_SCANNING_WIFI || st->state == OTA_CONNECTING_WIFI ||
                     st->state == OTA_CHECKING_VERSION || st->state == OTA_DOWNLOADING_FW ||
                     st->state == OTA_DOWNLOADING_SD || st->state == OTA_REBOOTING);
        if (busy) {
            lv_obj_add_flag(ota_ov_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(ota_ov_btn, LV_OBJ_FLAG_HIDDEN);
            if (st->state == OTA_UPDATE_AVAILABLE)
                lv_label_set_text_fmt(ota_ov_btn_lbl, "Install v%s", st->available_version);
            else
                lv_label_set_text(ota_ov_btn_lbl, "Check Again");
        }
    }
}

static void show_ota_update_overlay() {
    if (ota_overlay) return; // already open

    ota_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ota_overlay, 466, 466);
    lv_obj_center(ota_overlay);
    lv_obj_set_style_bg_color(ota_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ota_overlay, 255, 0);
    lv_obj_set_style_border_width(ota_overlay, 0, 0);
    lv_obj_clear_flag(ota_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Clean up timer + pointers whenever the overlay is destroyed (X or screen change)
    lv_obj_add_event_cb(ota_overlay, [](lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
        if (ota_ov_timer) { lv_timer_del(ota_ov_timer); ota_ov_timer = NULL; }
        ota_overlay = NULL; ota_ov_status = NULL; ota_ov_bar = NULL;
        ota_ov_btn = NULL; ota_ov_btn_lbl = NULL;
    }, LV_EVENT_DELETE, NULL);

    // Close (X) — small button, large invisible tap target so it's easy to hit
    lv_obj_t * btn_x = lv_btn_create(ota_overlay);
    lv_obj_set_size(btn_x, 56, 56);
    lv_obj_set_style_radius(btn_x, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn_x, lv_color_hex(0x333333), 0);
    lv_obj_align(btn_x, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_ext_click_area(btn_x, 36);   // enlarge the tap target beyond the visible circle
    lv_obj_t * lx = lv_label_create(btn_x);
    lv_label_set_text(lx, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(lx, &lv_font_montserrat_20, 0);
    lv_obj_center(lx);
    lv_obj_add_event_cb(btn_x, [](lv_event_t * e) {
        lv_event_code_t c = lv_event_get_code(e);
        if (c == LV_EVENT_CLICKED || c == LV_EVENT_RELEASED) ota_overlay_close();
    }, LV_EVENT_ALL, NULL);

    // Title
    lv_obj_t * title = lv_label_create(ota_overlay);
    lv_label_set_text(title, "SOFTWARE UPDATE");
    lv_obj_set_style_text_font(title, &ui_font_rajdhani1, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 124);  // clear of the top close (X) button

    // Status (centered, wraps)
    ota_ov_status = lv_label_create(ota_overlay);
    lv_label_set_long_mode(ota_ov_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ota_ov_status, 360);
    lv_obj_set_style_text_align(ota_ov_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ota_ov_status, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(ota_ov_status, lv_color_hex(0xCCCCCC), 0);
    lv_label_set_text(ota_ov_status, "Starting update check...");
    lv_obj_align(ota_ov_status, LV_ALIGN_CENTER, 0, -20);

    // Progress bar
    ota_ov_bar = lv_bar_create(ota_overlay);
    lv_obj_set_size(ota_ov_bar, 320, 16);
    lv_obj_align(ota_ov_bar, LV_ALIGN_CENTER, 0, 44);
    lv_bar_set_range(ota_ov_bar, 0, 100);
    lv_bar_set_value(ota_ov_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ota_ov_bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ota_ov_bar, lv_color_hex(0x999999), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ota_ov_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(ota_ov_bar, 8, LV_PART_INDICATOR);

    // Action button (Check Again / Install vX)
    ota_ov_btn = lv_btn_create(ota_overlay);
    lv_obj_set_size(ota_ov_btn, 300, 60);
    lv_obj_align(ota_ov_btn, LV_ALIGN_CENTER, 0, 112);
    lv_obj_set_style_bg_color(ota_ov_btn, lv_color_hex(0xFF6A00), 0);
    lv_obj_set_style_radius(ota_ov_btn, 14, 0);
    lv_obj_add_flag(ota_ov_btn, LV_OBJ_FLAG_HIDDEN);  // hidden until refresh decides (prevents start flash)
    ota_ov_btn_lbl = lv_label_create(ota_ov_btn);
    lv_label_set_text(ota_ov_btn_lbl, "Check Again");
    lv_obj_set_style_text_font(ota_ov_btn_lbl, &ui_font_rajdhani1, 0);
    lv_obj_center(ota_ov_btn_lbl);
    lv_obj_add_event_cb(ota_ov_btn, [](lv_event_t * e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        const OTAStatus* st = ota_get_status();
        if (st->state == OTA_UPDATE_AVAILABLE) ota_install();
        else                                    ota_check_for_update();
    }, LV_EVENT_ALL, NULL);

    lv_obj_move_foreground(ota_overlay);
    ota_ov_timer = lv_timer_create(ota_overlay_refresh, 400, NULL);

    // Kick off the check immediately
    ota_check_for_update();
}
