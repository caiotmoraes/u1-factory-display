#pragma once
/* Parser de JSON do Spoolman / proxy Moonraker — sem Arduino, testável no host. */
#include "snapshot.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WAREHOUSE_MAX
#define WAREHOUSE_MAX 80
#endif

#define SPOOL_LOCATION_LEN 65
#define SPOOL_COMMENT_LEN  256
#define SPOOL_UIDS_LEN     96
#define SPOOL_COLORS_LEN   81

typedef struct {
    SpoolInfo info;
    float initial_g;     /* -1 se desconhecido */
    float used_g;        /* -1 se desconhecido */
    char location[SPOOL_LOCATION_LEN];
    char comment[SPOOL_COMMENT_LEN];
    char lot[24];
    char card_uids[SPOOL_UIDS_LEN]; /* UIDs normalizados, separados por vírgula */
    char multi_color[SPOOL_COLORS_LEN];
    bool archived;
    int filament_id;
    float filament_weight;
    float density;
    float diameter;
} WarehouseSpool;

/* Preenche color_hex[7] (RRGGBB, maiúsculas) a partir de "#rrggbb", "rrggbb"
 * ou da primeira cor de uma lista "aa,bb". Sem cor válida: 59ECC5. */
void spool_parse_color(const char *hex, char out[8]);

/* Normaliza UID (só hex, maiúsculas). Retorna o número de dígitos. */
int spool_normalize_uid(const char *raw, char *out, size_t n);

/* UID válido no app: 4, 7 ou 10 bytes (8, 14 ou 20 hex) após normalizar.
 * O bruto só pode ter [0-9a-fA-F: -]. Retorna true se ok. */
bool spool_validate_uid(const char *raw, char *normalized, size_t n, char *err, size_t en);

/* Extrai UIDs de extra.card_uids (string, JSON-string, array) + tags[].
 * Escreve em out separados por vírgula. */
void spool_parse_uids_from_extra(const char *card_uids_raw, char *out, size_t n);

/* Identidade usada na exclusão: "#id · vendor material · name|filament_id" */
void spool_identity(const WarehouseSpool *s, char *out, size_t n);

/* Um objeto de rolo (já desembalado). */
bool spool_fill(const char *obj_json, size_t len, WarehouseSpool *out, int expected_id);

/* Envelope v2 / result / objeto cru de UM rolo.
 * Retorna false e preenche error_msg se houver error no envelope. */
bool spool_parse_one(const char *json, size_t len, WarehouseSpool *out, int expected_id,
                     char *error_msg, size_t error_msg_len);

/* Lista GET /v1/spool. Aceita array cru, {response:[...]}, {result:{response:[...]}}
 * e {result:[...]}. Retorna a quantidade escrita (>=0) ou -1 em erro. */
int spool_parse_list(const char *json, size_t len,
                     WarehouseSpool *out, int max,
                     char *error_msg, size_t error_msg_len);

/* Extrai spool_id de /server/spoolman/spool_id ou /server/spoolman/status.
 * Retorna true se o campo foi lido (pode ser 0 / null → *id=0). */
bool spool_parse_active_id(const char *json, size_t len, int *id, bool *spoolman_connected);

/* GET /firmware-config/api/settings (nginx :80, ~30 KB).
 * Procura items[].id == "rfid" e lê items[].current.
 * Retorna 1 se "external", 2 se outro valor (ex.: "openrfid"), 0 se ausente/erro.
 * Usa filtro ArduinoJson para não materializar o documento inteiro. */
int settings_parse_rfid_mode(const char *json, size_t len);

#ifdef __cplusplus
}
#endif
