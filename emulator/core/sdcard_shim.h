#pragma once
// L2: redirects "/sd_card/..." paths to a real local folder (EMU_SDCARD_ROOT,
// set via -D at compile time) so the firmware's existing plain POSIX file
// calls "just work" without any edits. Every other path falls through to the
// real libc call unchanged. Covers every POSIX file call the real, compiled
// firmware files actually use against /sd_card (fopen, opendir/readdir/
// closedir, stat, unlink) — readdir/closedir need no redirect since they
// operate on the DIR* already opened against the rewritten real path.
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef EMU_SDCARD_ROOT
#define EMU_SDCARD_ROOT "sd_files"
#endif

inline void emu_sdcard_rewrite(const char* path, char* out, size_t out_size) {
    if (std::strncmp(path, "/sd_card", 8) == 0) {
        std::snprintf(out, out_size, "%s%s", EMU_SDCARD_ROOT, path + 8);
    } else {
        std::snprintf(out, out_size, "%s", path);
    }
}

inline FILE* emu_fopen(const char* path, const char* mode) {
    char real[512]; emu_sdcard_rewrite(path, real, sizeof(real));
    return fopen(real, mode);
}
inline DIR* emu_opendir(const char* path) {
    char real[512]; emu_sdcard_rewrite(path, real, sizeof(real));
    return opendir(real);
}
inline int emu_stat(const char* path, struct stat* st) {
    char real[512]; emu_sdcard_rewrite(path, real, sizeof(real));
    return stat(real, st);
}
inline int emu_unlink(const char* path) {
    char real[512]; emu_sdcard_rewrite(path, real, sizeof(real));
    return unlink(real);
}
#define fopen(p, m) emu_fopen(p, m)
#define opendir(p) emu_opendir(p)
#define stat(p, s) emu_stat(p, s)
#define unlink(p) emu_unlink(p)
