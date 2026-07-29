// Convoy settings that outlive a reboot: which room to join, and which member
// in that room is this car.
//
// Deliberately tiny and dependency-light. The captive portal
// (PhotoFrameApp.cpp) WRITES these; the WiFi link (convoy_wifi.h) READS them.
// Neither should have to include the other — the portal has no business pulling
// in WiFiClientSecure, and the link has no business pulling in the web server —
// so the shared state lives here instead.
//
// Stored in Preferences("hellojimny"), the same namespace as grid_launcher /
// jimny_logo / tracker_en, so all the device prefs sit together.
#ifndef CONVOY_CFG_H
#define CONVOY_CFG_H

#include <Preferences.h>
#include "convoy_roster.h"   // convoy_normalize_code()

#define CONVOY_CFG_NS        "hellojimny"
#define CONVOY_CFG_KEY_ROOM  "cvy_room"
#define CONVOY_CFG_KEY_CALL  "cvy_call"

#define CONVOY_CFG_ROOM_MAX  16
#define CONVOY_CFG_CALL_MAX  12

// Read the saved room code into out[]. Empty string if never configured — the
// caller decides what that means (convoy_wifi.h treats it as "not set up yet"
// rather than silently joining some default room).
static inline void convoy_cfg_get_room(char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    Preferences p;
    if (!p.begin(CONVOY_CFG_NS, true)) return;
    String v = p.getString(CONVOY_CFG_KEY_ROOM, "");
    p.end();
    strncpy(out, v.c_str(), out_sz - 1);
    out[out_sz - 1] = '\0';
}

static inline void convoy_cfg_get_callsign(char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    Preferences p;
    if (!p.begin(CONVOY_CFG_NS, true)) return;
    String v = p.getString(CONVOY_CFG_KEY_CALL, "");
    p.end();
    strncpy(out, v.c_str(), out_sz - 1);
    out[out_sz - 1] = '\0';
}

// Strip anything that would break the HTML attribute these get echoed back into
// on /convoy, or the URL the room code is spliced into. Both are short
// alphanumeric tags in practice, so this costs nothing real.
static inline void convoy_cfg_sanitize(char* s) {
    char* w = s;
    for (char* r = s; *r; r++) {
        if (*r == '\'' || *r == '"' || *r == '<' || *r == '>' ||
            *r == '\\' || *r == '&' || *r == '/' || *r == '?' || *r == '#') continue;
        *w++ = *r;
    }
    *w = '\0';
}

// Save both. The room code is normalised on the way IN — whatever the user typed
// ("tm-aenp", " AENP ") is stored as the canonical room, so it matches what the
// phones computed. Callsign is stored as typed but compared case-insensitively
// later.
static inline void convoy_cfg_set(const char* room, const char* callsign) {
    char norm[CONVOY_CFG_ROOM_MAX];
    convoy_normalize_code(room, norm, sizeof(norm));
    convoy_cfg_sanitize(norm);

    Preferences p;
    if (!p.begin(CONVOY_CFG_NS, false)) return;
    p.putString(CONVOY_CFG_KEY_ROOM, norm);
    if (callsign) {
        char call[CONVOY_CFG_CALL_MAX];
        strncpy(call, callsign, sizeof(call) - 1);
        call[sizeof(call) - 1] = '\0';
        convoy_cfg_sanitize(call);
        p.putString(CONVOY_CFG_KEY_CALL, call);
    }
    p.end();
}

#endif // CONVOY_CFG_H
