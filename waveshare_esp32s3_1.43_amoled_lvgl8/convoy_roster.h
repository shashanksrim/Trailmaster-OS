#pragma once
//
// Pure, hardware-independent parser for the convoy roster JSON that the board
// pulls from the Firebase Realtime DB (see CONVOY_WIFI_PLAN.md).
//
// Kept free of Arduino/ESP32 headers — same idea as ota_logic.h — so the fiddly
// part (scanning JSON) is unit-tested on the host by test/test_ota.cpp instead
// of by reflashing a board.
//
// Shape written by docs/convoy/app.js, at convoys/<code>/members:
//
//   {"<memberId>":{"name":"Shashank","callsign":"LEAD","lat":12.9716,
//                  "lon":77.5946,"heading":184,"speed":8.3,
//                  "ts":1753800000000,"color":"#00E5FF"}, ...}
//
// Member ids are random, so the keys cannot be matched by name — the parser
// walks the top-level object generically. An empty room returns the literal
// `null`, which parses to zero members.
//
#include <stdio.h>
#include <string.h>
#include <strings.h>     // strcasecmp
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "ota_logic.h"   // json_get_str / json_get_num

#define CONVOY_ROSTER_MAX      8       // members considered per poll
#define CONVOY_ROSTER_ONLINE_MS 120000 // matches convoy_ui's online window

typedef struct {
    char   callsign[12];
    double lat, lon;
    double heading;    // degrees from true north; < 0 = unknown
    double speed;      // m/s
    double ts;         // ms since epoch, as written by the phone
    uint32_t color;    // 0xRRGGBB as chosen by the web app; 0 = unspecified
    bool   has_fix;    // a real position, not the 0,0 placeholder
} convoy_member_t;

// Step over a JSON string starting at *p == '"'. Returns the char just past the
// closing quote, or NULL if unterminated.
static inline const char* cvr_skip_string(const char* p) {
    if (*p != '"') return NULL;
    p++;
    while (*p) {
        if (*p == '\\') { p++; if (!*p) return NULL; p++; continue; }
        if (*p == '"') return p + 1;
        p++;
    }
    return NULL;
}

// Step over a JSON object starting at *p == '{'. Brace counting alone is not
// enough: a member's free-text "name" may contain { or }, so strings have to be
// skipped wholesale rather than scanned character by character.
static inline const char* cvr_skip_object(const char* p) {
    if (*p != '{') return NULL;
    int depth = 0;
    while (*p) {
        if (*p == '"') { p = cvr_skip_string(p); if (!p) return NULL; continue; }
        if (*p == '{') depth++;
        else if (*p == '}') { depth--; if (depth == 0) return p + 1; }
        p++;
    }
    return NULL;
}

// Parse the roster object into out[]. Returns the number of members filled.
// Unparseable input (including "null") yields 0 rather than an error — an empty
// room and a room that does not exist should look the same to the caller.
static inline int convoy_roster_parse(const char* json, convoy_member_t* out, int max) {
    if (!json || !out || max <= 0) return 0;
    const char* p = strchr(json, '{');
    if (!p) return 0;
    p++;   // past the opening brace of the roster object

    int n = 0;
    while (*p && n < max) {
        while (*p == ',' || *p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p != '"') break;                       // '}' or malformed → done

        const char* key_end = cvr_skip_string(p);   // the member id; not needed
        if (!key_end) break;
        p = key_end;
        while (*p == ' ' || *p == ':') p++;
        if (*p != '{') break;

        const char* obj     = p;
        const char* obj_end = cvr_skip_object(p);
        if (!obj_end) break;
        p = obj_end;

        // Copy the member out so the string helpers, which scan to a NUL, stay
        // inside this member and cannot read fields from the next one.
        char buf[320];
        size_t len = (size_t)(obj_end - obj);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, obj, len);
        buf[len] = '\0';

        convoy_member_t m;
        memset(&m, 0, sizeof(m));
        m.heading = -1.0;                            // unknown until proven
        json_get_str(buf, "callsign", m.callsign, sizeof(m.callsign));
        double v;
        if (json_get_num(buf, "lat",     &v)) m.lat     = v;
        if (json_get_num(buf, "lon",     &v)) m.lon     = v;
        if (json_get_num(buf, "heading", &v)) m.heading = v;
        if (json_get_num(buf, "speed",   &v)) m.speed   = v;
        if (json_get_num(buf, "ts",      &v)) m.ts      = v;
        // Take the colour the web app already assigned, so a car is the same
        // colour on the phone map and on the device radar.
        char col[10];
        if (json_get_str(buf, "color", col, sizeof(col)) && col[0] == '#')
            m.color = (uint32_t)strtoul(col + 1, NULL, 16);
        m.has_fix = (m.lat != 0.0 || m.lon != 0.0);

        out[n++] = m;
    }
    return n;
}

// Newest timestamp in the roster, or 0 if none carry one.
//
// Presence is judged against this rather than against a wall clock, because the
// board has no synced time — no NTP, and convoy mode may be running with no
// route to a time server. Comparing each member to the freshest member is
// self-calibrating: if the liveliest car reported 2s ago and this one 5 minutes
// ago, this one is stale regardless of what either clock says.
static inline double convoy_roster_newest_ts(const convoy_member_t* m, int n) {
    double newest = 0;
    for (int i = 0; i < n; i++) if (m[i].ts > newest) newest = m[i].ts;
    return newest;
}

static inline bool convoy_roster_is_online(const convoy_member_t* m, double newest_ts) {
    if (m->ts <= 0 || newest_ts <= 0) return false;
    return (newest_ts - m->ts) < (double)CONVOY_ROSTER_ONLINE_MS;
}

// Fold a typed room code to the canonical room, matching normalizeCode() in
// docs/convoy/app.js exactly: trim, uppercase, drop ALL whitespace, then drop a
// leading "TM-". Both ends must agree or the phones and the board land in
// different rooms — which is precisely the bug that split the first live test
// (one phone in TM-NA4V, the other in NA4V).
static inline void convoy_normalize_code(const char* raw, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!raw) return;

    char up[64];
    size_t n = 0;
    for (const char* p = raw; *p && n < sizeof(up) - 1; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') continue;  // all whitespace
        char c = *p;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        up[n++] = c;
    }
    up[n] = '\0';

    const char* start = up;
    if (n >= 3 && up[0] == 'T' && up[1] == 'M' && up[2] == '-') start = up + 3;

    strncpy(out, start, out_sz - 1);
    out[out_sz - 1] = '\0';
}

// Index of the member whose callsign matches `self` (case-insensitive), or -1.
static inline int convoy_roster_find(const convoy_member_t* m, int n, const char* self) {
    if (!self || !*self) return -1;
    for (int i = 0; i < n; i++) {
        if (strcasecmp(m[i].callsign, self) == 0) return i;
    }
    return -1;
}
