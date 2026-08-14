#ifndef GPS_CONVOY_H
#define GPS_CONVOY_H
//
// Bridge: on-board GPS → the convoy radar's own-position setters.
//
// Kept separate from gps.h so that stays pure transport (no LVGL, no UI), and
// separate from convoy_ui.h so that stays renderable by the WASM sim, which has
// no GPS. This header is the only place the two meet, and it is firmware-only.
//
// ── What this changes ────────────────────────────────────────────────────────
// Until now the board had no position of its own. Each source derived "self"
// from somebody else:
//   convoy_wifi.h  looked up our callsign in the Firebase roster and borrowed
//                  the owner phone's entry — hence CALLSIGN existing at all
//   convoy_net.h   took the 'S' line the paired phone pushed over BLE
//   convoy_link.h  took the T-Beam's own GPS over the mesh
//
// With a real receiver on the board, all three become fallbacks. The local fix
// is better than every one of them: it is ~250 ms fresh instead of a poll or a
// radio hop away, it needs no phone, and on the Meshtastic path it works even
// when the T-Beam itself has no sky view.
//
// ── Why the sources must SKIP rather than be overwritten ─────────────────────
// The feeders run on convoyLinkTask and would keep writing convoy_set_self()
// from the roster every 1.5 s, fighting whatever the GPS wrote. So each source
// now guards its own self-write with gps_has_fix(); this header supplies the
// positive half. Only one writer is ever active, so there is no last-writer
// race to reason about.

#include "gps.h"
#include "convoy_ui.h"

// m/s below which course-over-ground is meaningless. Matches the identical
// thresholds in convoy_link.h / convoy_net.h / convoy_wifi.h — a GPS cannot
// tell which way a stationary car points, it only knows which way it MOVED.
//
// This used to mean "hold north-up when stopped". It now means "hand heading to
// the compass", which is what the board finally has. Defined BEFORE the include
// below because mag_convoy.h builds its hysteresis band around this value.
#define GPS_CONVOY_MOVING_MIN 1.5

#include "mag_convoy.h"   // owns the GPS-course vs compass choice

// Push the local fix into the radar. Returns true if it supplied a position,
// false if there is no usable fix and the caller should fall back to its own
// source.
//
// CALLER MUST GATE THIS. convoy_refresh() keys its waiting panel — the only
// tap-through to the source picker — on !convoy_self_fix. Feeding a position
// before the user has armed a source would hide that panel and strand them on
// a radar with no way to change source. See the call site in convoy_mock_tick.
static inline bool gps_feed_convoy_self(void) {
    gps_fix_t f;
    gps_get(&f);

    if (!gps_has_fix()) {
        // The chip must describe whatever position is actually ON SCREEN.
        // If a mesh or phone source is supplying it, our local satellite count
        // says nothing about THAT fix's quality — showing it would imply the
        // T-Beam's position was built on birds we can see. Plain "GPS" instead.
        if (convoy_self_fix) {
            convoy_set_sats(-1);
        } else {
            // Nobody has a position. Report satellites IN VIEW so the chip
            // reads "6 SAT" while acquiring rather than a bare "NO FIX" — that
            // distinguishes "hunting, nearly there" from "antenna
            // disconnected", which is the question you actually have when it is
            // not working. -1 if the module is silent entirely.
            convoy_set_sats(gps_is_alive() ? f.sats_in_view : -1);
        }
        return false;
    }

    convoy_set_self(f.lat, f.lon, true);
    convoy_self_updates++;               // field-debug counter, as the others do
    // With a fix, satellites USED is the honest number: in-view counts birds we
    // can hear but are not solving with, which would overstate the quality.
    convoy_set_sats(f.sats_used);

    // Heading is no longer ours alone to decide: below GPS_CONVOY_MOVING_MIN the
    // compass is the better source, and mag_convoy.h owns that choice. It still
    // passes validity explicitly rather than leaning on a sentinel, because
    // course_deg is -1 when unknown and convoy_set_heading() would wrap that to
    // 359 degrees.
    mag_convoy_set_heading(&f);
    return true;
}

#endif // GPS_CONVOY_H
