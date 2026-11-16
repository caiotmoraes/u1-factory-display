/* Cliente do estoque: HTTP numa task própria (nunca na task do LVGL).
 * Lista e escritas passam pelo proxy Moonraker /server/spoolman/proxy.
 * Ações da impressora (spoollink, gcode) vão direto no Moonraker da U1. */
#include "inventory.h"
#include "moonraker.h"
#include "history.h"
#include "spool_parse.h"
#include "inventory_logic.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

static SemaphoreHandle_t mutex;
static TaskHandle_t task_h;
static InventorySnapshot *g_inv;
static CatalogSnapshot *g_cat;
static ActionResult g_act;
static uint32_t inv_seq = 1, cat_seq = 1, act_seq = 1;

static char *http_buf;
static size_t http_cap = 192 * 1024; /* PSRAM; firmware-config real ~30 KB, lista Spoolman maior */

/* Pedido pendente (um de cada vez, com parâmetros copiados). */
static volatile ActKind pending = ACT_NONE;
static int p_id, p_channel;
static bool p_set_weight, p_load;
static float p_remaining;
static char p_location[SPOOL_LOCATION_LEN];
static char p_comment[SPOOL_COMMENT_LEN];
static char p_uid[24];
static std::atomic<bool> nfc_auto_cancelled{false};
static int nfc_auto_id;
static char nfc_auto_uid[24];
static char p_identity[160];
static CreateSpoolReq p_create;

static void cpy(char *d, size_t n, const char *s) {
    if (!d || n == 0) return;
    if (!s) { d[0] = 0; return; }
    strncpy(d, s, n - 1);
    d[n - 1] = 0;
}

static void *psram_z(size_t n) {
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(n);
    if (p) memset(p, 0, n);
    return p;
}

static void act_set(ActState st, const char *msg, int sid = 0) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_act.state = st;
    if (msg) cpy(g_act.message, sizeof(g_act.message), msg);
    if (sid) g_act.spool_id = sid;
    act_seq++;
    g_act.seq = act_seq;
    xSemaphoreGive(mutex);
}

static void inv_publish(InventoryStatus st, const char *err) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_inv->status = st;
    if (err) cpy(g_inv->error_msg, sizeof(g_inv->error_msg), err);
    else g_inv->error_msg[0] = 0;
    g_inv->fetched_ms = millis();
    inv_seq++;
    g_inv->seq = inv_seq;
    xSemaphoreGive(mutex);
}

static bool endpoint(char *ip, uint16_t *port) {
    return moonraker_endpoint(ip, 16, port) && ip[0];
}

/* Lê o corpo HTTP para o buffer PSRAM. Retorna o código, ou -1 em falha de conexão. */
static int http_do(const char *url, const char *method, const char *payload,
                   int timeout_ms, int *outlen) {
    HTTPClient http;
    int t = timeout_ms > 0 ? timeout_ms : 8000;
    http.setConnectTimeout(t > 8000 ? 8000 : t);
    http.setTimeout(t);
    http.setReuse(false);
    if (!http.begin(url)) return -1;
    if (payload) http.addHeader("Content-Type", "application/json");
    int code;
    if (!method || strcmp(method, "GET") == 0) code = http.GET();
    else if (strcmp(method, "POST") == 0) code = http.POST(payload ? payload : "");
    else if (strcmp(method, "PATCH") == 0) code = http.sendRequest("PATCH", (uint8_t *)(payload ? payload : ""), payload ? strlen(payload) : 0);
    else if (strcmp(method, "DELETE") == 0) code = http.sendRequest("DELETE");
    else code = http.POST(payload ? payload : "");

    int len = 0;
    if (http_buf && http_cap) {
        WiFiClient *s = http.getStreamPtr();
        unsigned long start = millis();
        while (len < (int)http_cap - 1 && (millis() - start) < (unsigned long)t) {
            int avail = s->available();
            if (avail <= 0) {
                if (!http.connected() && s->available() == 0) break;
                delay(4);
                continue;
            }
            int n = s->read((uint8_t *)http_buf + len, (int)http_cap - 1 - len);
            if (n > 0) len += n;
            else break;
        }
        http_buf[len] = 0;
    }
    http.end();
    if (outlen) *outlen = len;
    return code;
}

static int http_get_url(const char *url, int timeout_ms, int *outlen) {
    return http_do(url, "GET", nullptr, timeout_ms, outlen);
}

static int http_post_url(const char *url, const char *payload, int timeout_ms, int *outlen) {
    return http_do(url, "POST", payload, timeout_ms, outlen);
}

/* Monta o JSON do proxy e POSTa. Corpo de escrita (PATCH/POST) vai em `body_json`. */
static bool proxy(const char *ip, uint16_t port,
                  const char *method, const char *path, const char *body_json,
                  int timeout_ms, int *outlen, char *err, size_t errn) {
    JsonDocument doc;
    doc["request_method"] = method;
    doc["path"] = path;
    doc["use_v2_response"] = true;
    if (body_json && body_json[0]) doc["body"] = serialized(body_json);
    static char payload[2048];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    if (n == 0 || n >= sizeof(payload)) {
        if (err) cpy(err, errn, "Pedido grande demais");
        return false;
    }
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%u/server/spoolman/proxy", ip, port);
    int len = 0;
    int code = http_post_url(url, payload, timeout_ms, &len);
    if (outlen) *outlen = len;
    if (code < 0) {
        if (err) cpy(err, errn, "Sem conexao com a U1");
        return false;
    }
    /* Moonraker / v2 pode responder 200 com error no envelope. */
    if (http_buf && len > 0) {
        char emsg[160] = {0};
        /* tenta detectar error v2 sem parse completo do array */
        JsonDocument ed;
        if (!deserializeJson(ed, http_buf, len)) {
            JsonVariant r = ed["result"];
            JsonObject root = r.is<JsonObject>() ? r.as<JsonObject>() : ed.as<JsonObject>();
            if (root["error"].is<JsonObject>()) {
                JsonObject e = root["error"];
                const char *m = e["message"] | e["error"] | "";
                if (m && m[0]) {
                    if (err) cpy(err, errn, m);
                    return false;
                }
            } else if (root["error"].is<const char *>()) {
                const char *m = root["error"];
                if (m && m[0] && strcmp(m, "null") != 0) {
                    if (err) cpy(err, errn, m);
                    return false;
                }
            }
        }
        (void)emsg;
    }
    if (code < 200 || code >= 300) {
        if (err) {
            snprintf(err, errn, "HTTP %d em %s", code, path);
        }
        return false;
    }
    return true;
}

static bool u1_get(const char *ip, uint16_t port, const char *path, int timeout_ms, int *outlen, char *err, size_t errn) {
    char url[320];
    snprintf(url, sizeof(url), "http://%s:%u%s", ip, port, path);
    int code = http_get_url(url, timeout_ms, outlen);
    if (code < 0) { if (err) cpy(err, errn, "Sem conexao com a U1"); return false; }
    if (code < 200 || code >= 300) { if (err) snprintf(err, errn, "HTTP %d em %s", code, path); return false; }
    return true;
}

static bool u1_post(const char *ip, uint16_t port, const char *path, const char *payload, int timeout_ms, int *outlen, char *err, size_t errn) {
    char url[320];
    snprintf(url, sizeof(url), "http://%s:%u%s", ip, port, path);
    int code = http_post_url(url, payload, timeout_ms, outlen);
    if (code < 0) { if (err) cpy(err, errn, "Sem conexao com a U1"); return false; }
    if (code < 200 || code >= 300) {
        /* tenta mensagem de erro do Moonraker */
        if (http_buf && *outlen && err) {
            JsonDocument d;
            if (!deserializeJson(d, http_buf, *outlen)) {
                const char *m = d["error"]["message"] | d["error"] | d["message"] | "";
                if (m && m[0]) { cpy(err, errn, m); return false; }
            }
        }
        if (err) snprintf(err, errn, "HTTP %d em %s", code, path);
        return false;
    }
    return true;
}

/* ---- firmware-config: Filament Detection = External ----
 * Na U1 real isto é servido pelo nginx na porta 80, NÃO pelo Moonraker
 * (GET :7125/firmware-config/api/settings → 404). O JSON tem ~30 066 bytes;
 * http_buf (192 KB em PSRAM) cabe folgado. O parse usa filtro ArduinoJson
 * e só extrai o item "rfid" (settings_parse_rfid_mode). Valor real atual:
 * "openrfid" → modo 2 → require_external mostra a 2ª mensagem (igual ao app). */
static int read_external_mode(const char *ip, uint16_t port) {
    char err[80];
    int len = 0;
    (void)port;
    if (!u1_get(ip, 80, "/firmware-config/api/settings", 4000, &len, err, sizeof(err)))
        return 0;
    if (!http_buf || len <= 0) return 0;
    return settings_parse_rfid_mode(http_buf, (size_t)len);
}

static bool parse_feeder_fresh(FeederInfo *f, char *err, size_t errn, const char *ip,
                               uint16_t port, int uid_channel = -1, char *uid = nullptr) {
    int len = 0;
    if (!u1_get(ip, port,
                "/printer/objects/query?filament_feed%20left&filament_feed%20right&filament_detect&print_task_config&print_stats&webhooks",
                4000, &len, err, errn))
        return false;
    JsonDocument doc;
    if (deserializeJson(doc, http_buf, len)) {
        cpy(err, errn, "Resposta invalida dos feeders");
        return false;
    }
    JsonObject root = doc["result"].is<JsonObject>() ? doc["result"].as<JsonObject>() : doc.as<JsonObject>();
    JsonObject st = root["status"].is<JsonObject>() ? root["status"].as<JsonObject>() : root;
    memset(f, 0, sizeof(*f));
    const char *keys[4][2] = {
        {"filament_feed left", "extruder0"},
        {"filament_feed left", "extruder1"},
        {"filament_feed right", "extruder2"},
        {"filament_feed right", "extruder3"},
    };
    int got = 0;
    for (int i = 0; i < 4; i++) {
        if (!st[keys[i][0]].is<JsonObject>()) continue;
        JsonObject side = st[keys[i][0]];
        if (!side[keys[i][1]].is<JsonObject>()) continue;
        JsonObject ex = side[keys[i][1]];
        if (!ex["filament_detected"].is<bool>()) continue;
        f->ch[i].module_exist = ex["module_exist"] | false;
        f->ch[i].filament_detected = ex["filament_detected"] | false;
        f->ch[i].disable_auto = ex["disable_auto"] | false;
        cpy(f->ch[i].channel_state, sizeof(f->ch[i].channel_state), ex["channel_state"] | "");
        cpy(f->ch[i].channel_error, sizeof(f->ch[i].channel_error), ex["channel_error"] | "ok");
        got++;
    }
    if (st["print_task_config"].is<JsonObject>()) {
        JsonObject ptc = st["print_task_config"];
        if (ptc["filament_spool_id"].is<JsonArray>()) {
            JsonArray a = ptc["filament_spool_id"];
            for (int i = 0; i < 4 && i < (int)a.size(); i++)
                f->ch[i].spool_id = a[i].is<int>() ? a[i].as<int>() : 0;
        }
        if (ptc["filament_type"].is<JsonArray>()) {
            JsonArray a = ptc["filament_type"];
            for (int i = 0; i < 4 && i < (int)a.size(); i++)
                cpy(f->ch[i].filament_type, sizeof(f->ch[i].filament_type), a[i] | "");
        }
    }
    const char *ws = "ready";
    if (st["webhooks"].is<JsonObject>()) ws = st["webhooks"]["state"] | "ready";
    f->printer_ready = (strcasecmp(ws, "ready") == 0);
    const char *ps = "";
    if (st["print_stats"].is<JsonObject>()) ps = st["print_stats"]["state"] | "";
    f->busy_print = (strcasecmp(ps, "printing") == 0 || strcasecmp(ps, "paused") == 0);
    f->valid = got > 0;
    if (uid) {
        uid[0] = 0;
        JsonArray bytes = st["filament_detect"]["info"][uid_channel]["CARD_UID"].as<JsonArray>();
        const size_t count = bytes.size();
        if (count == 4 || count == 7 || count == 10) {
            for (size_t i = 0; i < count; ++i) {
                if (!bytes[i].is<uint8_t>()) { uid[0] = 0; break; }
                snprintf(uid + i * 2, 3, "%02X", static_cast<unsigned>(bytes[i].as<uint8_t>()));
            }
        }
    }
    if (!f->printer_ready) {
        cpy(err, errn, "A impressora nao esta pronta");
        return false;
    }
    return true;
}

static const char *ok_phases[] = {
    "wait_insert", "preload_finish", "load_finish", "unload_finish", "none", "inited", nullptr
};

static bool idle_for_manual(const FeederInfo *f, char *err, size_t errn, bool allow_print) {
    if (!f || !f->valid) { cpy(err, errn, "Status dos feeders indisponivel"); return false; }
    /* allow_print: associar rolo é só metadado (spoollink), pode ocorrer durante a impressão.
     * Carga/descarga física continua bloqueada. */
    if (f->busy_print && !allow_print) { cpy(err, errn, "Troca manual indisponivel durante impressao ou pausa."); return false; }
    for (int i = 0; i < 4; i++) {
        if (!f->ch[i].module_exist) {
            snprintf(err, errn, "Feeder %d indisponivel", i + 1);
            return false;
        }
        if (f->ch[i].channel_error[0] && strcasecmp(f->ch[i].channel_error, "ok") != 0) {
            snprintf(err, errn, "Resolva o erro do feeder %d na U1 antes da troca.", i + 1);
            return false;
        }
        const char *ph = f->ch[i].channel_state;
        bool okp = (ph[0] == 0);
        for (int k = 0; ok_phases[k]; k++) if (strcmp(ph, ok_phases[k]) == 0) okp = true;
        if (!okp) {
            snprintf(err, errn, "Aguarde a operacao atual do feeder %d terminar.", i + 1);
            return false;
        }
    }
    return true;
}

static bool require_external(const char *ip, uint16_t port, char *err, size_t errn) {
    int m = read_external_mode(ip, port);
    if (m == 0) {
        cpy(err, errn, "Nao foi possivel verificar Filament Detection. Confirme o modo External no Firmware Config.");
        return false;
    }
    if (m != 1) {
        cpy(err, errn, "Selecione Filament Detection = External no Firmware Config antes de associar ou carregar rolos. Leitores internos podem apagar os dados.");
        return false;
    }
    return true;
}

/* ---- REFRESH ---- */
static void do_refresh(void) {
    char ip[16] = {0};
    uint16_t port = 7125;
    char err[160] = {0};
    if (!WiFi.isConnected()) {
        inv_publish(INV_OFFLINE, "Sem Wi-Fi");
        act_set(AR_ERR, "Sem Wi-Fi");
        return;
    }
    if (!endpoint(ip, &port)) {
        inv_publish(INV_OFFLINE, "U1 nao encontrada na rede");
        act_set(AR_ERR, "U1 nao encontrada na rede");
        return;
    }
    inv_publish(INV_LOADING, nullptr);
    int len = 0;
    if (!proxy(ip, port, "GET", "/v1/spool?allow_archived=true", nullptr, 12000, &len, err, sizeof(err))) {
        inv_publish(INV_ERROR, err[0] ? err : "Falha ao ler o estoque");
        act_set(AR_ERR, err[0] ? err : "Falha ao ler o estoque");
        return;
    }
    WarehouseSpool *tmp = (WarehouseSpool *)psram_z(sizeof(WarehouseSpool) * WAREHOUSE_MAX);
    if (!tmp) {
        inv_publish(INV_ERROR, "Sem memoria para o estoque");
        act_set(AR_ERR, "Sem memoria");
        return;
    }
    int n = spool_parse_list(http_buf, (size_t)len, tmp, WAREHOUSE_MAX, err, sizeof(err));
    if (n < 0) {
        free(tmp);
        inv_publish(INV_ERROR, err[0] ? err : "JSON do estoque invalido");
        act_set(AR_ERR, err[0] ? err : "JSON invalido");
        return;
    }
    int active = 0;
    bool sm_ok = false;
    /* spool_id dedicado, depois status */
    int slen = 0;
    char e2[80];
    if (u1_get(ip, port, "/server/spoolman/spool_id", 3000, &slen, e2, sizeof(e2))) {
        spool_parse_active_id(http_buf, (size_t)slen, &active, &sm_ok);
    }
    if (active <= 0) {
        slen = 0;
        if (u1_get(ip, port, "/server/spoolman/status", 3000, &slen, e2, sizeof(e2))) {
            spool_parse_active_id(http_buf, (size_t)slen, &active, &sm_ok);
        }
    }
    int ext = read_external_mode(ip, port);

    xSemaphoreTake(mutex, portMAX_DELAY);
    g_inv->count = n;
    g_inv->active_id = active;
    g_inv->external_mode = ext;
    memcpy(g_inv->items, tmp, sizeof(WarehouseSpool) * (size_t)n);
    if (n < WAREHOUSE_MAX)
        memset(g_inv->items + n, 0, sizeof(WarehouseSpool) * (size_t)(WAREHOUSE_MAX - n));
    g_inv->status = INV_OK;
    g_inv->error_msg[0] = 0;
    g_inv->fetched_ms = millis();
    inv_seq++;
    g_inv->seq = inv_seq;
    xSemaphoreGive(mutex);
    free(tmp);

    char msg[80];
    snprintf(msg, sizeof(msg), "Estoque: %d rolo%s", n, n == 1 ? "" : "s");
    act_set(AR_OK, msg);
    Serial.printf("[inventory] %d rolos, ativo=%d\n", n, active);
}

/* ---- CATALOG ---- */
static int unwrap_array(JsonDocument &doc, JsonArray &out) {
    if (doc.is<JsonArray>()) { out = doc.as<JsonArray>(); return (int)out.size(); }
    JsonVariant r = doc["result"];
    if (r.is<JsonArray>()) { out = r.as<JsonArray>(); return (int)out.size(); }
    if (r.is<JsonObject>()) {
        JsonObject o = r.as<JsonObject>();
        if (o["response"].is<JsonArray>()) { out = o["response"].as<JsonArray>(); return (int)out.size(); }
        if (o["error"].is<JsonObject>()) return -1;
    }
    if (doc["response"].is<JsonArray>()) { out = doc["response"].as<JsonArray>(); return (int)out.size(); }
    return -1;
}

static void do_catalog(void) {
    char ip[16] = {0};
    uint16_t port = 7125;
    char err[160] = {0};
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 nao encontrada na rede"); return; }
    int len = 0;
    if (!proxy(ip, port, "GET", "/v1/vendor", nullptr, 8000, &len, err, sizeof(err))) {
        act_set(AR_ERR, err); return;
    }
    JsonDocument vdoc;
    if (deserializeJson(vdoc, http_buf, len)) { act_set(AR_ERR, "Catalogo (vendor) invalido"); return; }
    JsonArray va;
    int nv = unwrap_array(vdoc, va);
    if (nv < 0) nv = 0;

    xSemaphoreTake(mutex, portMAX_DELAY);
    memset(g_cat->vendors, 0, sizeof(g_cat->vendors));
    int nvo = 0;
    for (JsonObject o : va) {
        if (nvo >= VENDOR_MAX) break;
        g_cat->vendors[nvo].id = o["id"] | 0;
        cpy(g_cat->vendors[nvo].name, sizeof(g_cat->vendors[nvo].name), o["name"] | "");
        nvo++;
    }
    g_cat->n_vendors = nvo;
    xSemaphoreGive(mutex);

    len = 0;
    if (!proxy(ip, port, "GET", "/v1/filament", nullptr, 10000, &len, err, sizeof(err))) {
        act_set(AR_ERR, err); return;
    }
    JsonDocument fdoc;
    if (deserializeJson(fdoc, http_buf, len)) { act_set(AR_ERR, "Catalogo (filament) invalido"); return; }
    JsonArray fa;
    int nf = unwrap_array(fdoc, fa);
    if (nf < 0) nf = 0;

    xSemaphoreTake(mutex, portMAX_DELAY);
    memset(g_cat->filaments, 0, sizeof(g_cat->filaments));
    int nfo = 0;
    for (JsonObject o : fa) {
        if (nfo >= FILAMENT_MAX) break;
        FilamentItem *fi = &g_cat->filaments[nfo];
        fi->id = o["id"] | 0;
        fi->vendor_id = 0;
        if (o["vendor"].is<JsonObject>()) {
            fi->vendor_id = o["vendor"]["id"] | 0;
            cpy(fi->vendor, sizeof(fi->vendor), o["vendor"]["name"] | "");
        }
        cpy(fi->name, sizeof(fi->name), o["name"] | "");
        cpy(fi->material, sizeof(fi->material), o["material"] | "");
        const char *hx = o["color_hex"] | "";
        if (hx[0] == '#') hx++;
        cpy(fi->color_hex, sizeof(fi->color_hex), hx);
        cpy(fi->multi_color, sizeof(fi->multi_color), o["multi_color_hexes"] | "");
        fi->density = o["density"] | 0.0f;
        fi->diameter = o["diameter"] | 0.0f;
        fi->weight = o["weight"] | 0.0f;
        nfo++;
    }
    g_cat->n_filaments = nfo;
    g_cat->status = INV_OK;
    g_cat->error_msg[0] = 0;
    cat_seq++;
    g_cat->seq = cat_seq;
    xSemaphoreGive(mutex);
    act_set(AR_OK, "Catalogo atualizado");
}

/* ---- INSPECT ---- */
static void do_inspect(const char *uid) {
    char norm[24] = {0}, err[80] = {0};
    if (!spool_validate_uid(uid, norm, sizeof(norm), err, sizeof(err))) {
        act_set(AR_ERR, err[0] ? err : "UID NFC invalido");
        return;
    }
    /* garante lista atual */
    if (g_inv->status != INV_OK) do_refresh();
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_act.n_matches = 0;
    g_act.inspect_uid[0] = 0;
    cpy(g_act.inspect_uid, sizeof(g_act.inspect_uid), norm);
    for (int i = 0; i < g_inv->count && g_act.n_matches < 8; i++) {
        const char *u = g_inv->items[i].card_uids;
        if (!u[0]) continue;
        /* uids separados por virgula */
        const char *p = u;
        while (*p) {
            char tok[24] = {0};
            int k = 0;
            while (*p && *p != ',' && k < 23) tok[k++] = *p++;
            tok[k] = 0;
            if (*p == ',') p++;
            char tn[24] = {0};
            spool_normalize_uid(tok, tn, sizeof(tn));
            if (strcmp(tn, norm) == 0) {
                g_act.matches[g_act.n_matches++] = g_inv->items[i];
                break;
            }
        }
    }
    int nm = g_act.n_matches;
    WarehouseSpool first = (nm > 0) ? g_act.matches[0] : WarehouseSpool{};
    xSemaphoreGive(mutex);
    if (nm == 0) {
        char m[120];
        snprintf(m, sizeof(m), "Tag nao cadastrada no Spoolman: %s. Use Novo rolo no Armazem.", norm);
        act_set(AR_ERR, m);
        return;
    }
    if (nm > 1) {
        act_set(AR_ERR, "UID duplicado no Spoolman. Corrija os cadastros antes de associar.");
        return;
    }
    if (first.archived) {
        char m[140], idn[96];
        spool_identity(&first, idn, sizeof(idn));
        snprintf(m, sizeof(m), "Tag ja cadastrada: %s. Este rolo esta arquivado.", idn);
        act_set(AR_ERR, m);
        return;
    }
    char m[80];
    snprintf(m, sizeof(m), "Rolo #%d identificado", first.info.id);
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_act.saved = first;
    g_act.spool_id = first.info.id;
    xSemaphoreGive(mutex);
    act_set(AR_OK, m, first.info.id);
}

static bool unique_uid(const char *norm, int self, char *err, size_t errn) {
    if (g_inv->status != INV_OK) {
        /* tenta refresh curto: usa o que houver */
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    int n = g_inv->count;
    /* copy ids+uids out to not hold mutex long — small */
    struct { int id; char uids[SPOOL_UIDS_LEN]; bool archived; } *rows =
        (decltype(rows))malloc(sizeof(*rows) * (size_t)(n > 0 ? n : 1));
    if (!rows) { xSemaphoreGive(mutex); cpy(err, errn, "Sem memoria"); return false; }
    for (int i = 0; i < n; i++) {
        rows[i].id = g_inv->items[i].info.id;
        cpy(rows[i].uids, sizeof(rows[i].uids), g_inv->items[i].card_uids);
        rows[i].archived = g_inv->items[i].archived;
    }
    xSemaphoreGive(mutex);
    for (int i = 0; i < n; i++) {
        if (rows[i].id == self) continue;
        const char *p = rows[i].uids;
        while (*p) {
            char tok[24] = {0};
            int k = 0;
            while (*p && *p != ',' && k < 23) tok[k++] = *p++;
            if (*p == ',') p++;
            char tn[24] = {0};
            spool_normalize_uid(tok, tn, sizeof(tn));
            if (strcmp(tn, norm) == 0) {
                snprintf(err, errn, "Tag ja vinculada ao rolo #%d%s. Nenhuma tag foi transferida.",
                         rows[i].id, rows[i].archived ? " (arquivado)" : "");
                free(rows);
                return false;
            }
        }
    }
    free(rows);
    return true;
}

static bool tag_field(const char *ip, uint16_t port, char *err, size_t errn) {
    int len = 0;
    if (!proxy(ip, port, "GET", "/v1/field/spool", nullptr, 6000, &len, err, errn))
        return false;
    JsonDocument doc;
    if (deserializeJson(doc, http_buf, len)) { cpy(err, errn, "Campos do Spoolman invalidos"); return false; }
    JsonArray arr;
    int n = unwrap_array(doc, arr);
    if (n >= 0) {
        for (JsonObject f : arr) {
            const char *key = f["key"] | "";
            if (strcmp(key, "card_uids") == 0) {
                const char *ft = f["field_type"] | "";
                if (strcmp(ft, "text") != 0) {
                    cpy(err, errn, "O campo card_uids precisa ser do tipo texto no Spoolman.");
                    return false;
                }
                return true;
            }
        }
    }
    /* cria o campo */
    JsonDocument body;
    body["name"] = "Card UIDs";
    body["field_type"] = "text";
    body["order"] = 1;
    body["default_value"] = "\"\"";
    char bj[256];
    serializeJson(body, bj, sizeof(bj));
    len = 0;
    if (!proxy(ip, port, "POST", "/v1/field/spool/card_uids", bj, 6000, &len, err, errn))
        return false;
    return true;
}

static void do_update(int id, const char *location, const char *comment, bool set_w, float rem) {
    char ip[16] = {0};
    uint16_t port = 7125;
    char err[160] = {0};
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 nao encontrada"); return; }
    JsonDocument body;
    body["location"] = location ? location : "";
    body["comment"] = comment ? comment : "";
    if (set_w) body["remaining_weight"] = rem;
    char bj[SPOOL_COMMENT_LEN + 160];
    serializeJson(body, bj, sizeof(bj));
    char path[40];
    snprintf(path, sizeof(path), "/v1/spool/%d", id);
    int len = 0;
    if (!proxy(ip, port, "PATCH", path, bj, 8000, &len, err, sizeof(err))) {
        act_set(AR_ERR, err);
        history_add(err);
        return;
    }
    WarehouseSpool saved{};
    spool_parse_one(http_buf, (size_t)len, &saved, id, err, sizeof(err));
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_act.saved = saved;
    g_act.spool_id = id;
    xSemaphoreGive(mutex);
    char h[120];
    snprintf(h, sizeof(h), "Rolo #%d atualizado no Spoolman", id);
    history_add(h);
    act_set(AR_OK, h, id);
    do_refresh();
}

static void do_bind(int id, const char *uid) {
    char ip[16] = {0};
    uint16_t port = 7125;
    char err[160] = {0}, norm[24] = {0};
    if (!spool_validate_uid(uid, norm, sizeof(norm), err, sizeof(err))) {
        act_set(AR_ERR, err); return;
    }
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 nao encontrada"); return; }
    if (g_inv->status != INV_OK) do_refresh();
    if (!unique_uid(norm, id, err, sizeof(err))) { act_set(AR_ERR, err); return; }
    if (!tag_field(ip, port, err, sizeof(err))) { act_set(AR_ERR, err); return; }
    char path[40];
    snprintf(path, sizeof(path), "/v1/spool/%d", id);
    int len = 0;
    if (!proxy(ip, port, "GET", path, nullptr, 6000, &len, err, sizeof(err))) {
        act_set(AR_ERR, err); return;
    }
    WarehouseSpool cur{};
    if (!spool_parse_one(http_buf, (size_t)len, &cur, id, err, sizeof(err))) {
        act_set(AR_ERR, err[0] ? err : "Rolo invalido"); return;
    }
    if (cur.archived) { act_set(AR_ERR, "Desarquive o rolo antes de vincular NFC."); return; }

    /* preserva extra e une UIDs */
    JsonDocument src;
    if (deserializeJson(src, http_buf, len)) { act_set(AR_ERR, "JSON do rolo invalido"); return; }
    JsonObject obj = src.as<JsonObject>();
    if (src["result"].is<JsonObject>()) {
        JsonObject r = src["result"].as<JsonObject>();
        if (r["response"].is<JsonObject>()) obj = r["response"].as<JsonObject>();
    } else if (src["response"].is<JsonObject>()) obj = src["response"].as<JsonObject>();

    char joined[SPOOL_UIDS_LEN] = {0};
    if (cur.card_uids[0]) {
        snprintf(joined, sizeof(joined), "%s,%s", cur.card_uids, norm);
    } else {
        cpy(joined, sizeof(joined), norm);
    }
    /* valor no Spoolman: JSON-quoted string, ex. "\"AABBCCDD,EEFF\"" */
    char quoted[SPOOL_UIDS_LEN + 8];
    snprintf(quoted, sizeof(quoted), "\"%s\"", joined);

    JsonDocument patch;
    if (obj["extra"].is<JsonObject>()) {
        for (JsonPair kv : obj["extra"].as<JsonObject>()) {
            patch["extra"][kv.key()] = kv.value();
        }
    }
    patch["extra"]["card_uids"] = quoted;
    char bj[512];
    serializeJson(patch, bj, sizeof(bj));
    len = 0;
    if (!proxy(ip, port, "PATCH", path, bj, 8000, &len, err, sizeof(err))) {
        act_set(AR_ERR, err);
        history_add(err);
        return;
    }
    WarehouseSpool saved{};
    spool_parse_one(http_buf, (size_t)len, &saved, id, err, sizeof(err));
    if (saved.card_uids[0] && !strstr(saved.card_uids, norm)) {
        act_set(AR_ERR, "Vinculo recebido sem NFC confirmado. Atualize o estoque.");
        return;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_act.saved = saved;
    g_act.spool_id = id;
    xSemaphoreGive(mutex);
    char h[140];
    snprintf(h, sizeof(h), "NFC %s associado ao rolo #%d", norm, id);
    history_add(h);
    act_set(AR_OK, h, id);
    do_refresh();
}

static int find_or_create_vendor(const char *ip, uint16_t port, const char *name, char *err, size_t errn) {
    int len = 0;
    if (!proxy(ip, port, "GET", "/v1/vendor", nullptr, 8000, &len, err, errn)) return 0;
    JsonDocument doc;
    if (deserializeJson(doc, http_buf, len)) { cpy(err, errn, "Lista de marcas invalida"); return 0; }
    JsonArray arr;
    unwrap_array(doc, arr);
    for (JsonObject v : arr) {
        const char *n = v["name"] | "";
        if (n[0] && strcasecmp(n, name) == 0) return v["id"] | 0;
    }
    JsonDocument body;
    body["name"] = name;
    char bj[128];
    serializeJson(body, bj, sizeof(bj));
    len = 0;
    if (!proxy(ip, port, "POST", "/v1/vendor", bj, 8000, &len, err, errn)) return 0;
    JsonDocument rd;
    if (deserializeJson(rd, http_buf, len)) return 0;
    JsonObject obj = rd.as<JsonObject>();
    if (rd["result"].is<JsonObject>() && rd["result"]["response"].is<JsonObject>())
        obj = rd["result"]["response"].as<JsonObject>();
    else if (rd["response"].is<JsonObject>()) obj = rd["response"].as<JsonObject>();
    return obj["id"] | 0;
}

static bool same_filament(JsonObject f, const CreateSpoolReq *r, const char *color0, const char *multi, const char *dir) {
    auto eq = [](const char *a, const char *b) {
        if (!a) a = "";
        if (!b) b = "";
        return strcasecmp(a, b) == 0;
    };
    if (!eq(f["name"] | "", r->name)) return false;
    if (!eq(f["material"] | "", r->material)) return false;
    if (!eq(f["color_hex"] | "", color0)) return false;
    if (!eq(f["multi_color_hexes"] | "", multi)) return false;
    if (!eq(f["multi_color_direction"] | "", dir)) return false;
    float d = f["density"] | -1.0f, di = f["diameter"] | -1.0f, w = f["weight"] | -1.0f;
    if (d != r->density || di != r->diameter || w != r->initial_g) return false;
    return true;
}

static int find_or_create_filament(const char *ip, uint16_t port, const CreateSpoolReq *r,
                                   int vendor_id, const char *colors, const char *dir,
                                   char *err, size_t errn) {
    char color0[8] = {0};
    char multi[SPOOL_COLORS_LEN] = {0};
    /* colors já normalizado: RRGGBB ou RRGGBB,RRGGBB */
    const char *c = colors;
    int nc = 0;
    const char *p = c;
    while (*p) {
        if (*p == ',') nc++;
        p++;
    }
    if (c[0]) nc++;
    if (nc <= 1) {
        cpy(color0, sizeof(color0), c);
    } else {
        cpy(multi, sizeof(multi), c);
        /* primeira cor */
        int i = 0;
        while (c[i] && c[i] != ',' && i < 6) { color0[i] = c[i]; i++; }
        color0[i] = 0;
    }
    int len = 0;
    if (!proxy(ip, port, "GET", "/v1/filament", nullptr, 10000, &len, err, errn)) return 0;
    JsonDocument doc;
    if (deserializeJson(doc, http_buf, len)) { cpy(err, errn, "Lista de filamentos invalida"); return 0; }
    JsonArray arr;
    unwrap_array(doc, arr);
    for (JsonObject f : arr) {
        int vid = 0;
        if (f["vendor"].is<JsonObject>()) vid = f["vendor"]["id"] | 0;
        else vid = f["vendor_id"] | 0;
        if (vid == vendor_id && same_filament(f, r, color0, multi, nc > 1 ? dir : ""))
            return f["id"] | 0;
    }
    JsonDocument body;
    body["name"] = r->name;
    body["material"] = r->material;
    body["vendor_id"] = vendor_id;
    body["density"] = r->density;
    body["diameter"] = r->diameter;
    body["weight"] = r->initial_g;
    if (nc <= 1) {
        body["color_hex"] = color0;
    } else {
        body["color_hex"] = color0;
        body["multi_color_hexes"] = multi;
        body["multi_color_direction"] = dir && dir[0] ? dir : "coaxial";
    }
    char bj[512];
    serializeJson(body, bj, sizeof(bj));
    len = 0;
    if (!proxy(ip, port, "POST", "/v1/filament", bj, 8000, &len, err, errn)) return 0;
    JsonDocument rd;
    if (deserializeJson(rd, http_buf, len)) return 0;
    JsonObject obj = rd.as<JsonObject>();
    if (rd["result"].is<JsonObject>() && rd["result"]["response"].is<JsonObject>())
        obj = rd["result"]["response"].as<JsonObject>();
    else if (rd["response"].is<JsonObject>()) obj = rd["response"].as<JsonObject>();
    return obj["id"] | 0;
}

static void do_create(const CreateSpoolReq *r) {
    char ip[16] = {0};
    uint16_t port = 7125;
    char err[160] = {0}, norm[24] = {0};
    if (!spool_validate_uid(r->uid, norm, sizeof(norm), err, sizeof(err))) {
        act_set(AR_ERR, err); return;
    }
    if (r->remaining_g > r->initial_g + 0.01f) {
        act_set(AR_ERR, "Peso restante nao pode ser maior que o inicial."); return;
    }
    if (r->initial_g < 0.01f) { act_set(AR_ERR, "Peso inicial invalido."); return; }
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 nao encontrada"); return; }
    if (g_inv->status != INV_OK) do_refresh();
    if (!unique_uid(norm, 0, err, sizeof(err))) { act_set(AR_ERR, err); return; }
    if (!tag_field(ip, port, err, sizeof(err))) { act_set(AR_ERR, err); return; }

    int fid = r->filament_id;
    if (fid <= 0) {
        if (!r->vendor[0] || !r->name[0] || !r->material[0] || !r->colors[0]) {
            act_set(AR_ERR, "Preencha marca, nome, material e cor."); return;
        }
        /* normaliza cores */
        char colors[SPOOL_COLORS_LEN] = {0};
        int ci = 0, ncolors = 1;
        for (const char *p = r->colors; *p && ci < (int)sizeof(colors) - 1; p++) {
            char c = *p;
            if (c == '#') continue;
            if (c == ' ') continue;
            if (c >= 'a' && c <= 'f') c -= 32;
            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || c == ',') {
                if (c == ',') ncolors++;
                colors[ci++] = c;
            } else {
                act_set(AR_ERR, "Use cores hexadecimais com seis digitos."); return;
            }
        }
        if (ncolors > 5) { act_set(AR_ERR, "Use ate cinco cores."); return; }
        /* cada segmento 6 hex */
        int acc = 0;
        for (int i = 0; ; i++) {
            if (colors[i] == ',' || colors[i] == 0) {
                if (acc != 6) { act_set(AR_ERR, "Use cores hexadecimais com seis digitos."); return; }
                acc = 0;
                if (!colors[i]) break;
            } else acc++;
        }
        const char *dir = r->direction[0] ? r->direction : "coaxial";
        if (ncolors > 1 && strcmp(dir, "coaxial") != 0 && strcmp(dir, "longitudinal") != 0) {
            act_set(AR_ERR, "Direcao das cores invalida."); return;
        }
        int vid = find_or_create_vendor(ip, port, r->vendor, err, sizeof(err));
        if (vid <= 0) { act_set(AR_ERR, err[0] ? err : "Falha ao criar marca"); return; }
        fid = find_or_create_filament(ip, port, r, vid, colors, dir, err, sizeof(err));
        if (fid <= 0) { act_set(AR_ERR, err[0] ? err : "Falha ao criar filamento"); return; }
    }

    if (!unique_uid(norm, 0, err, sizeof(err))) { act_set(AR_ERR, err); return; }

    char quoted[32];
    snprintf(quoted, sizeof(quoted), "\"%s\"", norm);
    JsonDocument body;
    body["filament_id"] = fid;
    body["initial_weight"] = r->initial_g;
    body["used_weight"] = r->initial_g - r->remaining_g;
    body["location"] = r->location;
    body["comment"] = r->comment;
    body["extra"]["card_uids"] = quoted;
    char bj[768];
    serializeJson(body, bj, sizeof(bj));
    int len = 0;
    if (!proxy(ip, port, "POST", "/v1/spool", bj, 8000, &len, err, sizeof(err))) {
        act_set(AR_ERR, err);
        history_add(err);
        return;
    }
    WarehouseSpool saved{};
    if (!spool_parse_one(http_buf, (size_t)len, &saved, 0, err, sizeof(err))) {
        act_set(AR_ERR, err[0] ? err : "Cadastro sem resposta valida");
        return;
    }
    if (!strstr(saved.card_uids, norm)) {
        act_set(AR_ERR, "Cadastro recebido sem NFC confirmado. Atualize o estoque antes de repetir.");
        return;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_act.saved = saved;
    g_act.spool_id = saved.info.id;
    xSemaphoreGive(mutex);
    char h[140];
    snprintf(h, sizeof(h), "Rolo #%d cadastrado (NFC %s)", saved.info.id, norm);
    history_add(h);
    act_set(AR_OK, h, saved.info.id);
    do_refresh();
}

static void do_delete(int id, const char *identity) {
    char ip[16] = {0};
    uint16_t port = 7125;
    char err[160] = {0};
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 nao encontrada"); return; }
    /* não apagar se instalado num feeder */
    FeederInfo f{};
    char fe[120] = {0};
    if (parse_feeder_fresh(&f, fe, sizeof(fe), ip, port) && f.valid) {
        for (int i = 0; i < 4; i++) {
            if (f.ch[i].spool_id == id) {
                snprintf(err, sizeof(err), "Rolo #%d esta no feeder %d. Remova antes de excluir.", id, i + 1);
                act_set(AR_ERR, err);
                return;
            }
        }
    }
    /* confere identidade */
    const WarehouseSpool *found = nullptr;
    WarehouseSpool copy{};
    xSemaphoreTake(mutex, portMAX_DELAY);
    for (int i = 0; i < g_inv->count; i++) {
        if (g_inv->items[i].info.id == id) { copy = g_inv->items[i]; found = &copy; break; }
    }
    xSemaphoreGive(mutex);
    if (found) {
        char idn[160];
        spool_identity(found, idn, sizeof(idn));
        if (identity && identity[0] && strcmp(identity, idn) != 0) {
            act_set(AR_ERR, "Identidade nao confere. Atualize o estoque e tente de novo.");
            return;
        }
    }
    char path[40];
    snprintf(path, sizeof(path), "/v1/spool/%d", id);
    int len = 0;
    if (!proxy(ip, port, "DELETE", path, nullptr, 8000, &len, err, sizeof(err))) {
        act_set(AR_ERR, err);
        history_add(err);
        return;
    }
    char h[80];
    snprintf(h, sizeof(h), "Rolo #%d excluido do Spoolman", id);
    history_add(h);
    act_set(AR_OK, h, id);
    do_refresh();
}

static bool get_spool_copy(int id, WarehouseSpool *out) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool ok = false;
    for (int i = 0; i < g_inv->count; i++) {
        if (g_inv->items[i].info.id == id) { *out = g_inv->items[i]; ok = true; break; }
    }
    xSemaphoreGive(mutex);
    return ok;
}

static void do_assign(int ch, int spool_id) {
    char ip[16] = {0};
    uint16_t port = 7125;
    char err[200] = {0};
    if (ch < 0 || ch > 3) { act_set(AR_ERR, "Feeder invalido."); return; }
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 nao encontrada"); return; }
    if (!require_external(ip, port, err, sizeof(err))) { act_set(AR_ERR, err); return; }
    FeederInfo f{};
    if (!parse_feeder_fresh(&f, err, sizeof(err), ip, port)) { act_set(AR_ERR, err); return; }
    if (!idle_for_manual(&f, err, sizeof(err), true)) { act_set(AR_ERR, err); return; }
    if (!f.ch[ch].filament_detected) {
        snprintf(err, sizeof(err), "Insira o filamento no feeder %d antes de associar o rolo.", ch + 1);
        act_set(AR_ERR, err); return;
    }
    for (int i = 0; i < 4; i++) {
        if (i != ch && f.ch[i].filament_detected && f.ch[i].spool_id == spool_id) {
            snprintf(err, sizeof(err), "Este rolo ja esta associado ao feeder %d.", i + 1);
            act_set(AR_ERR, err); return;
        }
    }
    WarehouseSpool s{};
    if (!get_spool_copy(spool_id, &s)) {
        /* busca direto */
        char path[40];
        snprintf(path, sizeof(path), "/v1/spool/%d", spool_id);
        int len = 0;
        if (!proxy(ip, port, "GET", path, nullptr, 6000, &len, err, sizeof(err)) ||
            !spool_parse_one(http_buf, (size_t)len, &s, spool_id, err, sizeof(err))) {
            act_set(AR_ERR, err[0] ? err : "Rolo nao encontrado");
            return;
        }
    }
    char info[480], ie[80];
    if (!spool_build_link_info(&s, info, sizeof(info), ie, sizeof(ie))) {
        act_set(AR_ERR, ie[0] ? ie : "Rolo sem material ou cor.");
        return;
    }
    JsonDocument body;
    body["channel"] = ch;
    body["info"] = serialized(info);
    body["message"] = "U1 Factory Display: associacao manual";
    char bj[640];
    serializeJson(body, bj, sizeof(bj));
    int len = 0;
    if (!u1_post(ip, port, "/printer/spoollink/set", bj, 8000, &len, err, sizeof(err))) {
        act_set(AR_ERR, err);
        history_add(err);
        return;
    }
    char h[140];
    snprintf(h, sizeof(h), "Rolo #%d associado ao feeder %d", spool_id, ch + 1);
    history_add(h);
    act_set(AR_OK, h, spool_id);
}

static bool uid_list_contains(const char *list, const char *uid) {
    while (*list) {
        char normalized[21]{};
        size_t n = 0;
        while (*list && *list != ',' && *list != ';') {
            const unsigned char c = *list++;
            if (isxdigit(c)) {
                if (n < sizeof(normalized) - 1) normalized[n] = toupper(c);
                ++n;
            }
        }
        if (n < sizeof(normalized) && !strcmp(normalized, uid)) return true;
        if (*list) ++list;
    }
    return false;
}

static bool nfc_spool_matches(const char *ip, uint16_t port, const char *uid, int id,
                              char *err, size_t errn) {
    int len = 0;
    if (!proxy(ip, port, "GET", "/v1/spool?allow_archived=true", nullptr, 8000, &len, err, errn))
        return false;
    JsonDocument filter, doc;
    JsonDocument fields;
    fields["id"] = true;
    fields["archived"] = true;
    fields["extra"]["card_uids"] = true;
    const char *json = http_buf;
    while (isspace(static_cast<unsigned char>(*json))) ++json;
    if (*json == '[') filter[0].set(fields.as<JsonObject>());
    else {
        filter["response"][0].set(fields.as<JsonObject>());
        filter["result"]["response"][0].set(fields.as<JsonObject>());
    }
    JsonArray rows;
    if (deserializeJson(doc, http_buf, len, DeserializationOption::Filter(filter)) ||
        unwrap_array(doc, rows) < 0) {
        cpy(err, errn, "Falha ao consultar os UIDs no Spoolman.");
        return false;
    }
    unsigned matches = 0;
    bool active_match = false;
    for (JsonObject row : rows) {
        JsonVariant value = row["extra"]["card_uids"];
        String array_json;
        if (value.is<JsonArray>()) serializeJson(value, array_json);
        const char *list = value.is<const char *>() ? value.as<const char *>() : array_json.c_str();
        if (!uid_list_contains(list, uid)) continue;
        ++matches;
        active_match = (row["id"] | 0) == id && !(row["archived"] | false);
    }
    if (matches == 1 && active_match) return true;
    cpy(err, errn, "UID sem vinculo unico com este rolo ativo no Spoolman.");
    return false;
}

static unsigned feeder_presence(const FeederInfo &f) {
    unsigned present = 0;
    for (int i = 0; i < 4; ++i)
        if (f.ch[i].filament_detected) present |= 1u << i;
    return present;
}

static bool preload_done(const FeederChannel &ch) {
    return !strcmp(ch.channel_state, "preload_finish") || !strcmp(ch.channel_state, "load_finish");
}

static void do_nfc_auto(int id, const char *request_uid) {
    char ip[16]{}, uid[24]{}, err[160]{};
    cpy(uid, sizeof(uid), request_uid);
    uint16_t port = 7125;
    FeederInfo f{};
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 nao encontrada."); return; }
    if (!require_external(ip, port, err, sizeof(err)) ||
        !nfc_spool_matches(ip, port, uid, id, err, sizeof(err)) ||
        !parse_feeder_fresh(&f, err, sizeof(err), ip, port) ||
        !idle_for_manual(&f, err, sizeof(err), false)) { act_set(AR_ERR, err); return; }
    const unsigned baseline = feeder_presence(f);
    for (int i = 0; i < 4; ++i) {
        if (f.ch[i].filament_detected && f.ch[i].spool_id == id) {
            act_set(AR_ERR, "Este rolo ja esta em um alimentador."); return;
        }
    }
    if (baseline == 15) { act_set(AR_ERR, "Retire o filamento de um alimentador primeiro."); return; }
    act_set(AR_RUNNING, "Insira o filamento em apenas um alimentador livre.");
    const uint32_t start = millis();
    unsigned inserted = 0, previous = baseline;
    uint32_t since = 0;
    int ch = -1;
    while (!nfc_auto_cancelled && millis() - start < 120000) {
        vTaskDelay(pdMS_TO_TICKS(550));
        if (!parse_feeder_fresh(&f, err, sizeof(err), ip, port) || f.busy_print) {
            act_set(AR_ERR, "Estado da U1 mudou. Leia a tag novamente."); return;
        }
        for (const auto &sensor : f.ch) {
            if (!sensor.module_exist || strcmp(sensor.channel_error, "ok") ||
                strstr(sensor.channel_state, "_fail")) {
                act_set(AR_ERR, "Um alimentador esta indisponivel ou com erro."); return;
            }
        }
        const unsigned present = feeder_presence(f);
        const unsigned new_edges = present & ~previous;
        previous = present;
        if (new_edges) {
            inserted |= new_edges;
            since = millis();
        }
        if ((inserted & baseline) || (inserted & (inserted - 1))) {
            act_set(AR_ERR, "Mais de um canal mudou. Leia a tag novamente."); return;
        }
        if (!inserted) continue;
        if (!(present & inserted)) {
            act_set(AR_ERR, "Filamento retirado. Leia a tag novamente."); return;
        }
        ch = __builtin_ctz(inserted);
        if (preload_done(f.ch[ch]) && millis() - since >= 1200) break;
        ch = -1;
    }
    if (ch < 0 || nfc_auto_cancelled) {
        act_set(AR_ERR, nfc_auto_cancelled ? "Associacao cancelada." : "Prazo de insercao esgotado."); return;
    }
    if (!require_external(ip, port, err, sizeof(err)) ||
        !nfc_spool_matches(ip, port, uid, id, err, sizeof(err)) ||
        !parse_feeder_fresh(&f, err, sizeof(err), ip, port) ||
        !idle_for_manual(&f, err, sizeof(err), false) || nfc_auto_cancelled ||
        (feeder_presence(f) & ~baseline) != inserted || !preload_done(f.ch[ch])) {
        act_set(AR_ERR, err[0] ? err : "Estado mudou. Leia a tag novamente."); return;
    }
    for (int i = 0; i < 4; ++i) {
        if (i != ch && f.ch[i].filament_detected && f.ch[i].spool_id == id) {
            act_set(AR_ERR, "Este rolo ja esta em outro alimentador."); return;
        }
    }
    JsonDocument command;
    command["channel"] = ch;
    command["info"]["CARD_TYPE"] = "NTAG";
    JsonArray bytes = command["info"]["CARD_UID"].to<JsonArray>();
    for (size_t i = 0; uid[i]; i += 2) {
        const char pair[] = {uid[i], uid[i + 1], 0};
        bytes.add(strtoul(pair, nullptr, 16));
    }
    char payload[160];
    serializeJson(command, payload, sizeof(payload));
    int len = 0;
    const bool sent = u1_post(ip, port, "/printer/filament_detect/set", payload,
                              6000, &len, err, sizeof(err));
    // On a lost reply, check the slot without repeating the write.
    const uint32_t verifying = millis();
    unsigned confirmed = 0;
    while (millis() - verifying < 18000) {
        char observed[21]{};
        if (parse_feeder_fresh(&f, err, sizeof(err), ip, port, ch, observed) &&
            f.ch[ch].filament_detected && f.ch[ch].spool_id == id && !strcmp(uid, observed)) {
            if (++confirmed == 2) {
                char message[112];
                snprintf(message, sizeof(message), "Rolo #%d confirmado no alimentador %d.", id, ch + 1);
                history_add(message);
                act_set(AR_OK, message, id);
                return;
            }
        } else confirmed = 0;
        vTaskDelay(pdMS_TO_TICKS(750));
    }
    act_set(AR_ERR, sent ? "U1 nao confirmou o rolo e o UID no slot." : "Envio sem confirmacao. Confira o slot na U1.");
}

static void do_feed(int ch, bool load) {
    char ip[16] = {0};
    uint16_t port = 7125;
    char err[200] = {0};
    if (ch < 0 || ch > 3) { act_set(AR_ERR, "Feeder invalido."); return; }
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 nao encontrada"); return; }
    if (!require_external(ip, port, err, sizeof(err))) { act_set(AR_ERR, err); return; }
    FeederInfo f{};
    if (!parse_feeder_fresh(&f, err, sizeof(err), ip, port)) { act_set(AR_ERR, err); return; }
    if (!idle_for_manual(&f, err, sizeof(err), false)) { act_set(AR_ERR, err); return; }
    if (!f.ch[ch].filament_detected) {
        snprintf(err, sizeof(err), "Insira o filamento no feeder %d antes de carregar ou retirar.", ch + 1);
        act_set(AR_ERR, err); return;
    }
    if (load) {
        const char *mat = f.ch[ch].filament_type;
        if (!mat[0] || strcasecmp(mat, "NONE") == 0) {
            act_set(AR_ERR, "Identifique o material antes de carregar.");
            return;
        }
        if (f.ch[ch].disable_auto) {
            act_set(AR_ERR, "Alimentacao automatica desativada neste feeder.");
            return;
        }
    }
    char script[48];
    snprintf(script, sizeof(script), "AUTO_FEEDING EXTRUDER=%d %s=1", ch, load ? "LOAD" : "UNLOAD");
    JsonDocument body;
    body["script"] = script;
    char bj[80];
    serializeJson(body, bj, sizeof(bj));
    int len = 0;
    if (!u1_post(ip, port, "/printer/gcode/script", bj, 240000, &len, err, sizeof(err))) {
        act_set(AR_ERR, err);
        history_add(err);
        return;
    }
    char h[120];
    snprintf(h, sizeof(h), "%s no feeder %d", load ? "Carregamento" : "Retirada", ch + 1);
    history_add(h);
    act_set(AR_OK, h);
}

/* ---- task e API ---- */
static void run_kind(ActKind k) {
    switch (k) {
    case ACT_REFRESH: do_refresh(); break;
    case ACT_CATALOG: do_catalog(); break;
    case ACT_INSPECT: do_inspect(p_uid); break;
    case ACT_UPDATE:  do_update(p_id, p_location, p_comment, p_set_weight, p_remaining); break;
    case ACT_BIND:    do_bind(p_id, p_uid); break;
    case ACT_CREATE:  do_create(&p_create); break;
    case ACT_DELETE:  do_delete(p_id, p_identity); break;
    case ACT_ASSIGN:  do_assign(p_channel, p_id); break;
    case ACT_NFC_AUTO: do_nfc_auto(nfc_auto_id, nfc_auto_uid); break;
    case ACT_LOAD:    do_feed(p_channel, true); break;
    case ACT_UNLOAD:  do_feed(p_channel, false); break;
    default: act_set(AR_IDLE, ""); break;
    }
}

static void inv_task(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
        ActKind k;
        xSemaphoreTake(mutex, portMAX_DELAY);
        k = pending;
        pending = ACT_NONE;
        if (k != ACT_NONE) {
            g_act.kind = k;
            g_act.state = AR_RUNNING;
            g_act.message[0] = 0;
            act_seq++;
            g_act.seq = act_seq;
        }
        xSemaphoreGive(mutex);
        if (k != ACT_NONE) run_kind(k);
    }
}

static void enqueue(ActKind k) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (g_act.kind == ACT_NFC_AUTO && g_act.state == AR_RUNNING) {
        xSemaphoreGive(mutex);
        return;
    }
    pending = k;
    g_act.kind = k;
    g_act.state = AR_RUNNING;
    g_act.message[0] = 0;
    act_seq++;
    g_act.seq = act_seq;
    xSemaphoreGive(mutex);
    if (task_h) xTaskNotifyGive(task_h);
}

void inventory_init(void) {
    mutex = xSemaphoreCreateMutex();
    g_inv = (InventorySnapshot *)psram_z(sizeof(InventorySnapshot));
    g_cat = (CatalogSnapshot *)psram_z(sizeof(CatalogSnapshot));
    http_buf = (char *)psram_z(http_cap);
    if (!g_inv || !g_cat) Serial.println("[inventory] falha ao alocar snapshots");
    if (!http_buf) Serial.println("[inventory] falha ao alocar buffer HTTP");
    memset(&g_act, 0, sizeof(g_act));
    if (g_inv) g_inv->status = INV_IDLE;
    if (g_cat) g_cat->status = INV_IDLE;
}

void inventory_start_task(void) {
    xTaskCreatePinnedToCore(inv_task, "inventory", 20480, nullptr, 3, &task_h, 0);
}

bool inventory_busy(void) {
    if (!mutex) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool b = (g_act.state == AR_RUNNING) || (pending != ACT_NONE);
    xSemaphoreGive(mutex);
    return b;
}

void inventory_request_refresh(void) { enqueue(ACT_REFRESH); }
void inventory_request_catalog(void) { enqueue(ACT_CATALOG); }

void inventory_request_inspect(const char *uid) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    cpy(p_uid, sizeof(p_uid), uid ? uid : "");
    xSemaphoreGive(mutex);
    enqueue(ACT_INSPECT);
}

void inventory_request_update(int id, const char *location, const char *comment,
                              bool set_weight, float remaining_g) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    p_id = id;
    cpy(p_location, sizeof(p_location), location ? location : "");
    cpy(p_comment, sizeof(p_comment), comment ? comment : "");
    p_set_weight = set_weight;
    p_remaining = remaining_g;
    xSemaphoreGive(mutex);
    enqueue(ACT_UPDATE);
}

void inventory_request_bind(int id, const char *uid) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    p_id = id;
    cpy(p_uid, sizeof(p_uid), uid ? uid : "");
    xSemaphoreGive(mutex);
    enqueue(ACT_BIND);
}

void inventory_request_create(const CreateSpoolReq *req) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (req) p_create = *req;
    else memset(&p_create, 0, sizeof(p_create));
    xSemaphoreGive(mutex);
    enqueue(ACT_CREATE);
}

void inventory_request_delete(int id, const char *identity) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    p_id = id;
    cpy(p_identity, sizeof(p_identity), identity ? identity : "");
    xSemaphoreGive(mutex);
    enqueue(ACT_DELETE);
}

void inventory_request_assign(int channel, int spool_id) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    p_channel = channel;
    p_id = spool_id;
    xSemaphoreGive(mutex);
    enqueue(ACT_ASSIGN);
}

bool inventory_request_nfc_auto(int spool_id, const char *uid) {
    char normalized[24]{};
    if (!mutex || !task_h || spool_id <= 0 ||
        !spool_validate_uid(uid, normalized, sizeof(normalized), nullptr, 0)) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool available = g_act.state != AR_RUNNING && pending == ACT_NONE;
    if (available) {
        nfc_auto_id = spool_id;
        cpy(nfc_auto_uid, sizeof(nfc_auto_uid), normalized);
        nfc_auto_cancelled = false;
        pending = g_act.kind = ACT_NFC_AUTO;
        g_act.state = AR_RUNNING;
        g_act.message[0] = 0;
        g_act.seq = ++act_seq;
    }
    xSemaphoreGive(mutex);
    if (available) xTaskNotifyGive(task_h);
    return available;
}

void inventory_cancel_nfc_auto(void) { nfc_auto_cancelled = true; }

void inventory_request_feed(int channel, bool load) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    p_channel = channel;
    p_load = load;
    xSemaphoreGive(mutex);
    enqueue(load ? ACT_LOAD : ACT_UNLOAD);
}

bool inventory_copy(InventorySnapshot *out, uint32_t *seq) {
    if (!out || !mutex || !g_inv) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool fresh = !seq || *seq != inv_seq;
    memcpy(out, g_inv, sizeof(*out));
    if (seq) *seq = inv_seq;
    xSemaphoreGive(mutex);
    return fresh;
}

bool inventory_copy_catalog(CatalogSnapshot *out, uint32_t *seq) {
    if (!out || !mutex || !g_cat) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool fresh = !seq || *seq != cat_seq;
    memcpy(out, g_cat, sizeof(*out));
    if (seq) *seq = cat_seq;
    xSemaphoreGive(mutex);
    return fresh;
}

bool inventory_copy_action(ActionResult *out, uint32_t *seq) {
    if (!out || !mutex) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool fresh = !seq || *seq != act_seq;
    *out = g_act;
    if (seq) *seq = act_seq;
    xSemaphoreGive(mutex);
    return fresh;
}

const WarehouseSpool *inventory_find(const InventorySnapshot *s, int id) {
    if (!s) return nullptr;
    for (int i = 0; i < s->count; i++)
        if (s->items[i].info.id == id) return &s->items[i];
    return nullptr;
}
