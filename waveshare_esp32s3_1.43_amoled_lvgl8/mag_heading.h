// Magnetometer -> compass heading. Raw axes are not a heading; two corrections
// stand between them, and both matter on this product.
//
// 1. TILT COMPENSATION. A flat atan2(y, x) is only correct while the sensor is
//    level. This is an off-road dash — it is level almost never, and the
//    inclinometer screen exists precisely because the car spends its time on
//    slopes. Uncompensated, pitching nose-up on a climb swings the heading by
//    tens of degrees, which is worse than no compass at all because it moves
//    when the car has not turned. We de-rotate using the pitch/roll the IMU
//    already produces (raw_imu_pitch / raw_imu_roll in the .ino).
//
// 2. DECLINATION. convoy_ui.h:28 places every blip by TRUE bearing, so feeding
//    it magnetic north would rotate the whole scope against the car positions.
//
// NOT here, by design (deferred): hard-iron calibration. The car is a steel box
// and an uncalibrated compass will carry a constant offset of tens of degrees.
// That offset is stable, so heading still tracks rotation correctly — which is
// what makes "raw first, calibrate later" a reasonable order. See
// mag_calibrate() below for the hook it will plug into.
#ifndef MAG_HEADING_H
#define MAG_HEADING_H

#include <math.h>
#include "mag_mmc5983.h"

// Pitch/roll in degrees, already filtered, from the IMU in the .ino.
extern float raw_imu_pitch, raw_imu_roll;

// ── Sensor axis mapping ─────────────────────────────────────────────────────
// The breakout hangs off the GPS extension header on a lead, so its axes are
// NOT known to line up with the board's. These three constants are the whole
// mapping; get them wrong and the heading turns the wrong way or is 90 deg out.
//
// Determine them empirically (see the rotation check in the plan): point the
// car north and confirm the heading reads ~0, then turn right 90 deg and
// confirm it climbs toward 90. If it counts DOWN, flip MAG_AXIS_Y_SIGN.
#define MAG_AXIS_X_SIGN   (+1.0f)
#define MAG_AXIS_Y_SIGN   (+1.0f)
#define MAG_AXIS_Z_SIGN   (+1.0f)

// Magnetic declination, degrees east of true north. Around Bengaluru this is
// roughly -1 deg (about 1 deg WEST), which is well inside the error of an
// uncalibrated compass — so this is a placeholder that becomes worth setting
// properly at the same time as hard-iron calibration.
//
// A full WMM model is deliberately NOT implemented: it is a large coefficient
// table to carry for a correction currently smaller than our error budget. The
// setter exists so a value can be supplied from the GPS fix later.
static float s_mag_declination_deg = -1.0f;
static inline void mag_set_declination(float deg) { s_mag_declination_deg = deg; }

// Hard-iron offsets, microtesla. Zero until calibration lands; subtracting zero
// is a no-op, so the pipeline below is already calibration-shaped.
static float s_mag_hi_x = 0.0f, s_mag_hi_y = 0.0f, s_mag_hi_z = 0.0f;
static inline void mag_calibrate(float x, float y, float z) {
    s_mag_hi_x = x; s_mag_hi_y = y; s_mag_hi_z = z;
}

// Tilt-compensated true heading, degrees clockwise from true north [0,360).
// Returns false when there is no usable sample, in which case *out is untouched.
static bool mag_heading_deg(float *out) {
    mag_sample_t m;
    mag_get(&m);
    if (!mag_is_alive()) return false;

    const float mx = (m.x_ut - s_mag_hi_x) * MAG_AXIS_X_SIGN;
    const float my = (m.y_ut - s_mag_hi_y) * MAG_AXIS_Y_SIGN;
    const float mz = (m.z_ut - s_mag_hi_z) * MAG_AXIS_Z_SIGN;

    // A near-zero field means a dead or saturated sensor; atan2 on noise would
    // hand back a confident-looking, meaningless bearing.
    const float mag_len = sqrtf(mx * mx + my * my + mz * mz);
    if (mag_len < 5.0f) return false;

    const float p = raw_imu_pitch * (float)M_PI / 180.0f;
    const float r = raw_imu_roll  * (float)M_PI / 180.0f;
    const float cp = cosf(p), sp = sinf(p);
    const float cr = cosf(r), sr = sinf(r);

    // Standard tilt compensation: rotate the field back into the horizontal
    // plane using pitch and roll, then take the bearing in that plane.
    const float xh = mx * cp + mz * sp;
    const float yh = mx * sr * sp + my * cr - mz * sr * cp;

    float deg = atan2f(-yh, xh) * 180.0f / (float)M_PI;
    deg += s_mag_declination_deg;                 // magnetic -> true
    while (deg < 0.0f)     deg += 360.0f;
    while (deg >= 360.0f)  deg -= 360.0f;
    *out = deg;
    return true;
}

// Field-debug tick: rotate the board and watch these. Two things to look for.
//
//  |B| should stay CONSTANT as you rotate — magnitude is rotation-invariant, so
//  a magnitude that swings while you turn means hard iron riding along with the
//  sensor (a magnet, a speaker, a DC trace), not a scale error. That is the one
//  check that separates "wrong maths" from "wrong environment", and it cannot be
//  done from a single sample.
//
//  hdg should sweep smoothly and wrap 359 -> 0 with no jump.
static void mag_debug_tick(void) {
    static uint32_t last = 0;
    if (millis() - last < 2000) return;
    last = millis();
    mag_sample_t m;
    mag_get(&m);
    if (!mag_is_alive()) {
        Serial.printf("[MAG] no sample (present=%d reads=%u fails=%u)\n",
                      (int)mag_present(), (unsigned)s_mag_reads, (unsigned)s_mag_fails);
        return;
    }
    float hdg = -1.0f;
    bool ok = mag_heading_deg(&hdg);
    Serial.printf("[MAG] %6.1f %6.1f %6.1f uT  |B|=%5.1f  pitch=%5.1f roll=%5.1f  hdg=%s\n",
                  m.x_ut, m.y_ut, m.z_ut, mag_field_strength(&m),
                  raw_imu_pitch, raw_imu_roll,
                  ok ? String(hdg, 1).c_str() : "n/a");
}

#endif // MAG_HEADING_H
