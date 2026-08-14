#pragma once
//
// Pure, hardware-independent NMEA 0183 parser for the on-board NEO-M9N.
//
// Kept free of Arduino/ESP32 headers — same idea as ota_logic.h and
// convoy_roster.h — so the fiddly part (counting commas) is unit-tested on the
// host by test/test_ota.cpp instead of by reflashing a board. The transport
// lives in gps.h; this file never touches a bus.
//
// The module reaches us over I2C at 0x42, NOT UART. See gps.h for why.
//
// Sentences we care about, as actually emitted by this module at its defaults:
//
//   $GNRMC,182105.00,A,1256.21613,N,07741.94587,E,0.058,,020826,,,A,V*1D
//   $GNGGA,182105.00,1256.21613,N,07741.94587,E,1,12,1.29,922.8,M,-86.5,M,,*66
//   $GNGSA,A,3,14,17,20,30,22,,,,,,,,2.22,1.29,1.81,1*03
//   $GBGSV,1,1,01,29,,,28,1*76
//
// RMC carries position + speed + course + date, GGA carries fix quality +
// satellites used + HDOP + altitude, GSA carries 2D/3D mode, GSV carries how
// many satellites are visible per constellation.
//
// ── A note on field indices ──────────────────────────────────────────────────
// Fields are counted from the talker ID as field 0, so GGA's `quality` sits
// after SIX commas. An early throwaway version of this counted seven, which
// silently read numSV as the quality and HDOP as the satellite count — and it
// looked plausible, reporting "99 satellites" because HDOP was 99.99. The
// off-by-one only showed up against real data. Hence the fixture-based tests.

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

// GSV reports satellites in view PER CONSTELLATION, one set of sentences per
// talker. Totals therefore have to be tracked per talker and summed, never
// accumulated as they arrive — otherwise every epoch adds to the last and the
// count grows without bound (an early version reported 192 for a ~16-satellite
// sky, because it summed twelve epochs).
typedef enum {
    GPS_TALKER_GP = 0,   // GPS
    GPS_TALKER_GL,       // GLONASS
    GPS_TALKER_GA,       // Galileo
    GPS_TALKER_GB,       // BeiDou
    GPS_TALKER_GQ,       // QZSS
    GPS_TALKER_OTHER,
    GPS_TALKER_COUNT
} gps_talker_t;

typedef struct {
    double lat, lon;        // decimal degrees, positive N / E
    double alt_m;           // metres above mean sea level
    bool   has_fix;         // RMC status 'A' — a usable position
    int    fix_quality;     // GGA: 0 none, 1 GPS, 2 DGPS/SBAS
    int    nav_mode;        // GSA: 1 none, 2 = 2D, 3 = 3D

    double speed_mps;
    double course_deg;      // degrees from TRUE north
    bool   course_valid;    // RMC leaves course empty when stationary

    double hdop;
    int    sats_used;       // GGA numSV — satellites in the solution
    int    sats_in_view;    // summed across constellations

    int  hour, minute, second;
    int  day, month, year;  // year is already 2000-based
    bool time_valid, date_valid;

    uint8_t view_by_talker[GPS_TALKER_COUNT];
} gps_fix_t;

static inline void gps_fix_reset(gps_fix_t *f) {
    if (!f) return;
    memset(f, 0, sizeof(*f));
    f->course_deg = -1.0;
    f->hdop       = 99.99;
}

// Validate "$....*HH": XOR of every byte between '$' and '*'.
//
// Every sentence is checked before it is parsed. On a shared I2C bus a
// truncated read is a normal event, not an exception, so this is what stops a
// half-sentence being interpreted as a position.
static inline bool gps_checksum_ok(const char *s, int len) {
    if (!s || len < 4 || s[0] != '$') return false;
    int star = -1;
    for (int i = len - 3; i > 0; i--) if (s[i] == '*') { star = i; break; }
    if (star < 1) return false;
    uint8_t sum = 0;
    for (int i = 1; i < star; i++) sum ^= (uint8_t)s[i];
    char hex[3] = { s[star + 1], s[star + 2], 0 };
    char *end = NULL;
    long want = strtol(hex, &end, 16);
    if (end == hex) return false;
    return (uint8_t)want == sum;
}

// Copy comma-separated field `idx` (0 = the talker ID) into out[].
// Stops at '*' so the checksum never bleeds into the last field.
static inline bool gps_field(const char *line, int idx, char *out, size_t n) {
    if (!line || !out || n == 0) return false;
    out[0] = '\0';
    int f = 0;
    const char *p = line;
    while (*p && f < idx) { if (*p == ',') f++; p++; }
    if (f != idx) return false;
    size_t w = 0;
    while (*p && *p != ',' && *p != '*' && w < n - 1) out[w++] = *p++;
    out[w] = '\0';
    return true;
}

// NMEA packs latitude as ddmm.mmmm and longitude as dddmm.mmmm — degrees and
// minutes run together with no separator, so the split point differs between
// the two and cannot be found by scanning. deg_digits says where to cut.
static inline double gps_ddmm_to_deg(const char *v, const char *hemi, int deg_digits) {
    if (!v || !*v) return 0.0;
    if ((int)strlen(v) < deg_digits + 1) return 0.0;
    char d[4] = {0};
    memcpy(d, v, (size_t)deg_digits);
    const double deg = atof(d);
    const double min = atof(v + deg_digits);
    double out = deg + min / 60.0;
    if (hemi && (*hemi == 'S' || *hemi == 'W')) out = -out;
    return out;
}

static inline gps_talker_t gps_talker_of(const char *line) {
    if (!line || strlen(line) < 3) return GPS_TALKER_OTHER;
    if (line[1] == 'G' && line[2] == 'P') return GPS_TALKER_GP;
    if (line[1] == 'G' && line[2] == 'L') return GPS_TALKER_GL;
    if (line[1] == 'G' && line[2] == 'A') return GPS_TALKER_GA;
    if (line[1] == 'G' && line[2] == 'B') return GPS_TALKER_GB;
    if (line[1] == 'G' && line[2] == 'Q') return GPS_TALKER_GQ;
    return GPS_TALKER_OTHER;
}

static inline bool gps_is(const char *line, const char *type) {
    return line && strlen(line) >= 6 && strncmp(line + 3, type, 3) == 0;
}

// hhmmss.ss
static inline void gps_parse_time(const char *v, gps_fix_t *f) {
    if (!v || strlen(v) < 6) return;
    char b[3] = {0};
    b[0] = v[0]; b[1] = v[1]; f->hour   = atoi(b);
    b[0] = v[2]; b[1] = v[3]; f->minute = atoi(b);
    b[0] = v[4]; b[1] = v[5]; f->second = atoi(b);
    f->time_valid = true;
}

// ddmmyy. Two-digit years are unambiguous here: this module shipped well after
// 2000 and NMEA has no century field, so 2000+yy is the only sane reading.
static inline void gps_parse_date(const char *v, gps_fix_t *f) {
    if (!v || strlen(v) < 6) return;
    char b[3] = {0};
    b[0] = v[0]; b[1] = v[1]; f->day   = atoi(b);
    b[0] = v[2]; b[1] = v[3]; f->month = atoi(b);
    b[0] = v[4]; b[1] = v[5]; f->year  = 2000 + atoi(b);
    f->date_valid = true;
}

// Fold one sentence into the running fix. Returns true if the sentence was one
// we understand and its checksum held.
//
// State is cumulative on purpose: a single epoch is spread across RMC, GGA, GSA
// and several GSV sentences, so no one sentence has the whole picture.
static inline bool gps_parse_line(const char *line, gps_fix_t *f) {
    if (!line || !f) return false;
    const int len = (int)strlen(line);
    if (!gps_checksum_ok(line, len)) return false;

    char v[16], h[4];

    // $xxRMC,time,status,lat,NS,lon,EW,spd,cog,date,...
    if (gps_is(line, "RMC")) {
        gps_field(line, 1, v, sizeof(v)); gps_parse_time(v, f);
        gps_field(line, 2, v, sizeof(v));
        f->has_fix = (v[0] == 'A');
        if (f->has_fix) {
            gps_field(line, 3, v, sizeof(v)); gps_field(line, 4, h, sizeof(h));
            f->lat = gps_ddmm_to_deg(v, h, 2);
            gps_field(line, 5, v, sizeof(v)); gps_field(line, 6, h, sizeof(h));
            f->lon = gps_ddmm_to_deg(v, h, 3);
        }
        // Speed is in KNOTS in RMC (VTG carries km/h). Everything downstream —
        // convoy_ui's MOVING_MIN, the Firebase schema — is m/s.
        gps_field(line, 7, v, sizeof(v));
        if (v[0]) f->speed_mps = atof(v) * 0.514444;
        // Course over ground is empty when stationary: a GPS cannot know which
        // way a parked car points. Callers must not treat 0 as "due north".
        gps_field(line, 8, v, sizeof(v));
        if (v[0]) { f->course_deg = atof(v); f->course_valid = true; }
        else      { f->course_deg = -1.0;    f->course_valid = false; }
        gps_field(line, 9, v, sizeof(v)); gps_parse_date(v, f);
        return true;
    }

    // $xxGGA,time,lat,NS,lon,EW,quality,numSV,HDOP,alt,M,...
    if (gps_is(line, "GGA")) {
        gps_field(line, 6, v, sizeof(v)); f->fix_quality = atoi(v);
        gps_field(line, 7, v, sizeof(v)); f->sats_used   = atoi(v);
        gps_field(line, 8, v, sizeof(v)); if (v[0]) f->hdop = atof(v);
        gps_field(line, 9, v, sizeof(v)); if (v[0]) f->alt_m = atof(v);
        if (f->fix_quality > 0) {
            gps_field(line, 2, v, sizeof(v)); gps_field(line, 3, h, sizeof(h));
            if (v[0]) f->lat = gps_ddmm_to_deg(v, h, 2);
            gps_field(line, 4, v, sizeof(v)); gps_field(line, 5, h, sizeof(h));
            if (v[0]) f->lon = gps_ddmm_to_deg(v, h, 3);
        }
        return true;
    }

    // $xxGSA,opMode,navMode,...
    if (gps_is(line, "GSA")) {
        gps_field(line, 2, v, sizeof(v));
        if (v[0]) f->nav_mode = atoi(v);
        return true;
    }

    // $xxGSV,numMsg,msgNum,numSV,...
    // Only the FIRST message of each talker's burst carries a fresh count, so
    // keying on msgNum == 1 makes this idempotent across repeated epochs.
    if (gps_is(line, "GSV")) {
        gps_field(line, 2, v, sizeof(v));
        if (atoi(v) == 1) {
            gps_field(line, 3, v, sizeof(v));
            f->view_by_talker[gps_talker_of(line)] = (uint8_t)atoi(v);
            int total = 0;
            for (int i = 0; i < GPS_TALKER_COUNT; i++) total += f->view_by_talker[i];
            f->sats_in_view = total;
        }
        return true;
    }

    return false;
}
