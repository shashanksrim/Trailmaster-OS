#pragma once
//
// Pure, hardware-independent OTA helper logic.
// Kept in a header (no Arduino/ESP32 dependencies) so it can be unit-tested
// on the host with a normal C++ compiler. See test/test_ota.cpp.
//
#include <stdio.h>
#include <string.h>
#include <stddef.h>

// Compare dotted version strings like "3.0" < "3.1".
// Returns true if `remote` is a newer version than `current`.
static inline bool version_is_newer(const char* remote, const char* current) {
    int rMaj = 0, rMin = 0, cMaj = 0, cMin = 0;
    sscanf(remote,  "%d.%d", &rMaj, &rMin);
    sscanf(current, "%d.%d", &cMaj, &cMin);
    return (rMaj > cMaj) || (rMaj == cMaj && rMin > cMin);
}

// Tiny JSON string-field extractor: finds  "key": "value"  and copies value.
// Returns true and null-terminates out[] (capacity out_sz) if a non-empty
// value is found.
static inline bool json_get_str(const char* json, const char* key, char* out, size_t out_sz) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == ' ') p++;  // skip : and spaces
    if (*p != '"') return false;
    p++; // skip opening quote
    size_t i = 0;
    while (*p && *p != '"' && i < out_sz - 1) {
        if (*p == '\\') p++; // skip escape char
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}
