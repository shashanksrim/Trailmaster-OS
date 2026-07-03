#pragma once
// L2: fake PSRAM allocator — there's no PSRAM/internal-RAM distinction on a
// desktop, so just map straight to malloc/free. C/C++ agnostic since LVGL's
// own .c sources (compiled as plain C) also pull this in transitively.
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
static inline void* heap_caps_malloc(size_t size, unsigned int cap) { (void)cap; return malloc(size); }
static inline void* heap_caps_aligned_alloc(size_t align, size_t size, unsigned int cap) { (void)align; (void)cap; return malloc(size); }
static inline void  heap_caps_free(void* p) { free(p); }
static inline void* heap_caps_realloc(void* p, size_t size, unsigned int cap) { (void)cap; return realloc(p, size); }
