// Convoy pairing picker — one-time screen to bind this board to ITS T-Beam.
//
// In a packed convoy several T-Beams advertise at once, so we don't auto-grab
// "the first Meshtastic device": we list them strongest-RSSI-first (your own,
// bolted to the dash ~10-20 cm away, is dramatically the loudest → top row) and
// let the user tap it once. convoy_link_bind() then persists that MAC; every
// future boot reconnects straight to it and ignores the neighbours.
//
// FIRMWARE-ONLY (pulls in convoy_link.h → NimBLE). Not built by the sim.
//
// Usage:
//   lv_disp_load_scr(convoy_picker_build(on_bound));   // on first run
// where on_bound() switches to the convoy radar screen once a T-Beam is chosen.
#ifndef CONVOY_PICKER_UI_H
#define CONVOY_PICKER_UI_H

#include "lvgl.h"
#include "convoy_link.h"

static lv_obj_t   *convoy_picker_screen = NULL;
static lv_obj_t   *convoy_picker_list   = NULL;
static lv_timer_t *convoy_picker_timer  = NULL;
static void      (*convoy_picker_done)(void) = NULL;

// Tap a row → bind to that MAC, tear down the scan, hand back to the caller.
static void convoy_picker_row_cb(lv_event_t *e) {
    const char *mac = (const char *)lv_event_get_user_data(e);
    convoy_link_bind(mac);
    if (convoy_picker_timer) { lv_timer_del(convoy_picker_timer); convoy_picker_timer = NULL; }
    if (convoy_picker_done) convoy_picker_done();
}

// Rebuild the row list from the current (RSSI-sorted) scan results.
static void convoy_picker_refresh(lv_timer_t *t) {
    (void)t;
    lv_obj_clean(convoy_picker_list);
    int n = convoy_link_scan_count();
    if (n == 0) {
        lv_obj_t *l = lv_label_create(convoy_picker_list);
        lv_label_set_text(l, "Scanning for T-Beams...");
        lv_obj_set_style_text_color(l, lv_color_hex(0x6B8595), 0);
        return;
    }
    for (int i = 0; i < n; i++) {
        static convoy_scan_dev_t dev[CONVOY_SCAN_MAX];   // static: rows keep the mac ptr
        if (!convoy_link_scan_get(i, &dev[i])) continue;
        // Signal strength → 1..4 bars; closest (your own) is ~4.
        int bars = dev[i].rssi > -50 ? 4 : dev[i].rssi > -65 ? 3 : dev[i].rssi > -80 ? 2 : 1;
        char label[48];
        snprintf(label, sizeof(label), "%s   %.*s%s",
                 dev[i].name, bars, "||||", i == 0 ? "   (closest)" : "");
        lv_obj_t *btn = lv_list_add_btn(convoy_picker_list, LV_SYMBOL_GPS, label);
        lv_obj_set_style_text_color(btn, i == 0 ? lv_color_hex(0xFF6A00)
                                                : lv_color_hex(0xDDE6EC), 0);
        lv_obj_add_event_cb(btn, convoy_picker_row_cb, LV_EVENT_CLICKED, dev[i].mac);
    }
}

static void convoy_picker_rescan_cb(lv_event_t *e) {
    (void)e;
    convoy_link_start_scan(6);
}

// Build (once) the picker screen. on_bound is called after the user picks.
static lv_obj_t *convoy_picker_build(void (*on_bound)(void)) {
    convoy_picker_done = on_bound;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, 466, 466);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x05080D), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    convoy_picker_screen = scr;

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "PAIR CONVOY RADIO");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xDDE6EC), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Tap your T-Beam (the closest one)");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6B8595), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 72);

    // Round-display safe: keep the list within the inscribed circle.
    convoy_picker_list = lv_list_create(scr);
    lv_obj_set_size(convoy_picker_list, 300, 240);
    lv_obj_align(convoy_picker_list, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_bg_opa(convoy_picker_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(convoy_picker_list, 0, 0);

    lv_obj_t *rescan = lv_btn_create(scr);
    lv_obj_align(rescan, LV_ALIGN_BOTTOM_MID, 0, -46);
    lv_obj_add_event_cb(rescan, convoy_picker_rescan_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(rescan);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH " Rescan");
    lv_obj_center(rl);

    convoy_link_start_scan(6);
    convoy_picker_timer = lv_timer_create(convoy_picker_refresh, 800, NULL);
    return scr;
}

#endif // CONVOY_PICKER_UI_H
