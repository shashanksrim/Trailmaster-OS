// Convoy NET link — BLE *peripheral* driven by a phone (Web Bluetooth).
//
// This is the network (online) convoy path, the counterpart to convoy_link.h
// (the Meshtastic mesh path). Here the board is the GATT SERVER: the paired
// phone runs the Trailmaster Convoy web app, gets everyone's GPS from Firebase
// over cellular, and writes a compact roster to us over BLE. We parse it and
// feed the SAME convoy_set_self()/convoy_set_heading()/convoy_set_car() the
// radar (convoy_ui.h) already renders. The board needs no GPS and no internet —
// the phone is the gateway.
//
// FIRMWARE-ONLY (needs NimBLE + a radio). Not included by the WASM sim.
// Requires NimBLE-Arduino 2.x (matches convoy_link.h).
//
// Threading: NimBLE runs the write callback on its own stack task. The callback
// only writes the plain-data convoy_set_* globals (no LVGL) — same discipline as
// convoy_link.h. The UI thread's lv_timer calls convoy_refresh().
//
// ── Wire format (phone → board, one write per update, newline-delimited) ──────
//   S,<lat>,<lon>,<hdg>,<spd>,<fix>        self = the connected phone's own car
//   C,<callsign>,<lat>,<lon>,<online>      one line per other car (repeated)
// Example:
//   S,12.971600,77.594600,184,8.3,1
//   C,LEAD,12.975000,77.598000,1
//   C,SWEEP,12.968000,77.590000,0
// hdg is degrees from true north (or -1 if unknown); spd is m/s; fix/online 0|1.
#ifndef CONVOY_NET_H
#define CONVOY_NET_H

#include <NimBLEDevice.h>
#include <string.h>
#include <stdio.h>
#include "convoy_ui.h"
#include "convoy_ble_guard.h"   // convoy_ble_init/up/deinit — never crash on a failed radio
#include "gps.h"                // gps_has_fix() — the local receiver outranks the phone

// Custom 128-bit UUIDs (also referenced by docs/convoy/app.js — keep in sync).
// 54524149="TRAI", 4d53="MS", 5452="TR".
#define CONVOY_NET_SVC "54524149-4d53-5452-0001-000000000001"
#define CONVOY_NET_CHR "54524149-4d53-5452-0001-000000000002"

#define CONVOY_NET_MOVING_MIN 1.5     // m/s: above this we treat heading as valid
#define CONVOY_NET_RX_TTL_MS  10000   // "online" if a roster arrived within this

static const uint32_t CONVOY_NET_HUES[6] = {   // mirrors convoy_ui / the web app
    0x00E5FF, 0x00E676, 0xFFD54F, 0xFF4081, 0xB388FF, 0xFF8A65
};

typedef enum {
    CONVOY_NET_ADVERTISING,   // no phone connected yet
    CONVOY_NET_CONNECTED,     // phone connected, no roster received yet
    CONVOY_NET_ONLINE         // roster streaming
} convoy_net_state_t;

static volatile bool     s_net_connected = false;
static volatile uint32_t s_net_last_rx   = 0;
static NimBLEServer      *s_net_server    = nullptr;

// ── Parse one roster blob → convoy_set_* ─────────────────────────────────────
static void convoy_net_parse(const uint8_t *data, size_t len) {
    static char buf[640];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, data, n); buf[n] = 0;

    int car = 0;
    char *save = nullptr;
    for (char *ln = strtok_r(buf, "\r\n", &save); ln; ln = strtok_r(nullptr, "\r\n", &save)) {
        if (ln[0] == 'S') {
            // The phone's own fix is only a fallback now: when the board's GPS
            // has a lock it is fresher and does not depend on the phone's
            // location permission still being granted. Ignore the S line
            // entirely in that case rather than letting it overwrite us.
            if (gps_has_fix()) continue;
            double lat = 0, lon = 0, hdg = -1, spd = 0; int fix = 0;
            if (sscanf(ln, "S,%lf,%lf,%lf,%lf,%d", &lat, &lon, &hdg, &spd, &fix) >= 5) {
                bool valid = fix != 0 && (lat != 0.0 || lon != 0.0);
                convoy_set_self(lat, lon, valid);
                if (valid) convoy_self_updates++;
                if (valid && hdg >= 0) convoy_set_heading(hdg, spd >= CONVOY_NET_MOVING_MIN);
            }
        } else if (ln[0] == 'C' && car < CONVOY_MAX_CARS) {
            char cs[8] = {0}; double lat = 0, lon = 0; int online = 0;
            if (sscanf(ln, "C,%7[^,],%lf,%lf,%d", cs, &lat, &lon, &online) >= 4) {
                bool has_fix = (lat != 0.0 || lon != 0.0);
                convoy_set_car(car, cs, lat, lon,
                               lv_color_hex(CONVOY_NET_HUES[car % 6]),
                               online != 0, has_fix);
                car++;
            }
        }
    }
    // Hide any slots the roster no longer includes.
    for (int i = car; i < CONVOY_MAX_CARS; i++)
        convoy_set_car(i, "", 0, 0, lv_color_hex(0), false, false);

    s_net_last_rx = millis();
    Serial.printf("[NET] roster: %d car(s)\n", car);
}

// ── NimBLE callbacks (2.x signatures) ────────────────────────────────────────
class ConvoyNetChrCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &info) override {
        (void)info;
        NimBLEAttValue v = c->getValue();
        if (v.size()) convoy_net_parse(v.data(), v.size());
    }
};
class ConvoyNetSrvCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *s, NimBLEConnInfo &info) override {
        (void)s; (void)info; s_net_connected = true;
        Serial.println("[NET] phone connected");
    }
    void onDisconnect(NimBLEServer *s, NimBLEConnInfo &info, int reason) override {
        (void)s; (void)info; (void)reason; s_net_connected = false;
        Serial.println("[NET] phone disconnected — re-advertising");
        NimBLEDevice::startAdvertising();
    }
};
static ConvoyNetChrCb s_net_chr_cb;
static ConvoyNetSrvCb s_net_srv_cb;

// ── Public API (mirrors convoy_link_begin/end/status) ────────────────────────
static bool convoy_net_begin(void) {
    if (!convoy_ble_init("Trailmaster")) return false;
    NimBLEDevice::setSecurityAuth(true, false, false);   // bond, NO_PIN (Just Works)
    NimBLEDevice::setMTU(247);                            // room for a full roster

    s_net_server = NimBLEDevice::createServer();
    // The `false` is load-bearing: setCallbacks() defaults deleteCallbacks=true, so
    // NimBLE would `delete` s_net_srv_cb — a STATIC — from ~NimBLEServer() during
    // deinit. That asserts ("free() target pointer is outside heap areas") and
    // reboots the board on any switch away from phone mode. Same reason
    // convoy_link.h passes false to setScanCallbacks().
    s_net_server->setCallbacks(&s_net_srv_cb, false);
    NimBLEService *svc = s_net_server->createService(CONVOY_NET_SVC);
    NimBLECharacteristic *chr = svc->createCharacteristic(
        CONVOY_NET_CHR, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    chr->setCallbacks(&s_net_chr_cb);
    svc->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(CONVOY_NET_SVC);
    adv->setName("Trailmaster");
    adv->enableScanResponse(true);
    adv->start();
    Serial.println("[NET] advertising as 'Trailmaster' — waiting for phone");
    return true;
}

static convoy_net_state_t convoy_net_status(void) {
    if (!s_net_connected) return CONVOY_NET_ADVERTISING;
    if (millis() - s_net_last_rx < CONVOY_NET_RX_TTL_MS) return CONVOY_NET_ONLINE;
    return CONVOY_NET_CONNECTED;
}

static void convoy_net_end(void) {
    if (!convoy_ble_up()) return;   // never came up (or already down)
    NimBLEDevice::stopAdvertising();
    if (s_net_server && s_net_server->getConnectedCount() > 0) {
        // Drop the phone link and WAIT for it. Same hazard as convoy_link_end():
        // disconnect is asynchronous, and deinit'ing with a live connection tears the
        // stack down under it ("ble_hs_stop: failed to terminate connection"), after
        // which the next bring-up panics. Previously this only stopped advertising
        // and never dropped the peer at all.
        for (uint16_t h : s_net_server->getPeerDevices()) s_net_server->disconnect(h);
        for (int i = 0; i < 50 && s_net_server->getConnectedCount() > 0; i++)
            vTaskDelay(pdMS_TO_TICKS(20));
        if (s_net_server->getConnectedCount() > 0)
            Serial.println("[NET] WARN: phone still connected at deinit");
    }
    convoy_ble_deinit();
    s_net_server = nullptr; s_net_connected = false; s_net_last_rx = 0;
    Serial.printf("[NET] stopped, BLE deinit (internal RAM=%u)\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

#endif // CONVOY_NET_H
