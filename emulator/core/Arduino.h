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

using std::uint8_t; using std::uint16_t; using std::uint32_t; using std::int32_t;

// ── millis()/delay() ─────────────────────────────────────────────────────────
inline uint32_t millis() {
    static const auto t0 = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
}
inline void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline void delayMicroseconds(uint32_t us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }

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

// ── String (very small subset — most sketches use it like a std::string) ─────
using String = std::string;
