#pragma once
/* Filtro, busca, ordenação, classificação de histórico — sem Arduino. */
#include "spool_parse.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STOCK_LOW_FILTER_G 200.0f  /* filtro "até 200 g" do app (o alerta do HUD é 100 g) */

typedef enum {
    STOCK_SORT_ID = 0,
    STOCK_SORT_WEIGHT,
    STOCK_SORT_MATERIAL,
    STOCK_SORT_BRAND
} StockSort;

typedef struct {
    char query[80];
    char material[24];
    char brand[24];
    char location[SPOOL_LOCATION_LEN];
    StockSort sort;
    bool include_archived;
    bool low_stock;          /* remaining <= 200 g */
} StockFilter;

void stock_filter_clear(StockFilter *f);

/* Escreve índices de `in` em `idx` (até max_out). Retorna a quantidade. */
int stock_apply_filter(const WarehouseSpool *in, int n, const StockFilter *f,
                       int *idx, int max_out);

/* Coleta valores únicos (já existentes) para preencher dropdowns.
 * Retorna quantidade escrita em `out` (cada um com `elem_size` bytes). */
int stock_unique_field(const WarehouseSpool *in, int n, int field,
                       char *out, int max, size_t elem_size);
/* field: 0=material, 1=brand, 2=location */

/* Resumo: ativos, peso conhecido em g, quantos sem peso, quantos <=200 g. */
typedef struct {
    int active;
    int archived;
    float known_weight_g;
    int known_count;
    int unknown_weight;
    int low200;
} StockSummary;
StockSummary stock_summary(const WarehouseSpool *in, int n);

/* Busca no estilo do app: NFD+sem acento, minúsculas, todas as palavras. */
bool stock_matches_query(const char *text, const char *query);

/* Nome aproximado da cor (paleta do app, em português). */
void stock_color_name(const char *color_hex, char *out, size_t n);

/* Classifica um evento do histórico: "problem", "association", "read", "other". */
const char *history_event_type(const char *text);
const char *history_event_label_pt(const char *type);

/* Monta o objeto `info` de POST /printer/spoollink/set (JSON, ~400 bytes).
 * Retorna true se o rolo tem material e cor válidos. */
bool spool_build_link_info(const WarehouseSpool *s, char *json, size_t n, char *err, size_t en);

/* Vínculo extrusor → rolo no SpoolLink da U1 (fix 2026-11-18).
 * Script G-code "SET_SPOOL_ID CHANNEL=<0..3> SPOOL_ID=<id>" (CHANNEL 0-based).
 * false se canal fora de 0..3, id <= 0 ou buffer pequeno. */
bool spoollink_set_spool_script(int channel, int spool_id, char *out, size_t n);
/* Percent-encoding RFC 3986 para query string (só A-Z a-z 0-9 - _ . ~ passam;
 * espaço vira %20, '=' vira %3D). false se não couber em `out`. */
bool url_encode_component(const char *in, char *out, size_t n);
/* Local do rolo no Spoolman: "Snapmaker U1 - Extrusor N" (N = channel + 1). */
bool spool_u1_location(int channel, char *out, size_t n);
/* Corpo do PATCH: {"location":"..."} com escape JSON de aspas, barra e controle. */
bool spool_location_patch_json(const char *location, char *out, size_t n);

/* Qual spool_id o HUD deve mostrar: o do extrusor ativo em
 * print_task_config.filament_spool_id, NÃO o spool_id global do
 * plugin Moonraker (que na U1 costuma ser null mesmo com rolos associados). */
int hud_parse_extruder_name(const char *name);
int hud_active_spool_id(const int filament_spool_id[4], int active_extruder);

/* Peso do cartão Filamentos (Início) do bico com filament_spool_id = spool_id:
 * remaining_weight do estoque cruzado por id. O rolo ativo (lido por id a cada
 * poll do Moonraker) prevalece por ser mais recente. -1 = desconhecido. */
float hud_channel_grams(const WarehouseSpool *items, int count, int spool_id,
                        int active_id, float active_g);
/* true se algum filament_spool_id[i] > 0 não está no estoque carregado. */
bool hud_inventory_missing(const WarehouseSpool *items, int count, const int ids[4]);
/* Chave do mapeamento bico → rolo (muda quando qualquer filament_spool_id muda). */
uint32_t hud_mapping_key(const int ids[4]);

#ifdef __cplusplus
}
#endif
