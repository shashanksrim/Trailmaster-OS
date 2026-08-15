// Convoy map — a radar-style relative view of the cars in the mesh.
//
// Shared UI (like screen_ui.h / grid_launcher_ui.h): included by both the
// firmware and the sim, single translation unit. The firmware feeds it real
// lat/lon pulled from the co-located T-Beam over BLE (Meshtastic node DB); the
// sim feeds mock positions. All the rendering/geometry lives here so both look
// identical.
//
// Model: own car at the center, every other car placed by true bearing (north
// up) and great-circle distance. Range rings auto-scale to the farthest car.
#ifndef CONVOY_UI_H
#define CONVOY_UI_H

#include "lvgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>   // labs() — the Arduino core pulls this in transitively, the sim does not
#include <string.h>

#define CONVOY_MAX_CARS 6

// ── Geometry (px on the 466x466 round panel) ────────────────────────────────
// The radar scope stays CONCENTRIC with the physical circle (own car at the
// true panel centre, rings sharing the display's centre) so nothing looks
// off-axis on a round display. All metadata lives in a floating card in the
// lower third that a single tap dismisses to reveal the full scope — the
// bottom-sheet pattern adapted to a circular face.
//
// North is true geographic north by construction: every blip is placed by the
// great-circle bearing between absolute GPS positions, so "up" is always true
// north and the N anchor is simply pinned to 12 o'clock (no magnetometer).
#define CV_PANEL 466
#define CV_CX 233          // panel centre x  (== radar centre)
#define CV_CY 233          // panel centre y  (== radar centre)
#define CV_R  172          // outer range-ring radius (fills the circle)
#define CV_DOT 18          // other-car dot diameter
#define CV_SELF 22         // own-car dot diameter

// Trailmaster brand orange for "me"; distinct hues for the other cars.
#define CV_ORANGE lv_color_hex(0xFF6A00)

typedef struct {
    char       name[6];
    lv_color_t color;
    double     lat, lon;
    bool       online;
    bool       has_fix;
    lv_obj_t  *dot;
    lv_obj_t  *lbl;
} convoy_car_t;

// ── State (single-include header, so file-static is fine) ───────────────────
static convoy_car_t convoy_cars[CONVOY_MAX_CARS];
static int          convoy_count    = 0;
static double       convoy_self_lat  = 0, convoy_self_lon = 0;
static bool         convoy_self_fix  = false;
static int          convoy_self_updates = 0;   // # of own-position packets received
// Satellites in the current solution, or -1 when the source cannot report it.
// Only the on-board receiver knows this; the mesh and phone feeders hand us a
// position with no quality metadata, so they leave it at -1 and the chip falls
// back to a plain "GPS". Kept here rather than in gps.h so convoy_ui.h stays
// source-agnostic and the WASM sim still builds without a receiver.
static int          convoy_sats = -1;
static double       convoy_self_hdg  = 0;      // smoothed heading, deg from true N
static bool         convoy_hdg_valid = false;  // false ⇒ hold north-up (stopped)
static bool         convoy_north_up  = false;  // user-chosen orientation lock

static lv_obj_t *convoy_screen   = NULL;
static lv_obj_t *convoy_self_dot = NULL;
static lv_obj_t *convoy_pulse    = NULL;
static lv_obj_t *convoy_cmark[4] = { NULL, NULL, NULL, NULL }; // N,E,S,W ring marks
static lv_obj_t *convoy_hdg_lbl  = NULL;    // top heading readout = orientation toggle
static lv_obj_t *convoy_me_lbl   = NULL;    // own-car label ("ME" or the callsign)
static char      convoy_self_name[8] = "ME";
static lv_obj_t *convoy_swipe_cue = NULL;   // orange chevron: swipe up for details
static lv_obj_t *convoy_card     = NULL;    // floating metadata panel (lower third)
static lv_obj_t *convoy_gps_lbl  = NULL;    // GPS status chip (card, top-left)
static lv_obj_t *convoy_count_lbl = NULL;   // "N CARS" (card, top-right)
static lv_obj_t *convoy_scale_lbl = NULL;   // outer-ring range tag (radar NE)
static lv_obj_t *convoy_info_dist = NULL;   // nearest car distance (hero)
static lv_obj_t *convoy_info_dir  = NULL;   // nearest: callsign · bearing · NEAREST
static lv_obj_t *convoy_status_lbl = NULL;  // link-status overlay ("CONNECTING")
static lv_obj_t *convoy_link_lbl  = NULL;   // "WIFI AENP" / "MESH" / "PHONE" chip
static char      convoy_link_src[12] = "";  // set by the owning task each loop
static char      convoy_link_room[12] = ""; // room code, cloud source only
static bool      convoy_link_ok  = false;   // link actually carrying data
static lv_obj_t *convoy_info_cta  = NULL;   // "Tap for settings" CTA (waiting state)
static bool      convoy_card_shown = true;
static bool      convoy_waiting    = false; // true while no live source/fix
static void    (*convoy_settings_cb)(void) = NULL;  // tap-while-waiting → source picker
static const char *convoy_wait_line = "Waiting for Mesh/Phone";  // middle line, muted
static const char *convoy_wait_cta  = "Hold for settings";       // bright CTA line

// ── Math helpers (great-circle distance + bearing) ──────────────────────────
static inline double cv_d2r(double d) { return d * 0.017453292519943295; }

static double convoy_dist_m(double la1, double lo1, double la2, double lo2) {
    const double R = 6371000.0;
    double dla = cv_d2r(la2 - la1), dlo = cv_d2r(lo2 - lo1);
    double a = sin(dla / 2) * sin(dla / 2) +
               cos(cv_d2r(la1)) * cos(cv_d2r(la2)) * sin(dlo / 2) * sin(dlo / 2);
    return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}

static double convoy_bearing_deg(double la1, double lo1, double la2, double lo2) {
    double y = sin(cv_d2r(lo2 - lo1)) * cos(cv_d2r(la2));
    double x = cos(cv_d2r(la1)) * sin(cv_d2r(la2)) -
               sin(cv_d2r(la1)) * cos(cv_d2r(la2)) * cos(cv_d2r(lo2 - lo1));
    double b = atan2(y, x) * 57.29577951308232;
    return b < 0 ? b + 360 : b;
}

static const char *convoy_compass(double b) {
    static const char *d[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    return d[((int)((b + 22.5) / 45.0)) & 7];
}

// Outer-ring range, snapped to a readable 1/2/5 ladder.
//
// The ladder used to start at 200 m and stop at 20 km, which broke the scope at
// both ends. Regrouping in a car park with everyone inside 45 m drew every blip
// on top of the centre dot with 80% of the scope empty — exactly when you most
// want to see who is where. And beyond 20 km it pinned to a single 50 km step,
// so a car 300 km away sat on the rim indistinguishable from one at 60 km.
//
// 25 m is the sensible floor: below that GPS error (a few metres each, on two
// independent fixes) is a large fraction of the ring spacing, and the blips are
// noise-driven rather than real. The top end runs to 500 km, which is past any
// plausible convoy but keeps the scale honest rather than saturating.
static double convoy_nice_scale(double maxm) {
    static const double steps[] = { 25, 50, 100, 200, 500, 1000, 2000, 5000,
                                    10000, 20000, 50000, 100000, 200000 };
    for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
        if (maxm <= steps[i]) return steps[i];
    return 500000;
}

static void convoy_fmt_dist(char *buf, int n, double m) {
    if (m < 1000) snprintf(buf, n, "%d m", (int)(m + 0.5));
    else          snprintf(buf, n, "%.1f km", m / 1000.0);
}

// ── Small styling helpers ───────────────────────────────────────────────────
static lv_obj_t *cv_dot(lv_obj_t *parent, int sz, lv_color_t c) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, sz, sz);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *cv_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t c) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

// Pulsing "you are here" ring around the own-car dot.
static void convoy_pulse_exec(void *obj, int32_t v) {  // v: 0..100
    int sz = CV_SELF + v * 46 / 100;
    lv_obj_set_size((lv_obj_t *)obj, sz, sz);
    lv_obj_align((lv_obj_t *)obj, LV_ALIGN_CENTER, 0, 0);  // own car sits at panel centre
    lv_obj_set_style_border_opa((lv_obj_t *)obj,
                                (lv_opa_t)(LV_OPA_COVER - v * LV_OPA_COVER / 100), 0);
}

// Single tap anywhere toggles the floating metadata card (bottom-sheet style).
// A LONG PRESS anywhere opens the source picker (switch mesh ⇄ phone ⇄ cloud).
//
// The picker used to be a plain tap on the metadata card. That put a
// screen-changing action on the same gesture as the card's own show/hide, on the
// one element you actually read while driving — so glancing at GPS state and
// nudging the panel would throw you out of the radar. A long press cannot be
// hit by accident and leaves every tap on this screen non-destructive.
static void convoy_open_settings(lv_event_t *e) {
    (void)e;
    if (convoy_settings_cb) convoy_settings_cb();
}
static void convoy_toggle_card(lv_event_t *e) {
    (void)e;
    // While waiting there is nothing to show in the card, so a tap would be a
    // no-op that reads as "the screen is dead". Keep the shortcut here — but the
    // CTA now says "Hold for settings", and the long press works everywhere.
    if (convoy_waiting && convoy_settings_cb) { convoy_settings_cb(); return; }
    convoy_card_shown = !convoy_card_shown;
    if (convoy_card_shown) {
        lv_obj_clear_flag(convoy_card, LV_OBJ_FLAG_HIDDEN);
        if (convoy_swipe_cue) lv_obj_add_flag(convoy_swipe_cue, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(convoy_card, LV_OBJ_FLAG_HIDDEN);
        if (convoy_swipe_cue) lv_obj_clear_flag(convoy_swipe_cue, LV_OBJ_FLAG_HIDDEN);
    }
}

// Orientation toggle, driven by tapping the top readout button. Heading-up shows
// the forward line + chevron and a live heading; north-up hides them and grows
// the N mark into a big anchor. The button stays put in both modes so a second
// tap flips back.
static void convoy_refresh(void);  // defined below

// Draw the link chip. Green when the link is up, orange when it is not — a down
// link is the commonest reason the radar is empty, and that should be readable
// at a glance rather than inferred from an absence of blips.
static void convoy_render_link(void) {
    if (!convoy_link_lbl) return;
    if (convoy_link_src[0] == '\0') { lv_label_set_text(convoy_link_lbl, ""); return; }
    const char *icon = strcmp(convoy_link_src, "WIFI")  == 0 ? LV_SYMBOL_WIFI
                     : strcmp(convoy_link_src, "PHONE") == 0 ? LV_SYMBOL_BLUETOOTH
                     : LV_SYMBOL_SHUFFLE;
    if (convoy_link_room[0])
        lv_label_set_text_fmt(convoy_link_lbl, "%s %s%s", icon,
                              convoy_link_room, convoy_link_ok ? "" : " ...");
    else
        lv_label_set_text_fmt(convoy_link_lbl, "%s%s", icon,
                              convoy_link_ok ? "" : " ...");
    lv_obj_set_style_text_color(convoy_link_lbl,
        lv_color_hex(convoy_link_ok ? 0x00E676 : 0xFFB020), 0);
}
static void convoy_apply_orientation(void) {
    if (convoy_north_up) {
        lv_obj_set_style_text_font(convoy_cmark[0], &lv_font_montserrat_36, 0);
    } else {
        lv_obj_set_style_text_font(convoy_cmark[0], &lv_font_montserrat_28, 0);
    }
    convoy_refresh();
}
static void convoy_toggle_orientation(lv_event_t *e) {
    (void)e;
    convoy_north_up = !convoy_north_up;
    convoy_apply_orientation();
}

// ── Convoy order list (page 2 of the Tracker) ───────────────────────────────
// The radar answers "where is everyone" spatially, which is the right view while
// moving. This page answers "what order are we in, and how far apart" — the
// question you have when regrouping, waiting at a junction, or working out who
// to call on the radio. A list is better at that: exact numbers, no overlapping
// blips, and it stays readable when two cars share a bearing.
//
// Paged with the same horizontal scroll-snap + dots the About screen uses, so
// it is the app's one paging idiom rather than a bespoke gesture.
//
// AHEAD/BEHIND is the ALONG-TRACK component of each car's offset, i.e.
// distance x cos(bearing - heading). A car 500 m away at 80 deg off your nose is
// only 87 m ahead of you, and ordering by raw distance would put it above
// someone 100 m directly in front. Projecting onto the heading is what makes the
// order match what you would see out of the windscreen.
static lv_obj_t *convoy_pages    = NULL;   // horizontal snap container
static lv_obj_t *convoy_dot[2]   = { NULL, NULL };
static lv_obj_t *convoy_list_box = NULL;   // flex column the rows go into

// One row, matched to the settings/about row idiom so every list in the app has
// the same touch target and reading weight. Width is trimmed from 400 to 356
// because a 400 px row is wider than the round panel's chord away from the
// centre line and the corners get eaten by the bezel.
static void convoy_list_row(lv_obj_t *parent, const char *name, lv_color_t col,
                            const char *right, bool is_self) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 356, 80);
    lv_obj_set_style_bg_color(row, lv_color_hex(is_self ? 0x2A1B0E : 0x10171E), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_border_color(row, is_self ? CV_ORANGE : lv_color_hex(0x24384A), 0);
    lv_obj_set_style_border_width(row, is_self ? 2 : 1, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pip = cv_dot(row, 20, col);
    lv_obj_align(pip, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *nm = cv_label(row, &lv_font_montserrat_28,
                            is_self ? CV_ORANGE : lv_color_hex(0xFFFFFF));
    lv_label_set_text(nm, name);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 46, 0);

    lv_obj_t *rt = cv_label(row, &lv_font_montserrat_24,
                            is_self ? CV_ORANGE : lv_color_hex(0xDDE6EC));
    lv_label_set_text(rt, right);
    lv_obj_align(rt, LV_ALIGN_RIGHT_MID, -16, 0);
}

// Rebuild the rows from current positions. Called when the page comes into view
// rather than every refresh: repopulating 60 times a second would fight the
// user's scroll and churn the heap for a page nobody is looking at.
static void convoy_fill_list(void) {
    if (!convoy_list_box) return;
    lv_obj_clean(convoy_list_box);

    if (!convoy_self_fix) {
        // Centre it. The box is a flex COLUMN, so a bare label gets packed to the
        // top-left corner — which is why this read as an off-centre stray string
        // rather than an empty-state message.
        lv_obj_set_flex_align(convoy_list_box, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *m = cv_label(convoy_list_box, &lv_font_montserrat_20, lv_color_hex(0x8CA6B6));
        lv_obj_set_width(m, 340);
        lv_label_set_text(m, "No own position yet.\nRelative order needs a fix.");
        lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }
    // Rows pack from the top again once there is something to list.
    lv_obj_set_flex_align(convoy_list_box, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // No usable heading (stopped, no compass) means there is no meaningful
    // "ahead", so fall back to north rather than ordering against noise.
    const double ref = convoy_hdg_valid ? convoy_self_hdg : 0.0;

    struct { int idx; double along, dist, brg; } ent[CONVOY_MAX_CARS + 1];
    int n = 0;
    for (int i = 0; i < convoy_count; i++) {
        if (!(convoy_cars[i].online && convoy_cars[i].has_fix)) continue;
        double d = convoy_dist_m(convoy_self_lat, convoy_self_lon,
                                 convoy_cars[i].lat, convoy_cars[i].lon);
        double b = convoy_bearing_deg(convoy_self_lat, convoy_self_lon,
                                      convoy_cars[i].lat, convoy_cars[i].lon);
        double rel = b - ref;
        while (rel < -180.0) rel += 360.0;
        while (rel >  180.0) rel -= 360.0;
        ent[n].idx = i; ent[n].dist = d; ent[n].brg = b;
        ent[n].along = d * cos(cv_d2r(rel));
        n++;
    }
    ent[n].idx = -1; ent[n].along = 0; ent[n].dist = 0; ent[n].brg = 0;   // us
    n++;

    for (int i = 1; i < n; i++) {            // insertion sort; n <= 7
        for (int j = i; j > 0 && ent[j].along > ent[j - 1].along; j--) {
            int ti = ent[j].idx; double ta = ent[j].along, td = ent[j].dist, tb = ent[j].brg;
            ent[j] = ent[j - 1];
            ent[j - 1].idx = ti; ent[j - 1].along = ta; ent[j - 1].dist = td; ent[j - 1].brg = tb;
        }
    }

    char right[40], dbuf[24];
    for (int k = 0; k < n; k++) {
        if (ent[k].idx < 0) {
            convoy_list_row(convoy_list_box, convoy_self_name, CV_ORANGE, "YOU", true);
            continue;
        }
        const convoy_car_t *c = &convoy_cars[ent[k].idx];
        convoy_fmt_dist(dbuf, sizeof(dbuf), ent[k].dist);
        snprintf(right, sizeof(right), "%s  %s", dbuf, convoy_compass(ent[k].brg));
        convoy_list_row(convoy_list_box, c->name, c->color, right, false);
    }

    // Known but unplaceable cars go last, so a missing vehicle is visible rather
    // than silently absent from the roster.
    for (int i = 0; i < convoy_count; i++) {
        if (convoy_cars[i].online && convoy_cars[i].has_fix) continue;
        convoy_list_row(convoy_list_box, convoy_cars[i].name, lv_color_hex(0x55636E),
                        convoy_cars[i].online ? "no fix" : "offline", false);
    }
}

// Tapping the GPS chip jumps to the order page — a shortcut, not the only route.
static void convoy_open_list(lv_event_t *e) {
    (void)e;
    if (convoy_pages) lv_obj_scroll_to_x(convoy_pages, CV_PANEL, LV_ANIM_ON);
}

// ── Build the screen (call once) ────────────────────────────────────────────
static lv_obj_t *convoy_build_screen(void) {
    // Re-check validity, don't just trust the cached pointer: lv_scr_load_anim()'s
    // auto_del path (lv_disp.c, `if(d->del_prev) lv_obj_del(act_scr)`) can delete
    // whatever screen is showing when another animated load lands on top of it. If
    // that was this screen, the cached pointer is freed and returning it hands out
    // dangling memory forever. Rebuild instead.
    if (convoy_screen && lv_obj_is_valid(convoy_screen)) return convoy_screen;
    convoy_screen = NULL;
    // The rebuild below frees every child, the order-list overlay included. Its
    // handle must be dropped with them: a stale non-NULL pointer here makes
    // convoy_open_list() short-circuit forever ("already open") and lets
    // convoy_close_list() delete freed memory.
    convoy_pages = NULL; convoy_list_box = NULL;
    convoy_dot[0] = convoy_dot[1] = NULL;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, 466, 466);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x05080D), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    convoy_screen = scr;

    // ── Two pages: RADAR | ORDER ────────────────────────────────────────────
    // Same horizontal scroll-snap container the About screen uses, so paging in
    // the Tracker feels identical to paging anywhere else in the app.
    lv_obj_t *scr_root = scr;                 // dots/status overlay ride above the pages
    convoy_pages = lv_obj_create(scr_root);
    lv_obj_remove_style_all(convoy_pages);
    lv_obj_set_size(convoy_pages, CV_PANEL, CV_PANEL);
    lv_obj_align(convoy_pages, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(convoy_pages, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_snap_x(convoy_pages, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(convoy_pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(convoy_pages, 0, 0);
    lv_obj_set_style_pad_column(convoy_pages, 0, 0);
    lv_obj_add_flag(convoy_pages, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(convoy_pages, LV_DIR_HOR);

    lv_obj_t *page_radar = lv_obj_create(convoy_pages);
    lv_obj_remove_style_all(page_radar);
    lv_obj_set_size(page_radar, CV_PANEL, CV_PANEL);
    lv_obj_clear_flag(page_radar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *page_list = lv_obj_create(convoy_pages);
    lv_obj_remove_style_all(page_list);
    lv_obj_set_size(page_list, CV_PANEL, CV_PANEL);
    lv_obj_clear_flag(page_list, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lttl = cv_label(page_list, &lv_font_montserrat_20, CV_ORANGE);
    lv_label_set_text(lttl, "CONVOY ORDER");
    lv_obj_align(lttl, LV_ALIGN_TOP_MID, 0, 30);

    convoy_list_box = lv_obj_create(page_list);
    lv_obj_remove_style_all(convoy_list_box);
    lv_obj_set_size(convoy_list_box, 376, 296);
    lv_obj_align(convoy_list_box, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_flex_flow(convoy_list_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(convoy_list_box, 6, 0);
    lv_obj_set_scroll_dir(convoy_list_box, LV_DIR_VER);

    // Everything below builds into page 1. `scr` is a local, so repointing it
    // keeps all the radar's LV_ALIGN_CENTER geometry working unchanged — the
    // page is the same 466x466 as the screen it used to sit on.
    scr = page_radar;

    // Explicit full-panel opaque backdrop as the bottom child. A plain child
    // object paints its bg reliably; relying on the screen's own bg left the
    // previous screen (launcher) showing through the empty areas between the
    // radar elements on the AMOLED partial-refresh path.
    lv_obj_t *backdrop = lv_obj_create(scr);
    lv_obj_remove_style_all(backdrop);
    lv_obj_set_size(backdrop, 466, 466);
    lv_obj_align(backdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x05080D), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_COVER, 0);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Radar scope — concentric with the physical circle ───────────────────
    // Range rings (3), auto-scaled by convoy_refresh; outer brightest.
    for (int i = 1; i <= 3; i++) {
        int r = CV_R * i / 3;
        lv_obj_t *ring = lv_obj_create(scr);
        lv_obj_remove_style_all(ring);
        lv_obj_set_size(ring, r * 2, r * 2);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(0x35617D), 0);
        lv_obj_set_style_border_width(ring, i == 3 ? 4 : 3, 0);
        lv_obj_set_style_border_opa(ring, i == 3 ? LV_OPA_COVER : LV_OPA_80, 0);
        lv_obj_align(ring, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    // Rotating compass ring: N/E/S/W marks are repositioned every refresh by
    // −heading, so they swing around as we turn (heading-up). N is the bold
    // orange one (grows big in north-up mode); E/S/W dim. Positions set in refresh.
    const char *cmk[4] = { "N", "E", "S", "W" };
    for (int i = 0; i < 4; i++) {
        // All four in white: they are a compass scale, and colouring one of them
        // brand-orange made it read as a status light rather than a bearing. N
        // stays distinguishable by SIZE (and grows again in north-up mode).
        convoy_cmark[i] = cv_label(scr, &lv_font_montserrat_24, lv_color_hex(0xFFFFFF));
        lv_label_set_text(convoy_cmark[i], cmk[i]);
        lv_obj_align(convoy_cmark[i], LV_ALIGN_CENTER, 0, -CV_R);
    }

    // Forward reference: a lubber line straight up from the centre + a chevron.
    // Shown only in heading-up; "up" is always our direction of travel.
    // Top heading readout — this is the orientation toggle BUTTON. Styled as a
    // subtle pill so it reads as tappable; a tap flips heading-up ⇄ north-up.
    convoy_hdg_lbl = cv_label(scr, &lv_font_montserrat_20, lv_color_hex(0xDDE6EC));
    lv_label_set_text(convoy_hdg_lbl, "");
    lv_obj_set_style_bg_color(convoy_hdg_lbl, lv_color_hex(0x14202B), 0);
    lv_obj_set_style_bg_opa(convoy_hdg_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(convoy_hdg_lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(convoy_hdg_lbl, CV_ORANGE, 0);
    lv_obj_set_style_border_width(convoy_hdg_lbl, 2, 0);
    lv_obj_set_style_border_opa(convoy_hdg_lbl, LV_OPA_50, 0);
    lv_obj_set_style_pad_hor(convoy_hdg_lbl, 12, 0);
    lv_obj_set_style_pad_ver(convoy_hdg_lbl, 4, 0);
    lv_obj_align(convoy_hdg_lbl, LV_ALIGN_CENTER, 0, -(CV_R + 32));
    lv_obj_add_flag(convoy_hdg_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(convoy_hdg_lbl, 14);
    lv_obj_add_event_cb(convoy_hdg_lbl, convoy_toggle_orientation, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(convoy_hdg_lbl, convoy_open_settings, LV_EVENT_LONG_PRESSED, NULL);

    // Scale tag (outer ring range) — rides the outer ring on the NE diagonal so
    // it stays visible above the floating card and clear of the centre.
    convoy_scale_lbl = cv_label(scr, &lv_font_montserrat_20, lv_color_hex(0xFFFFFF));
    lv_label_set_text(convoy_scale_lbl, "");
    lv_obj_align(convoy_scale_lbl, LV_ALIGN_CENTER, 116, -112);

    // Own-car marker: pulse ring + orange dot + up-chevron (points forward).
    convoy_pulse = lv_obj_create(scr);
    lv_obj_remove_style_all(convoy_pulse);
    lv_obj_set_size(convoy_pulse, CV_SELF, CV_SELF);
    lv_obj_set_style_radius(convoy_pulse, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(convoy_pulse, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(convoy_pulse, CV_ORANGE, 0);
    lv_obj_set_style_border_width(convoy_pulse, 3, 0);
    lv_obj_align(convoy_pulse, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(convoy_pulse, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    convoy_self_dot = cv_dot(scr, CV_SELF, CV_ORANGE);
    lv_obj_align(convoy_self_dot, LV_ALIGN_CENTER, 0, 0);
    // Own-car label. "ME" is only the fallback: once a callsign is configured,
    // showing it here means the radar and the order list agree with what every
    // other car in the room sees for you.
    convoy_me_lbl = cv_label(scr, &lv_font_montserrat_20, CV_ORANGE);
    lv_label_set_text(convoy_me_lbl, convoy_self_name);
    lv_obj_align(convoy_me_lbl, LV_ALIGN_CENTER, 0, -26);

    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, convoy_pulse);
    lv_anim_set_exec_cb(&a, convoy_pulse_exec);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, 1600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    // Per-car dots + bigger callsign labels (created up front, shown as needed).
    for (int i = 0; i < CONVOY_MAX_CARS; i++) {
        convoy_cars[i].dot = cv_dot(scr, CV_DOT, lv_color_hex(0x888888));
        convoy_cars[i].lbl = cv_label(scr, &lv_font_montserrat_24, lv_color_hex(0xFFFFFF));
        lv_obj_add_flag(convoy_cars[i].dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(convoy_cars[i].lbl, LV_OBJ_FLAG_HIDDEN);
    }

    // ── Floating metadata card (lower third) ────────────────────────────────
    // 80%-black rounded panel; tapping it opens the source picker so you can
    // switch mesh ⇄ phone at any time (a tap elsewhere toggles the card).
    convoy_card = lv_obj_create(scr);
    lv_obj_remove_style_all(convoy_card);
    lv_obj_set_size(convoy_card, 330, 124);
    lv_obj_set_style_radius(convoy_card, 22, 0);
    lv_obj_set_style_bg_color(convoy_card, lv_color_hex(0x272E35), 0);
    lv_obj_set_style_bg_opa(convoy_card, LV_OPA_90, 0);
    lv_obj_set_style_border_color(convoy_card, lv_color_hex(0x1E3A4C), 0);
    lv_obj_set_style_border_width(convoy_card, 2, 0);
    lv_obj_set_style_border_opa(convoy_card, LV_OPA_70, 0);
    lv_obj_align(convoy_card, LV_ALIGN_BOTTOM_MID, 0, -58);
    lv_obj_clear_flag(convoy_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(convoy_card, LV_OBJ_FLAG_CLICKABLE);
    // Tap = toggle, same as the rest of the screen. Long press = source picker.
    // Both are registered on the card itself because it sits ABOVE the
    // full-screen catcher and would otherwise swallow the gesture.
    lv_obj_add_event_cb(convoy_card, convoy_toggle_card, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(convoy_card, convoy_open_settings, LV_EVENT_LONG_PRESSED, NULL);

    // Status row inside the card: GPS state (left) + online count (right).
    convoy_gps_lbl = cv_label(convoy_card, &lv_font_montserrat_20, lv_color_hex(0x00E676));
    lv_label_set_text(convoy_gps_lbl, LV_SYMBOL_GPS " GPS");
    // Tapping the GPS chip opens the convoy order list. It sits above the card,
    // so it takes the tap before the card's toggle handler sees it.
    lv_obj_add_flag(convoy_gps_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(convoy_gps_lbl, 12);
    lv_obj_add_event_cb(convoy_gps_lbl, convoy_open_list, LV_EVENT_CLICKED, NULL);
    lv_obj_align(convoy_gps_lbl, LV_ALIGN_TOP_LEFT, 12, 8);

    convoy_link_lbl = cv_label(convoy_card, &lv_font_montserrat_16, lv_color_hex(0x8CA6B6));
    lv_label_set_text(convoy_link_lbl, "");
    lv_obj_align(convoy_link_lbl, LV_ALIGN_TOP_MID, 0, 8);

    convoy_count_lbl = cv_label(convoy_card, &lv_font_montserrat_20, lv_color_hex(0x8CA6B6));
    lv_label_set_text(convoy_count_lbl, "");
    lv_obj_align(convoy_count_lbl, LV_ALIGN_TOP_RIGHT, -12, 8);

    // Hero distance + callsign/bearing line.
    convoy_info_dist = cv_label(convoy_card, &lv_font_montserrat_36, lv_color_hex(0xFFFFFF));
    lv_label_set_text(convoy_info_dist, "");
    lv_obj_align(convoy_info_dist, LV_ALIGN_CENTER, 0, 6);
    convoy_info_dir = cv_label(convoy_card, &lv_font_montserrat_20, lv_color_hex(0x8CA6B6));
    lv_label_set_recolor(convoy_info_dir, true);
    lv_label_set_text(convoy_info_dir, "");
    lv_obj_align(convoy_info_dir, LV_ALIGN_BOTTOM_MID, 0, -6);

    // "Tap for settings" — shown only while waiting for a source (near-white,
    // a touch bigger than the status line). Hidden once data is flowing.
    convoy_info_cta = cv_label(convoy_card, &lv_font_montserrat_20, lv_color_hex(0xE6E6E6));
    lv_label_set_text(convoy_info_cta, convoy_wait_cta);
    lv_obj_align(convoy_info_cta, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_add_flag(convoy_info_cta, LV_OBJ_FLAG_HIDDEN);

    // Transparent full-screen tap catcher (bottom of the interactive layer):
    // a tap that misses the specific targets below toggles the metadata card.
    lv_obj_t *tap = lv_obj_create(scr);
    lv_obj_remove_style_all(tap);
    lv_obj_set_size(tap, 466, 466);
    lv_obj_align(tap, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(tap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tap, convoy_toggle_card, LV_EVENT_CLICKED, NULL);

    // Orange chevron at the foot of the scope, shown only while the card is
    // hidden: it is the one affordance telling you the details are still there.
    // The previous text cue was removed for rendering badly; a glyph says the
    // same thing in less space and cannot clip.
    convoy_swipe_cue = cv_label(scr, &lv_font_montserrat_24, CV_ORANGE);
    lv_label_set_text(convoy_swipe_cue, LV_SYMBOL_UP);
    lv_obj_align(convoy_swipe_cue, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_obj_add_flag(convoy_swipe_cue, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(tap, convoy_open_settings, LV_EVENT_LONG_PRESSED, NULL);

    // Keep the orientation button + the floating card above the catcher so they
    // get their own taps (card → source picker; a tap elsewhere toggles the card).
    lv_obj_move_foreground(convoy_hdg_lbl);
    lv_obj_move_foreground(convoy_card);

    // ── Pagination dots (same styling/behaviour as the About screen) ────────
    lv_obj_t *dot_cont = lv_obj_create(scr_root);
    lv_obj_set_size(dot_cont, 60, 16);
    lv_obj_align(dot_cont, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_bg_opa(dot_cont, 0, 0);
    lv_obj_set_style_border_width(dot_cont, 0, 0);
    lv_obj_set_flex_flow(dot_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dot_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dot_cont, 8, 0);
    lv_obj_clear_flag(dot_cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 2; i++) {
        convoy_dot[i] = lv_obj_create(dot_cont);
        lv_obj_set_size(convoy_dot[i], 10, 10);
        lv_obj_set_style_radius(convoy_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(convoy_dot[i],
                                  lv_color_hex(i == 0 ? 0xFF9800 : 0x444444), 0);
        lv_obj_set_style_border_width(convoy_dot[i], 0, 0);
        lv_obj_clear_flag(convoy_dot[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_add_event_cb(convoy_pages, [](lv_event_t *ev) {
        (void)ev;
        int page = (lv_obj_get_scroll_x(convoy_pages) + CV_PANEL / 2) / CV_PANEL;
        lv_obj_set_style_bg_color(convoy_dot[0], lv_color_hex(page == 0 ? 0xFF9800 : 0x444444), 0);
        lv_obj_set_style_bg_color(convoy_dot[1], lv_color_hex(page == 1 ? 0xFF9800 : 0x444444), 0);
        // Repopulate as the order page comes into view, not on every refresh.
        static int last = 0;
        if (page != last) { last = page; if (page == 1) convoy_fill_list(); }
    }, LV_EVENT_SCROLL, NULL);

    // Link-status overlay (e.g. "STARTING RADIO" / "CONNECTING") shown while the
    // BLE link is coming up; hidden once data streams. On top of everything.
    lv_obj_add_event_cb(scr_root, [](lv_event_t *ev) {
        (void)ev;
        lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (d == LV_DIR_TOP    && !convoy_card_shown) convoy_toggle_card(NULL);
        else if (d == LV_DIR_BOTTOM && convoy_card_shown) convoy_toggle_card(NULL);
    }, LV_EVENT_GESTURE, NULL);

    convoy_status_lbl = cv_label(scr_root, &lv_font_montserrat_20, CV_ORANGE);
    lv_label_set_text(convoy_status_lbl, "");
    lv_obj_set_style_bg_color(convoy_status_lbl, lv_color_hex(0x0A0F16), 0);
    lv_obj_set_style_bg_opa(convoy_status_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(convoy_status_lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(convoy_status_lbl, 16, 0);
    lv_obj_set_style_pad_ver(convoy_status_lbl, 8, 0);
    lv_obj_align(convoy_status_lbl, LV_ALIGN_CENTER, 0, -70);
    lv_obj_add_flag(convoy_status_lbl, LV_OBJ_FLAG_HIDDEN);

    convoy_apply_orientation();   // start in heading-up (line shown, N normal)
    return scr_root;
}

// ── Recompute + redraw from current state ───────────────────────────────────
static void convoy_refresh(void) {
    if (!convoy_screen) return;

    int online = 0;
    for (int i = 0; i < convoy_count; i++)
        if (convoy_cars[i].online && convoy_cars[i].has_fix) online++;
    // Count OTHER cars, not the convoy including us. "1 CARS" while alone on
    // the radar was actively confusing: the number you want at a glance is how
    // many people you can still see, and that is zero when you are on your own.
    // Singular/plural because "1 CARS" reads like a bug even when it is right.
    lv_label_set_text_fmt(convoy_count_lbl, online == 1 ? "%d CAR" : "%d CARS", online);
    convoy_render_link();

    // GPS status chip — always visible; green with fix, orange while acquiring.
    // With the on-board receiver we can show the satellite count, which turns a
    // binary "GPS/NO FIX" into something diagnostic: a solution on 5 satellites
    // behaves very differently from one on 14, and watching the count is how you
    // tell "about to lose it under trees" from "solid". %d is safe here — it is
    // only %f that LVGL's snprintf cannot handle.
    if (convoy_self_fix) {
        lv_obj_set_style_text_color(convoy_gps_lbl, lv_color_hex(0x00E676), 0);
        if (convoy_sats >= 0)
            lv_label_set_text_fmt(convoy_gps_lbl, LV_SYMBOL_GPS " %d SAT", convoy_sats);
        else
            lv_label_set_text(convoy_gps_lbl, LV_SYMBOL_GPS " GPS");
    } else {
        lv_obj_set_style_text_color(convoy_gps_lbl, CV_ORANGE, 0);
        // Acquiring: satellites in view climb well before a fix lands, so show
        // them — it is the difference between "working on it" and "no antenna".
        if (convoy_sats > 0)
            lv_label_set_text_fmt(convoy_gps_lbl, LV_SYMBOL_GPS " %d SAT", convoy_sats);
        else
            lv_label_set_text(convoy_gps_lbl, LV_SYMBOL_GPS " NO FIX");
    }

    // Orientation: heading-up rotates everything by −heading ("up" = our travel
    // direction); north-up locks hdg = 0. We also hold north-up when heading is
    // invalid (stopped / no COG) so the scope doesn't spin on GPS noise.
    double hdg = (convoy_north_up || !convoy_hdg_valid) ? 0.0 : convoy_self_hdg;
    static const int cmk_abs[4] = { 0, 90, 180, 270 };   // N,E,S,W true bearings
    for (int i = 0; i < 4; i++) {
        double sa = cmk_abs[i] - hdg;                     // screen angle, 0 = up
        // Marks ride OUTSIDE the outer range ring, in the margin between it and
        // the panel edge, so they never sit on top of the scope contents. At 34 pt
        // (44 for N in north-up) the glyph still clears the 233 px panel radius.
        // This is the live value — the build-time align is only the initial
        // placement, since refresh repositions all four every frame by -heading.
        int r = CV_R;
        lv_obj_align(convoy_cmark[i], LV_ALIGN_CENTER,
                     (int)lround(r * sin(cv_d2r(sa))),
                     (int)lround(-r * cos(cv_d2r(sa))));

    }
    // Toggle-button label: mode name in north-up, live heading in heading-up.
    if (convoy_north_up)
        lv_label_set_text(convoy_hdg_lbl, "NORTH UP");
    else if (convoy_hdg_valid)
        lv_label_set_text_fmt(convoy_hdg_lbl, "%03d %s",
                              ((int)(hdg + 0.5)) % 360, convoy_compass(hdg));
    else
        lv_label_set_text(convoy_hdg_lbl, "-- HOLD");

    if (!convoy_self_fix) {
        // Waiting for a source (mesh/phone) or a GPS fix. The whole panel taps
        // through to the source picker; hide the now-meaningless GPS/count chips.
        convoy_waiting = true;
        lv_obj_add_flag(convoy_gps_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(convoy_count_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(convoy_info_dist, "ACQUIRING");
        lv_obj_set_style_text_color(convoy_info_dist, CV_ORANGE, 0);
        lv_obj_align(convoy_info_dist, LV_ALIGN_CENTER, 0, -24);
        lv_label_set_text(convoy_info_dir, convoy_wait_line);
        lv_obj_set_style_text_color(convoy_info_dir, lv_color_hex(0x6B8595), 0);
        lv_obj_align(convoy_info_dir, LV_ALIGN_CENTER, 0, 10);
        lv_label_set_text(convoy_info_cta, convoy_wait_cta);
        lv_obj_clear_flag(convoy_info_cta, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(convoy_scale_lbl, "");
        convoy_render_link();
        for (int i = 0; i < CONVOY_MAX_CARS; i++) {
            lv_obj_add_flag(convoy_cars[i].dot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(convoy_cars[i].lbl, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    // Live source with a fix — restore the normal card layout.
    convoy_waiting = false;
    lv_obj_clear_flag(convoy_gps_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(convoy_count_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(convoy_info_cta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(convoy_info_dist, LV_ALIGN_CENTER, 0, -2);
    lv_obj_align(convoy_info_dir, LV_ALIGN_BOTTOM_MID, 0, -8);

    // Auto-scale to farthest visible car.
    double maxd = 0;
    for (int i = 0; i < convoy_count; i++) {
        if (!(convoy_cars[i].online && convoy_cars[i].has_fix)) continue;
        double d = convoy_dist_m(convoy_self_lat, convoy_self_lon,
                                 convoy_cars[i].lat, convoy_cars[i].lon);
        if (d > maxd) maxd = d;
    }
    double scale = convoy_nice_scale(maxd);
    char sbuf[16]; convoy_fmt_dist(sbuf, sizeof(sbuf), scale);
    lv_label_set_text(convoy_scale_lbl, sbuf);

    int   nearest = -1;
    double nd = 1e18;
    for (int i = 0; i < CONVOY_MAX_CARS; i++) {
        if (i >= convoy_count || !(convoy_cars[i].online && convoy_cars[i].has_fix)) {
            lv_obj_add_flag(convoy_cars[i].dot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(convoy_cars[i].lbl, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        double d = convoy_dist_m(convoy_self_lat, convoy_self_lon,
                                 convoy_cars[i].lat, convoy_cars[i].lon);
        double b = convoy_bearing_deg(convoy_self_lat, convoy_self_lon,
                                      convoy_cars[i].lat, convoy_cars[i].lon);
        double rel = b - hdg;                     // relative to our heading (up)
        double rpx = d / scale * CV_R;
        if (rpx > CV_R) rpx = CV_R;               // clamp beyond-range cars to edge
        int dx = (int)lround(rpx * sin(cv_d2r(rel)));
        int dy = (int)lround(-rpx * cos(cv_d2r(rel)));

        lv_obj_set_style_bg_color(convoy_cars[i].dot, convoy_cars[i].color, 0);
        lv_obj_align(convoy_cars[i].dot, LV_ALIGN_CENTER, dx, dy);
        lv_obj_set_style_text_color(convoy_cars[i].lbl, convoy_cars[i].color, 0);
        lv_label_set_text(convoy_cars[i].lbl, convoy_cars[i].name);
        lv_obj_align(convoy_cars[i].lbl, LV_ALIGN_CENTER, dx, dy - 18);
        lv_obj_clear_flag(convoy_cars[i].dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(convoy_cars[i].lbl, LV_OBJ_FLAG_HIDDEN);

        if (d < nd) { nd = d; nearest = i; }
    }

    if (nearest >= 0) {
        double b = convoy_bearing_deg(convoy_self_lat, convoy_self_lon,
                                      convoy_cars[nearest].lat, convoy_cars[nearest].lon);
        char dbuf[16]; convoy_fmt_dist(dbuf, sizeof(dbuf), nd);
        lv_label_set_text(convoy_info_dist, dbuf);
        lv_obj_set_style_text_color(convoy_info_dist, lv_color_hex(0xFFFFFF), 0);
        // "C4    W    NEAREST" — callsign recoloured to the car's own hue.
        // (Montserrat subset is ASCII-only, so separators are spacing, not dots.)
        uint32_t hex = lv_color_to32(convoy_cars[nearest].color) & 0xFFFFFF;
        lv_label_set_text_fmt(convoy_info_dir, "#%06X %s#    %s    NEAREST",
                              (unsigned)hex, convoy_cars[nearest].name, convoy_compass(b));
    } else {
        // Solo with a fix (e.g. field bring-up, one T-Beam) — show our own live
        // coordinates as confirmation the GPS fix landed.
        lv_label_set_text(convoy_info_dist, "SOLO");
        lv_obj_set_style_text_color(convoy_info_dist, lv_color_hex(0x6B8595), 0);

        // Coordinates are formatted by hand with integer maths, NOT "%.5f".
        // lv_label_set_text_fmt() routes through LVGL's own lv_snprintf, which
        // does not implement %f (and lv_conf.h sets LV_USE_FLOAT 0). The
        // specifier was silently dropped and the literal 'f' survived, so this
        // line rendered as "f, f" instead of a position. Using the C library's
        // snprintf on a pre-split value keeps the fix local — flipping
        // LV_USE_FLOAT would change float handling across the whole UI.
        char cbuf[48];
        const long lat5 = (long)labs(lround(convoy_self_lat * 100000.0));
        const long lon5 = (long)labs(lround(convoy_self_lon * 100000.0));
        snprintf(cbuf, sizeof(cbuf), "%s%ld.%05ld, %s%ld.%05ld",
                 convoy_self_lat < 0 ? "-" : "", lat5 / 100000, lat5 % 100000,
                 convoy_self_lon < 0 ? "-" : "", lon5 / 100000, lon5 % 100000);
        lv_label_set_text(convoy_info_dir, cbuf);
    }
}

// Show/hide the link-status overlay ("STARTING RADIO", "CONNECTING", …).
// Pass NULL/empty to hide it (link is up). Safe before the screen is built.
static inline void convoy_set_status(const char *msg) {
    if (!convoy_status_lbl) return;
    if (msg && msg[0]) {
        lv_label_set_text(convoy_status_lbl, msg);
        lv_obj_align(convoy_status_lbl, LV_ALIGN_CENTER, 0, -70);
        lv_obj_clear_flag(convoy_status_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(convoy_status_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

// Customise the waiting-panel text — e.g. phone mode shows "Connect via browser"
// + the short URL. Pass NULL to restore the defaults.
static inline void convoy_set_wait_text(const char *line, const char *cta) {
    convoy_wait_line = line ? line : "Waiting for Mesh/Phone";
    convoy_wait_cta  = cta  ? cta  : "Hold for settings";
    if (convoy_info_dir) lv_label_set_text(convoy_info_dir, convoy_wait_line);
    if (convoy_info_cta) lv_label_set_text(convoy_info_cta, convoy_wait_cta);
}

// ── Setters the data source (firmware BLE / sim mock) calls ──────────────────
// Report the live link: source name ("MESH"/"PHONE"/"WIFI"), room code (cloud
// only, may be NULL/empty), and whether it is actually carrying data.
//
// Until now the card said nothing about WHERE positions came from. Mesh, phone
// and cloud all render an identical radar, so "I cannot see anyone" gave no clue
// whether the wrong source was selected, the room code was wrong, or the link
// was simply down — which cost a debugging session on 2026-08-12.
static inline void convoy_set_link(const char *src, const char *room, bool ok) {
    snprintf(convoy_link_src,  sizeof(convoy_link_src),  "%s", src  ? src  : "");
    snprintf(convoy_link_room, sizeof(convoy_link_room), "%s", room ? room : "");
    convoy_link_ok = ok;
}

// Our own callsign, as configured on the device / assigned by the app. Falls
// back to "ME" when unset.
static inline void convoy_set_self_name(const char *name) {
    snprintf(convoy_self_name, sizeof(convoy_self_name), "%s",
             (name && name[0]) ? name : "ME");
    if (convoy_me_lbl) lv_label_set_text(convoy_me_lbl, convoy_self_name);
}

static inline void convoy_set_self(double lat, double lon, bool fix) {
    convoy_self_lat = lat; convoy_self_lon = lon; convoy_self_fix = fix;
}

// Satellites in the solution, for the status chip. Pass -1 when the source has
// no idea (mesh/phone), which renders a plain "GPS" instead of a count.
static inline void convoy_set_sats(int n) { convoy_sats = n; }

// Feed our heading (deg from true north, e.g. GPS course-over-ground). Pass
// valid=false when stopped / COG unreliable so the scope holds north-up instead
// of spinning on noise. Smoothed with a shortest-angle low-pass to damp jitter.
static inline void convoy_set_heading(double deg, bool valid) {
    if (!valid) { convoy_hdg_valid = false; return; }
    while (deg < 0)   deg += 360;
    while (deg >= 360) deg -= 360;
    if (!convoy_hdg_valid) { convoy_self_hdg = deg; convoy_hdg_valid = true; return; }
    double diff = deg - convoy_self_hdg;
    while (diff > 180)  diff -= 360;
    while (diff < -180) diff += 360;
    convoy_self_hdg += diff * 0.25;               // low-pass toward new heading
    while (convoy_self_hdg < 0)    convoy_self_hdg += 360;
    while (convoy_self_hdg >= 360) convoy_self_hdg -= 360;
}
static inline void convoy_set_car(int i, const char *name, double lat, double lon,
                                  lv_color_t color, bool online, bool has_fix) {
    if (i < 0 || i >= CONVOY_MAX_CARS) return;
    strncpy(convoy_cars[i].name, name, sizeof(convoy_cars[i].name) - 1);
    convoy_cars[i].name[sizeof(convoy_cars[i].name) - 1] = 0;
    convoy_cars[i].lat = lat; convoy_cars[i].lon = lon;
    convoy_cars[i].color = color;
    convoy_cars[i].online = online; convoy_cars[i].has_fix = has_fix;
    if (i + 1 > convoy_count) convoy_count = i + 1;
}

#endif // CONVOY_UI_H
