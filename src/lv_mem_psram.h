#pragma once
/* Heap do LVGL na PSRAM: objetos e estilos são pequenos (< 4 KB) e, pelo
 * SPIRAM_MALLOC_ALWAYSINTERNAL=4096, iriam todos para a RAM interna que o
 * Wi-Fi precisa. Cai para a interna se a PSRAM faltar. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *lv_psram_alloc(size_t size);
void lv_psram_free(void *p);
void *lv_psram_realloc(void *p, size_t size);
#ifdef __cplusplus
}
#endif
