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
static double       convoy_self_hdg  = 0;      // smoothed heading, deg from true N
static bool         convoy_hdg_valid = false;  // false ⇒ hold north-up (stopped)
static bool         convoy_north_up  = false;  // user-chosen orientation lock

static lv_obj_t *convoy_screen   = NULL;
static lv_obj_t *convoy_self_dot = NULL;
static lv_obj_t *convoy_pulse    = NULL;
static lv_obj_t *convoy_cmark[4] = { NULL, NULL, NULL, NULL }; // N,E,S,W ring marks
static lv_obj_t *convoy_hdg_lbl  = NULL;    // top heading readout = orientation toggle
static lv_obj_t *convoy_lubber   = NULL;    // forward line (heading-up only)
static lv_obj_t *convoy_fwd      = NULL;    // forward chevron (heading-up only)
static lv_obj_t *convoy_card     = NULL;    // floating metadata panel (lower third)
static lv_obj_t *convoy_hint     = NULL;    // "tap for details" cue when card hidden
static lv_obj_t *convoy_gps_lbl  = NULL;    // GPS status chip (card, top-left)
static lv_obj_t *convoy_count_lbl = NULL;   // "N CARS" (card, top-right)
static lv_obj_t *convoy_scale_lbl = NULL;   // outer-ring range tag (radar NE)
static lv_obj_t *convoy_info_dist = NULL;   // nearest car distance (hero)
static lv_obj_t *convoy_info_dir  = NULL;   // nearest: callsign · bearing · NEAREST
static lv_obj_t *convoy_status_lbl = NULL;  // link-status overlay ("CONNECTING")
static bool      convoy_card_shown = true;

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

static double convoy_nice_scale(double maxm) {
    static const double steps[] = { 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
        if (maxm <= steps[i]) return steps[i];
    return 50000;
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
static void convoy_toggle_card(lv_event_t *e) {
    (void)e;
    convoy_card_shown = !convoy_card_shown;
    if (convoy_card_shown) {
        lv_obj_clear_flag(convoy_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(convoy_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(convoy_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(convoy_hint, LV_OBJ_FLAG_HIDDEN);
    }
}

// Orientation toggle, driven by tapping the top readout button. Heading-up shows
// the forward line + chevron and a live heading; north-up hides them and grows
// the N mark into a big anchor. The button stays put in both modes so a second
// tap flips back.
static void convoy_refresh(void);  // defined below
static void convoy_apply_orientation(void) {
    if (convoy_north_up) {
        lv_obj_add_flag(convoy_lubber, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(convoy_fwd,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(convoy_cmark[0], &lv_font_montserrat_28, 0);
    } else {
        lv_obj_clear_flag(convoy_lubber, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(convoy_fwd,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(convoy_cmark[0], &lv_font_montserrat_20, 0);
    }
    convoy_refresh();
}
static void convoy_toggle_orientation(lv_event_t *e) {
    (void)e;
    convoy_north_up = !convoy_north_up;
    convoy_apply_orientation();
}

// ── Build the screen (call once) ────────────────────────────────────────────
static lv_obj_t *convoy_build_screen(void) {
    if (convoy_screen) return convoy_screen;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, 466, 466);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x05080D), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    convoy_screen = scr;

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
        lv_obj_set_style_border_color(ring, lv_color_hex(0x1E3A4C), 0);
        lv_obj_set_style_border_width(ring, i == 3 ? 2 : 1, 0);
        lv_obj_set_style_border_opa(ring, i == 3 ? LV_OPA_80 : LV_OPA_50, 0);
        lv_obj_align(ring, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    // Rotating compass ring: N/E/S/W marks are repositioned every refresh by
    // −heading, so they swing around as we turn (heading-up). N is the bold
    // orange one (grows big in north-up mode); E/S/W dim. Positions set in refresh.
    const char *cmk[4] = { "N", "E", "S", "W" };
    for (int i = 0; i < 4; i++) {
        convoy_cmark[i] = cv_label(scr,
            i == 0 ? &lv_font_montserrat_20 : &lv_font_montserrat_14,
            i == 0 ? CV_ORANGE : lv_color_hex(0x4A6472));
        lv_label_set_text(convoy_cmark[i], cmk[i]);
        lv_obj_align(convoy_cmark[i], LV_ALIGN_CENTER, 0, -(CV_R - 14));
    }

    // Forward reference: a lubber line straight up from the centre + a chevron.
    // Shown only in heading-up; "up" is always our direction of travel.
    convoy_lubber = lv_obj_create(scr);
    lv_obj_remove_style_all(convoy_lubber);
    lv_obj_set_size(convoy_lubber, 2, CV_R - CV_SELF);
    lv_obj_set_style_bg_color(convoy_lubber, CV_ORANGE, 0);
    lv_obj_set_style_bg_opa(convoy_lubber, LV_OPA_30, 0);
    lv_obj_align(convoy_lubber, LV_ALIGN_CENTER, 0, -(CV_R + CV_SELF) / 2);
    lv_obj_clear_flag(convoy_lubber, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    convoy_fwd = cv_label(scr, &lv_font_montserrat_20, CV_ORANGE);
    lv_label_set_text(convoy_fwd, LV_SYMBOL_UP);
    lv_obj_align(convoy_fwd, LV_ALIGN_CENTER, 0, -(CV_R + 8));

    // Top heading readout — this is the orientation toggle BUTTON. Styled as a
    // subtle pill so it reads as tappable; a tap flips heading-up ⇄ north-up.
    convoy_hdg_lbl = cv_label(scr, &lv_font_montserrat_16, lv_color_hex(0xDDE6EC));
    lv_label_set_text(convoy_hdg_lbl, "");
    lv_obj_set_style_bg_color(convoy_hdg_lbl, lv_color_hex(0x14202B), 0);
    lv_obj_set_style_bg_opa(convoy_hdg_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(convoy_hdg_lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(convoy_hdg_lbl, CV_ORANGE, 0);
    lv_obj_set_style_border_width(convoy_hdg_lbl, 1, 0);
    lv_obj_set_style_border_opa(convoy_hdg_lbl, LV_OPA_50, 0);
    lv_obj_set_style_pad_hor(convoy_hdg_lbl, 12, 0);
    lv_obj_set_style_pad_ver(convoy_hdg_lbl, 4, 0);
    lv_obj_align(convoy_hdg_lbl, LV_ALIGN_CENTER, 0, -(CV_R + 30));
    lv_obj_add_flag(convoy_hdg_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(convoy_hdg_lbl, 14);
    lv_obj_add_event_cb(convoy_hdg_lbl, convoy_toggle_orientation, LV_EVENT_CLICKED, NULL);

    // Scale tag (outer ring range) — rides the outer ring on the NE diagonal so
    // it stays visible above the floating card and clear of the centre.
    convoy_scale_lbl = cv_label(scr, &lv_font_montserrat_14, lv_color_hex(0x4E6675));
    lv_label_set_text(convoy_scale_lbl, "");
    lv_obj_align(convoy_scale_lbl, LV_ALIGN_CENTER, 116, -112);

    // Own-car marker: pulse ring + orange dot + up-chevron (points forward).
    convoy_pulse = lv_obj_create(scr);
    lv_obj_remove_style_all(convoy_pulse);
    lv_obj_set_size(convoy_pulse, CV_SELF, CV_SELF);
    lv_obj_set_style_radius(convoy_pulse, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(convoy_pulse, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(convoy_pulse, CV_ORANGE, 0);
    lv_obj_set_style_border_width(convoy_pulse, 2, 0);
    lv_obj_align(convoy_pulse, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(convoy_pulse, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    convoy_self_dot = cv_dot(scr, CV_SELF, CV_ORANGE);
    lv_obj_align(convoy_self_dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *me = cv_label(scr, &lv_font_montserrat_14, CV_ORANGE);
    lv_label_set_text(me, "ME");
    lv_obj_align(me, LV_ALIGN_CENTER, 0, -22);

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
        convoy_cars[i].lbl = cv_label(scr, &lv_font_montserrat_20, lv_color_hex(0xFFFFFF));
        lv_obj_add_flag(convoy_cars[i].dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(convoy_cars[i].lbl, LV_OBJ_FLAG_HIDDEN);
    }

    // ── Floating metadata card (lower third, tap to dismiss) ────────────────
    // A translucent rounded panel sized so its lower corners tuck inside the
    // round bezel; single tap hides it to reveal the whole scope.
    convoy_card = lv_obj_create(scr);
    lv_obj_remove_style_all(convoy_card);
    lv_obj_set_size(convoy_card, 280, 100);
    lv_obj_set_style_radius(convoy_card, 22, 0);
    lv_obj_set_style_bg_color(convoy_card, lv_color_hex(0x0A0F16), 0);
    lv_obj_set_style_bg_opa(convoy_card, LV_OPA_90, 0);
    lv_obj_set_style_border_color(convoy_card, lv_color_hex(0x1E3A4C), 0);
    lv_obj_set_style_border_width(convoy_card, 1, 0);
    lv_obj_set_style_border_opa(convoy_card, LV_OPA_70, 0);
    lv_obj_align(convoy_card, LV_ALIGN_BOTTOM_MID, 0, -58);
    lv_obj_clear_flag(convoy_card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Status row inside the card: GPS state (left) + online count (right).
    convoy_gps_lbl = cv_label(convoy_card, &lv_font_montserrat_16, lv_color_hex(0x00E676));
    lv_label_set_text(convoy_gps_lbl, LV_SYMBOL_GPS " GPS");
    lv_obj_align(convoy_gps_lbl, LV_ALIGN_TOP_LEFT, 8, 6);

    convoy_count_lbl = cv_label(convoy_card, &lv_font_montserrat_16, lv_color_hex(0x6B8595));
    lv_label_set_text(convoy_count_lbl, "");
    lv_obj_align(convoy_count_lbl, LV_ALIGN_TOP_RIGHT, -8, 6);

    // Hero distance + callsign/bearing line.
    convoy_info_dist = cv_label(convoy_card, &lv_font_montserrat_28, lv_color_hex(0xFFFFFF));
    lv_label_set_text(convoy_info_dist, "");
    lv_obj_align(convoy_info_dist, LV_ALIGN_CENTER, 0, 6);
    convoy_info_dir = cv_label(convoy_card, &lv_font_montserrat_16, lv_color_hex(0x6B8595));
    lv_label_set_recolor(convoy_info_dir, true);
    lv_label_set_text(convoy_info_dir, "");
    lv_obj_align(convoy_info_dir, LV_ALIGN_BOTTOM_MID, 0, -6);

    // Dismissed-state cue.
    convoy_hint = cv_label(scr, &lv_font_montserrat_14, lv_color_hex(0x4E6675));
    lv_label_set_text(convoy_hint, LV_SYMBOL_UP " TAP FOR DETAILS");
    lv_obj_align(convoy_hint, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_flag(convoy_hint, LV_OBJ_FLAG_HIDDEN);

    // Transparent full-screen tap catcher (bottom of the interactive layer):
    // a tap that misses the specific targets below toggles the metadata card.
    lv_obj_t *tap = lv_obj_create(scr);
    lv_obj_remove_style_all(tap);
    lv_obj_set_size(tap, 466, 466);
    lv_obj_align(tap, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(tap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tap, convoy_toggle_card, LV_EVENT_CLICKED, NULL);

    // Keep the orientation button above the catcher so it gets its own taps
    // (taps elsewhere fall through to the catcher and toggle the metadata card).
    lv_obj_move_foreground(convoy_hdg_lbl);

    // Link-status overlay (e.g. "STARTING RADIO" / "CONNECTING") shown while the
    // BLE link is coming up; hidden once data streams. On top of everything.
    convoy_status_lbl = cv_label(scr, &lv_font_montserrat_20, CV_ORANGE);
    lv_label_set_text(convoy_status_lbl, "");
    lv_obj_set_style_bg_color(convoy_status_lbl, lv_color_hex(0x0A0F16), 0);
    lv_obj_set_style_bg_opa(convoy_status_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(convoy_status_lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(convoy_status_lbl, 16, 0);
    lv_obj_set_style_pad_ver(convoy_status_lbl, 8, 0);
    lv_obj_align(convoy_status_lbl, LV_ALIGN_CENTER, 0, -70);
    lv_obj_add_flag(convoy_status_lbl, LV_OBJ_FLAG_HIDDEN);

    convoy_apply_orientation();   // start in heading-up (line shown, N normal)
    return scr;
}

// ── Recompute + redraw from current state ───────────────────────────────────
static void convoy_refresh(void) {
    if (!convoy_screen) return;

    int online = 0;
    for (int i = 0; i < convoy_count; i++)
        if (convoy_cars[i].online && convoy_cars[i].has_fix) online++;
    lv_label_set_text_fmt(convoy_count_lbl, "%d CARS", online + (convoy_self_fix ? 1 : 0));

    // GPS status chip — always visible; green with fix, orange while acquiring.
    if (convoy_self_fix) {
        lv_obj_set_style_text_color(convoy_gps_lbl, lv_color_hex(0x00E676), 0);
        lv_label_set_text(convoy_gps_lbl, LV_SYMBOL_GPS " GPS");
    } else {
        lv_obj_set_style_text_color(convoy_gps_lbl, CV_ORANGE, 0);
        lv_label_set_text(convoy_gps_lbl, LV_SYMBOL_GPS " NO FIX");
    }

    // Orientation: heading-up rotates everything by −heading ("up" = our travel
    // direction); north-up locks hdg = 0. We also hold north-up when heading is
    // invalid (stopped / no COG) so the scope doesn't spin on GPS noise.
    double hdg = (convoy_north_up || !convoy_hdg_valid) ? 0.0 : convoy_self_hdg;
    static const int cmk_abs[4] = { 0, 90, 180, 270 };   // N,E,S,W true bearings
    for (int i = 0; i < 4; i++) {
        double sa = cmk_abs[i] - hdg;                     // screen angle, 0 = up
        int r = CV_R - 14;
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
        lv_label_set_text(convoy_info_dist, "ACQUIRING");
        lv_obj_set_style_text_color(convoy_info_dist, CV_ORANGE, 0);
        // Live feedback while waiting for the T-Beam GPS fix: rx = own-position
        // packets received over BLE (climbs even at 0,0, so you can see the link
        // is alive); flips to real coordinates the moment the fix lands.
        lv_label_set_text_fmt(convoy_info_dir, "waiting for GPS   rx %d", convoy_self_updates);
        lv_label_set_text(convoy_scale_lbl, "");
        for (int i = 0; i < CONVOY_MAX_CARS; i++) {
            lv_obj_add_flag(convoy_cars[i].dot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(convoy_cars[i].lbl, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

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
        lv_label_set_text_fmt(convoy_info_dir, "%.5f, %.5f", convoy_self_lat, convoy_self_lon);
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

// ── Setters the data source (firmware BLE / sim mock) calls ──────────────────
static inline void convoy_set_self(double lat, double lon, bool fix) {
    convoy_self_lat = lat; convoy_self_lon = lon; convoy_self_fix = fix;
}

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
