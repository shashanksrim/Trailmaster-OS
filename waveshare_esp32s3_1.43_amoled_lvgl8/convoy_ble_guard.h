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

// True only when the BLE stack is actually up and safe to call into.
static inline bool convoy_ble_up(void) { return s_convoy_ble_up; }

// Bring the stack up. Returns false (without crashing) if the controller can't
// be initialised — caller must NOT make any further NimBLE calls.
static bool convoy_ble_init(const char *name) {
    if (s_convoy_ble_up) return true;
    unsigned internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (!NimBLEDevice::init(name)) {
        Serial.printf("[CVY] BLE init FAILED — controller out of memory "
                      "(internal RAM=%u). Radio unavailable.\n", internal);
        return false;
    }
    s_convoy_ble_up = true;
    Serial.printf("[CVY] BLE up (internal RAM %u -> %u)\n", internal,
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
}

// Tear the stack down. Safe to call when it was never up.
static void convoy_ble_deinit(void) {
    if (!s_convoy_ble_up) return;
    NimBLEDevice::deinit(true);
    s_convoy_ble_up = false;
}

#endif // CONVOY_BLE_GUARD_H
