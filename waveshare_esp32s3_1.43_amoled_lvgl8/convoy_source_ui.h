// Convoy SOURCE picker — choose where the Tracker gets convoy positions.
//
// Shared UI (firmware + sim, single translation unit), same pattern as
// convoy_ui.h. Two pages, each with an X close on top (matching the app's other
// contextual screens):
//   Page 1 (source):  MESHTASTIC  |  USE PHONE     X → back to the radar
//   Page 2 (scan):    list of T-Beams by name+RSSI  X → back to page 1
//
// Rendering is data-source-agnostic: firmware feeds real scan results, the sim
// feeds a mock list, both through convoy_src_* setters; user actions fire the
// convoy_src_on_* callbacks the caller wires to real behaviour.
//   MESHTASTIC → opens page 2 + convoy_src_on_scan()
//   a T-Beam   → convoy_src_on_pick_device(index)
//   USE PHONE  → convoy_src_on_use_phone()   (radar shows "Connect via browser")
//   X (page 1) → convoy_src_on_back()
#ifndef CONVOY_SOURCE_UI_H
#define CONVOY_SOURCE_UI_H

#include "lvgl.h"
#include <stdio.h>
#include "convoy_ui.h"     // reuse cv_label + CV_ORANGE + the dark palette

#define CVSRC_MAX_DEV 8
#define CVSRC_CYAN  lv_color_hex(0x00E5FF)
#define CVSRC_GREEN lv_color_hex(0x00E676)

// ── Callbacks (assigned by firmware / sim) ───────────────────────────────────
static void (*convoy_src_on_scan)(void)       = NULL;  // opened page 2 → start scan
static void (*convoy_src_on_pick_device)(int) = NULL;  // tapped a scanned T-Beam
static void (*convoy_src_on_use_phone)(void)  = NULL;  // tapped Use phone
static void (*convoy_src_on_use_cloud)(void)  = NULL;  // tapped Convoy (WiFi)
static void (*convoy_src_on_back)(void)       = NULL;  // X on page 1 → radar

// Screen loader hook: the firmware installs a "clean" loader (deferred load +
// full AMOLED redraw) so the static picker pages don't show a half-old/half-new
// partial-refresh frame. The sim leaves it NULL → plain load.
static void (*convoy_src_load_fn)(lv_obj_t *) = NULL;
static void convoy_src_show(lv_obj_t *scr) {
    if (convoy_src_load_fn) convoy_src_load_fn(scr);
    else lv_scr_load(scr);
}

// ── State ────────────────────────────────────────────────────────────────────
static lv_obj_t *convoy_src_screen      = NULL;   // page 1: source select
static lv_obj_t *convoy_src_scan_screen = NULL;   // page 2: device scan
static lv_obj_t *convoy_src_status = NULL;        // "Scanning..." / "N found"
static lv_obj_t *convoy_src_list   = NULL;        // scrollable device-row container
static lv_obj_t *convoy_src_rows[CVSRC_MAX_DEV] = { NULL };
static int       convoy_src_count  = 0;

// ── Close (X) button, top-mid, matching the other contextual screens ─────────
static lv_obj_t *cvsrc_close_btn(lv_obj_t *parent, lv_event_cb_t cb) {
    lv_obj_t *x = lv_btn_create(parent);
    lv_obj_set_size(x, 56, 56);
    lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(x, lv_color_hex(0x333333), 0);
    lv_obj_align(x, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_ext_click_area(x, 36);
    lv_obj_t *lx = lv_label_create(x);
    lv_label_set_text(lx, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(lx, &lv_font_montserrat_20, 0);
    lv_obj_center(lx);
    lv_obj_add_event_cb(x, cb, LV_EVENT_CLICKED, NULL);
    return x;
}

// ── Event handlers ───────────────────────────────────────────────────────────
static void cvsrc_mesh_evt(lv_event_t *e) {          // page 1: MESHTASTIC → page 2
    (void)e;
    for (int i = 0; i < convoy_src_count; i++)
        if (convoy_src_rows[i]) { lv_obj_del(convoy_src_rows[i]); convoy_src_rows[i] = NULL; }
    convoy_src_count = 0;
    if (convoy_src_status) lv_label_set_text(convoy_src_status, "Scanning...");
    convoy_src_show(convoy_src_scan_screen);
    if (convoy_src_on_scan) convoy_src_on_scan();
}
static void cvsrc_phone_evt(lv_event_t *e) { (void)e; if (convoy_src_on_use_phone) convoy_src_on_use_phone(); }
static void cvsrc_cloud_evt(lv_event_t *e) { (void)e; if (convoy_src_on_use_cloud) convoy_src_on_use_cloud(); }
static void cvsrc_back_evt(lv_event_t *e)  { (void)e; if (convoy_src_on_back)  convoy_src_on_back();  }
static void cvsrc_scan_back_evt(lv_event_t *e) { (void)e; convoy_src_show(convoy_src_screen); }  // page 2 X → page 1
static void cvsrc_dev_evt(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (convoy_src_on_pick_device) convoy_src_on_pick_device(idx);
}

// ── A tappable option card (title + subtitle, accent border) ─────────────────
static lv_obj_t *cvsrc_card(lv_obj_t *parent, const char *title, const char *sub,
                            lv_color_t accent, lv_event_cb_t cb) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 300, 66);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x0A0F16), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, accent, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_opa(c, LV_OPA_60, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t = cv_label(c, &lv_font_montserrat_20, accent);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 16, 12);
    lv_obj_t *s = cv_label(c, &lv_font_montserrat_14, lv_color_hex(0x6B8595));
    lv_label_set_text(s, sub);
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 16, -12);
    return c;
}

// A page shell — dark round screen with the title under the top X. Explicit
// alignment (like ota_overlay_ui.h) so the X sits cleanly at the top.
static lv_obj_t *cvsrc_page(const char *titletext) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, 466, 466);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x05080D), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    // Opaque full-panel backdrop (bottom child) — the screen's own bg doesn't
    // reliably repaint on the AMOLED partial-refresh path, leaving the previous
    // screen ghosted between elements. Same fix as convoy_ui.h.
    lv_obj_t *bd = lv_obj_create(scr);
    lv_obj_remove_style_all(bd);
    lv_obj_set_size(bd, 466, 466);
    lv_obj_align(bd, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(bd, lv_color_hex(0x05080D), 0);
    lv_obj_set_style_bg_opa(bd, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bd, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *title = cv_label(scr, &lv_font_montserrat_16, lv_color_hex(0x8DA2B0));
    lv_label_set_text(title, titletext);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 92);
    return scr;
}

// ── Build both pages (call once) ─────────────────────────────────────────────
static lv_obj_t *convoy_src_build_screen(void) {
    // Validity-checked, not just non-NULL — see convoy_build_screen(): an animated
    // screen load with auto_del can delete a showing screen out from under us, and a
    // cached pointer to a freed screen is worse than no cache. Both pages are
    // rebuilt together so they can never end up from different generations.
    if (convoy_src_screen && lv_obj_is_valid(convoy_src_screen)
        && convoy_src_scan_screen && lv_obj_is_valid(convoy_src_scan_screen))
        return convoy_src_screen;
    convoy_src_screen = NULL; convoy_src_scan_screen = NULL;
    for (int i = 0; i < CVSRC_MAX_DEV; i++) convoy_src_rows[i] = NULL;

    // Page 1 — source select
    convoy_src_screen = cvsrc_page("CONVOY SOURCE");
    // Three sources now. 82px pitch keeps a 16px gap between the 66px cards and
    // still clears the round screen's edges at the top and bottom rows.
    lv_obj_t *m = cvsrc_card(convoy_src_screen, "MESHTASTIC", "Scan for T-Beam", CVSRC_CYAN, cvsrc_mesh_evt);
    lv_obj_align(m, LV_ALIGN_CENTER, 0, -82);
    lv_obj_t *c = cvsrc_card(convoy_src_screen, "CONVOY", "Over phone hotspot", CVSRC_GREEN, cvsrc_cloud_evt);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *p = cvsrc_card(convoy_src_screen, "USE PHONE", "Relay over Bluetooth", CV_ORANGE, cvsrc_phone_evt);
    lv_obj_align(p, LV_ALIGN_CENTER, 0, 82);
    cvsrc_close_btn(convoy_src_screen, cvsrc_back_evt);

    // Page 2 — device scan
    convoy_src_scan_screen = cvsrc_page("SELECT T-BEAM");
    convoy_src_status = cv_label(convoy_src_scan_screen, &lv_font_montserrat_14, lv_color_hex(0x4E6675));
    lv_label_set_text(convoy_src_status, "Scanning...");
    lv_obj_align(convoy_src_status, LV_ALIGN_TOP_MID, 0, 124);
    convoy_src_list = lv_obj_create(convoy_src_scan_screen);
    lv_obj_remove_style_all(convoy_src_list);
    lv_obj_set_size(convoy_src_list, 300, 210);
    lv_obj_set_flex_flow(convoy_src_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(convoy_src_list, 8, 0);
    lv_obj_align(convoy_src_list, LV_ALIGN_TOP_MID, 0, 150);
    cvsrc_close_btn(convoy_src_scan_screen, cvsrc_scan_back_evt);

    return convoy_src_screen;
}

// ── Setters the data source (firmware scan / sim mock) calls ─────────────────
static void convoy_src_clear_devices(void) {
    for (int i = 0; i < convoy_src_count; i++)
        if (convoy_src_rows[i]) { lv_obj_del(convoy_src_rows[i]); convoy_src_rows[i] = NULL; }
    convoy_src_count = 0;
}
static void convoy_src_add_device(const char *name, int rssi) {
    if (!convoy_src_list || convoy_src_count >= CVSRC_MAX_DEV) return;
    int i = convoy_src_count++;
    lv_obj_t *row = lv_obj_create(convoy_src_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 300, 44);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x10161E), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x1E3A4C), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cvsrc_dev_evt, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t *nm = cv_label(row, &lv_font_montserrat_16, lv_color_hex(0xFFFFFF));
    lv_label_set_text(nm, name);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t *rs = cv_label(row, &lv_font_montserrat_14, CVSRC_CYAN);
    lv_label_set_text_fmt(rs, "%d dBm", rssi);
    lv_obj_align(rs, LV_ALIGN_RIGHT_MID, -14, 0);
    convoy_src_rows[i] = row;

    if (convoy_src_status) lv_label_set_text_fmt(convoy_src_status, "%d found", convoy_src_count);
}
static void convoy_src_set_scanning(bool on) {
    if (convoy_src_status)
        lv_label_set_text(convoy_src_status, on ? "Scanning..." :
                          (convoy_src_count ? "" : "No T-Beam found"));
}
static void convoy_src_reset(void) { convoy_src_clear_devices(); }

#endif // CONVOY_SOURCE_UI_H
