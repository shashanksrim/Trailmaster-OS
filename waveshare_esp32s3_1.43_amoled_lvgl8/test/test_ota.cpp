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

int main() {
    printf("=== Trailmaster-OS unit tests ===\n");
    test_version_is_newer();
    test_json_get_str();
    test_obd_parsers();
    printf("=================================\n");
    printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
