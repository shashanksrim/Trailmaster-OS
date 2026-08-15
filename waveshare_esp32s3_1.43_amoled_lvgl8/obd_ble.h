// ELM327-over-BLE telemetry source — the alternative to the WiFi ELM327 the
// dash has used until now (SSID "WiFi_OBDII", TCP 192.168.0.10:35000).
//
// Both sources feed the SAME five volatile globals the gauges and speedometer
// already read (car_rpm / car_speed / car_engine_temp / car_engine_load /
// car_voltage), and both decode with the same pure parsers in obd_parse.h. So
// this is purely a transport swap: no UI, no parser and no gauge code changes.
//
// MEASURED ON HARDWARE 2026-08-05 (probe: test/ble_wifi_coex/), against the
// adapter advertising as 'OBDBLE'. This is what the wire actually looks like:
//
//   service 0xFFF0
//     char  0xFFF1   write + writeNR + NOTIFY   <- the serial pipe, both ways
//     char  0xFFF2   write only                 <- unused
//   MTU 248. Commands are CR-terminated ASCII. Responses arrive in chunks and
//   are complete when a '>' prompt shows up. NO pairing and NO passkey — the
//   "1234" on the sticker is the Bluetooth *Classic* SPP PIN and is irrelevant
//   here. Just connect and write.
//
//   ATDPN auto-detects 'A6' (ISO 15765-4, 11-bit, 500 kbit/s) on the JB74, so
//   unlike jimny_sniffer.py we do NOT need to force ATSP6 — auto-search works
//   with a healthy adapter. Confirmed live: 0100 -> two responders, 010C -> 41
//   0C 0B F6 (765 rpm at idle), 010D -> 41 0D 00.
//
// WHY THE RESPONSE FORMAT MATTERS: obd_parse.h scans for space-separated hex
// ("41 0C 1A F8"). ELM327 has an ATS0 command that strips those spaces, which
// would silently break every parser. We deliberately never send it.
#ifndef OBD_BLE_H
#define OBD_BLE_H

#include <NimBLEDevice.h>
#include <Preferences.h>
#include "convoy_ble_guard.h"   // shared NimBLE stack: refcounted acquire/release
#include "obd_parse.h"

// Defined in the .ino; written here, read by screen_ui.h / ui_godzillaspeedometer.
extern volatile int   car_engine_temp, car_engine_load, car_rpm, car_speed;
extern volatile float car_voltage;

// The adapter advertises the vendor service; some clones only put the name in
// the scan response, so we accept either. Matching is deliberately loose —
// these dongles are all rebadges of the same handful of modules.
#define OBD_BLE_SVC   "fff0"
#define OBD_BLE_CHR   "fff1"

typedef enum { OBD_BLE_DOWN, OBD_BLE_SEARCHING, OBD_BLE_LINKING, OBD_BLE_READY } obd_ble_state_t;

static obd_ble_state_t s_ob_state = OBD_BLE_DOWN;
static NimBLEClient   *s_ob_client = nullptr;
static NimBLERemoteCharacteristic *s_ob_chr = nullptr;
// NimBLEDevice::deinit(true) frees every client the stack owns, so a pointer
// cached across a teardown dangles. Record which incarnation of the stack our
// client came from and drop it when the generation moves — the same discipline
// convoy_link.h uses, and the fix for the LoadProhibited crash documented in
// convoy_ble_guard.h.
static uint32_t s_ob_gen = 0;
static char     s_ob_mac[18] = {0};       // bound adapter, remembered in NVS
// Must come from the ADVERTISEMENT, never assumed. 81:23:45:67:89:ba has the
// '10' high bits of a random-static address, but this clone actually reports
// PUBLIC — connecting with the wrong type just times out with no diagnostic.
static uint8_t  s_ob_addr_type = BLE_ADDR_PUBLIC;
static uint32_t s_ob_next_try_ms = 0;     // backoff between reconnect attempts

// ── Serial pipe over GATT ────────────────────────────────────────────────────
static char             s_ob_rx[512];
static volatile size_t  s_ob_rx_len = 0;

static void obd_ble_notify_cb(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify) {
    (void)c; (void)isNotify;
    for (size_t i = 0; i < len && s_ob_rx_len < sizeof(s_ob_rx) - 1; i++)
        s_ob_rx[s_ob_rx_len++] = (char)data[i];
    s_ob_rx[s_ob_rx_len] = '\0';
}

// Send one ELM command, wait for the '>' prompt. Returns false on timeout.
//
// The settle-then-clear order is not cosmetic: a slow reply (an auto protocol
// search can take >5 s) that lands after we gave up would otherwise be glued to
// the FRONT of the next command's buffer, and the parsers would decode the
// previous PID's bytes as this one's. Seen exactly that during bring-up.
static bool obd_ble_cmd(const char *cmd, uint32_t wait_ms) {
    if (!s_ob_chr) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    s_ob_rx_len = 0; s_ob_rx[0] = '\0';
    char line[24];
    int n = snprintf(line, sizeof(line), "%s\r", cmd);
    if (!s_ob_chr->writeValue((uint8_t *)line, n, false)) return false;
    const uint32_t deadline = millis() + wait_ms;
    while (millis() < deadline) {
        if (memchr(s_ob_rx, '>', s_ob_rx_len)) return true;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

static bool obd_ble_connected(void) {
    return s_ob_state == OBD_BLE_READY && s_ob_client && s_ob_client->isConnected();
}

// Link state, for the worker's handover check and for any status UI.
static inline obd_ble_state_t obd_ble_state(void) { return s_ob_state; }

// Forget a client that a stack teardown has already freed.
static void obd_ble_drop_stale(void) {
    if (s_ob_client && s_ob_gen != convoy_ble_gen()) {
        s_ob_client = nullptr; s_ob_chr = nullptr;
        s_ob_state  = OBD_BLE_DOWN;
    }
}

// ── Scan ─────────────────────────────────────────────────────────────────────
static bool s_ob_found = false;
class ObdScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *d) override {
        if (s_ob_found) return;
        std::string nm = d->getName();
        bool match = d->isAdvertisingService(NimBLEUUID(OBD_BLE_SVC));
        if (!match && !nm.empty()) {
            const char *n = nm.c_str();
            match = strcasestr(n, "obd") || strcasestr(n, "elm") || strcasestr(n, "vlink") ||
                    strcasestr(n, "vgate") || strcasestr(n, "veepeak") || strcasestr(n, "obdlink");
        }
        if (!match) return;
        s_ob_found = true;
        snprintf(s_ob_mac, sizeof(s_ob_mac), "%s", d->getAddress().toString().c_str());
        s_ob_addr_type = d->getAddress().getType();
        Serial.printf("[OBDBLE] found '%s' %s type=%u (%d dBm)\n",
                      nm.empty() ? "(no name)" : nm.c_str(), s_ob_mac,
                      s_ob_addr_type, d->getRSSI());
    }
};
static ObdScanCB s_ob_scan_cb;

// ── Bring-up ─────────────────────────────────────────────────────────────────
// Returns false without crashing if the radio is unavailable, exactly like the
// convoy paths — a failed controller init leaves NimBLE's mutexes NULL and the
// next call in asserts and reboots the board.
static bool obd_ble_begin(void) {
    if (!convoy_ble_acquire("Trailmaster")) {
        Serial.println("[OBDBLE] BLE stack unavailable — staying down");
        return false;
    }
    obd_ble_drop_stale();
    s_ob_gen = convoy_ble_gen();

    // Reuse the bound adapter if we have one; these clones all ship the SAME
    // cloned address (01:23:45:67:89:BA, advertised as 81:... with the random-
    // static bit set), so this is a fast path, not a unique identity.
    if (s_ob_mac[0] == '\0') {
        Preferences p;
        if (p.begin("obd", true)) {
            String m = p.getString("ble_mac", "");
            if (m.length() >= 17) {
                snprintf(s_ob_mac, sizeof(s_ob_mac), "%s", m.c_str());
                s_ob_addr_type = p.getUChar("ble_atype", BLE_ADDR_PUBLIC);
            }
            p.end();
        }
    }

    if (s_ob_mac[0] == '\0') {
        s_ob_state = OBD_BLE_SEARCHING;
        s_ob_found = false;
        NimBLEScan *scan = NimBLEDevice::getScan();
        scan->setScanCallbacks(&s_ob_scan_cb, false);
        scan->setActiveScan(true);          // names live in the scan response
        Serial.println("[OBDBLE] scanning for an ELM327...");
        scan->start(6000, false);
        const uint32_t dl = millis() + 8000;
        while (scan->isScanning() && millis() < dl) vTaskDelay(pdMS_TO_TICKS(50));
        scan->clearResults();
        if (!s_ob_found) { Serial.println("[OBDBLE] no adapter in range"); s_ob_state = OBD_BLE_DOWN; return false; }
        Preferences p; p.begin("obd", false);
        p.putString("ble_mac", s_ob_mac);
        p.putUChar("ble_atype", s_ob_addr_type);
        p.end();
    }

    s_ob_state = OBD_BLE_LINKING;
    if (!s_ob_client) s_ob_client = NimBLEDevice::createClient();
    NimBLEAddress addr(std::string(s_ob_mac), s_ob_addr_type);
    if (!s_ob_client->connect(addr)) {
        // A stale NVS binding (wrong type, or a different adapter) must not wedge
        // us forever: forget it so the next attempt rediscovers by scanning.
        Serial.printf("[OBDBLE] connect to %s type=%u failed — clearing binding\n",
                      s_ob_mac, s_ob_addr_type);
        s_ob_mac[0] = '\0';
        Preferences p; if (p.begin("obd", false)) { p.remove("ble_mac"); p.remove("ble_atype"); p.end(); }
        s_ob_state = OBD_BLE_DOWN;
        return false;
    }

    NimBLERemoteService *svc = s_ob_client->getService(NimBLEUUID(OBD_BLE_SVC));
    s_ob_chr = svc ? svc->getCharacteristic(NimBLEUUID(OBD_BLE_CHR)) : nullptr;
    if (!s_ob_chr || !s_ob_chr->canNotify()) {
        Serial.println("[OBDBLE] no FFF0/FFF1 serial pipe — not an ELM327 bridge?");
        s_ob_client->disconnect();
        s_ob_state = OBD_BLE_DOWN;
        return false;
    }
    s_ob_chr->subscribe(true, obd_ble_notify_cb);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Adapter init. ATE0 matters most: with echo on, every reply is prefixed by
    // the command itself and strstr("41 0C") would still match, but ATRV's
    // sscanf("%fV") would not. ATL0 drops the linefeeds. We never send ATS0 —
    // see the header comment.
    bool ok = obd_ble_cmd("ATZ", 5000);
    Serial.printf("[OBDBLE] ATZ -> %s\n", ok ? s_ob_rx : "(timeout)");
    obd_ble_cmd("ATE0", 2000);
    obd_ble_cmd("ATL0", 2000);
    obd_ble_cmd("0100", 12000);   // let auto-search establish the bus session
    obd_ble_cmd("ATDPN", 2000);
    Serial.printf("[OBDBLE] protocol -> %s (expect 'A6' on the JB74)\n", s_ob_rx);

    s_ob_state = OBD_BLE_READY;
    Serial.printf("[OBDBLE] READY (internal RAM=%u)\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
}

static void obd_ble_end(void) {
    obd_ble_drop_stale();
    if (s_ob_client) {
        if (s_ob_client->isConnected()) s_ob_client->disconnect();
        NimBLEDevice::deleteClient(s_ob_client);
        s_ob_client = nullptr;
    }
    s_ob_chr   = nullptr;
    s_ob_state = OBD_BLE_DOWN;
    convoy_ble_release();
}

// ── Poll ─────────────────────────────────────────────────────────────────────
// One pass. Mirrors the WiFi worker's screen-aware split so the adapter is not
// asked for gauges nobody is looking at: the round trip is ~50-100 ms per PID
// over BLE, so polling all five would visibly slow the speedometer.
//
// A failed read leaves the previous value in place — same contract as the pure
// parsers, which only write through the out pointer on a successful match.
static void obd_ble_poll(bool gauge_active) {
    if (!obd_ble_connected()) return;
    // Throttled readback of what actually landed in the globals the gauges read.
    // Cheap, and it is the only way to tell "the link is up" from "the link is up
    // and the numbers are moving" without a screen in front of you.
    static uint32_t last_log = 0;
    if (millis() - last_log > 3000) {
        last_log = millis();
        Serial.printf("[OBDBLE] rpm=%d speed=%d temp=%dC load=%d%% batt=%.1fV\n",
                      car_rpm, car_speed, car_engine_temp, car_engine_load, car_voltage);
    }
    if (gauge_active) {
        if (obd_ble_cmd("0105", 2000)) obd_parse_coolant(s_ob_rx, (int *)&car_engine_temp);
        if (obd_ble_cmd("0104", 2000)) obd_parse_load(s_ob_rx, (int *)&car_engine_load);
        if (obd_ble_cmd("ATRV", 2000)) obd_parse_voltage(s_ob_rx, (float *)&car_voltage);
    } else {
        if (obd_ble_cmd("010C", 2000)) obd_parse_rpm(s_ob_rx, (int *)&car_rpm);
        if (obd_ble_cmd("010D", 2000)) obd_parse_speed(s_ob_rx, (int *)&car_speed);
    }
}

// Keep the link up. Call from the OBD worker; it self-heals with a backoff so a
// dropped adapter (engine off, out of range) does not spin the task.
static bool obd_ble_service(void) {
    obd_ble_drop_stale();
    if (obd_ble_connected()) return true;
    if (millis() < s_ob_next_try_ms) return false;
    s_ob_next_try_ms = millis() + 5000;
    return obd_ble_begin();
}

#endif // OBD_BLE_H
