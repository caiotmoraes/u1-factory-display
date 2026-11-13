#pragma once
/* Filtro, busca, ordenação, classificação de histórico — sem Arduino. */
#include "spool_parse.h"
#include <stddef.h>
#include <stdbool.h>

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

/* Qual spool_id o HUD deve mostrar: o do extrusor ativo em
 * print_task_config.filament_spool_id, NÃO o spool_id global do
 * plugin Moonraker (que na U1 costuma ser null mesmo com rolos associados). */
int hud_parse_extruder_name(const char *name);
int hud_active_spool_id(const int filament_spool_id[4], int active_extruder);

#ifdef __cplusplus
}
#endif
