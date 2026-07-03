#pragma once
// L2: in-memory NVS stand-in for the ESP32 Preferences library. Not persisted
// across runs (real device NVS survives reboots — emulator doesn't need to).
#include <map>
#include <string>
#include <cstdint>

class Preferences {
public:
    void begin(const char* /*ns*/, bool /*readOnly*/ = false) {}
    void end() {}

    void putInt(const char* key, int32_t v)        { ints_[key] = v; }
    int32_t getInt(const char* key, int32_t def=0) { auto it = ints_.find(key); return it == ints_.end() ? def : it->second; }

    void putString(const char* key, const char* v) { strs_[key] = v; }
    void putString(const char* key, const std::string& v) { strs_[key] = v; }
    std::string getString(const char* key, const std::string& def = "") {
        auto it = strs_.find(key); return it == strs_.end() ? def : it->second;
    }

    void remove(const char* key) { ints_.erase(key); strs_.erase(key); }

private:
    std::map<std::string, int32_t>    ints_;
    std::map<std::string, std::string> strs_;
};
