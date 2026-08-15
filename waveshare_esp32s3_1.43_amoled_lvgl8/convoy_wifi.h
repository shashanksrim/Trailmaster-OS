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
#include "convoy_cfg.h"      // room code + callsign, set from the app
#include "gps.h"             // gps_has_fix() — the local receiver outranks the roster
#include "pair_code.h"      // the six characters that identify THIS board to a phone

// Firebase Realtime DB — keep in sync with docs/convoy/config.js (databaseURL).
#define CONVOY_WIFI_DB_HOST "https://trailmaster-e43b1-default-rtdb.asia-southeast1.firebasedatabase.app"

// The room code and callsign are NOT compiled in. They arrive from the app's
// board picker, which writes them into this board's devices/<id> node (the user
// taps their board; nothing is typed), and convoy_cfg.h persists them. The
// captive portal used to offer a manual override — that was removed when the
// portal shrank to Wi-Fi provisioning, since a phone on the board's AP has no
// internet and so cannot be in a room in the first place.
// CALLSIGN is which member in the room is THIS car: the board has no GPS, so its
// own position comes from the owner phone's entry, found by callsign. Everyone
// else in the room renders as another car.

// How often the board pushes its OWN position into the room.
//
// Deliberately slower than the read poll. Reads are cheap and latency-sensitive
// (you want other cars to move smoothly); writes cost RTDB quota and fan out to
// every listening phone, so 3 s is a better trade — at road speed that is ~40 m
// between updates, which the receiving apps interpolate over anyway.
#define CONVOY_WIFI_PUB_MS    3000

#define CONVOY_WIFI_POLL_MS   1500    // roster refresh cadence
#define CONVOY_WIFI_RX_TTL_MS 10000   // "online" if a roster arrived within this
#define CONVOY_WIFI_MOVING_MIN 1.5    // m/s: above this we treat heading as valid

// How often the board touches its own devices/<id> node — to announce itself so
// the app can list it, and to notice a room the app has assigned. Fast while
// unpaired (the user is staring at the screen waiting for it to appear in the
// app), lazy once configured (it only needs to catch a re-point to a new room).
#define CONVOY_WIFI_PAIR_FAST_MS 2000
#define CONVOY_WIFI_PAIR_SLOW_MS 10000

// How long after entering the Tracker the board stays listed and assignable.
//
// SECURITY: devices/<id> must be world-readable for the app to list boards, and
// world-writable for it to assign a room — there is no auth to scope it to an
// owner. A board that announced itself forever would be a public, permanently
// re-pointable registry entry. Bounding it to a few minutes means the node only
// exists while someone is deliberately pairing, so a drive-by write to an idle
// board lands on nothing. Re-pointing at a new convoy still works — reopening
// the Tracker starts a fresh window.
#define CONVOY_WIFI_PAIR_WINDOW_MS 180000   // 3 minutes

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
static uint32_t s_cvw_pair_until = 0;     // window closes at this millis()
static bool     s_cvw_announced  = false; // devices/<id> currently published
static char     s_cvw_url[224];
static char     s_cvw_pair_url[224];
static char     s_cvw_pub_url[248];       // our own members/<id> node
static uint32_t s_cvw_next_pub  = 0;
static uint32_t s_cvw_published = 0;      // successful writes, for the log
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

    // The board's own GPS outranks the roster: it is ~250 ms fresh rather than
    // a 1.5 s poll behind, and it does not depend on the owner's phone being in
    // the room at all. Skip our self-write entirely while it has a fix,
    // otherwise this would overwrite the local position on every poll.
    if (!gps_has_fix()) {
        if (self_idx >= 0) {
            const convoy_member_t *s = &mem[self_idx];
            convoy_set_self(s->lat, s->lon, s->has_fix);
            if (s->has_fix) convoy_self_updates++;
            if (s->has_fix && s->heading >= 0)
                convoy_set_heading(s->heading, s->speed >= CONVOY_WIFI_MOVING_MIN);
        } else {
            // Nobody in the room is us. The radar still shows everyone else; we
            // just have no ME position, same as an unfixed GPS.
            convoy_set_self(0, 0, false);
        }
    }

    int car = 0;
    for (int i = 0; i < n && car < CONVOY_MAX_CARS; i++) {
        // Skip EVERY entry bearing our callsign, not just the first match.
        // Now that the board publishes its own position, the room can legitimately
        // hold two entries for this car — the board's and the owner phone's, if
        // the app is also running. Keying off self_idx alone would skip one and
        // draw the other as a separate car sitting on top of us.
        if (i == self_idx) continue;
        if (s_cvw_call[0] && strcasecmp(mem[i].callsign, s_cvw_call) == 0) continue;
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

// Room-scoped URLs. Rebuilt whenever the room changes, which happens both at
// begin() and when the app re-points us mid-session via convoy_wifi_pair_tick().
static void cvw_build_room_urls(void) {
    snprintf(s_cvw_url, sizeof(s_cvw_url), "%s/convoys/%s/members.json",
             CONVOY_WIFI_DB_HOST, s_cvw_room);
    // We publish under the stable device id rather than a random member id, so
    // reconnecting updates our existing entry instead of littering the room
    // with a fresh ghost car on every drive.
    snprintf(s_cvw_pub_url, sizeof(s_cvw_pub_url), "%s/convoys/%s/members/%s.json",
             CONVOY_WIFI_DB_HOST, s_cvw_room, s_cvw_dev_id);
}

// ── Publish our own position ─────────────────────────────────────────────────
// The board is now a first-class convoy member, not just a display: other cars
// see this Trailmaster whether or not the owner's phone app is running. That is
// the whole point of putting a receiver on the board.
//
// Only ever called with a local fix. Publishing a stale or absent position
// would be worse than publishing nothing — the receiving apps have no way to
// tell a frozen car from a parked one, and would happily navigate toward it.
// When the fix drops we simply stop writing, and our "ts" ages out so everyone
// else's freshness filter marks us offline on its own.
static void convoy_wifi_publish(void) {
    if (!gps_has_fix()) return;

    gps_fix_t f;
    gps_get(&f);

    // heading -1 means "unknown", matching the convention convoy_roster.h uses
    // and what the web app already handles. A parked car has no course, and
    // sending 0 would point every other driver's arrow due north.
    const double hdg = f.course_valid ? f.course_deg : -1.0;

    // "ts" uses RTDB's server-value sentinel rather than our GPS clock. We DO
    // now have accurate UTC, but the phones all write server time, and mixing
    // clock sources would make the freshness comparison meaningless.
    char body[320];
    snprintf(body, sizeof(body),
             "{\"name\":\"%s\",\"callsign\":\"%s\",\"lat\":%.6f,\"lon\":%.6f,"
             "\"heading\":%.1f,\"speed\":%.2f,\"color\":\"#00E5FF\","
             "\"ts\":{\".sv\":\"timestamp\"}}",
             s_cvw_dev_name, s_cvw_call[0] ? s_cvw_call : "TM",
             f.lat, f.lon, hdg, f.speed_mps);

    if (cvw_request(s_cvw_pub_url, "PATCH", body, nullptr)) {
        s_cvw_published++;
        if (s_cvw_published == 1 || (s_cvw_published % 20) == 0)
            Serial.printf("[CVW] published #%u: %.6f,%.6f hdg=%.0f sats=%d\n",
                          (unsigned)s_cvw_published, f.lat, f.lon, hdg, f.sats_used);
    }
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
    // ORDER MATTERS: read, reconcile, then write.
    //
    // This used to announce first and read second, which made the two
    // directions mutually exclusive. Writing our callsign before reading would
    // destroy an app assignment we had not seen yet; not writing it at all (the
    // old behaviour) meant an on-device callsign change never reached the app,
    // so the board advertised a name nobody else ever saw. Reading first lets
    // both work: the app's change is adopted below, and whatever survives
    // reconciliation is what we publish back.
    String node;
    if (!cvw_request(s_cvw_pair_url, "GET", nullptr, &node)) return;

    char room[CONVOY_CFG_ROOM_MAX] = {}, call[CONVOY_CFG_CALL_MAX] = {};
    json_get_str(node.c_str(), "room",     room, sizeof(room));
    json_get_str(node.c_str(), "callsign", call, sizeof(call));
    // Publish our identity back, including the callsign that survived the
    // reconciliation below. Done unconditionally (even before a room exists) so
    // the board still appears in the app's list for pairing.
    {
        char body[200];
        snprintf(body, sizeof(body),
                 "{\"name\":\"%s\",\"callsign\":\"%s\",\"ts\":{\".sv\":\"timestamp\"}}",
                 s_cvw_dev_name, s_cvw_call[0] ? s_cvw_call : "TM");
        if (cvw_request(s_cvw_pair_url, "PATCH", body, nullptr)) s_cvw_announced = true;
    }

    // Publish the code -> device mapping the app resolves when someone types the
    // six characters shown on this board's settings screen. Written under the
    // CODE, not the device id, which is the whole point: you cannot look up a
    // board without already knowing its code, and the code is only ever on its
    // own screen. Rules keep the pair collection itself unlistable, so there is
    // nothing to enumerate — only ~887 million exact guesses.
    {
        char url[224], body[200];
        snprintf(url, sizeof(url), "%s/pair/%s.json", CONVOY_WIFI_DB_HOST, pair_code_get());
        snprintf(body, sizeof(body), "{\"dev\":\"%s\",\"name\":\"%s\",\"ts\":{\".sv\":\"timestamp\"}}",
                 s_cvw_dev_id, s_cvw_dev_name);
        cvw_request(url, "PATCH", body, nullptr);
    }

    // Wi-Fi handed down from the app's Wi-Fi tab. Consumed and DELETED in the
    // same tick: this node is world-readable, so a credential left sitting in it
    // is a credential published. Clearing it is the only thing keeping the
    // exposure to seconds rather than forever — see the app's own warning.
    //
    // Handled before the room check because adding a network is useful even on a
    // board that has never been linked to a convoy.
    {
        char w_ssid[33] = {}, w_pass[65] = {};
        json_get_str(node.c_str(), "wssid", w_ssid, sizeof(w_ssid));
        json_get_str(node.c_str(), "wpass", w_pass, sizeof(w_pass));
        if (w_ssid[0]) {
            ota_add_network(w_ssid, w_pass);
            cvw_request(s_cvw_pair_url, "PATCH", "{\"wssid\":null,\"wpass\":null}", nullptr);
            Serial.printf("[CVW] wifi '%s' saved from the app; node cleared\n", w_ssid);
        }
    }

    if (!room[0]) return;                       // not linked yet

    char norm[CONVOY_CFG_ROOM_MAX];
    convoy_normalize_code(room, norm, sizeof(norm));

    // Only adopt a callsign the APP ACTIVELY CHANGED, not merely one that
    // differs from ours.
    //
    // This node is the app's inbox, and it keeps returning whatever was written
    // last — so a callsign the user just set on the device portal loses to the
    // stale cloud value on the very next poll. Observed 2026-08-12: the portal
    // set "Try1", this tick read back "TM1" from a node written nine days
    // earlier, and every position published thereafter carried the wrong
    // callsign with no indication why.
    //
    // Remembering the last value we took FROM the cloud disambiguates the two
    // cases: unchanged means stale (keep the local edit), changed means the app
    // genuinely reassigned us (adopt it).
    static char s_cvw_cloud_call[CONVOY_CFG_CALL_MAX] = {0};
    const bool app_changed_call = call[0] && strcasecmp(call, s_cvw_cloud_call) != 0;
    if (call[0]) { strncpy(s_cvw_cloud_call, call, sizeof(s_cvw_cloud_call) - 1);
                   s_cvw_cloud_call[sizeof(s_cvw_cloud_call) - 1] = '\0'; }

    if (strcmp(norm, s_cvw_room) == 0 && !app_changed_call) return;   // nothing new

    // Adopt it, and remember it so the next drive needs no linking at all.
    strncpy(s_cvw_room, norm, sizeof(s_cvw_room) - 1); s_cvw_room[sizeof(s_cvw_room) - 1] = '\0';
    if (app_changed_call) { strncpy(s_cvw_call, call, sizeof(s_cvw_call) - 1); s_cvw_call[sizeof(s_cvw_call) - 1] = '\0'; }
    convoy_cfg_set(s_cvw_room, s_cvw_call);
    cvw_build_room_urls();
    s_cvw_last_rx = 0;
    Serial.printf("[CVW] linked: room=%s self=%s (%s)\n", s_cvw_room, s_cvw_call,
                  app_changed_call ? "callsign from app" : "callsign kept from device");
}

// Withdraw from the board list. Called when the pairing window closes and when
// leaving the Tracker, so an idle board is not sitting in a public, writable
// registry for anyone to re-point.
static void convoy_wifi_unannounce(void) {
    if (!s_cvw_announced) return;
    s_cvw_announced = false;
    cvw_request(s_cvw_pair_url, "DELETE", nullptr, nullptr);
    Serial.println("[CVW] pairing window closed — withdrawn from the board list");
}

// ── Public API (mirrors convoy_net_begin/loop/status/end) ────────────────────
// Shown on the Tracker's waiting panel so the user knows which entry in the
// app's board list is this board.
static const char * convoy_wifi_device_name(void) { return s_cvw_dev_name; }

// True while this board is still listed and will accept a room assignment.
static bool convoy_wifi_pairing_open(void) {
    return s_cvw_joined && (int32_t)(millis() - s_cvw_pair_until) < 0;
}
static bool convoy_wifi_begin(void) {
    s_cvw_joined = s_cvw_begun = false;
    s_cvw_last_rx = 0;
    s_cvw_next_poll = 0;
    s_cvw_next_pub  = 0;
    s_cvw_published = 0;

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

    cvw_build_room_urls();
    snprintf(s_cvw_pair_url, sizeof(s_cvw_pair_url), "%s/devices/%s.json",
             CONVOY_WIFI_DB_HOST, s_cvw_dev_id);
    s_cvw_client.setInsecure();          // same posture as the OTA client
    s_cvw_http.setReuse(true);
    s_cvw_http.setTimeout(8000);
    s_cvw_begun = true;                  // cvw_request() owns begin()/end() now
    s_cvw_announced  = false;
    s_cvw_pair_until = millis() + CONVOY_WIFI_PAIR_WINDOW_MS;

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

    // Announce / check for an assignment, but only inside the pairing window.
    // Fast while unlinked because the user is watching the app for this board to
    // appear; lazy afterwards, when it only needs to catch a re-point.
    if ((int32_t)(now - s_cvw_pair_until) < 0) {
        if ((int32_t)(now - s_cvw_next_pair) >= 0) {
            s_cvw_next_pair = now + (s_cvw_room[0] ? CONVOY_WIFI_PAIR_SLOW_MS
                                                   : CONVOY_WIFI_PAIR_FAST_MS);
            convoy_wifi_pair_tick();
        }
    } else {
        convoy_wifi_unannounce();       // no-op once already withdrawn
    }

    if (!s_cvw_room[0]) return;         // nothing to poll until we are linked

    // Publish before polling. Both share one TLS connection, and writing first
    // means the roster we then read already contains our current position —
    // which keeps our own entry consistent with what the other cars just saw.
    if ((int32_t)(now - s_cvw_next_pub) >= 0) {
        s_cvw_next_pub = now + CONVOY_WIFI_PUB_MS;
        convoy_wifi_publish();
    }

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
    // Withdraw BEFORE the socket goes: leaving the Tracker should take the board
    // out of the public list immediately, not leave it there until it ages out.
    if (s_cvw_joined && WiFi.status() == WL_CONNECTED) convoy_wifi_unannounce();
    // setReuse(true) is what makes polling cheap: end() deliberately KEEPS the
    // socket and its mbedTLS context alive so the next request skips the ~47KB
    // handshake. On teardown that is exactly wrong — it strands those 47KB of
    // INTERNAL RAM, which is the memory the BT controller needs to come up. Left
    // as-is, each cloud session walked the free-heap baseline down (174K -> 130K
    // -> 127K) until switching to Meshtastic could no longer bring NimBLE up.
    // Turning reuse off first makes end() actually close it, and stop() frees the
    // TLS context even if the socket was already gone.
    s_cvw_http.setReuse(false);
    if (s_cvw_begun) { s_cvw_http.end(); s_cvw_begun = false; }
    s_cvw_client.stop();
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
