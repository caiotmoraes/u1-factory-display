/* Dados falsos para os previews: U1 imprimindo, estoque real do Spoolman. */
#include "moonraker.h"
#include "inventory.h"
#include "history.h"
#include "nfc_hook.h"
#include "wifi_setup.h"
#include "control.h"
#include "thumb.h"
#include <Arduino.h>
#include <string>
#include <fstream>
#include <sstream>

SerialStub Serial;
uint32_t g_ms = 1000;
uint32_t millis(void) { return g_ms; }

PrinterSnapshot fake_snap;
uint32_t fake_seq = 2;
static InventorySnapshot *inv;
static uint32_t inv_seq = 2;
static uint16_t thumb_px[THUMB_W * THUMB_H];
static lv_img_dsc_t thumb_dsc;
static bool thumb_on = false;
static uint32_t thumb_seq = 2;

void fake_set_thumb(const char *path) {
    FILE *f = path ? fopen(path, "rb") : nullptr;
    thumb_on = f && fread(thumb_px, 2, THUMB_W * THUMB_H, f) == THUMB_W * THUMB_H;
    if (f) fclose(f);
    thumb_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    thumb_dsc.header.w = THUMB_W;
    thumb_dsc.header.h = THUMB_H;
    thumb_dsc.data_size = sizeof(thumb_px);
    thumb_dsc.data = (const uint8_t *)thumb_px;
    thumb_seq++;
}
bool thumb_get(const lv_img_dsc_t **d, uint32_t *seq) {
    bool f = *seq != thumb_seq;
    *seq = thumb_seq;
    *d = thumb_on ? &thumb_dsc : nullptr;
    return f;
}
void thumb_update(const char *, uint16_t, const char *) {}

bool moonraker_copy(PrinterSnapshot *out, uint32_t *seq) {
    bool f = !seq || *seq != fake_seq;
    *out = fake_snap;
    if (seq) *seq = fake_seq;
    return f;
}
void moonraker_force_discover(void) {}
bool moonraker_endpoint(char *ip, size_t n, uint16_t *port) { snprintf(ip, n, "192.168.0.18"); *port = 7125; return true; }

void fake_load_inventory(const char *path) {
    inv = (InventorySnapshot *)calloc(1, sizeof(*inv));
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string j = ss.str();
    char err[120];
    inv->count = spool_parse_list(j.data(), j.size(), inv->items, WAREHOUSE_MAX, err, sizeof(err));
    inv->status = INV_OK;
    inv->active_id = 38;
    inv->fetched_ms = 1000;
    inv_seq++;
}
/* repro v0.6.1: boot sem estoque carregado (status IDLE, 0 rolos) */
int fake_quiet_req = 0, fake_loud_req = 0;
void fake_inventory_empty(void) {
    if (!inv) inv = (InventorySnapshot *)calloc(1, sizeof(*inv));
    memset(inv, 0, sizeof(*inv));
    inv->status = INV_IDLE;
    inv_seq++;
}
const WarehouseSpool *fake_spool(int id) { return inventory_find(inv, id); }

bool inventory_busy(void) { return false; }
void inventory_init(void) {}
void inventory_start_task(void) {}
void inventory_request_refresh(void) { fake_loud_req++; }
void inventory_request_refresh_quiet(void) { fake_quiet_req++; }
void inventory_request_catalog(void) {}
void inventory_request_inspect(const char *) {}
void inventory_request_update(int, const char *, const char *, bool, float) {}
void inventory_request_bind(int, const char *) {}
void inventory_request_create(const CreateSpoolReq *) {}
void inventory_request_delete(int, const char *) {}
void inventory_request_assign(int, int) {}
bool inventory_request_nfc_auto(int, const char *) { return true; }
void inventory_cancel_nfc_auto(void) {}
void inventory_request_feed(int, bool) {}
bool inventory_copy(InventorySnapshot *out, uint32_t *seq) {
    bool f = !seq || *seq != inv_seq;
    if (f && inv) memcpy(out, inv, sizeof(*out));
    if (seq) *seq = inv_seq;
    return f;
}
bool inventory_copy_catalog(CatalogSnapshot *, uint32_t *seq) { if (seq) *seq = 1; return false; }
bool inventory_copy_action(ActionResult *, uint32_t *seq) { if (seq) *seq = 1; return false; }
const WarehouseSpool *inventory_find(const InventorySnapshot *s, int id) {
    if (!s) return nullptr;
    for (int i = 0; i < s->count; i++) if (s->items[i].info.id == id) return &s->items[i];
    return nullptr;
}
bool control_submit(ControlOp, int, const char *) { return true; }
void control_copy_files(ControlFiles &out) {
    memset(&out, 0, sizeof(out));
    out.version = 1;
    const char *n[] = {"suporte_parede_v3.gcode", "engrenagem_32d_petg.gcode", "caixa_eletronica_tampa.gcode",
                       "porta_filamento_4x.gcode", "clip_cabo_x10.gcode"};
    for (int i = 0; i < 5; i++) snprintf(out.paths[i], sizeof(out.paths[i]), "%s", n[i]);
    out.count = 5;
}
void nfc_init() {}
bool nfc_present() { return true; }
bool nfc_request(int) { return true; }
bool nfc_result(NfcResult &) { return false; }
void nfc_cancel() {}
void wifi_setup_start(void) {}
bool wifi_is_portal(void) { return false; }
bool wifi_is_connected(void) { return true; }
void wifi_current(char *s, size_t sn, char *ip, size_t in) { snprintf(s, sn, "Casa Moraes 2.4G"); snprintf(ip, in, "192.168.0.27"); }
void wifi_prepare_scan(void) {}
bool wifi_scan_start(void) { return true; }
int wifi_scan_poll(void) { return 4; }
int wifi_scan_copy(WifiNet *o, int max) {
    const char *n[] = {"Casa Moraes 2.4G", "Oficina", "VIVO-8A21", "Convidados"};
    int r[] = {-48, -61, -73, -82};
    for (int i = 0; i < 4 && i < max; i++) { snprintf(o[i].ssid, sizeof(o[i].ssid), "%s", n[i]); o[i].rssi = r[i]; o[i].locked = i != 3; }
    return 4;
}
void wifi_save_and_restart(const char *, const char *) {}
