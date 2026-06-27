#pragma once
//
// Pure OBD-II / ELM327 response parsers (no Arduino/WiFi dependencies) so the
// telemetry math can be unit-tested on the host. See test/test_ota.cpp.
//
// Each parser scans an ELM327 ASCII response (e.g. "41 0C 1A F8") and, on a
// successful match, writes the decoded engineering value via the out pointer
// and returns true. On no match it leaves *out untouched and returns false —
// this mirrors the worker behaviour of keeping the last good reading.
//
#include <stdio.h>
#include <string.h>

// PID 0105 — Engine coolant temperature (°C).  A - 40
static inline bool obd_parse_coolant(const char* resp, int* out) {
    const char* pt = strstr(resp, "41 05");
    int a;
    if (pt && sscanf(pt, "41 05 %x", &a) == 1) { *out = a - 40; return true; }
    return false;
}

// PID 0104 — Calculated engine load (%).  A * 100 / 255
static inline bool obd_parse_load(const char* resp, int* out) {
    const char* pt = strstr(resp, "41 04");
    int a;
    if (pt && sscanf(pt, "41 04 %x", &a) == 1) { *out = (a * 100) / 255; return true; }
    return false;
}

// PID 010C — Engine RPM.  ((A * 256) + B) / 4
static inline bool obd_parse_rpm(const char* resp, int* out) {
    const char* pt = strstr(resp, "41 0C");
    int a, b;
    if (pt && sscanf(pt, "41 0C %x %x", &a, &b) == 2) { *out = ((a * 256) + b) / 4; return true; }
    return false;
}

// PID 010D — Vehicle speed (km/h).  A
static inline bool obd_parse_speed(const char* resp, int* out) {
    const char* pt = strstr(resp, "41 0D");
    int a;
    if (pt && sscanf(pt, "41 0D %x", &a) == 1) { *out = a; return true; }
    return false;
}

// ATRV — Battery voltage, e.g. "13.8V"
static inline bool obd_parse_voltage(const char* resp, float* out) {
    float v;
    if (sscanf(resp, "%fV", &v) == 1) { *out = v; return true; }
    return false;
}
