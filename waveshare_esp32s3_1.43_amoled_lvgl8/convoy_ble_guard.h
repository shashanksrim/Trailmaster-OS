// Shared NimBLE bring-up guard for the convoy paths (mesh central + phone
// peripheral). FIRMWARE ONLY — the sim never includes this.
//
// WHY THIS EXISTS (measured 2026-07-28): NimBLEDevice::init() starts the ESP32
// BT *controller*, which needs internal, DMA-capable RAM. With WiFi (or the
// photoframe/setup AP) up there isn't enough, and it fails:
//
//   E BLE_INIT: Malloc failed
//   E BLE_INIT: esp_bt_controller_init -4        (-4 = ESP_ERR_NO_MEM)
//
// init() returns false here (NimBLEDevice.cpp: esp_bt_controller_init check),
// but nothing used to look. The next NimBLE call then touched a NULL mutex
// handle and asserted -- "assert failed: npl_freertos_mutex_pend
// npl_os_freertos.c:265 (mu->handle)" -- which panics and REBOOTS the board.
// That was the Tracker reboot, not any display/DMA collision.
//
// So: every bring-up goes through convoy_ble_init(), which reports failure
// instead of crashing, and every teardown / loop / call-into-NimBLE entry point
// checks convoy_ble_up() first. A failed radio must degrade to a message.
#ifndef CONVOY_BLE_GUARD_H
#define CONVOY_BLE_GUARD_H

#include <NimBLEDevice.h>
#include "esp_heap_caps.h"

static bool s_convoy_ble_up = false;

// Bumped on every teardown. NimBLEDevice::deinit(true) destroys EVERY object the
// stack owns — clients, services, characteristics — so any pointer cached across a
// deinit dangles. Anyone caching one records the generation it came from and drops
// it when this moves. This is needed because the roles tear down each OTHER's stack:
// convoy_net_end() (phone) frees the NimBLEClient that convoy_link.h (mesh) still
// holds in s_client, and convoy_net.h cannot see that static to null it. Switching
// phone -> mesh then connected through freed memory: LoadProhibited in
// NimBLEClient::connect. Intermittent, because it only bites when a mesh session ran
// before the phone one.
static uint32_t s_convoy_ble_gen = 0;

// True only when the BLE stack is actually up and safe to call into.
static inline bool convoy_ble_up(void) { return s_convoy_ble_up; }
// Which incarnation of the stack is current; see s_convoy_ble_gen.
static inline uint32_t convoy_ble_gen(void) { return s_convoy_ble_gen; }

// How many independent subsystems currently need the stack. Added 2026-08-05
// when the OBD source moved to BLE (obd_ble.h): the telemetry link and the
// convoy Tracker are now two unrelated owners of one NimBLE stack, and whoever
// finished first used to call deinit(true) and free the OTHER one's client —
// the same class of dangling-pointer crash s_convoy_ble_gen was added for, but
// across subsystems instead of across roles. The stack now goes down only when
// the LAST owner lets go.
static int s_convoy_ble_refs = 0;

// Bring the stack up. Returns false (without crashing) if the controller can't
// be initialised — caller must NOT make any further NimBLE calls.
static bool convoy_ble_init(const char *name) {
    if (s_convoy_ble_up) { s_convoy_ble_refs++; return true; }
    unsigned internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (!NimBLEDevice::init(name)) {
        Serial.printf("[CVY] BLE init FAILED — controller out of memory "
                      "(internal RAM=%u). Radio unavailable.\n", internal);
        return false;
    }
    s_convoy_ble_up = true;
    s_convoy_ble_refs = 1;
    Serial.printf("[CVY] BLE up (internal RAM %u -> %u)\n", internal,
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
}

// Tear the stack down — but only once every owner has released it. Safe to call
// when it was never up.
static void convoy_ble_deinit(void) {
    if (!s_convoy_ble_up) return;
    if (s_convoy_ble_refs > 0) s_convoy_ble_refs--;
    if (s_convoy_ble_refs > 0) {
        Serial.printf("[CVY] BLE still held by %d other owner(s) — not tearing down\n",
                      s_convoy_ble_refs);
        return;
    }
    NimBLEDevice::deinit(true);
    s_convoy_ble_up = false;
    s_convoy_ble_gen++;      // everything the stack owned is now freed
}

// Explicit names for the refcounted pair. The convoy paths keep calling
// init/deinit (their call sites are already balanced); new owners should use
// these so the shared-ownership contract is obvious at the call site.
static inline bool convoy_ble_acquire(const char *name) { return convoy_ble_init(name); }
static inline void convoy_ble_release(void)             { convoy_ble_deinit(); }

#endif // CONVOY_BLE_GUARD_H
