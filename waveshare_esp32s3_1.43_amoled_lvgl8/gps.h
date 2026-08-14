#ifndef GPS_H
#define GPS_H
//
// On-board NEO-M9N reader — transport half. The parsing lives in gps_parse.h,
// which is pure and host-tested; this file only moves bytes.
//
// ── Why I2C and not UART ─────────────────────────────────────────────────────
// The module is on the board's existing I2C bus (SDA=47, SCL=48) at address
// 0x42, sharing it with the FT3168 touch controller (0x38), the PCF85063 RTC
// (0x51) and the QMI8658C IMU (0x6B). No address collides.
//
// A UART attempt on GPIO44/43 was abandoned: it read ZERO bytes — not garbled
// bytes — across every free GPIO and five baud rates, with UART0 explicitly
// detached. Zero rather than corrupted means the signal never arrived at all.
// The likely cause is that those wires came off the breakout's Qwiic socket,
// which carries SDA/SCL and has no TX/RX on it.
//
// DO NOT move this to GPIO43/44 later. They are UART0, and the ESP32-S3 ROM
// bootloader drives GPIO43 push-pull at 115200 on every reset — a peripheral
// TX on that pin is output-fighting-output.
//
// I2C also wins on expansion: the magnetometer will daisy-chain onto the same
// Qwiic cable with no new GPIOs. Load is negligible — ~500 bytes/s against a
// 300 kHz bus is about 1.5% utilisation.
//
// ── Threading ────────────────────────────────────────────────────────────────
// Runs on its own low-priority task. It must NOT use Arduino Wire: Touch_Init()
// (FT3168.cpp, called from the sketch) already installs the ESP-IDF legacy I2C
// driver on I2C_PORT, and Wire would try to install a second driver on the same
// port. Sharing that driver also gets us mutual exclusion for free —
// i2c_master_write_read_device() takes the driver's per-port lock, so
// concurrent touch/IMU access is already serialised and no extra mutex is
// needed for the bus itself.
//
// NOTE: use i2c.h, NOT i2c_bsp.h. Both declare the same three functions, but
// i2c_bsp.* is dead code — nothing includes it, and its I2C_master_Init() is
// never called. The live implementation is i2c.c, which is what FT3168.cpp and
// qmi8658c.cpp use.
//
// gps_begin() must therefore run AFTER Touch_Init(), or there is no driver
// installed to talk through.
//
// Like the convoy feeders, this only writes plain data and never touches LVGL.

#include <Arduino.h>
#include "i2c.h"
#include "gps_parse.h"

#define GPS_I2C_ADDR      0x42
#define GPS_REG_LEN_HI    0xFD   // 2-byte count of bytes waiting
#define GPS_REG_STREAM    0xFF   // the data stream itself

// 1 Hz nav rate, so polling faster than this only burns bus time. 250 ms keeps
// latency low enough that a fix feels live without hammering the bus.
#define GPS_POLL_MS       250

// A fix older than this is stale — the module has stopped solving, or the bus
// has gone quiet. Chosen well above the 1 Hz rate so a single dropped epoch
// does not flap the indicator.
#define GPS_FIX_TTL_MS    5000

// I2C_read_buff takes a uint8_t length, and the IDF driver wants a bounded
// transfer anyway. NMEA lines are <100 bytes, so 64 reads a line or two.
#define GPS_CHUNK         64

// Bytes waiting before we bother reading. The module returns 0xFF filler when
// idle; ignoring dribbles avoids a read per poll for nothing.
#define GPS_MIN_PENDING   8

static gps_fix_t      s_gps_fix;             // owned by gpsTask
static volatile bool  s_gps_running  = false;
static volatile uint32_t s_gps_last_sentence = 0;   // millis of last valid NMEA
static volatile uint32_t s_gps_last_fix      = 0;   // millis of last A-status fix
static volatile uint32_t s_gps_sentences     = 0;   // lifetime counter, for logs

// Guards the copy of s_gps_fix handed to other tasks. lat/lon are doubles, so
// a reader without this could observe a half-updated position — latitude from
// the new epoch against longitude from the old, which lands the car somewhere
// it has never been.
static portMUX_TYPE s_gps_mux = portMUX_INITIALIZER_UNLOCKED;

// ── Snapshot API ─────────────────────────────────────────────────────────────
// Callers get a copy, never a pointer into live state.
static inline void gps_get(gps_fix_t *out) {
    if (!out) return;
    portENTER_CRITICAL(&s_gps_mux);
    memcpy(out, (const void *)&s_gps_fix, sizeof(*out));
    portEXIT_CRITICAL(&s_gps_mux);
}

// True when we hold a position recent enough to steer by.
static inline bool gps_has_fix(void) {
    const uint32_t t = s_gps_last_fix;
    return t != 0 && (millis() - t) < GPS_FIX_TTL_MS;
}

// True when the module is talking at all, fix or not. Distinguishing this from
// gps_has_fix() is what lets the UI say "acquiring" rather than "no GPS" while
// the antenna is still hunting.
static inline bool gps_is_alive(void) {
    const uint32_t t = s_gps_last_sentence;
    return t != 0 && (millis() - t) < GPS_FIX_TTL_MS;
}

// ── DDC transport ────────────────────────────────────────────────────────────
// u-blox exposes a byte-stream over I2C: registers 0xFD/0xFE hold a 16-bit
// count of bytes waiting, and 0xFF is the stream. Reading the count first
// avoids pulling 0xFF filler when the module has nothing to say.
static uint16_t gps_pending(void) {
    uint8_t n[2] = {0, 0};
    if (I2C_read_buff(GPS_I2C_ADDR, GPS_REG_LEN_HI, n, 2) != 0) return 0;
    return ((uint16_t)n[0] << 8) | n[1];
}

// Accumulate stream bytes into complete sentences and hand each to the parser.
// A read can end mid-sentence, so the line buffer is deliberately static and
// survives across calls — the checksum in gps_parse_line() is what catches a
// line that never completes.
static void gps_consume(const uint8_t *buf, int len, gps_fix_t *fix) {
    static char line[128];
    static int  n = 0;

    for (int i = 0; i < len; i++) {
        const char c = (char)buf[i];
        if (c == (char)0xFF) continue;          // idle filler, never data
        if (c == '\r' || c == '\n') {
            if (n > 0) {
                line[n] = '\0';
                if (gps_parse_line(line, fix)) {
                    s_gps_last_sentence = millis();
                    s_gps_sentences++;
                    if (fix->has_fix) s_gps_last_fix = millis();
                }
                n = 0;
            }
        } else if (n < (int)sizeof(line) - 1) {
            line[n++] = c;
        } else {
            n = 0;   // overlong: not a sentence, resync on the next newline
        }
    }
}

static void gps_poll_once(gps_fix_t *fix) {
    uint16_t avail = gps_pending();
    if (avail < GPS_MIN_PENDING) return;

    // Bound the work per poll. The module buffers happily, so anything left
    // over is simply read on the next tick rather than blocking the bus here.
    int budget = 512;
    uint8_t buf[GPS_CHUNK];
    while (avail >= GPS_MIN_PENDING && budget > 0) {
        const uint8_t want = avail > GPS_CHUNK ? GPS_CHUNK : (uint8_t)avail;
        if (I2C_read_buff(GPS_I2C_ADDR, GPS_REG_STREAM, buf, want) != 0) return;
        gps_consume(buf, want, fix);
        avail  -= want;
        budget -= want;
    }
}

// ── Task ─────────────────────────────────────────────────────────────────────
static void gpsTask(void *arg) {
    (void)arg;
    gps_fix_t local;
    gps_fix_reset(&local);

    Serial.println("[GPS] reader up (I2C 0x42, SDA=47 SCL=48)");

    uint32_t next_log = 0;
    while (s_gps_running) {
        gps_poll_once(&local);

        portENTER_CRITICAL(&s_gps_mux);
        memcpy((void *)&s_gps_fix, &local, sizeof(s_gps_fix));
        portEXIT_CRITICAL(&s_gps_mux);

        // Heartbeat rather than per-sentence spam: at 1 Hz across six sentence
        // types a per-line log makes a serial capture unreadable.
        if (millis() > next_log) {
            next_log = millis() + 5000;
            if (local.has_fix)
                Serial.printf("[GPS] %.6f,%.6f alt=%.0fm sats=%d/%d hdop=%.2f "
                              "spd=%.1fm/s %s %02d:%02d:%02dZ\n",
                              local.lat, local.lon, local.alt_m,
                              local.sats_used, local.sats_in_view, local.hdop,
                              local.speed_mps,
                              local.nav_mode == 3 ? "3D" : "2D",
                              local.hour, local.minute, local.second);
            else
                Serial.printf("[GPS] no fix - %d in view, q=%d, %u sentences\n",
                              local.sats_in_view, local.fix_quality,
                              (unsigned)s_gps_sentences);
        }
        vTaskDelay(pdMS_TO_TICKS(GPS_POLL_MS));
    }

    Serial.println("[GPS] reader stopped");
    vTaskDelete(NULL);
}

// Start the reader. Safe to call once from setup(), after Touch_Init().
//
// Pinned to core 0 alongside the convoy task, leaving core 1 to LVGL. The stack
// is modest because the parser is all stack-light integer/float work with no
// allocation.
static void gps_begin(void) {
    if (s_gps_running) return;
    gps_fix_reset(&s_gps_fix);
    s_gps_running = true;
    xTaskCreatePinnedToCore(gpsTask, "GPS", 4096, NULL, 1, NULL, 0);
}

static void gps_end(void) { s_gps_running = false; }

#endif // GPS_H
