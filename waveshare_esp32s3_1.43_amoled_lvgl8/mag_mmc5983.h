// MMC5983MA 3-axis magnetometer (7Semi micro breakout) — pure transport.
//
// Daisy-chained onto the GPS module's extension header, so it shares the board's
// existing I2C bus (SDA=47 SCL=48) with the FT3168 touch controller (0x38), the
// NEO-M9N (0x42), the PCF85063 RTC (0x51) and the QMI8658C IMU (0x6A/0x6B).
// This part sits at 0x30 — no collision.
//
// Structured like gps.h deliberately: transport only, no LVGL and no UI, so the
// fusion (mag_heading.h) and the radar bridge (mag_convoy.h) stay separable and
// the sim never has to build any of it.
//
// ── Two things that make or break this driver ───────────────────────────────
//
// 1. NOT Arduino Wire. Touch_Init() already installed an ESP-IDF driver on
//    I2C_NUM_0, and Wire would try to install a second one on the same port and
//    fail. gps.h:27-40 documents the same trap. We use I2C_read_buff /
//    I2C_writr_buff, whose underlying i2c_master_write_read_device() takes the
//    driver's per-port lock — so concurrent touch/IMU/GPS access is already
//    serialised and no extra mutex is needed (gps.h:31).
//
// 2. SET/RESET degaussing is mandatory. MEMSIC's AMR bridge carries a large
//    offset that drifts with temperature and can be FLIPPED OUTRIGHT by a
//    magnetic shock — a speaker magnet, or a coil-carrying wire in a car door.
//    A single measurement cannot distinguish field from offset. So we measure
//    twice, once after a SET pulse and once after a RESET pulse, which inverts
//    the sensed field but NOT the offset:
//
//        after SET:    +H + offset
//        after RESET:  -H + offset
//        =>  H = (set - reset) / 2      offset = (set + reset) / 2
//
//    Skipping this yields a compass that looks plausible and is quietly, and
//    unrecoverably, wrong. It is the most common defect in drivers for this part.
#ifndef MAG_MMC5983_H
#define MAG_MMC5983_H

#include <Arduino.h>
#include "i2c_bsp.h"

#define MAG_I2C_ADDR        0x30

// Register map (MMC5983MA datasheet rev. C)
#define MAG_REG_XOUT0       0x00   // [17:10] of each axis, X/Y/Z at 0x00/0x02/0x04
#define MAG_REG_XYZOUT2     0x06   // low 2 bits: X[7:6] Y[5:4] Z[3:2]
#define MAG_REG_STATUS      0x08   // bit0 Meas_M_Done
#define MAG_REG_CTRL0       0x09   // bit0 TM_M, bit3 SET, bit4 RESET
#define MAG_REG_CTRL1       0x0A   // bit7 SW_RST, bits[1:0] bandwidth
#define MAG_REG_CTRL2       0x0B
#define MAG_REG_PRODUCT_ID  0x2F   // == 0x30 on the MMC5983MA

#define MAG_CTRL0_TM_M      0x01
#define MAG_CTRL0_SET       0x08
#define MAG_CTRL0_RESET     0x10
#define MAG_STATUS_M_DONE   0x01

// 18-bit unsigned output: 0 field sits at mid-scale, and the part is 16384
// counts/Gauss. 1 Gauss = 100 uT, so counts -> uT is /163.84.
#define MAG_ZERO_COUNTS     131072.0f
#define MAG_COUNTS_PER_UT   163.84f

typedef struct {
    float x_ut, y_ut, z_ut;      // offset-corrected field, microtesla
    float offset_x, offset_y, offset_z;  // the bridge offset we removed (diagnostic)
    uint32_t updated_ms;         // millis() of this sample; 0 = never
} mag_sample_t;

static mag_sample_t   s_mag = {0};
static portMUX_TYPE   s_mag_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool  s_mag_running = false;
static volatile bool  s_mag_present = false;
static volatile uint32_t s_mag_reads = 0, s_mag_fails = 0;

// Snapshot the latest sample. Safe from any task.
static inline void mag_get(mag_sample_t *out) {
    portENTER_CRITICAL(&s_mag_mux);
    *out = s_mag;
    portEXIT_CRITICAL(&s_mag_mux);
}

// True once the product ID has been confirmed AND a sample has landed. The two
// are separate failures — a part that IDs but never completes a measurement is
// a wiring/bus problem, not a missing part — so the log distinguishes them.
static inline bool mag_is_alive(void) {
    return s_mag_present && s_mag.updated_ms != 0 &&
           (millis() - s_mag.updated_ms) < 2000;
}
static inline bool mag_present(void) { return s_mag_present; }

static inline uint8_t mag_write_reg(uint8_t reg, uint8_t val) {
    return I2C_writr_buff(MAG_I2C_ADDR, reg, &val, 1);
}

// One triggered measurement. Returns raw 18-bit counts per axis.
static bool mag_measure_raw(uint32_t *rx, uint32_t *ry, uint32_t *rz) {
    if (mag_write_reg(MAG_REG_CTRL0, MAG_CTRL0_TM_M) != 0) return false;
    // ~8 ms at the default bandwidth; poll rather than blind-delay so a slow
    // part is reported instead of silently returning the previous conversion.
    uint8_t st = 0;
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
        if (I2C_read_buff(MAG_I2C_ADDR, MAG_REG_STATUS, &st, 1) != 0) return false;
        if (st & MAG_STATUS_M_DONE) break;
    }
    if (!(st & MAG_STATUS_M_DONE)) return false;

    uint8_t b[7];
    if (I2C_read_buff(MAG_I2C_ADDR, MAG_REG_XOUT0, b, 7) != 0) return false;
    // Each axis is 18 bits: 8 high, 8 mid, and 2 more packed into XYZout2.
    *rx = ((uint32_t)b[0] << 10) | ((uint32_t)b[1] << 2) | ((b[6] >> 6) & 0x03);
    *ry = ((uint32_t)b[2] << 10) | ((uint32_t)b[3] << 2) | ((b[6] >> 4) & 0x03);
    *rz = ((uint32_t)b[4] << 10) | ((uint32_t)b[5] << 2) | ((b[6] >> 2) & 0x03);
    return true;
}

// SET/RESET pair -> field with the bridge offset removed. See the header note.
static bool mag_measure_corrected(mag_sample_t *out) {
    uint32_t sx, sy, sz, rx, ry, rz;

    if (mag_write_reg(MAG_REG_CTRL0, MAG_CTRL0_SET) != 0) return false;
    vTaskDelay(pdMS_TO_TICKS(1));            // SET pulse needs ~500 us to settle
    if (!mag_measure_raw(&sx, &sy, &sz)) return false;

    if (mag_write_reg(MAG_REG_CTRL0, MAG_CTRL0_RESET) != 0) return false;
    vTaskDelay(pdMS_TO_TICKS(1));
    if (!mag_measure_raw(&rx, &ry, &rz)) return false;

    // (set - reset)/2 cancels the offset; (set + reset)/2 IS the offset, kept
    // for diagnostics because a large or wandering value is the tell-tale of a
    // magnetised part or a bad SET pulse.
    out->x_ut = ((float)sx - (float)rx) / 2.0f / MAG_COUNTS_PER_UT;
    out->y_ut = ((float)sy - (float)ry) / 2.0f / MAG_COUNTS_PER_UT;
    out->z_ut = ((float)sz - (float)rz) / 2.0f / MAG_COUNTS_PER_UT;
    out->offset_x = (((float)sx + (float)rx) / 2.0f - MAG_ZERO_COUNTS) / MAG_COUNTS_PER_UT;
    out->offset_y = (((float)sy + (float)ry) / 2.0f - MAG_ZERO_COUNTS) / MAG_COUNTS_PER_UT;
    out->offset_z = (((float)sz + (float)rz) / 2.0f - MAG_ZERO_COUNTS) / MAG_COUNTS_PER_UT;
    out->updated_ms = millis();
    return true;
}

// Total field strength, uT. Earth's is ~25-65 uT depending on latitude (~40-48
// around Bengaluru). This is the cheapest correctness check there is: a wildly
// different magnitude means the 18-bit unpacking or the scale factor is wrong,
// and it is far easier to spot here than by squinting at a heading.
static inline float mag_field_strength(const mag_sample_t *m) {
    return sqrtf(m->x_ut * m->x_ut + m->y_ut * m->y_ut + m->z_ut * m->z_ut);
}

// Probe + configure. Returns true once the part answers with the right ID.
static bool mag_try_init(void) {
    uint8_t id = 0;
    if (I2C_read_buff(MAG_I2C_ADDR, MAG_REG_PRODUCT_ID, &id, 1) != 0) return false;
    if (id != 0x30) {
        static uint8_t warned = 0xFF;
        if (warned != id) {   // log a WRONG part once, not every retry
            warned = id;
            Serial.printf("[MAG] product ID 0x%02X at 0x30, expected 0x30 — wrong part\n", id);
        }
        return false;
    }
    mag_write_reg(MAG_REG_CTRL1, 0x80);      // SW_RST
    vTaskDelay(pdMS_TO_TICKS(15));           // datasheet: 10 ms power-on time
    mag_write_reg(MAG_REG_CTRL1, 0x00);      // 100 Hz bandwidth, all axes enabled
    Serial.println("[MAG] MMC5983MA up (ID 0x30)");
    return true;
}

static void magTask(void *arg) {
    (void)arg;
    // Let the bus settle: Touch_Init() and the GPS reader are both coming up
    // around now, and the very first transaction is the one most likely to
    // collide.
    vTaskDelay(pdMS_TO_TICKS(500));

    bool logged_first = false;
    uint32_t next_probe = 0;

    while (s_mag_running) {
        // Re-probe rather than giving up. The part hangs off the GPS module's
        // extension header on a removable lead, and a single failed read at boot
        // used to kill the task for the whole session — observed 2026-08-05,
        // where it enumerated on one boot and not the next. Retrying also gets
        // hot-plug for free.
        if (!s_mag_present) {
            if (millis() < next_probe) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
            next_probe = millis() + 2000;
            if (!mag_try_init()) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
            s_mag_present = true;
        }

        mag_sample_t m;
        if (mag_measure_corrected(&m)) {
            portENTER_CRITICAL(&s_mag_mux);
            s_mag = m;
            portEXIT_CRITICAL(&s_mag_mux);
            s_mag_reads++;
            if (!logged_first) {
                logged_first = true;
                Serial.printf("[MAG] first sample: %.1f %.1f %.1f uT  |B|=%.1f uT  "
                              "bridge offset %.1f %.1f %.1f uT\n",
                              m.x_ut, m.y_ut, m.z_ut, mag_field_strength(&m),
                              m.offset_x, m.offset_y, m.offset_z);
            }
        } else {
            // Repeated failures mean the lead came out; drop back to probing.
            if (++s_mag_fails % 10 == 0) s_mag_present = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));      // ~10 Hz; a compass needs no more
    }
    s_mag_present = false;
    vTaskDelete(NULL);
}

// Start the reader. Call once from setup() AFTER Touch_Init(), for the same
// reason gps_begin() must: there is no I2C driver installed before that.
static void mag_begin(void) {
    if (s_mag_running) return;
    s_mag_running = true;
    xTaskCreatePinnedToCore(magTask, "MAG", 3072, NULL, 1, NULL, 0);
}

static void mag_end(void) { s_mag_running = false; }

#endif // MAG_MMC5983_H
