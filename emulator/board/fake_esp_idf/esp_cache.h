#pragma once
// L1: fake esp_cache — the real one syncs the CPU/DMA cache for a PSRAM
// framebuffer on real hardware; no cache coherency concerns on a desktop, so
// this is a pure no-op.
#include <cstddef>
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C 0
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 1
inline int esp_cache_msync(void*, size_t, int) { return 0; }
