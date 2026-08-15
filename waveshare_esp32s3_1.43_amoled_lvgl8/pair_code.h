#pragma once
// A per-board pairing code — the thing that tells one Trailmaster apart from
// another, and gates what the companion app is allowed to send it.
//
// WHY NOT THE DEVICE ID. The board is already uniquely identified by its eFuse
// MAC, and that is what devices/<id> is keyed on. But a MAC is 12 hex characters
// nobody will type, and it is not a secret: the app lists every board that is
// pairing, so anyone could address anyone else's. This code is short enough to
// read off the screen and long enough not to be guessed.
//
// SIX CHARACTERS, not four. Four digits is 10,000 possibilities, and Firebase
// applies no rate limiting, so the whole space can be walked in minutes — which
// would put a stranger's images on your dash and expose a Wi-Fi password in
// transit. This alphabet is 31 characters, so six of them is ~887 million.
//
// The alphabet drops the pairs that get misread off a screen: no O or 0, no I,
// L or 1. Everything here survives being read aloud in a car park.
#include <Preferences.h>

#define PAIR_CODE_LEN 6

static const char PAIR_ALPHABET[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";   // 31 chars

// Stable for the life of the board unless explicitly reset. Persisted, because a
// code that changed on every boot would have to be re-typed into every phone
// that pairs — and the phone is meant to remember it forever.
inline const char * pair_code_get(void) {
    static char code[PAIR_CODE_LEN + 1] = {0};
    if (code[0]) return code;

    Preferences p;
    p.begin("pair", false);
    String saved = p.getString("code", "");
    if (saved.length() == PAIR_CODE_LEN) {
        strncpy(code, saved.c_str(), PAIR_CODE_LEN);
    } else {
        // esp_random() is the hardware RNG, properly seeded once WiFi/BT have
        // been up; even before that it is far better than rand() with a fixed
        // seed, which would hand every board off the line the same code.
        const size_t n = sizeof(PAIR_ALPHABET) - 1;
        for (int i = 0; i < PAIR_CODE_LEN; i++) code[i] = PAIR_ALPHABET[esp_random() % n];
        code[PAIR_CODE_LEN] = 0;
        p.putString("code", code);
        Serial.printf("[PAIR] generated pairing code %s\n", code);
    }
    p.end();
    return code;
}

// Forget the current code and mint a new one. The only way to revoke a phone
// that was paired and should not be any more — there is no per-phone record to
// delete, since pairing is a capability rather than an account.
inline void pair_code_reset(void) {
    Preferences p;
    p.begin("pair", false);
    p.remove("code");
    p.end();
    Serial.println("[PAIR] code cleared; a new one is issued on next read");
}
