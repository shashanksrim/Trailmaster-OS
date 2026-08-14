// Bridge: pick the heading source for the convoy radar. Mirrors gps_convoy.h,
// which is the only place GPS and the radar meet; this is the only place the
// compass meets it.
//
// ── Why two sources rather than one ─────────────────────────────────────────
// They fail in opposite conditions, which is exactly what makes the pair worth
// having:
//
//   GPS course-over-ground  needs MOTION. It cannot tell you which way a parked
//                           car points, only which way it last moved. But while
//                           moving it is immune to the magnetic junk in a steel
//                           box full of current-carrying wires, and it needs no
//                           calibration at all.
//   Magnetometer            works at a standstill, which is the whole point. But
//                           it is the one that hard iron, door speakers and a
//                           running alternator corrupt.
//
// So: moving -> GPS, stopped -> compass. That is not a compromise, it is each
// sensor used where it is the better one.
//
// Before this existed, gps_convoy.h passed valid=false when stopped and the
// radar snapped to north-up (convoy_ui.h:64) — losing orientation precisely
// when you are parked and looking at where everyone else is.
#ifndef MAG_CONVOY_H
#define MAG_CONVOY_H

#include "gps.h"
#include "mag_heading.h"
#include "convoy_ui.h"

// Hysteresis band around GPS_CONVOY_MOVING_MIN. A single threshold makes the
// source flap at walking pace: course-over-ground goes valid/invalid every
// couple of samples, and each flip is a visible jump between two headings that
// disagree by the uncalibrated hard-iron offset. Switch to GPS above the high
// mark, back to compass below the low one.
#define MAG_CONVOY_GPS_ON_MPS   GPS_CONVOY_MOVING_MIN   // 1.5 m/s (~5.4 km/h)
#define MAG_CONVOY_GPS_OFF_MPS  0.8

static bool s_magc_using_gps = false;

// Feed the radar exactly one heading, from exactly one source. Called from
// gps_convoy.h with the current fix so both bridges stay on one task and there
// is still no last-writer race to reason about.
static void mag_convoy_set_heading(const gps_fix_t *f) {
    const bool gps_usable = f->course_valid;

    if (s_magc_using_gps) {
        if (!gps_usable || f->speed_mps < MAG_CONVOY_GPS_OFF_MPS) s_magc_using_gps = false;
    } else {
        if (gps_usable && f->speed_mps >= MAG_CONVOY_GPS_ON_MPS)  s_magc_using_gps = true;
    }

    if (s_magc_using_gps) {
        convoy_set_heading(f->course_deg, true);
        return;
    }

    float hdg;
    if (mag_heading_deg(&hdg)) {
        convoy_set_heading(hdg, true);
        return;
    }

    // No compass (absent, dead, or saturated) and not moving: fall back to the
    // old behaviour rather than holding a stale bearing. A frozen heading is
    // worse than an honest north-up, because it looks live.
    convoy_set_heading(0.0, false);
}

// Which source is currently driving the radar — for status UI and field debug.
static inline bool mag_convoy_using_gps(void) { return s_magc_using_gps; }

#endif // MAG_CONVOY_H
