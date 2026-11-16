#include "lv_mem_psram.h"
#include <stdlib.h>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#define PS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define IN (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
void *lv_psram_alloc(size_t size) { return heap_caps_malloc_prefer(size, 2, PS, IN); }
void lv_psram_free(void *p) { heap_caps_free(p); }
void *lv_psram_realloc(void *p, size_t size) { return heap_caps_realloc_prefer(p, size, 2, PS, IN); }
#else
void *lv_psram_alloc(size_t size) { return malloc(size); }
void lv_psram_free(void *p) { free(p); }
void *lv_psram_realloc(void *p, size_t size) { return realloc(p, size); }
#endif
