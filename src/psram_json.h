#pragma once
/* Alocador ArduinoJson na PSRAM. Sem ele cada JsonDocument usa a RAM interna
 * em blocos pequenos (< 4 KB ficam sempre internos), o que esgotava a heap
 * interna (Wi-Fi/LVGL) ao ler o estoque do Spoolman. */
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <stdlib.h>

struct PsramJsonAllocator : ArduinoJson::Allocator {
    void *allocate(size_t size) override {
        void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : malloc(size);
    }
    void deallocate(void *ptr) override { heap_caps_free(ptr); }
    void *reallocate(void *ptr, size_t new_size) override {
        void *p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : realloc(ptr, new_size);
    }
};

inline ArduinoJson::Allocator *psram_json() {
    static PsramJsonAllocator a;
    return &a;
}
