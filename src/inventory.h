#pragma once
/* Cliente do estoque: lista, catálogo e escritas via proxy Moonraker.
 * Todas as chamadas HTTP rodam numa task própria — a UI só enfileira
 * pedidos e lê snapshots. Toda escrita exige confirmação na UI antes. */
#include "spool_parse.h"
#include "inventory_logic.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INV_IDLE = 0,
    INV_LOADING,
    INV_OK,
    INV_ERROR,
    INV_OFFLINE
} InventoryStatus;

typedef struct {
    InventoryStatus status;
    char error_msg[120];
    int count;
    int active_id;          /* GET /server/spoolman/spool_id (0 = nenhum) */
    uint32_t seq;
    uint32_t fetched_ms;
    int external_mode;      /* 0 desconhecido, 1 External, 2 outro */
    WarehouseSpool items[WAREHOUSE_MAX];
} InventorySnapshot;

#define VENDOR_MAX    64
#define FILAMENT_MAX 128

typedef struct {
    int id;
    char name[48];
} VendorItem;

typedef struct {
    int id;
    int vendor_id;
    char vendor[24];
    char name[48];
    char material[24];
    char color_hex[8];
    char multi_color[SPOOL_COLORS_LEN];
    float density, diameter, weight;
} FilamentItem;

typedef struct {
    InventoryStatus status;
    char error_msg[120];
    int n_vendors, n_filaments;
    uint32_t seq;
    VendorItem vendors[VENDOR_MAX];
    FilamentItem filaments[FILAMENT_MAX];
} CatalogSnapshot;

typedef enum {
    ACT_NONE = 0,
    ACT_REFRESH,
    ACT_CATALOG,
    ACT_INSPECT,
    ACT_UPDATE,
    ACT_BIND,
    ACT_CREATE,
    ACT_DELETE,
    ACT_ASSIGN,
    ACT_NFC_AUTO,
    ACT_CONTROL,
    ACT_LOAD,
    ACT_UNLOAD
} ActKind;

typedef enum { AR_IDLE = 0, AR_RUNNING, AR_OK, AR_ERR } ActState;

typedef struct {
    ActKind kind;
    ActState state;
    char message[160];
    int spool_id;
    WarehouseSpool saved;
    char inspect_uid[24];
    int n_matches;
    WarehouseSpool matches[8];
    uint32_t seq;
} ActionResult;

typedef struct {
    char uid[24];
    int filament_id;           /* 0 = cadastrar filamento novo */
    char vendor[65];
    char name[65];
    char material[65];
    char colors[SPOOL_COLORS_LEN];
    char direction[16];        /* coaxial | longitudinal */
    float density, diameter, initial_g, remaining_g;
    char location[SPOOL_LOCATION_LEN];
    char comment[SPOOL_COMMENT_LEN];
} CreateSpoolReq;

void inventory_init(void);
void inventory_start_task(void);

bool inventory_busy(void);
void inventory_request_refresh(void);
void inventory_request_refresh_quiet(void); /* Início: sem toast */
void inventory_request_catalog(void);
void inventory_request_inspect(const char *uid);
/* set_weight = false: não envia remaining_weight (não sobrescreve o consumo). */
void inventory_request_update(int id, const char *location, const char *comment,
                              bool set_weight, float remaining_g);
void inventory_request_bind(int id, const char *uid);
void inventory_request_create(const CreateSpoolReq *req);
void inventory_request_delete(int id, const char *identity);
void inventory_request_assign(int channel, int spool_id);
bool inventory_request_nfc_auto(int spool_id, const char *uid);
/* Spec v1: CARD_TYPE "M1" para Mifare Classic, senão "NTAG". */
bool inventory_request_nfc_auto_typed(int spool_id, const char *uid, bool mifare_classic);
/* Prazo para inserir o filamento após a leitura (s, 60–600, padrão 180; salvo na NVS). */
int inventory_insert_deadline(void);
void inventory_set_insert_deadline(int seconds);
void inventory_cancel_nfc_auto(void);
void inventory_request_feed(int channel, bool load);

/* Cópias thread-safe. Retornam true se *seq mudou (dados novos). */
bool inventory_copy(InventorySnapshot *out, uint32_t *seq);
bool inventory_copy_catalog(CatalogSnapshot *out, uint32_t *seq);
bool inventory_copy_action(ActionResult *out, uint32_t *seq);

/* Acha um rolo já copiado (não faz HTTP). */
const WarehouseSpool *inventory_find(const InventorySnapshot *s, int id);

#ifdef __cplusplus
}
#endif
