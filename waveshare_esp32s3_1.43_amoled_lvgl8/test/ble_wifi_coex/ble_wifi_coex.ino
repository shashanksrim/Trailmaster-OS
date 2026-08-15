// BLE + WiFi coexistence probe — standalone diagnostic sketch, NOT part of the
// firmware. Flash it, watch the serial log, flash the real firmware back.
//
// WHY: the whole convoy radio-handover dance (convoy_radio_mode /
// convoy_obd_released in the main sketch) exists because of one measured
// result on 2026-07-28: with WiFi up, NimBLEDevice::init() fails with
//   E BLE_INIT: Malloc failed / esp_bt_controller_init -4   (ESP_ERR_NO_MEM)
// at 21-47 KB free internal RAM. That spike concluded "BLE and WiFi cannot
// coexist on this build" and set CONVOY_KEEP_WIFI 0.
//
// Two things that spike did NOT establish, and that this probe measures:
//
//  1. ORDER. It only ever tried WiFi-first-then-BLE, because that is the order
//     the firmware naturally lands in (the OBD worker owns WiFi from boot).
//     The BT controller wants a big contiguous internal/DMA block and, once it
//     has it, holds it forever. WiFi's buffers are smaller, later, and partly
//     relocatable. Bringing BLE up FIRST, at boot, when internal RAM is at its
//     peak and unfragmented, is the standard ESP-IDF ordering and has never
//     been tried here. Phase A tests it; phase B reproduces the known failure
//     for a side-by-side number.
//
//  2. LARGEST FREE BLOCK. The spike logged only heap_caps_get_free_size().
//     esp_bt_controller_init() needs *contiguous* internal DMA memory, so
//     "47 KB free" can still fail if the largest block is 12 KB. Fragmentation
//     and exhaustion have completely different fixes, and the old log cannot
//     tell them apart. Every checkpoint here prints free AND largest-block for
//     INTERNAL and for INTERNAL|DMA.
//
// It also answers a second, separate question: is there actually a BLE OBD
// adapter to talk to? The dash's ELM327 today is the WiFi one (SSID
// "WiFi_OBDII", 192.168.0.10:35000). The dongle paired to the Mac as
// /dev/cu.OBDII is Bluetooth *Classic* SPP, which the ESP32-S3 cannot speak at
// all (S3 is BLE-only, no BR/EDR). Phase A's scan lists every advertiser and
// flags the ones carrying a known ELM327-over-BLE service UUID, so we find out
// whether a BLE-capable adapter is in range before designing anything around
// one.
//
// Build/flash (from the sketch dir):
//   arduino-cli compile --profile amoled test/ble_wifi_coex
//   arduino-cli upload  --profile amoled -p /dev/cu.usbmodem101 test/ble_wifi_coex
//
// Serial: 115200. Send 'b' within 3 s of boot to run phase B instead of A.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include "esp_heap_caps.h"
#include "esp_bt.h"
#include "esp_wifi.h"

// Same NVS namespace/layout OTAManager.cpp writes, so the probe can reuse the
// networks already saved on this board instead of hardcoding credentials.
#define OTA_NVS_NS       "ota_wifi"
#define OTA_MAX_NETWORKS 8

// Known "ELM327 over BLE" service UUIDs. Clone adapters are transparent-serial
// bridges, so they advertise whatever module the vendor used:
//   FFF0 - the common CC254x/Vgate iCar Pro pattern
//   FFE0 - HM-10 style
//   18F0 - LELink
//   FFB0 - some Konnwei/Panlong units
// Anything matching is worth probing further; anything NOT matching may still
// be an adapter (some advertise nothing but a name), hence we print them all.
static const char *OBD_BLE_UUIDS[] = { "fff0", "ffe0", "18f0", "ffb0" };

// ── Heap reporting ───────────────────────────────────────────────────────────
// The four numbers that actually decide whether esp_bt_controller_init() can
// get its buffers. Printing free-only is what made the last spike ambiguous.
static void heap_report(const char *tag) {
    Serial.printf("[HEAP] %-28s int free=%6u largest=%6u | dma free=%6u largest=%6u | psram free=%8u\n",
                  tag,
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// ── BLE scan ─────────────────────────────────────────────────────────────────
static volatile int s_adv_seen = 0;
static volatile int s_obd_candidates = 0;
// Strongest OBD candidate seen so far, kept across scans so the ELM327 probe
// below has a target without a second scan pass.
static char s_obd_mac[18]  = {0};
static char s_obd_name[32] = {0};
static int  s_obd_rssi     = -127;
static uint8_t s_obd_addr_type = BLE_ADDR_PUBLIC;

class ProbeScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *d) override {
        s_adv_seen++;
        std::string nm  = d->getName();
        std::string mac = d->getAddress().toString();

        // Flag by advertised service UUID first (reliable), then by name as a
        // fallback — plenty of clones advertise "OBDII"/"Vgate"/"IOS-Vlink"
        // with no service UUID in the advertisement at all.
        bool obd = false;
        for (unsigned i = 0; i < sizeof(OBD_BLE_UUIDS) / sizeof(OBD_BLE_UUIDS[0]); i++) {
            if (d->isAdvertisingService(NimBLEUUID(OBD_BLE_UUIDS[i]))) { obd = true; break; }
        }
        if (!obd && !nm.empty()) {
            const char *n = nm.c_str();
            if (strcasestr(n, "obd") || strcasestr(n, "elm") || strcasestr(n, "vlink") ||
                strcasestr(n, "vgate") || strcasestr(n, "veepeak") || strcasestr(n, "obdlink")) obd = true;
        }
        if (obd) {
            s_obd_candidates++;
            if (d->getRSSI() > s_obd_rssi) {
                s_obd_rssi = d->getRSSI();
                snprintf(s_obd_mac,  sizeof(s_obd_mac),  "%s", mac.c_str());
                snprintf(s_obd_name, sizeof(s_obd_name), "%s", nm.empty() ? "(none)" : nm.c_str());
                s_obd_addr_type = d->getAddress().getType();
            }
        }

        Serial.printf("[BLE] %s name='%s' rssi=%d%s\n",
                      mac.c_str(), nm.empty() ? "(none)" : nm.c_str(), d->getRSSI(),
                      obd ? "   <<< OBD CANDIDATE" : "");
    }
};
static ProbeScanCB s_scan_cb;

// Blocking scan for `ms`, so the phases stay readable top-to-bottom.
static void ble_scan_blocking(uint32_t ms, const char *tag) {
    s_adv_seen = 0; s_obd_candidates = 0;
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_scan_cb, false);
    scan->setActiveScan(true);          // ask for scan responses: that is where names live
    Serial.printf("\n--- BLE scan (%s), %u ms ---\n", tag, ms);
    scan->start(ms, false);
    uint32_t deadline = millis() + ms + 2000;
    while (scan->isScanning() && millis() < deadline) delay(50);
    scan->clearResults();
    Serial.printf("--- scan done: %d advertiser(s), %d OBD candidate(s) ---\n",
                  s_adv_seen, s_obd_candidates);
}

// ── Radio bring-up helpers ───────────────────────────────────────────────────
// Returns false instead of crashing, exactly like convoy_ble_guard.h — a failed
// controller init leaves NimBLE's mutex handles NULL and the next call into the
// stack asserts and reboots, which is what used to hide this failure.
static bool ble_up(void) {
    heap_report("before NimBLE init");
    bool ok = NimBLEDevice::init("TMprobe");
    Serial.printf("[BLE] NimBLEDevice::init() -> %s\n", ok ? "OK" : "FAILED");
    heap_report(ok ? "after NimBLE init" : "after NimBLE init (failed)");
    Serial.printf("[BLE] esp_bt_controller_get_status() = %d (2 = ENABLED)\n",
                  (int)esp_bt_controller_get_status());
    return ok;
}

// STA up + a real scan. scanNetworks() is deliberate: it fully initialises the
// WiFi driver and does real RF work without needing credentials, so this phase
// is meaningful even with no AP in range.
static bool wifi_sta_up(void) {
    heap_report("before WiFi STA");
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    delay(200);
    heap_report("after WiFi.mode(STA)");
    int n = WiFi.scanNetworks();
    Serial.printf("[WIFI] scanNetworks() -> %d network(s)\n", n);
    for (int i = 0; i < n && i < 12; i++)
        Serial.printf("[WIFI]   %-32s %d dBm%s\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                      WiFi.SSID(i) == "WiFi_OBDII" ? "   <<< the dash's ELM327" : "");
    WiFi.scanDelete();
    heap_report("after WiFi scan");
    return n >= 0;
}

// Join a saved network so the coexistence test includes an associated link and
// real TCP traffic, not just a driver that has been initialised.
static bool wifi_connect_saved(void) {
    Preferences prefs;
    if (!prefs.begin(OTA_NVS_NS, true)) { Serial.println("[WIFI] no saved networks in NVS"); return false; }

    // Only try SSIDs that are actually in range. Attempting an absent one parks
    // the driver in "connecting" for the full timeout, and every subsequent
    // begin() is then rejected with "sta is connecting, cannot set config" —
    // which is what made the first run report a failure for a network that was
    // sitting there at -62 dBm.
    int nvis = WiFi.scanNetworks();
    char ks[16], kp[16];
    for (int i = 0; i < OTA_MAX_NETWORKS; i++) {
        snprintf(ks, sizeof(ks), "ssid_%d", i); snprintf(kp, sizeof(kp), "pass_%d", i);
        String s = prefs.getString(ks, "");
        if (s == "") continue;
        bool visible = false;
        for (int j = 0; j < nvis; j++) if (WiFi.SSID(j) == s) { visible = true; break; }
        if (!visible) { Serial.printf("[WIFI] skipping '%s' — not in range\n", s.c_str()); continue; }
        String p = prefs.getString(kp, "");
        Serial.printf("[WIFI] connecting to saved network '%s'...\n", s.c_str());
        WiFi.begin(s.c_str(), p.c_str());
        uint32_t t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(250);
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WIFI] connected, IP %s\n", WiFi.localIP().toString().c_str());
            prefs.end();
            heap_report("after WiFi associated");
            return true;
        }
        Serial.printf("[WIFI] '%s' failed (status %d)\n", s.c_str(), (int)WiFi.status());
        WiFi.disconnect(false, true);
        delay(500);
    }
    prefs.end();
    WiFi.scanDelete();
    Serial.println("[WIFI] no saved network joined");
    return false;
}

// The AP path matters on its own: the photoframe/setup portal runs SoftAP, and
// AP mode allocates more internal RAM than STA. If BLE survives STA but dies
// under AP, that is a different (and much more tolerable) constraint.
static bool wifi_ap_up(void) {
    heap_report("before SoftAP");
    bool ok = WiFi.softAP("TMprobeAP", "trailmaster");
    Serial.printf("[WIFI] softAP() -> %s, IP %s\n", ok ? "OK" : "FAILED",
                  WiFi.softAPIP().toString().c_str());
    heap_report("after SoftAP");
    return ok;
}

// ── ELM327-over-BLE probe ────────────────────────────────────────────────────
// A BLE ELM327 is a transparent serial bridge: one service with a write
// characteristic (commands, CR-terminated) and a notify characteristic
// (responses, arriving in chunks until the '>' prompt). Vendors do not agree on
// the UUIDs, so this discovers them rather than hardcoding: dump every service
// and characteristic, then pick the first write+notify pair and speak ELM to it.
// The dump is the useful output even if the conversation fails — it is exactly
// what a real driver would need to be written against.
static NimBLERemoteCharacteristic *s_obd_write = nullptr;
static NimBLERemoteCharacteristic *s_obd_notify = nullptr;
static char s_obd_rx[512];
static volatile size_t s_obd_rx_len = 0;

static void obd_notify_cb(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify) {
    (void)c; (void)isNotify;
    for (size_t i = 0; i < len && s_obd_rx_len < sizeof(s_obd_rx) - 1; i++)
        s_obd_rx[s_obd_rx_len++] = (char)data[i];
    s_obd_rx[s_obd_rx_len] = '\0';
}

// Send one ELM command and wait for the '>' prompt (or timeout).
static void obd_cmd(const char *cmd, uint32_t wait_ms) {
    if (!s_obd_write) return;
    // Let anything still in flight from the PREVIOUS command land before we
    // clear the buffer. Without this, a slow reply (0100's protocol search can
    // take >5 s) arrives after we gave up, and shows up glued to the front of
    // the next command's reply — which is what made the first run look like two
    // failures when it was one late answer plus one real one.
    delay(150);
    s_obd_rx_len = 0; s_obd_rx[0] = '\0';
    char line[32];
    snprintf(line, sizeof(line), "%s\r", cmd);
    s_obd_write->writeValue((uint8_t *)line, strlen(line), false);
    uint32_t t = millis();
    while (millis() - t < wait_ms && !strchr(s_obd_rx, '>')) delay(20);
    // Newlines make the log unreadable; show the raw reply on one line.
    for (size_t i = 0; i < s_obd_rx_len; i++)
        if (s_obd_rx[i] == '\r' || s_obd_rx[i] == '\n') s_obd_rx[i] = ' ';
    Serial.printf("[OBD] %-6s -> '%s'%s\n", cmd, s_obd_rx,
                  s_obd_rx_len == 0 ? "   (NO REPLY)" : "");
}

static void obd_ble_probe(void) {
    if (s_obd_mac[0] == '\0') {
        Serial.println("\n=== ELM327-over-BLE probe: no candidate seen in the scans, skipping ===");
        return;
    }
    Serial.printf("\n=== ELM327-over-BLE probe: %s '%s' (%d dBm), WiFi status=%d ===\n",
                  s_obd_mac, s_obd_name, s_obd_rssi, (int)WiFi.status());

    NimBLEClient *cl = NimBLEDevice::createClient();
    NimBLEAddress addr(std::string(s_obd_mac), s_obd_addr_type);
    if (!cl->connect(addr)) {
        Serial.println("[OBD] connect FAILED (out of range, or it only accepts one link at a time)");
        NimBLEDevice::deleteClient(cl);
        heap_report("after failed OBD connect");
        return;
    }
    Serial.printf("[OBD] connected, MTU %d\n", cl->getMTU());
    heap_report("after OBD BLE connect");

    const std::vector<NimBLERemoteService *> &svcs = cl->getServices(true);
    for (auto *svc : svcs) {
        Serial.printf("[OBD] service %s\n", svc->getUUID().toString().c_str());
        for (auto *ch : svc->getCharacteristics(true)) {
            Serial.printf("[OBD]   char %s  read=%d write=%d writeNR=%d notify=%d indicate=%d\n",
                          ch->getUUID().toString().c_str(),
                          ch->canRead(), ch->canWrite(), ch->canWriteNoResponse(),
                          ch->canNotify(), ch->canIndicate());
            // Skip the GAP/GATT boilerplate services when picking the serial pair.
            if (svc->getUUID().toString().find("1800") != std::string::npos ||
                svc->getUUID().toString().find("1801") != std::string::npos) continue;
            if (!s_obd_notify && (ch->canNotify() || ch->canIndicate())) s_obd_notify = ch;
            if (!s_obd_write  && (ch->canWrite() || ch->canWriteNoResponse())) s_obd_write = ch;
        }
    }

    if (s_obd_notify && s_obd_write) {
        Serial.printf("[OBD] using write=%s notify=%s\n",
                      s_obd_write->getUUID().toString().c_str(),
                      s_obd_notify->getUUID().toString().c_str());
        s_obd_notify->subscribe(true, obd_notify_cb);
        delay(200);
        obd_cmd("ATZ",  5000);   // reset — answers with the ELM version banner
        obd_cmd("ATE0", 2000);   // echo off
        obd_cmd("ATRV", 2000);   // battery voltage: answered locally, no bus needed
        // ATZ resets to ATSP0 (auto-search), and this adapter's auto-search does
        // not work on the JB74 — jimny_sniffer.py locks protocol 6 for exactly
        // this reason. Forcing it is the difference between SEARCHING.../UNABLE
        // TO CONNECT and an actual bus session.
        // Protocol 6 forced (run 2) gave an INSTANT "UNABLE TO CONNECT", and a
        // 3 s cutoff (run 1) never let auto-search finish. So neither run has
        // actually established a bus session. ATSP0 + a long timeout lets the
        // search run to completion, and ATDPN afterwards reports what it found —
        // which is the difference between "wrong protocol" and "CAN side dead".
        obd_cmd("ATSP0", 2000);  // auto-search
        obd_cmd("0100", 25000);  // let the search RUN — this is the whole test
        obd_cmd("ATDPN", 2000);  // what did it settle on?
        // Auto-search can legitimately take >5 s, so give 0100 room. It also
        // doubles as the warm-up the ECM needs: the channel sleeps ~1 s, so a
        // legislated request must precede each read.
        // Three passes: the ECM channel sleeps ~1 s, so a single failed read is
        // not evidence. Each PID is preceded by 0100 as the warm-up.
        for (int pass = 0; pass < 3; pass++) {
            Serial.printf("[OBD] --- read pass %d ---\n", pass + 1);
            obd_cmd("0100", 10000);
            obd_cmd("010C", 6000);   // RPM
            obd_cmd("010D", 6000);   // speed
        }
    } else {
        Serial.println("[OBD] no write+notify pair found — not a transparent serial bridge?");
    }

    cl->disconnect();
    delay(300);
    NimBLEDevice::deleteClient(cl);
    s_obd_write = nullptr; s_obd_notify = nullptr;
    heap_report("after OBD BLE disconnect");
}

// ── Concurrency check ────────────────────────────────────────────────────────
// Coexistence is not just "both stacks initialised". The controller can come up
// and then starve/blank once both radios contend for the antenna and time
// slots. So: run an HTTP GET while a BLE scan is in flight and require BOTH to
// produce results.
// DNS + TCP + HTTP, reported separately. A bare "GET -> -1" cannot tell a
// coexistence problem from a hotspot with no upstream, so this is run twice:
// once with BLE idle (the control) and once mid-scan.
static int http_probe(const char *tag) {
    IPAddress ip;
    bool dns = WiFi.hostByName("neverssl.com", ip);
    Serial.printf("[HTTP:%s] DNS neverssl.com -> %s (%s)\n", tag,
                  dns ? ip.toString().c_str() : "FAILED", dns ? "ok" : "no upstream?");
    WiFiClient c;
    bool tcp = dns && c.connect(ip, 80, 5000);
    Serial.printf("[HTTP:%s] TCP :80 -> %s\n", tag, tcp ? "connected" : "FAILED");
    if (tcp) c.stop();
    if (!dns || !tcp) return -1;

    HTTPClient http;
    http.setTimeout(6000);
    http.begin("http://neverssl.com/");
    int code = http.GET();
    Serial.printf("[HTTP:%s] GET -> %d (%d bytes)\n", tag, code, http.getSize());
    http.end();
    return code;
}

static void concurrent_ops(void) {
    Serial.println("\n=== CONTROL: HTTP with the BLE radio idle ===");
    int control_code = http_probe("control");

    Serial.println("\n=== CONCURRENT: BLE scan + HTTP GET at the same time ===");
    s_adv_seen = 0; s_obd_candidates = 0;
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_scan_cb, false);
    scan->setActiveScan(true);
    scan->start(8000, false);           // non-blocking: run HTTP underneath it

    int http_code = (WiFi.status() == WL_CONNECTED) ? http_probe("concurrent") : 0;
    if (WiFi.status() != WL_CONNECTED) Serial.println("[HTTP] skipped — not associated");

    uint32_t deadline = millis() + 10000;
    while (scan->isScanning() && millis() < deadline) delay(50);
    scan->clearResults();

    Serial.printf("[RESULT] BLE saw %d advertiser(s) during the transfer; "
                  "HTTP control=%d concurrent=%d\n", s_adv_seen, control_code, http_code);
    // Only call it degraded if BLE and WiFi each work alone but not together.
    // A control that already failed means the network is the problem, not the
    // radios, and the run says so rather than blaming coexistence.
    if (control_code <= 0)
        Serial.println("[RESULT] verdict: INCONCLUSIVE for WiFi — the control GET failed "
                       "too, so this AP has no working upstream. BLE side is unaffected.");
    else if (s_adv_seen > 0 && http_code > 0)
        Serial.println("[RESULT] verdict: BOTH RADIOS WORKING CONCURRENTLY");
    else
        Serial.println("[RESULT] verdict: DEGRADED — one radio stopped while the other ran");
    heap_report("after concurrent ops");
}

// ── Phases ───────────────────────────────────────────────────────────────────
// A: BLE first, then WiFi. The untested order, and the one ESP-IDF recommends.
static void phase_A(void) {
    Serial.println("\n############ PHASE A: BLE first, then WiFi ############");
    heap_report("boot baseline");

    if (!ble_up()) {
        Serial.println("[RESULT] A: BLE failed at boot with NO WiFi up — that is a "
                       "different problem from coexistence. Stopping.");
        return;
    }
    ble_scan_blocking(3000, "BLE only — looking for a BLE OBD adapter");

    Serial.println("\n--- now bringing WiFi up UNDER a live BLE stack ---");
    bool sta = wifi_sta_up();
    bool ble_alive = (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED);
    Serial.printf("[RESULT] A: WiFi STA up=%d, BT controller still enabled=%d\n", sta, ble_alive);

    ble_scan_blocking(2000, "BLE with WiFi STA up");

    // The headline test: a real BLE session with the OBD adapter while the WiFi
    // stack is up and associated. Run it before the SoftAP phase so the STA link
    // is still the live one.
    bool joined = wifi_connect_saved();
    obd_ble_probe();
    if (joined) concurrent_ops();
    else Serial.println("[RESULT] A: no association, skipping the concurrent traffic test");

    Serial.println("\n--- SoftAP under a live BLE stack (the portal case) ---");
    WiFi.mode(WIFI_AP_STA);
    wifi_ap_up();
    ble_scan_blocking(5000, "BLE with SoftAP up");
    Serial.printf("[RESULT] A: BT controller enabled after SoftAP=%d\n",
                  esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED);
}

// B: WiFi first, then BLE. Reproduces the 2026-07-28 failure, now with the
// largest-free-block number that tells exhaustion from fragmentation.
static void phase_B(void) {
    Serial.println("\n############ PHASE B: WiFi first, then BLE (known-failing order) ############");
    heap_report("boot baseline");

    wifi_sta_up();
    wifi_connect_saved();

    Serial.println("\n--- now bringing BLE up UNDER a live WiFi stack ---");
    if (ble_up()) {
        Serial.println("[RESULT] B: BLE came up with WiFi already running — the "
                       "2026-07-28 result no longer reproduces.");
        ble_scan_blocking(5000, "BLE after WiFi");
        obd_ble_probe();
        concurrent_ops();
    } else {
        Serial.println("[RESULT] B: BLE init failed with WiFi up — matches the "
                       "2026-07-28 spike. Compare 'largest' above against 'int free': "
                       "if largest is far below free, this is FRAGMENTATION, not exhaustion.");
    }
}

void setup() {
    Serial.begin(115200);
    // USB-CDC: the port re-enumerates after the post-upload reset, so the first
    // second of output is lost if we start straight away. Wait (bounded) for a
    // host to attach — the whole point of this sketch is its serial log.
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 20000) delay(50);
    delay(1500);
    Serial.println("\n\n================ BLE + WiFi coexistence probe ================");
    Serial.printf("chip: %s rev%d, %d core(s), PSRAM %u bytes\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                  ESP.getPsramSize());
    // The S3 has no Bluetooth Classic. Stated up front because a Classic-only
    // ELM327 (like the one paired to the Mac as /dev/cu.OBDII) can never work
    // here regardless of how this probe turns out.
    Serial.println("NOTE: ESP32-S3 is BLE-only — no BR/EDR, so no Bluetooth Classic SPP ELM327.");
    Serial.println("Send 'b' within 3 s for phase B (WiFi first); default is phase A (BLE first).");

    char sel = 'a';
    uint32_t t = millis();
    while (millis() - t < 3000) {
        if (Serial.available()) { int c = Serial.read(); if (c == 'b' || c == 'B') sel = 'b'; break; }
        delay(50);
    }

    if (sel == 'b') phase_B(); else phase_A();

    Serial.println("\n================ probe complete ================");
}

void loop() {
    // Keep reporting: a controller that comes up and then starves under
    // sustained WiFi traffic shows here, not in the one-shot phase output.
    static uint32_t last = 0;
    if (millis() - last > 10000) {
        last = millis();
        Serial.printf("[IDLE] bt=%d wifi=%d ", (int)esp_bt_controller_get_status(), (int)WiFi.status());
        heap_report("idle");
    }
    delay(100);
}
