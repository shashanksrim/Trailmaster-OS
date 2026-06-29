#pragma once
// L2: minimal Arduino-ESP32 core shim — enough for any ESP32 Arduino sketch's
// non-hardware-specific logic to compile and run on a host machine.
// Reusable across any sketch; nothing here is Trailmaster-specific.
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <chrono>
#include <thread>
#include <string>
#include <unistd.h> // unlink() etc. — declared via newlib globally on real ESP32-Arduino

using std::uint8_t; using std::uint16_t; using std::uint32_t; using std::int32_t;
using byte = uint8_t;

// ── millis()/delay() ─────────────────────────────────────────────────────────
inline uint32_t millis() {
    static const auto t0 = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
}
inline void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline void delayMicroseconds(uint32_t us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
inline void yield() {}

// ── Serial ────────────────────────────────────────────────────────────────────
struct SerialClass {
    void begin(uint32_t) {}
    void println(const char* s = "") { std::printf("%s\n", s); }
    void println(int v) { std::printf("%d\n", v); }
    void print(const char* s) { std::printf("%s", s); }
    void print(int v) { std::printf("%d", v); }
    void printf(const char* fmt, ...) {
        va_list args; va_start(args, fmt);
        std::vprintf(fmt, args);
        va_end(args);
    }
};
extern SerialClass Serial;

// ── String (small subset of Arduino's String API on top of std::string —
// most sketches use it like a std::string, but a few call Arduino-specific
// methods std::string doesn't have) ───────────────────────────────────────
class String : public std::string {
public:
    String() : std::string() {}
    String(const std::string& s) : std::string(s) {}
    String(const char* s) : std::string(s ? s : "") {}
    String(char c) : std::string(1, c) {}
    bool endsWith(const char* suffix) const {
        size_t n = std::strlen(suffix);
        return size() >= n && compare(size() - n, n, suffix) == 0;
    }
    bool isEmpty() const { return empty(); }
};

// ── FreeRTOS task primitives (Arduino-ESP32 makes these globally available
// after including Arduino.h, without a separate freertos/*.h include) ───────
using TaskHandle_t = void*;
inline uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
#define portTICK_PERIOD_MS 1

inline void vTaskDelay(uint32_t ticks) { delay(ticks); }
inline void vTaskDelete(TaskHandle_t) {}

// Runs the task function on a real detached thread — close enough to a
// FreeRTOS pinned task for a host simulation (core affinity is meaningless
// on a desktop CPU anyway).
inline void xTaskCreatePinnedToCore(void (*fn)(void*), const char* /*name*/,
                                     uint32_t /*stackDepth*/, void* arg,
                                     uint32_t /*priority*/, TaskHandle_t* handle,
                                     int /*core*/) {
    std::thread t(fn, arg);
    t.detach();
    if (handle) *handle = nullptr;
}

// ── random() — used by a couple of the games ──────────────────────────────────
#include <cstdlib>
inline long random(long max) { return max > 0 ? std::rand() % max : 0; }
inline long random(long min, long max) { return max > min ? min + std::rand() % (max - min) : min; }

// ── Misc Arduino-ESP32-core globals the real Arduino.h pulls in transitively ──
#ifndef PI
#define PI 3.14159265358979323846
#endif
#define RTC_DATA_ATTR // RTC memory persistence across deep-sleep doesn't apply on host

#include "esp_heap_caps.h"
inline uint32_t esp_get_free_heap_size() { return 8 * 1024 * 1024; }
inline size_t heap_caps_get_free_size(uint32_t) { return 8 * 1024 * 1024; }

// ── /sd_card path redirection (must come after all of the above; see file) ──
#include "sdcard_shim.h"
