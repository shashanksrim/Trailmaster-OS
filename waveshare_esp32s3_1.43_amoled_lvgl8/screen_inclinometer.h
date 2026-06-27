#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include "qmi8658c.h"

#define IMU_ALPHA       0.80f

extern bool imu_ready;
extern float raw_imu_pitch, raw_imu_roll;   // filtered IMU angles (degrees)
extern float imu_pitch_offset, imu_roll_offset; // zeroing offsets captured at reset
extern unsigned long last_imu_time;
extern int imu_settle_count;
extern uint32_t screen_load_time;
extern bool auto_reset_triggered;

extern "C" {
    extern lv_obj_t * ui_uiinclinometer;
    extern lv_obj_t * ui_uiimgRoll, * ui_uiimgPitch;
    extern lv_obj_t * ui_CarRollImg, * ui_CarPitchImg;
}

// ── arc gauge widget handles ──────────────────────────────────────────────────
static lv_obj_t * g_left_arc  = NULL;
static lv_obj_t * g_right_arc = NULL;
// moving pointer: two lv_line segments each forming > or <
static lv_obj_t * g_lp1 = NULL, * g_lp2 = NULL;   // left  pointer lines
static lv_obj_t * g_rp1 = NULL, * g_rp2 = NULL;   // right pointer lines
// static centre markers (blue) at arc midpoints
static lv_obj_t * g_lc1 = NULL, * g_lc2 = NULL;
static lv_obj_t * g_rc1 = NULL, * g_rc2 = NULL;
// line point storage (lv_line needs persistent points)
static lv_point_t g_lp1pts[2], g_lp2pts[2];
static lv_point_t g_rp1pts[2], g_rp2pts[2];

#define ARC_R    210
#define PTR_R    197   // pointer tip radius from centre
#define LABEL_R  176   // tick label radius (adjusted slightly inward for +10% visual spacing gap)
#define SCR_CX   233
#define SCR_CY   233
#define SAFE_DEG 35.0f
#define MAX_DEG  40.0f
// arc spans – ±50° each side → 100° total per arc
#define ARC_HALF 50.0f  // degrees of arc from centre to end

static inline lv_color_t danger_col(float a) {
    if (a > SAFE_DEG) a = SAFE_DEG;
    uint8_t gb = (uint8_t)(255.0f * (1.0f - a / SAFE_DEG));
    return lv_color_make(255, gb, gb);
}

// Helper: create a styled lv_line and set persistent points
static inline lv_obj_t * make_line(lv_obj_t * parent, lv_color_t col, int w) {
    lv_obj_t * ln = lv_line_create(parent);
    lv_obj_set_style_line_color(ln, col, 0);
    lv_obj_set_style_line_width(ln, w, 0);
    lv_obj_set_style_line_rounded(ln, true, 0);
    return ln;
}

// Position a ">" (pointing right) arrow centred at (cx,cy)
static inline void set_right_arrow(lv_obj_t * l1, lv_point_t * p1,
                                   lv_obj_t * l2, lv_point_t * p2,
                                   int cx, int cy) {
    int H = 9, W = 8;
    p1[0] = {(lv_coord_t)(cx - W), (lv_coord_t)(cy - H)};
    p1[1] = {(lv_coord_t)(cx + W), (lv_coord_t)(cy)};
    p2[0] = {(lv_coord_t)(cx + W), (lv_coord_t)(cy)};
    p2[1] = {(lv_coord_t)(cx - W), (lv_coord_t)(cy + H)};
    lv_line_set_points(l1, p1, 2);
    lv_line_set_points(l2, p2, 2);
}

// Position a "<" (pointing left) arrow centred at (cx,cy)
static inline void set_left_arrow(lv_obj_t * l1, lv_point_t * p1,
                                  lv_obj_t * l2, lv_point_t * p2,
                                  int cx, int cy) {
    int H = 9, W = 8;
    p1[0] = {(lv_coord_t)(cx + W), (lv_coord_t)(cy - H)};
    p1[1] = {(lv_coord_t)(cx - W), (lv_coord_t)(cy)};
    p2[0] = {(lv_coord_t)(cx - W), (lv_coord_t)(cy)};
    p2[1] = {(lv_coord_t)(cx + W), (lv_coord_t)(cy + H)};
    lv_line_set_points(l1, p1, 2);
    lv_line_set_points(l2, p2, 2);
}

static inline void build_roll_arcs(lv_obj_t * parent) {
    const float DEG = 3.14159265f / 180.0f;
    int sz = ARC_R * 2 + 4;

    auto make_arc = [&](int bg_start, int bg_end) -> lv_obj_t * {
        lv_obj_t * a = lv_arc_create(parent);
        lv_obj_set_size(a, sz, sz);
        lv_obj_align(a, LV_ALIGN_CENTER, 0, 0);
        lv_arc_set_bg_angles(a, bg_start, bg_end);
        lv_arc_set_range(a, -40, 40);
        lv_arc_set_value(a, 0);
        lv_arc_set_mode(a, LV_ARC_MODE_SYMMETRICAL);
        lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_color(a, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
        lv_obj_set_style_arc_width(a, 11, LV_PART_MAIN);
        lv_obj_set_style_arc_color(a, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(a, 12, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_border_width(a, 0, LV_PART_KNOB);
        lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
        return a;
    };

    // Left  arc: 130°–230° centred at 180°
    g_left_arc  = make_arc(130, 230);
    // Right arc: 310°–50°  centred at 0°/360°
    g_right_arc = make_arc(310, 50);

    // ── Moving pointer lines (orange > on left, < on right) ─────────────────
    lv_color_t orange = lv_color_hex(0xFFAA00);
    g_lp1 = make_line(parent, orange, 3);
    g_lp2 = make_line(parent, orange, 3);
    g_rp1 = make_line(parent, orange, 3);
    g_rp2 = make_line(parent, orange, 3);
    // initialise at zero position
    set_right_arrow(g_lp1, g_lp1pts, g_lp2, g_lp2pts, SCR_CX - PTR_R, SCR_CY);
    set_left_arrow (g_rp1, g_rp1pts, g_rp2, g_rp2pts, SCR_CX + PTR_R, SCR_CY);

    // ── Static grey centre markers (< pointing right on left, > on right) ────
    lv_color_t blue = lv_color_hex(0x888888); // Changed from blue to steel grey
    g_lc1 = make_line(parent, blue, 2);
    g_lc2 = make_line(parent, blue, 2);
    g_rc1 = make_line(parent, blue, 2);
    g_rc2 = make_line(parent, blue, 2);
    static lv_point_t lc1p[2], lc2p[2], rc1p[2], rc2p[2];
    set_right_arrow(g_lc1, lc1p, g_lc2, lc2p, SCR_CX - PTR_R + 12, SCR_CY);
    set_left_arrow (g_rc1, rc1p, g_rc2, rc2p, SCR_CX + PTR_R - 12, SCR_CY);

    // ── Tick labels ──────────────────────────────────────────────────────────
    // Left arc:  screen_angle = 180° - roll * (ARC_HALF/MAX_DEG)
    // Right arc: screen_angle =   0° + roll * (ARC_HALF/MAX_DEG)  BUT mirrored
    const int ticks[] = {-40, -30, -20, -10, 10, 20, 30, 40};
    float scale = ARC_HALF / MAX_DEG;  // 50/40 = 1.25°/° 
    for (int i = 0; i < 8; i++) {
        int r = ticks[i];
        bool danger = (abs(r) >= 30);
        lv_color_t col = danger ? lv_color_hex(0xFF3300) : lv_color_hex(0xBBBBBB);

        float la_r = (180.0f - (float)r * scale) * DEG;
        int lx = SCR_CX + (int)(LABEL_R * cosf(la_r)) - 7;
        int ly = SCR_CY + (int)(LABEL_R * sinf(la_r)) - 7;
        lv_obj_t * lt = lv_label_create(parent);
        lv_label_set_text_fmt(lt, "%d", abs(r));
        lv_obj_set_style_text_color(lt, col, 0);
        lv_obj_set_style_text_font(lt, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(lt, lx, ly);

        // Right arc mirrors: use negative angle so it goes opposite
        float ra_r = (-(float)r * scale) * DEG;
        int rx = SCR_CX + (int)(LABEL_R * cosf(ra_r)) - 7;
        int ry = SCR_CY + (int)(LABEL_R * sinf(ra_r)) - 7;
        lv_obj_t * rt = lv_label_create(parent);
        lv_label_set_text_fmt(rt, "%d", abs(r));
        lv_obj_set_style_text_color(rt, col, 0);
        lv_obj_set_style_text_font(rt, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(rt, rx, ry);
    }
}

static inline void update_arc_gauges(float fast_roll_val) {
    if (!g_left_arc) return;

    float c = fast_roll_val;
    if (c >  MAX_DEG) c =  MAX_DEG;
    if (c < -MAX_DEG) c = -MAX_DEG;

    // Both arcs use -c: because arc spans are MIRRORED, same sign gives
    // inverse screen direction — left fills UP while right fills DOWN (roll effect)
    lv_arc_set_value(g_left_arc,  (int)-c);
    lv_arc_set_value(g_right_arc, (int)-c);

    lv_color_t col = danger_col(fabsf(c));
    lv_obj_set_style_arc_color(g_left_arc,  col, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_right_arc, col, LV_PART_INDICATOR);

    const float DEG = 3.14159265f / 180.0f;
    float scale = ARC_HALF / MAX_DEG;

    // Left  pointer tracks arc: angle = 180° - c*scale
    // (negative c → angle > 180° → sin negative → pointer goes UP on screen)
    float la = (180.0f - c * scale) * DEG;
    int lx = SCR_CX + (int)(PTR_R * cosf(la));
    int ly = SCR_CY + (int)(PTR_R * sinf(la));
    set_right_arrow(g_lp1, g_lp1pts, g_lp2, g_lp2pts, lx, ly);

    // Right pointer must move OPPOSITE to left: use -c so when left goes up, right goes down
    float ra = (-c * scale) * DEG;
    int rx = SCR_CX + (int)(PTR_R * cosf(ra));
    int ry = SCR_CY + (int)(PTR_R * sinf(ra));
    set_left_arrow(g_rp1, g_rp1pts, g_rp2, g_rp2pts, rx, ry);

    lv_color_t ptr_col = (fabsf(c) >= SAFE_DEG) ? lv_color_hex(0xFF0000) : lv_color_hex(0xFFAA00);
    lv_obj_set_style_line_color(g_lp1, ptr_col, 0);
    lv_obj_set_style_line_color(g_lp2, ptr_col, 0);
    lv_obj_set_style_line_color(g_rp1, ptr_col, 0);
    lv_obj_set_style_line_color(g_rp2, ptr_col, 0);
}

// ── IMU math ─────────────────────────────────────────────────────────────────
// Axis convention (hardware-verified):
//   raw_imu_pitch : nose-up = positive  (uses ax vs ay/az plane – DECOUPLED from roll)
//   raw_imu_roll  : right-side-up = positive (uses ay vs az – DECOUPLED from pitch)
inline void update_imu_math() {
    if (!imu_ready) return;
    float accel[3] = {}, gyro[3] = {};
    qmi8658_read_xyz(accel, gyro);
    unsigned long now = millis();
    float dt = (now - last_imu_time) / 1000.0f;
    if (dt > 0.1f || dt <= 0.0f) dt = 0.01f;
    last_imu_time = now;

    // Gyro dead-zone: suppress noise below 0.05 rad/s
    if (fabsf(gyro[0]) < 0.05f) gyro[0] = 0.0f;
    if (fabsf(gyro[1]) < 0.05f) gyro[1] = 0.0f;

    // ── Decoupled accelerometer reference angles ──────────────────────────────
    // Pitch: accel[1] vs az responds to physical nose-up/down motion (DECOUPLED from roll)
    float accel_pitch = atan2f( accel[1], accel[2]) * 180.0f / PI;
    // Roll:  accel[0] vs sqrt(ay²+az²) responds to physical side-tilt motion (DECOUPLED from pitch)
    float accel_roll  = atan2f(-accel[0], sqrtf(accel[1]*accel[1] + accel[2]*accel[2])) * 180.0f / PI;

    if (imu_settle_count > 0) {
        // During settle: slam directly to accelerometer — no gyro integration yet
        raw_imu_pitch = accel_pitch;
        raw_imu_roll  = accel_roll;
        imu_settle_count--;
    } else {
        // Complementary filter: integrate gyro then correct with accel
        // gyro[0] → pitch rate,  gyro[1] → roll rate (hardware-verified axis mapping)
        raw_imu_pitch += gyro[0] * dt;
        raw_imu_roll  -= gyro[1] * dt;
        // Unwrap before blending
        if (raw_imu_pitch - accel_pitch >  180.0f) raw_imu_pitch -= 360.0f;
        else if (raw_imu_pitch - accel_pitch < -180.0f) raw_imu_pitch += 360.0f;
        if (raw_imu_roll  - accel_roll  >  180.0f) raw_imu_roll  -= 360.0f;
        else if (raw_imu_roll  - accel_roll  < -180.0f) raw_imu_roll  += 360.0f;
        // Blend: 80% gyro-integrated, 20% accel correction
        raw_imu_pitch = IMU_ALPHA * raw_imu_pitch + (1.0f - IMU_ALPHA) * accel_pitch;
        raw_imu_roll  = IMU_ALPHA * raw_imu_roll  + (1.0f - IMU_ALPHA) * accel_roll;
    }
    // Keep in [-180, 180]
    while (raw_imu_pitch >  180.0f) raw_imu_pitch -= 360.0f;
    while (raw_imu_pitch < -180.0f) raw_imu_pitch += 360.0f;
    while (raw_imu_roll  >  180.0f) raw_imu_roll  -= 360.0f;
    while (raw_imu_roll  < -180.0f) raw_imu_roll  += 360.0f;
    if (isnan(raw_imu_pitch)||isinf(raw_imu_pitch)) raw_imu_pitch = 0.0f;
    if (isnan(raw_imu_roll) ||isinf(raw_imu_roll))  raw_imu_roll  = 0.0f;
}

// ── Main update (called every loop from main .ino) ────────────────────────────
inline void update_screen_inclinometer() {
    static uint32_t t = 0;
    if (millis() - t > 25) {
        update_imu_math();

        // Hardcoded 2500ms initialization phase
        uint32_t elapsed = millis() - screen_load_time;
        
        if (!auto_reset_triggered && elapsed > 2500) {
            imu_pitch_offset = raw_imu_pitch;
            imu_roll_offset  = raw_imu_roll;
            auto_reset_triggered = true;
            Serial.printf("[IMU] 2500ms reset: pitch_offset=%.1f  roll_offset=%.1f\n",
                          imu_pitch_offset, imu_roll_offset);
        }

        // ── Display angles (zeroed relative to capture point) ────────────────
        float disp_pitch = (raw_imu_pitch - imu_pitch_offset);  // +ve = nose up
        float disp_roll  = (raw_imu_roll  - imu_roll_offset);   // +ve = right side up
        while (disp_pitch >  180.0f) disp_pitch -= 360.0f;
        while (disp_pitch < -180.0f) disp_pitch += 360.0f;
        while (disp_roll  >  180.0f) disp_roll  -= 360.0f;
        while (disp_roll  < -180.0f) disp_roll  += 360.0f;

        if (elapsed <= 2500) {
            // Init phase: show text, freeze graphics at zero
            if (ui_uiimgRoll)  lv_label_set_text(ui_uiimgRoll,  "Init...");
            if (ui_uiimgPitch) lv_label_set_text(ui_uiimgPitch, "Init...");
            disp_roll  = 0.0f;
            disp_pitch = 0.0f;
        } else {
            if (ui_uiimgRoll)  lv_label_set_text_fmt(ui_uiimgRoll,  "%d°", (int)disp_roll);
            if (ui_uiimgPitch) lv_label_set_text_fmt(ui_uiimgPitch, "%d°", (int)disp_pitch);
        }

        // ── Rotate car graphics ───────────────────────────────────────────────
        if (ui_CarRollImg) {
            int16_t a = (int16_t)(((int32_t)(disp_roll * 10)) % 3600);
            if (a < 0) a += 3600;
            lv_img_set_angle(ui_CarRollImg, a);
        }
        if (ui_CarPitchImg) {
            int16_t a = (int16_t)(((int32_t)(disp_pitch * 10)) % 3600);
            if (a < 0) a += 3600;
            lv_img_set_angle(ui_CarPitchImg, a);
        }

        // ── Arc gauges track roll ─────────────────────────────────────────────
        update_arc_gauges(disp_roll);

        t = millis();
    }
}
