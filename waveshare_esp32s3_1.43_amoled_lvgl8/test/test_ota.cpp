// Host-side unit tests for Trailmaster-OS pure logic (OTA + OBD parsing).
//
// These compile and run on your computer with g++ — no ESP32 needed — because
// they only exercise the hardware-independent headers ota_logic.h / obd_parse.h,
// which are the SAME headers the firmware uses.
//
// Run:  ./test/run_tests.sh     (or see that script for the g++ command)
//
#include <cstdio>
#include <cstring>
#include <cmath>

#include "ota_logic.h"
#include "obd_parse.h"
#include "convoy_roster.h"

// ── tiny test harness ───────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s  (line %d)\n", msg, __LINE__); } \
} while (0)

#define CHECK_INT(got, want, msg) do { \
    int _g = (got), _w = (want); \
    if (_g == _w) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s  got=%d want=%d (line %d)\n", msg, _g, _w, __LINE__); } \
} while (0)

#define CHECK_STR(got, want, msg) do { \
    if (strcmp((got), (want)) == 0) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s  got=\"%s\" want=\"%s\" (line %d)\n", msg, (got), (want), __LINE__); } \
} while (0)

#define CHECK_FLT(got, want, msg) do { \
    if (fabs((got) - (want)) < 0.01) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s  got=%f want=%f (line %d)\n", msg, (double)(got), (double)(want), __LINE__); } \
} while (0)

// ── version comparison ──────────────────────────────────────────────────────
static void test_version_is_newer() {
    printf("version_is_newer:\n");
    CHECK(version_is_newer("3.1", "3.0") == true,  "3.1 > 3.0");
    CHECK(version_is_newer("3.0", "3.1") == false, "3.0 not > 3.1");
    CHECK(version_is_newer("3.1", "3.1") == false, "equal is not newer");
    CHECK(version_is_newer("4.0", "3.9") == true,  "major bump wins");
    CHECK(version_is_newer("3.10", "3.9") == true, "minor 10 > 9 (numeric, not lexical)");
    CHECK(version_is_newer("3.2", "3.1") == true,  "3.2 > 3.1");
    CHECK(version_is_newer("2.9", "3.0") == false, "lower major");
}

// ── version.json field extraction ───────────────────────────────────────────
static void test_json_get_str() {
    printf("json_get_str:\n");
    const char* json =
        "{ \"version\": \"3.1\", "
        "\"changelog\": \"Fixed speedo flicker\", "
        "\"firmware_url\": \"https://raw.githubusercontent.com/shashanksrim/Trailmaster-OS/main/firmware.bin\", "
        "\"sd_files\": [] }";

    char buf[256] = {};
    CHECK(json_get_str(json, "version", buf, sizeof(buf)) == true, "version found");
    CHECK_STR(buf, "3.1", "version value");

    CHECK(json_get_str(json, "changelog", buf, sizeof(buf)) == true, "changelog found");
    CHECK_STR(buf, "Fixed speedo flicker", "changelog value");

    CHECK(json_get_str(json, "firmware_url", buf, sizeof(buf)) == true, "url found");
    CHECK_STR(buf, "https://raw.githubusercontent.com/shashanksrim/Trailmaster-OS/main/firmware.bin", "url value (owner = shashanksrim)");

    char miss[16] = "untouched";
    CHECK(json_get_str(json, "nope", miss, sizeof(miss)) == false, "missing key returns false");
}

// ── OBD-II PID parsers ──────────────────────────────────────────────────────
static void test_obd_parsers() {
    printf("obd parsers:\n");
    int v = -999; float f = -1.0f;

    // RPM: ((A*256)+B)/4
    CHECK(obd_parse_rpm("41 0C 1A F8", &v) == true, "rpm parsed");
    CHECK_INT(v, 1726, "rpm 0x1AF8/4");
    // tolerate ELM327 echo/search noise before the data
    CHECK(obd_parse_rpm("SEARCHING...\r41 0C 0F A0", &v) == true, "rpm with noise");
    CHECK_INT(v, 1000, "rpm 0x0FA0/4");

    // Speed: A
    CHECK(obd_parse_speed("41 0D 50", &v) == true, "speed parsed");
    CHECK_INT(v, 80, "speed 0x50");

    // Coolant: A - 40
    CHECK(obd_parse_coolant("41 05 7B", &v) == true, "coolant parsed");
    CHECK_INT(v, 83, "coolant 0x7B-40");

    // Load: A*100/255
    CHECK(obd_parse_load("41 04 FF", &v) == true, "load parsed");
    CHECK_INT(v, 100, "load full");
    CHECK(obd_parse_load("41 04 80", &v) == true, "load parsed 2");
    CHECK_INT(v, 50, "load half-ish");

    // Voltage: "13.8V"
    CHECK(obd_parse_voltage("13.8V", &f) == true, "voltage parsed");
    CHECK_FLT(f, 13.8, "voltage value");

    // No-match: value must be left untouched, return false
    v = 4242;
    CHECK(obd_parse_rpm("NO DATA", &v) == false, "no data -> false");
    CHECK_INT(v, 4242, "no data leaves last reading untouched");
}

// ── convoy roster parsing ───────────────────────────────────────────────────
static void test_convoy_roster() {
    printf("-- convoy_roster_parse --\n");
    convoy_member_t m[CONVOY_ROSTER_MAX];

    // An empty room: RTDB returns the literal null, not an empty object.
    CHECK_INT(convoy_roster_parse("null", m, CONVOY_ROSTER_MAX), 0, "null -> 0 members");
    CHECK_INT(convoy_roster_parse("", m, CONVOY_ROSTER_MAX), 0, "empty -> 0 members");
    CHECK_INT(convoy_roster_parse("{}", m, CONVOY_ROSTER_MAX), 0, "{} -> 0 members");

    const char* two =
        "{\"a1b2\":{\"name\":\"Shashank\",\"callsign\":\"LEAD\",\"lat\":12.9716,"
        "\"lon\":77.5946,\"heading\":184,\"speed\":8.3,\"ts\":1753800000000,"
        "\"color\":\"#00E5FF\"},"
        "\"c3d4\":{\"name\":\"Ravi\",\"callsign\":\"SWEEP\",\"lat\":12.968,"
        "\"lon\":77.59,\"heading\":12,\"speed\":0.4,\"ts\":1753799940000,"
        "\"color\":\"#00E676\"}}";
    CHECK_INT(convoy_roster_parse(two, m, CONVOY_ROSTER_MAX), 2, "two members");
    CHECK_STR(m[0].callsign, "LEAD", "first callsign");
    CHECK_STR(m[1].callsign, "SWEEP", "second callsign");
    CHECK_FLT(m[0].lat, 12.9716, "first lat");
    CHECK_FLT(m[1].lon, 77.59, "second lon");
    CHECK_FLT(m[0].heading, 184, "first heading");
    CHECK(m[0].has_fix, "first has fix");
    CHECK_INT((int)m[0].color, 0x00E5FF, "colour taken from the web app");
    CHECK_INT((int)m[1].color, 0x00E676, "second colour");

    // ts must survive as a full ms-since-epoch value — this is the case that a
    // float or an int32 would silently mangle.
    CHECK(m[0].ts == 1753800000000.0, "ts keeps full precision");

    // Presence is relative to the freshest member, not to any local clock.
    double newest = convoy_roster_newest_ts(m, 2);
    CHECK(newest == 1753800000000.0, "newest ts");
    CHECK(convoy_roster_is_online(&m[0], newest), "freshest member is online");
    CHECK(convoy_roster_is_online(&m[1], newest), "1min-old member still online");

    // The window is exclusive: a member exactly CONVOY_ROSTER_ONLINE_MS behind
    // the freshest one has already fallen out of it.
    convoy_member_t edge = m[1];
    edge.ts = newest - (double)CONVOY_ROSTER_ONLINE_MS;
    CHECK(!convoy_roster_is_online(&edge, newest), "exactly at the window edge is offline");
    edge.ts = newest - (double)CONVOY_ROSTER_ONLINE_MS + 1;
    CHECK(convoy_roster_is_online(&edge, newest), "1ms inside the window is online");

    const char* stale =
        "{\"x\":{\"callsign\":\"OLD\",\"lat\":1,\"lon\":1,\"ts\":1753000000000},"
        "\"y\":{\"callsign\":\"NEW\",\"lat\":2,\"lon\":2,\"ts\":1753800000000}}";
    CHECK_INT(convoy_roster_parse(stale, m, CONVOY_ROSTER_MAX), 2, "stale roster parsed");
    newest = convoy_roster_newest_ts(m, 2);
    CHECK(!convoy_roster_is_online(&m[0], newest), "long-stale member is offline");
    CHECK(convoy_roster_is_online(&m[1], newest), "newest member is online");

    // A member who joined but has no GPS yet: RTDB stores null, not 0.
    const char* nofix = "{\"z\":{\"callsign\":\"WAIT\",\"lat\":null,\"lon\":null,\"ts\":1753800000000}}";
    CHECK_INT(convoy_roster_parse(nofix, m, CONVOY_ROSTER_MAX), 1, "no-fix member parsed");
    CHECK(!m[0].has_fix, "null lat/lon -> no fix");
    CHECK_FLT(m[0].heading, -1, "absent heading -> -1");

    // Braces inside a free-text name must not end the object early.
    const char* braces =
        "{\"k\":{\"name\":\"Sri {the} Boss\",\"callsign\":\"BOSS\",\"lat\":5,\"lon\":6,\"ts\":9},"
        "\"j\":{\"callsign\":\"TWO\",\"lat\":7,\"lon\":8,\"ts\":9}}";
    CHECK_INT(convoy_roster_parse(braces, m, CONVOY_ROSTER_MAX), 2, "braces in name survive");
    CHECK_STR(m[0].callsign, "BOSS", "member after brace-y name");
    CHECK_STR(m[1].callsign, "TWO", "second member still found");

    // Self lookup is by callsign, case-insensitively.
    CHECK_INT(convoy_roster_find(m, 2, "two"), 1, "find self case-insensitive");
    CHECK_INT(convoy_roster_find(m, 2, "NOPE"), -1, "unknown callsign -> -1");
    CHECK_INT(convoy_roster_find(m, 2, ""), -1, "empty callsign -> -1");

    // More members than we have room for must not overrun.
    const char* many =
        "{\"1\":{\"callsign\":\"A\",\"lat\":1,\"lon\":1,\"ts\":1},"
        "\"2\":{\"callsign\":\"B\",\"lat\":1,\"lon\":1,\"ts\":1},"
        "\"3\":{\"callsign\":\"C\",\"lat\":1,\"lon\":1,\"ts\":1}}";
    CHECK_INT(convoy_roster_parse(many, m, 2), 2, "respects max");

    // Truncated payload (a dropped read) must not loop or crash.
    CHECK_INT(convoy_roster_parse("{\"a\":{\"callsign\":\"X\",\"lat\":1", m, CONVOY_ROSTER_MAX),
              0, "truncated object -> 0");
}

// ── room-code normalisation (must match docs/convoy/app.js normalizeCode) ────
static void test_convoy_normalize_code() {
    printf("-- convoy_normalize_code --\n");
    char out[16];

    convoy_normalize_code("AENP", out, sizeof(out));
    CHECK_STR(out, "AENP", "already canonical");

    convoy_normalize_code("aenp", out, sizeof(out));
    CHECK_STR(out, "AENP", "lowercased");

    convoy_normalize_code("  aenp  ", out, sizeof(out));
    CHECK_STR(out, "AENP", "trimmed");

    // This is the split that broke the first live test: one phone typed the
    // prefix, the other did not, and they landed in different rooms.
    convoy_normalize_code("TM-NA4V", out, sizeof(out));
    CHECK_STR(out, "NA4V", "TM- prefix stripped");
    convoy_normalize_code("tm-na4v", out, sizeof(out));
    CHECK_STR(out, "NA4V", "lowercase prefix stripped");

    convoy_normalize_code("na 4v", out, sizeof(out));
    CHECK_STR(out, "NA4V", "inner spaces dropped");

    // Only a LEADING prefix goes; TM- in the middle is part of the code.
    convoy_normalize_code("XTM-AB", out, sizeof(out));
    CHECK_STR(out, "XTM-AB", "non-leading TM- kept");

    convoy_normalize_code("", out, sizeof(out));
    CHECK_STR(out, "", "empty stays empty");
    convoy_normalize_code(NULL, out, sizeof(out));
    CHECK_STR(out, "", "null stays empty");

    // A code longer than the buffer must truncate, not overrun.
    char small[5];
    convoy_normalize_code("ABCDEFGHIJ", small, sizeof(small));
    CHECK_STR(small, "ABCD", "truncates to fit");
}

int main() {
    printf("=== Trailmaster-OS unit tests ===\n");
    test_version_is_newer();
    test_json_get_str();
    test_obd_parsers();
    test_convoy_roster();
    test_convoy_normalize_code();
    printf("=================================\n");
    printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
