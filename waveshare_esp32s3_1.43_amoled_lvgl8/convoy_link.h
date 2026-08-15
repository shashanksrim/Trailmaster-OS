// Convoy link — BLE client to the co-located Meshtastic T-Beam.
//
// The T-Beam is the BLE peripheral/GATT server (runs full Meshtastic); this
// board is the central/client. We bond once (NO_PIN "Just Works"), run the
// Meshtastic want_config handshake, and stream the node DB, feeding
// convoy_set_self()/convoy_set_heading()/convoy_set_car() in convoy_ui.h.
//
// FIRMWARE-ONLY (needs NimBLE + a real radio). Not included by the WASM sim.
// Requires NimBLE-Arduino 2.x (ESP32 Arduino core 3.x). API differs from 1.x.
//
// BRING-UP MODE: with CONVOY_AUTOBIND, if no MAC is bound we scan and connect
// to the STRONGEST Meshtastic advertiser (your dash T-Beam) without persisting
// — easy to re-test on the bench. The proximity picker (convoy_picker_ui.h) and
// persistent MAC binding come once the link is proven.
//
// Threading: run convoy_link_loop() from a dedicated FreeRTOS task, NOT the
// LVGL thread. It only writes the plain-data convoy_set_* globals; the UI
// thread's lv_timer calls convoy_refresh().
#ifndef CONVOY_LINK_H
#define CONVOY_LINK_H

#include <NimBLEDevice.h>
#include <Preferences.h>
#include <string>
#include "convoy_ui.h"
#include "convoy_ble_guard.h"   // convoy_ble_init/up/deinit — never crash on a failed radio
#include "gps.h"                // gps_has_fix() — the local receiver outranks the T-Beam

#define CONVOY_AUTOBIND 0   // diagnostic: scan+log only, don't connect yet
#define CONVOY_BENCH_MAC "a4:f0:0f:d8:c8:1a"  // bench: connect straight to C2

// ── Meshtastic BLE GATT API (verify against the flashed firmware, stable 2.7.x)
#define MESH_SVC       "6ba1b218-15a8-461f-9fa8-5dcae273eafd"
#define MESH_TORADIO   "f75c76d2-129e-4dad-a1dd-7866124401e7"  // write
#define MESH_FROMRADIO "2c55e69e-4993-11ed-b878-0242ac120002"  // read (drain)
#define MESH_FROMNUM   "ed9da18c-a800-4f66-a670-aa7547e34453"  // notify: data ready

// ── Meshtastic protobuf field tags (VERIFY against the pinned .proto) ────────
#define TAG_FR_PACKET        2
#define TAG_FR_MYINFO        3
#define TAG_FR_NODEINFO      4
#define TAG_FR_CONFIG_DONE   7
#define TAG_MYINFO_NUM       1
#define TAG_NODE_NUM         1
#define TAG_NODE_USER        2
#define TAG_NODE_POS         3
#define TAG_USER_SHORT       3
#define TAG_POS_LAT          1   // sfixed32, *1e7
#define TAG_POS_LON          2   // sfixed32, *1e7
#define TAG_POS_SPEED       15
#define TAG_POS_TRACK       16   // heading; VERIFY scaling
#define TAG_PKT_FROM         1
#define TAG_PKT_DECODED      4
#define TAG_DATA_PORT        1
#define TAG_DATA_PAYLOAD     2
#define TAG_TR_WANTCONFIG    3
#define PORTNUM_POSITION     3

#define CONVOY_TRACK_SCALE   1.0    // ground_track → degrees (verify on device)
#define CONVOY_MOVING_MIN    1.5    // "moving" threshold (verify units)

#define CONVOY_SCAN_MAX 8
typedef struct { char name[24]; char mac[18]; int rssi; } convoy_scan_dev_t;

typedef enum {
    CONVOY_LINK_IDLE, CONVOY_LINK_SCANNING, CONVOY_LINK_CONNECTING, CONVOY_LINK_ONLINE
} convoy_link_state_t;

typedef struct {
    uint32_t num; char name[6]; double lat, lon; bool has_pos; uint32_t last_ms;
} convoy_node_t;

static convoy_link_state_t s_link_state = CONVOY_LINK_IDLE;
static NimBLEClient       *s_client   = nullptr;
static NimBLERemoteCharacteristic *s_toradio = nullptr;
static NimBLERemoteCharacteristic *s_fromradio = nullptr;
static uint32_t            s_client_gen = 0;   // stack incarnation s_client came from
static NimBLEAddress        s_target;
static bool                 s_have_target = false;
static volatile bool        s_drain_pending = false;
static uint32_t             s_my_num = 0;
static convoy_node_t        s_nodes[CONVOY_MAX_CARS + 2];
static int                  s_node_count = 0;
static convoy_scan_dev_t    s_scan[CONVOY_SCAN_MAX];
static volatile int         s_scan_count = 0;
static uint32_t             s_next_action_ms = 0;
static uint32_t             s_last_poll_ms = 0;

static const uint32_t CONVOY_HUES[6] = {
    0x00E5FF, 0x00E676, 0xFFD54F, 0xFF4081, 0xB388FF, 0xFF8A65
};

// Drop any NimBLE pointer left over from a previous incarnation of the stack. The
// phone role's teardown (convoy_net_end) frees these without being able to null them
// from here, so they must be checked before use — see convoy_ble_gen(). Idempotent.
static inline void convoy_link_drop_stale(void) {
    if (s_client_gen == convoy_ble_gen()) return;
    s_client = nullptr; s_toradio = nullptr; s_fromradio = nullptr;
    s_have_target = false; s_drain_pending = false;
    s_link_state = CONVOY_LINK_IDLE;
    s_client_gen = convoy_ble_gen();
}

// ── Tiny protobuf reader (only the fields we care about) ─────────────────────
typedef struct { const uint8_t *p, *end; } pb_t;
static inline bool pb_varint(pb_t *b, uint64_t *out) {
    uint64_t v = 0; int shift = 0;
    while (b->p < b->end) { uint8_t c = *b->p++; v |= (uint64_t)(c & 0x7F) << shift;
        if (!(c & 0x80)) { *out = v; return true; } shift += 7; if (shift > 63) return false; }
    return false;
}
static inline bool pb_fixed32(pb_t *b, uint32_t *out) {
    if (b->end - b->p < 4) return false;
    *out = (uint32_t)b->p[0] | ((uint32_t)b->p[1] << 8) | ((uint32_t)b->p[2] << 16) | ((uint32_t)b->p[3] << 24);
    b->p += 4; return true;
}
static inline bool pb_tag(pb_t *b, int *field, int *wire) {
    if (b->p >= b->end) return false;
    uint64_t key; if (!pb_varint(b, &key)) return false;
    *field = (int)(key >> 3); *wire = (int)(key & 7); return true;
}
static inline bool pb_skip(pb_t *b, int wire) {
    uint64_t v; uint32_t w;
    switch (wire) {
        case 0: return pb_varint(b, &v);
        case 2: if (!pb_varint(b, &v)) return false; if ((uint64_t)(b->end - b->p) < v) return false; b->p += v; return true;
        case 5: return pb_fixed32(b, &w);
        case 1: if (b->end - b->p < 8) return false; b->p += 8; return true;
        default: return false;
    }
}
static inline bool pb_submsg(pb_t *b, pb_t *sub) {
    uint64_t len; if (!pb_varint(b, &len)) return false;
    if ((uint64_t)(b->end - b->p) < len) return false;
    sub->p = b->p; sub->end = b->p + len; b->p += len; return true;
}

// ── Node table + convoy_ui mapping ───────────────────────────────────────────
static convoy_node_t *convoy_node_get(uint32_t num) {
    for (int i = 0; i < s_node_count; i++) if (s_nodes[i].num == num) return &s_nodes[i];
    if (s_node_count >= (int)(sizeof(s_nodes) / sizeof(s_nodes[0]))) return nullptr;
    convoy_node_t *n = &s_nodes[s_node_count++];
    memset(n, 0, sizeof(*n)); n->num = num;
    snprintf(n->name, sizeof(n->name), "%04X", (unsigned)(num & 0xFFFF));
    return n;
}
static void convoy_link_push(void) {
    int slot = 0;
    for (int i = 0; i < s_node_count && slot < CONVOY_MAX_CARS; i++) {
        convoy_node_t *n = &s_nodes[i];
        if (n->num == s_my_num) continue;
        bool online = (millis() - n->last_ms) < 120000;
        convoy_set_car(slot, n->name, n->lat, n->lon, lv_color_hex(CONVOY_HUES[slot % 6]), online, n->has_pos);
        slot++;
    }
}
static void convoy_parse_position(pb_t pos, uint32_t num) {
    convoy_node_t *n = convoy_node_get(num); if (!n) return;
    double lat = 0, lon = 0, track = -1; bool moving = false; int f, w;
    while (pb_tag(&pos, &f, &w)) {
        if (f == TAG_POS_LAT && w == 5) { uint32_t v; pb_fixed32(&pos, &v); lat = (int32_t)v / 1e7; }
        else if (f == TAG_POS_LON && w == 5) { uint32_t v; pb_fixed32(&pos, &v); lon = (int32_t)v / 1e7; }
        else if (f == TAG_POS_TRACK && w == 0) { uint64_t v; pb_varint(&pos, &v); track = (double)v * CONVOY_TRACK_SCALE; }
        else if (f == TAG_POS_SPEED && w == 0) { uint64_t v; pb_varint(&pos, &v); moving = ((double)v >= CONVOY_MOVING_MIN); }
        else pb_skip(&pos, w);
    }
    // Meshtastic sends 0,0 when there's no fix — treat that as "no position".
    bool valid = (lat != 0.0 || lon != 0.0);
    n->lat = lat; n->lon = lon; n->has_pos = valid; n->last_ms = millis();
    if (num == s_my_num) {
        // The board's own receiver outranks the T-Beam's. This matters most
        // off-grid: the T-Beam usually rides inside the cabin, so it is the node
        // most likely to report 0,0 for want of sky view — exactly the case the
        // waiting panel calls "Mesh linked - no GPS fix". With a local fix we
        // have a position even when the mesh radio does not.
        // n->lat/lon are still updated above, so the node roster stays truthful.
        if (!gps_has_fix()) {
            convoy_set_self(lat, lon, valid);
            convoy_self_updates++;             // field-debug: own-position packets
            if (valid && track >= 0) convoy_set_heading(track, moving);
        }
        Serial.printf("[CVY] self %.6f,%.6f fix=%d trk=%.0f mv=%d%s\n",
                      lat, lon, valid, track, moving,
                      gps_has_fix() ? " (local GPS wins)" : "");
    } else {
        Serial.printf("[CVY] node %04X %s %.6f,%.6f fix=%d\n", (unsigned)(num & 0xFFFF), n->name, lat, lon, valid);
    }
}
static void convoy_parse_nodeinfo(pb_t ni) {
    uint32_t num = 0; pb_t user = {0,0}, pos = {0,0}; bool have_user=false, have_pos=false; int f, w;
    while (pb_tag(&ni, &f, &w)) {
        if (f == TAG_NODE_NUM && w == 0) { uint64_t v; pb_varint(&ni, &v); num = (uint32_t)v; }
        else if (f == TAG_NODE_USER && w == 2) { pb_submsg(&ni, &user); have_user = true; }
        else if (f == TAG_NODE_POS && w == 2) { pb_submsg(&ni, &pos); have_pos = true; }
        else pb_skip(&ni, w);
    }
    if (!num) return;
    convoy_node_t *n = convoy_node_get(num);
    if (n && have_user) {
        pb_t u = user; int uf, uw;
        while (pb_tag(&u, &uf, &uw)) {
            if (uf == TAG_USER_SHORT && uw == 2) {
                pb_t s; if (pb_submsg(&u, &s)) { int len = (int)(s.end - s.p); if (len > 5) len = 5; memcpy(n->name, s.p, len); n->name[len] = 0; }
            } else pb_skip(&u, uw);
        }
    }
    if (have_pos) convoy_parse_position(pos, num);
}
static void convoy_parse_packet(pb_t pkt) {
    uint32_t from = 0; pb_t decoded = {0,0}; bool have_dec = false; int f, w;
    while (pb_tag(&pkt, &f, &w)) {
        if (f == TAG_PKT_FROM && w == 5) { uint32_t v; pb_fixed32(&pkt, &v); from = v; }
        else if (f == TAG_PKT_DECODED && w == 2) { pb_submsg(&pkt, &decoded); have_dec = true; }
        else pb_skip(&pkt, w);
    }
    if (!from || !have_dec) return;
    int port = -1; pb_t payload = {0,0}; bool have_pl = false; int df, dw;
    while (pb_tag(&decoded, &df, &dw)) {
        if (df == TAG_DATA_PORT && dw == 0) { uint64_t v; pb_varint(&decoded, &v); port = (int)v; }
        else if (df == TAG_DATA_PAYLOAD && dw == 2) { pb_submsg(&decoded, &payload); have_pl = true; }
        else pb_skip(&decoded, dw);
    }
    if (port == PORTNUM_POSITION && have_pl) convoy_parse_position(payload, from);
}
static void convoy_parse_fromradio(const uint8_t *buf, size_t len) {
    pb_t b = { buf, buf + len }; int f, w;
    while (pb_tag(&b, &f, &w)) {
        if (f == TAG_FR_MYINFO && w == 2) {
            pb_t mi; if (pb_submsg(&b, &mi)) { int mf, mw;
                while (pb_tag(&mi, &mf, &mw)) { if (mf == TAG_MYINFO_NUM && mw == 0) { uint64_t v; pb_varint(&mi, &v); s_my_num = (uint32_t)v; Serial.printf("[CVY] my_node_num=%08X\n", (unsigned)s_my_num); } else pb_skip(&mi, mw); } }
        } else if (f == TAG_FR_NODEINFO && w == 2) { pb_t ni; if (pb_submsg(&b, &ni)) convoy_parse_nodeinfo(ni); }
        else if (f == TAG_FR_PACKET && w == 2)   { pb_t pk; if (pb_submsg(&b, &pk)) convoy_parse_packet(pk); }
        else if (f == TAG_FR_CONFIG_DONE)        { pb_skip(&b, w); Serial.println("[CVY] config_complete (node DB dumped)"); }
        else pb_skip(&b, w);
    }
    convoy_link_push();
}

// ── BLE plumbing (NimBLE 2.x) ────────────────────────────────────────────────
static void convoy_fromnum_cb(NimBLERemoteCharacteristic *c, uint8_t *d, size_t l, bool notify) {
    (void)c; (void)d; (void)l; (void)notify; s_drain_pending = true;
}
static void convoy_send_want_config(void) {
    static uint32_t nonce = 0x54524149;
    uint8_t msg[8]; int i = 0; msg[i++] = (TAG_TR_WANTCONFIG << 3) | 0;
    uint32_t v = nonce++;
    do { uint8_t byte = v & 0x7F; v >>= 7; if (v) byte |= 0x80; msg[i++] = byte; } while (v);
    if (s_toradio) { s_toradio->writeValue(msg, i, true); Serial.println("[CVY] sent want_config"); }
}
static void convoy_drain_fromradio(void) {
    if (!s_fromradio) return;
    for (int guard = 0; guard < 64; guard++) {
        NimBLEAttValue v = s_fromradio->readValue();
        if (v.size() == 0) break;
        convoy_parse_fromradio(v.data(), v.size());
    }
}
class ConvoyScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *d) override {
        std::string nm  = d->getName();
        std::string mac = d->getAddress().toString();
        bool hasSvc = d->isAdvertisingService(NimBLEUUID(MESH_SVC));
        // DIAGNOSTIC: log every advertiser so we can see how the T-Beam appears.
        Serial.printf("[CVY] adv name='%s' %s rssi=%d svc=%d\n",
                      nm.c_str(), mac.c_str(), d->getRSSI(), hasSvc);
        // Candidate = advertises the mesh service OR carries a (non-empty) name.
        // (Meshtastic often omits the 128-bit UUID from the adv packet; the node
        // name is the reliable signal on the bench.)
        if (!hasSvc && nm.empty()) return;
        for (int i = 0; i < s_scan_count; i++) if (mac == s_scan[i].mac) { s_scan[i].rssi = d->getRSSI(); return; }
        if (s_scan_count >= CONVOY_SCAN_MAX) return;
        convoy_scan_dev_t *e = &s_scan[s_scan_count];
        snprintf(e->mac, sizeof(e->mac), "%s", mac.c_str());
        snprintf(e->name, sizeof(e->name), "%s", nm.empty() ? "(no name)" : nm.c_str());
        e->rssi = d->getRSSI();
        s_scan_count++;
    }
};
static ConvoyScanCB s_scan_cb;

static bool convoy_connect_target(void) {
    if (!s_have_target) return false;
    Serial.printf("[CVY] connecting to %s ... (internal RAM=%u)\n",
                  s_target.toString().c_str(), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    if (!s_client) s_client = NimBLEDevice::createClient();
    if (!s_client->connect(s_target)) { Serial.println("[CVY] connect failed"); return false; }
    NimBLERemoteService *svc = s_client->getService(MESH_SVC);
    if (!svc) { Serial.println("[CVY] no mesh service"); s_client->disconnect(); return false; }
    s_toradio   = svc->getCharacteristic(MESH_TORADIO);
    s_fromradio = svc->getCharacteristic(MESH_FROMRADIO);
    NimBLERemoteCharacteristic *fromnum = svc->getCharacteristic(MESH_FROMNUM);
    if (!s_toradio || !s_fromradio || !fromnum) { Serial.println("[CVY] missing characteristic"); s_client->disconnect(); return false; }
    fromnum->subscribe(true, convoy_fromnum_cb);
    Serial.println("[CVY] connected + subscribed");
    convoy_send_want_config();
    s_drain_pending = true;
    return true;
}
static void convoy_start_scan(void) {
    s_scan_count = 0; s_link_state = CONVOY_LINK_SCANNING;
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_scan_cb, false);
    scan->setActiveScan(true);
    Serial.println("[CVY] scanning for Meshtastic...");
    scan->start(6000, false);   // 6 s, non-blocking
}

// ── Public API ───────────────────────────────────────────────────────────────
static bool convoy_link_begin(void) {
    if (!convoy_ble_init("Trailmaster")) return false;
    convoy_link_drop_stale();                            // fresh stack: forget old pointers
    NimBLEDevice::setSecurityAuth(true, false, false);   // bond, no MITM (NO_PIN)
    NimBLEDevice::setMTU(517);                            // large reads for FromRadio
    Preferences p; p.begin("convoy", true);
    String mac = p.getString("tbeam_mac", "");
    p.end();
    if (mac.length() >= 17) {
        s_target = NimBLEAddress(std::string(mac.c_str()), BLE_ADDR_PUBLIC);
        s_have_target = true; s_link_state = CONVOY_LINK_CONNECTING;
        Serial.printf("[CVY] bound MAC %s\n", mac.c_str());
    }
#ifdef CONVOY_BENCH_MAC
    else {
        s_target = NimBLEAddress(std::string(CONVOY_BENCH_MAC), BLE_ADDR_PUBLIC);
        s_have_target = true; s_link_state = CONVOY_LINK_CONNECTING;
        Serial.println("[CVY] bench target " CONVOY_BENCH_MAC);
    }
#else
    else {
        s_link_state = CONVOY_LINK_IDLE;
        Serial.println("[CVY] no bound MAC — will scan");
    }
#endif
    return true;
}

static void convoy_link_loop(void) {
    if (!convoy_ble_up()) return;
    convoy_link_drop_stale();
    NimBLEScan *scan = NimBLEDevice::getScan();
    switch (s_link_state) {
        case CONVOY_LINK_IDLE:
            convoy_start_scan();
            break;
        case CONVOY_LINK_SCANNING:
            if (scan->isScanning()) break;
            // scan finished — pick strongest Meshtastic advertiser
            if (s_scan_count > 0) {
                int best = 0; for (int i = 1; i < s_scan_count; i++) if (s_scan[i].rssi > s_scan[best].rssi) best = i;
                Serial.printf("[CVY] %d device(s); strongest '%s' %s (%d dBm)\n", s_scan_count, s_scan[best].name, s_scan[best].mac, s_scan[best].rssi);
#if CONVOY_AUTOBIND
                s_target = NimBLEAddress(std::string(s_scan[best].mac), BLE_ADDR_PUBLIC);
                s_have_target = true; s_link_state = CONVOY_LINK_CONNECTING;
#endif
            } else {
                Serial.println("[CVY] no Meshtastic found; rescan in 3s");
                s_next_action_ms = millis() + 3000; s_link_state = CONVOY_LINK_IDLE;
                // guard the immediate re-scan with a delay
                while (millis() < s_next_action_ms) vTaskDelay(pdMS_TO_TICKS(100));
            }
            break;
        case CONVOY_LINK_CONNECTING:
            if (convoy_connect_target()) s_link_state = CONVOY_LINK_ONLINE;
            else { s_have_target = false; s_link_state = CONVOY_LINK_IDLE; vTaskDelay(pdMS_TO_TICKS(2000)); }
            break;
        case CONVOY_LINK_ONLINE:
            if (!s_client || !s_client->isConnected()) { Serial.println("[CVY] disconnected"); s_have_target = false; s_link_state = CONVOY_LINK_IDLE; break; }
            // Drain on notify, plus a periodic poll every ~2.5s so we promptly
            // catch the T-Beam's own position update when its GPS finally locks.
            if (s_drain_pending || millis() - s_last_poll_ms > 2500) {
                s_drain_pending = false; s_last_poll_ms = millis();
                convoy_drain_fromradio();
            }
            break;
    }
}

static convoy_link_state_t convoy_link_status(void) { return s_link_state; }

// ── Source-picker support ────────────────────────────────────────────────────
// Scan-only bring-up: init the stack with no target so convoy_link_loop() just
// scans and fills s_scan (read via convoy_link_scan_list). Used by the picker.
static bool convoy_link_begin_scan(void) {
    Serial.println("[CVY] scan-only begin");
    if (!convoy_ble_init("Trailmaster")) return false;
    // Sync HERE, on a freshly built stack, and not from convoy_link_loop(): the task
    // calls convoy_link_connect_mac() between this and the first loop pass, so a late
    // drop would wipe the CONNECTING state that call just set and fall back to scanning.
    convoy_link_drop_stale();
    NimBLEDevice::setSecurityAuth(true, false, false);
    NimBLEDevice::setMTU(517);
    s_have_target = false;
    s_scan_count  = 0;
    s_link_state  = CONVOY_LINK_IDLE;   // loop starts scanning
    return true;
}
// Expose the current scan results (name/mac/rssi), newest snapshot. Returns count.
static int convoy_link_scan_list(convoy_scan_dev_t **out) { *out = s_scan; return s_scan_count; }
// Stop scanning and connect to a specific T-Beam MAC (from the picker).
static void convoy_link_connect_mac(const char *mac) {
    if (!convoy_ble_up()) return;
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan && scan->isScanning()) scan->stop();
    s_target = NimBLEAddress(std::string(mac), BLE_ADDR_PUBLIC);
    s_have_target = true;
    s_link_state = CONVOY_LINK_CONNECTING;
    Serial.printf("[CVY] connect to picked %s\n", mac);
}
// Restart scanning (stay central; no NimBLE re-init) — e.g. mesh → rescan.
static void convoy_link_rescan(void) {
    if (!convoy_ble_up()) return;
    convoy_link_drop_stale();          // s_client below may predate the current stack
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan && scan->isScanning()) scan->stop();
    if (s_client && s_client->isConnected()) s_client->disconnect();
    s_have_target = false;
    s_scan_count  = 0;
    s_link_state  = CONVOY_LINK_IDLE;   // loop restarts the scan
}

// Tear the link down and free the BLE stack (called when leaving the Tracker so
// the internal RAM goes back to WiFi/OBD).
static void convoy_link_end(void) {
    if (!convoy_ble_up()) return;   // never came up (or already down) — nothing to tear down
    // Stop any active scan BEFORE deinit — deinit while scanning panics NimBLE.
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan && scan->isScanning()) scan->stop();
    if (s_client && s_client->isConnected()) {
        s_client->disconnect();
        // disconnect() is ASYNCHRONOUS. Deinit'ing before the controller has actually
        // dropped the link tears the stack down underneath a live connection — the
        // log shows "ble_hs_stop: failed to terminate connection; rc=2" and the NEXT
        // NimBLEDevice::createClient() then panics inside its constructor. Wait for
        // the link to really be gone (~1s cap; it normally takes a few tens of ms).
        for (int i = 0; i < 50 && s_client->isConnected(); i++) vTaskDelay(pdMS_TO_TICKS(20));
        if (s_client->isConnected()) Serial.println("[CVY] WARN: link still up at deinit");
    }
    convoy_ble_deinit();
    s_client = nullptr; s_toradio = nullptr; s_fromradio = nullptr;
    s_have_target = false; s_drain_pending = false;
    s_node_count = 0; s_my_num = 0;
    s_link_state = CONVOY_LINK_IDLE;
    Serial.printf("[CVY] link stopped, BLE deinit (internal RAM=%u)\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

#endif // CONVOY_LINK_H
