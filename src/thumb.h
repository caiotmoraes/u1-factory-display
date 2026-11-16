#pragma once
/* Miniatura do G-code atual (thumbnail PNG do Moonraker), decodificada na
 * task do Moonraker para RGB565 em PSRAM. A UI só troca a fonte do lv_img. */
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#define THUMB_W 132
#define THUMB_H 132
#define THUMB_BG 0xEEF0F3   /* fundo onde a transparência do PNG é mesclada */

/* Chamado pela task do Moonraker a cada poll bem-sucedido. Busca e decodifica
 * só quando o arquivo muda (ou para nova tentativa após 60 s de falha). */
void thumb_update(const char *ip, uint16_t port, const char *filename);
/* UI: true se mudou desde *seq. *dsc = nullptr quando não há miniatura. */
bool thumb_get(const lv_img_dsc_t **dsc, uint32_t *seq);
