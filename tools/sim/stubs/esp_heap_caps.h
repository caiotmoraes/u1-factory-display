#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
inline void *heap_caps_malloc(size_t n, int) { return malloc(n); }
inline void *heap_caps_calloc(size_t a, size_t b, int) { return calloc(a, b); }
inline void *heap_caps_realloc(void *p, size_t n, int) { return realloc(p, n); }
inline void heap_caps_free(void *p) { free(p); }
inline size_t heap_caps_get_free_size(int c) { return c == MALLOC_CAP_SPIRAM ? 7600u * 1024 : 151u * 1024; }
