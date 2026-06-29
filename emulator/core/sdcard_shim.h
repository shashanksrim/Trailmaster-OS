#pragma once
// L2: redirects fopen("/sd_card/...") to a real local folder (EMU_SDCARD_ROOT,
// set via -D at compile time) so the firmware's existing plain POSIX fopen()
// calls "just work" without any edits. Every other path falls through to the
// real fopen unchanged.
#include <cstdio>
#include <cstring>

#ifndef EMU_SDCARD_ROOT
#define EMU_SDCARD_ROOT "sd_files"
#endif

inline FILE* emu_fopen(const char* path, const char* mode) {
    if (std::strncmp(path, "/sd_card", 8) == 0) {
        char real[512];
        std::snprintf(real, sizeof(real), "%s%s", EMU_SDCARD_ROOT, path + 8);
        return fopen(real, mode);
    }
    return fopen(path, mode);
}
#define fopen(p, m) emu_fopen(p, m)
