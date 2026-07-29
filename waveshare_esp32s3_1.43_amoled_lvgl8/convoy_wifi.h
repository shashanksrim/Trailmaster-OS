// Convoy WIFI link — the board pulls the roster from Firebase itself.
//
// Third convoy source, alongside convoy_link.h (Meshtastic mesh, BLE central)
// and convoy_net.h (phone relay, BLE peripheral). Here there is NO phone in the
// data path at all: the board joins the owner's hotspot as a STA and reads
// convoys/<code>/members straight off the Firebase RTDB, then feeds the same
// convoy_set_self()/convoy_set_car() the radar already renders.
//
// WHY this exists (full reasoning in repo CONVOY_WIFI_PLAN.md): a browser can
// never be a WiFi transport to the board. The convoy PWA needs geolocation and
// wake lock, both secure-context-only, so a board-served http page gets neither,
// and an HTTPS page cannot reach a board LAN IP over http (mixed content). That
// left Web Bluetooth as the only browser->board hop, which iOS does not have.
// Going direct-to-cloud removes the hop entirely: any phone, any browser, and
// the board keeps pulling positions while the phone is locked in a pocket.
//
// FIRMWARE-ONLY (needs WiFi). Not included by the WASM sim. The fiddly part —
// parsing the roster JSON — lives in convoy_roster.h, which is pure and covered
// by host tests in test/test_ota.cpp.
//
// NOTE: WiFi and BLE cannot coexist on this build — the BT controller cannot get
// its internal buffers with the WiFi stack up (spiked 2026-07-28, see the
// CONVOY_KEEP_WIFI comment in the .ino). So this source and the two BLE sources
// are mutually exclusive, which the runtime source picker already enforces.
//
// Threading: everything here runs on convoyLinkTask, which is where
// convoy_link_loop() already runs. The HTTP GET blocks that task, never the UI
// thread; like the other sources we only write the plain-data convoy_set_*
// globals and never touch LVGL.
#ifndef CONVOY_WIFI_H
#define CONVOY_WIFI_H

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_heap_caps.h"
#include "OTAManager.h"      // ota_wifi_connect_saved()
#include "convoy_ui.h"       // convoy_set_self / _heading / _car, CONVOY_MAX_CARS
#include "convoy_roster.h"   // pure JSON → convoy_member_t[]
#include "convoy_cfg.h"      // room code + callsign, set from the captive portal

// Firebase Realtime DB — keep in sync with docs/convoy/config.js (databaseURL).
#define CONVOY_WIFI_DB_HOST "https://trailmaster-e43b1-default-rtdb.asia-southeast1.firebasedatabase.app"

// The room code and callsign are NOT compiled in. They arrive one of two ways,
// both persisted by convoy_cfg.h:
//   - the app's board picker writes them into this board's devices/<id> node
//     (the normal path — the user taps their board, nothing is typed);
//   - or the captive portal's /convoy tab, as a manual override.
// CALLSIGN is which member in the room is THIS car: the board has no GPS, so its
// own position comes from the owner phone's entry, found by callsign. Everyone
// else in the room renders as another car.

#define CONVOY_WIFI_POLL_MS   1500    // roster refresh cadence
#define CONVOY_WIFI_RX_TTL_MS 10000   // "online" if a roster arrived within this
#define CONVOY_WIFI_MOVING_MIN 1.5    // m/s: above this we treat heading as valid

// How often the board touches its own devices/<id> node — to announce itself so
// the app can list it, and to notice a room the app has assigned. Fast while
// unpaired (the user is staring at the screen waiting for it to appear in the
// app), lazy once configured (it only needs to catch a re-point to a new room).
#define CONVOY_WIFI_PAIR_FAST_MS 2000
#define CONVOY_WIFI_PAIR_SLOW_MS 10000

// Fallback palette, used only for a member the web app gave no colour.
static const uint32_t CONVOY_WIFI_HUES[6] = {
    0x00E5FF, 0x00E676, 0xFFD54F, 0xFF4081, 0xB388FF, 0xFF8A65
};

typedef enum {
    CONVOY_WIFI_NO_NET,     // no saved network reachable
    CONVOY_WIFI_UNPAIRED,   // online, but no room assigned yet — link it in the app
    CONVOY_WIFI_WAITING,    // room known, but no roster received yet
    CONVOY_WIFI_ONLINE      // roster arriving
} convoy_wifi_state_t;

static bool     s_cvw_joined   = false;
static bool     s_cvw_begun    = false;   // HTTPClient has a live begin()
static uint32_t s_cvw_last_rx  = 0;
static uint32_t s_cvw_next_poll = 0;
static uint32_t s_cvw_next_pair = 0;
static char     s_cvw_url[224];
static char     s_cvw_pair_url[224];
// Stable per-board identity, derived from the eFuse MAC. The web app lists these
// so the user can pick their own board instead of typing a room code.
static char     s_cvw_dev_id[16];
static char     s_cvw_dev_name[24];
// Resolved once per session in convoy_wifi_begin(): saved config if the user has
// been through the portal, compiled-in fallback otherwise.
static char     s_cvw_room[CONVOY_CFG_ROOM_MAX];
static char     s_cvw_call[CONVOY_CFG_CALL_MAX];

// Kept alive across polls on purpose: with setReuse(true) the TLS session is
// reused, so a poll costs one small request instead of a fresh handshake. The
// handshake is the expensive part — Phase 0 measured it at ~47KB and seconds,
// which at a 1.5s cadence would be untenable.
static WiFiClientSecure s_cvw_client;
static HTTPClient       s_cvw_http;

// ── Roster → radar ───────────────────────────────────────────────────────────
static void convoy_wifi_apply(const char *json) {
    convoy_member_t mem[CONVOY_ROSTER_MAX];
    const int n = convoy_roster_parse(json, mem, CONVOY_ROSTER_MAX);
    const double newest = convoy_roster_newest_ts(mem, n);
    const int self_idx = convoy_roster_find(mem, n, s_cvw_call);

    if (self_idx >= 0) {
        const convoy_member_t *s = &mem[self_idx];
        convoy_set_self(s->lat, s->lon, s->has_fix);
        if (s->has_fix) convoy_self_updates++;
        if (s->has_fix && s->heading >= 0)
            convoy_set_heading(s->heading, s->speed >= CONVOY_WIFI_MOVING_MIN);
    } else {
        // Nobody in the room is us. The radar still shows everyone else; we just
        // have no ME position, same as an unfixed GPS.
        convoy_set_self(0, 0, false);
    }

    int car = 0;
    for (int i = 0; i < n && car < CONVOY_MAX_CARS; i++) {
        if (i == self_idx) continue;
        const convoy_member_t *m = &mem[i];
        const uint32_t hue = m->color ? m->color : CONVOY_WIFI_HUES[car % 6];
        convoy_set_car(car, m->callsign, m->lat, m->lon, lv_color_hex(hue),
                       convoy_roster_is_online(m, newest), m->has_fix);
        car++;
    }
    // Hide any slots the roster no longer includes.
    for (int i = car; i < CONVOY_MAX_CARS; i++)
        convoy_set_car(i, "", 0, 0, lv_color_hex(0), false, false);

    s_cvw_last_rx = millis();

    // Polling at 1.5s makes a per-roster log unreadable, but total silence makes
    // "is it actually receiving?" unanswerable from a serial capture. Log when
    // the shape changes, plus a slow heartbeat.
    static int      last_n = -1, last_self = -1;
    static uint32_t last_beat = 0;
    const int self_state = (self_idx >= 0) ? (mem[self_idx].has_fix ? 1 : 0) : -1;
    if (n != last_n || self_state != last_self || millis() - last_beat > 30000) {
        Serial.printf("[CVW] roster: %d member(s), %d car(s), self=%s\n", n, car,
                      self_idx < 0 ? "not in room" : (self_state ? "fix" : "no fix"));
        last_n = n; last_self = self_state; last_beat = millis();
    }
}

// ── One request on the shared TLS connection ─────────────────────────────────
// Everything here talks to one host, so end()+begin() with setReuse(true) keeps
// the socket and skips the handshake — which Phase 0 measured at ~47KB and
// seconds, far too expensive to pay per poll.
static bool cvw_request(const char *url, const char *method, const char *body, String *out) {
    s_cvw_http.end();
    if (!s_cvw_http.begin(s_cvw_client, url)) return false;
    int code;
    if (body) {
        s_cvw_http.addHeader("Content-Type", "application/json");
        code = s_cvw_http.sendRequest(method, (uint8_t *)body, strlen(body));
    } else {
        code = s_cvw_http.GET();
    }
    if (code != 200) {
        Serial.printf("[CVW] %s %s -> %d\n", method, url, code);
        return false;
    }
    if (out) *out = s_cvw_http.getString();
    return true;
}

// ── Pairing: announce this board, and pick up a room the app assigned ────────
// The board publishes devices/<id> so the convoy web app can list it; the user
// taps their board and the app writes room+callsign back into the same node.
// That replaces typing a room code on the device, and it keeps working after
// setup — re-pointing the board at a new convoy is just picking it again.
static void convoy_wifi_pair_tick(void) {
    // Announce. "ts" uses RTDB's server-value sentinel because the board has no
    // synced clock and could not write a truthful timestamp itself; the server
    // stamps it, which is also what makes the app's freshness filter meaningful.
    char body[160];
    snprintf(body, sizeof(body),
             "{\"name\":\"%s\",\"ts\":{\".sv\":\"timestamp\"}}", s_cvw_dev_name);
    cvw_request(s_cvw_pair_url, "PATCH", body, nullptr);

    // Read the node back: has the app assigned us a room?
    String node;
    if (!cvw_request(s_cvw_pair_url, "GET", nullptr, &node)) return;

    char room[CONVOY_CFG_ROOM_MAX] = {}, call[CONVOY_CFG_CALL_MAX] = {};
    json_get_str(node.c_str(), "room",     room, sizeof(room));
    json_get_str(node.c_str(), "callsign", call, sizeof(call));
    if (!room[0]) return;                       // not linked yet

    char norm[CONVOY_CFG_ROOM_MAX];
    convoy_normalize_code(room, norm, sizeof(norm));
    if (strcmp(norm, s_cvw_room) == 0 &&
        (!call[0] || strcasecmp(call, s_cvw_call) == 0)) return;   // no change

    // Adopt it, and remember it so the next drive needs no linking at all.
    strncpy(s_cvw_room, norm, sizeof(s_cvw_room) - 1); s_cvw_room[sizeof(s_cvw_room) - 1] = '\0';
    if (call[0]) { strncpy(s_cvw_call, call, sizeof(s_cvw_call) - 1); s_cvw_call[sizeof(s_cvw_call) - 1] = '\0'; }
    convoy_cfg_set(s_cvw_room, s_cvw_call);
    snprintf(s_cvw_url, sizeof(s_cvw_url), "%s/convoys/%s/members.json",
             CONVOY_WIFI_DB_HOST, s_cvw_room);
    s_cvw_last_rx = 0;
    Serial.printf("[CVW] linked by app: room=%s self=%s\n", s_cvw_room, s_cvw_call);
}

// ── Public API (mirrors convoy_net_begin/loop/status/end) ────────────────────
// Shown on the Tracker's waiting panel so the user knows which entry in the
// app's board list is this board.
static const char * convoy_wifi_device_name(void) { return s_cvw_dev_name; }
static bool convoy_wifi_begin(void) {
    s_cvw_joined = s_cvw_begun = false;
    s_cvw_last_rx = 0;
    s_cvw_next_poll = 0;

    convoy_cfg_get_room(s_cvw_room, sizeof(s_cvw_room));
    convoy_cfg_get_callsign(s_cvw_call, sizeof(s_cvw_call));

    // Stable identity from the eFuse MAC — the name the app's board list shows.
    const uint64_t mac = ESP.getEfuseMac();
    snprintf(s_cvw_dev_id, sizeof(s_cvw_dev_id), "%04X%08X",
             (uint16_t)(mac >> 32), (uint32_t)mac);
    snprintf(s_cvw_dev_name, sizeof(s_cvw_dev_name), "Trailmaster-%04X",
             (uint16_t)(mac & 0xFFFF));

    Serial.printf("[CVW] joining a saved network; internal=%u\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    if (!ota_wifi_connect_saved()) {
        Serial.println("[CVW] no saved network reachable — hotspot on? 2.4GHz?");
        return false;
    }
    s_cvw_joined = true;

    snprintf(s_cvw_url, sizeof(s_cvw_url), "%s/convoys/%s/members.json",
             CONVOY_WIFI_DB_HOST, s_cvw_room);
    snprintf(s_cvw_pair_url, sizeof(s_cvw_pair_url), "%s/devices/%s.json",
             CONVOY_WIFI_DB_HOST, s_cvw_dev_id);
    s_cvw_client.setInsecure();          // same posture as the OTA client
    s_cvw_http.setReuse(true);
    s_cvw_http.setTimeout(8000);
    s_cvw_begun = true;                  // cvw_request() owns begin()/end() now

    Serial.printf("[CVW] joined '%s' rssi=%d ip=%s as '%s' room=%s self=%s internal=%u\n",
                  WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(),
                  s_cvw_dev_name, s_cvw_room[0] ? s_cvw_room : "(unlinked)",
                  s_cvw_call[0] ? s_cvw_call : "(unset)",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
}

static void convoy_wifi_loop(void) {
    if (!s_cvw_joined) return;
    const uint32_t now = millis();

    if (WiFi.status() != WL_CONNECTED) {
        // Hotspot went away (phone locked the radio, drove out of range). Don't
        // spin on it — the next tick retries, and the roster ages out to
        // "offline" on its own rather than vanishing.
        return;
    }

    // Announce / check for an assignment. Fast while unlinked because the user
    // is watching the app for this board to appear; lazy afterwards.
    if ((int32_t)(now - s_cvw_next_pair) >= 0) {
        s_cvw_next_pair = now + (s_cvw_room[0] ? CONVOY_WIFI_PAIR_SLOW_MS
                                               : CONVOY_WIFI_PAIR_FAST_MS);
        convoy_wifi_pair_tick();
    }

    if (!s_cvw_room[0]) return;         // nothing to poll until we are linked
    if ((int32_t)(now - s_cvw_next_poll) < 0) return;
    s_cvw_next_poll = now + CONVOY_WIFI_POLL_MS;

    String payload;
    if (cvw_request(s_cvw_url, "GET", nullptr, &payload))
        convoy_wifi_apply(payload.c_str());
}

static convoy_wifi_state_t convoy_wifi_status(void) {
    if (!s_cvw_joined) return CONVOY_WIFI_NO_NET;
    if (!s_cvw_room[0]) return CONVOY_WIFI_UNPAIRED;
    if (s_cvw_last_rx && millis() - s_cvw_last_rx < CONVOY_WIFI_RX_TTL_MS)
        return CONVOY_WIFI_ONLINE;
    return CONVOY_WIFI_WAITING;
}

static void convoy_wifi_end(void) {
    if (s_cvw_begun) { s_cvw_http.end(); s_cvw_begun = false; }
    if (s_cvw_joined) {
        // Leave the radio powered: the OBD worker owns WiFi and re-inits it on
        // its own once convoy_radio_mode clears. Calling WIFI_OFF from here is
        // the cross-task move that corrupted the BLE handover.
        WiFi.disconnect(true, true);
        s_cvw_joined = false;
    }
    Serial.printf("[CVW] link down; internal=%u\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

#endif // CONVOY_WIFI_H
