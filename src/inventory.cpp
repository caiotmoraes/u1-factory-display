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
#include "psram_json.h"
#include <esp_heap_caps.h>
#include <atomic>
#include "control.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

static SemaphoreHandle_t mutex;
static TaskHandle_t task_h;
static InventorySnapshot *g_inv;
static CatalogSnapshot *g_cat;
static ActionResult &g_act = *psram_calloc_obj<ActionResult>(); /* PSRAM (~6 KB) */
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
static ControlOp control_op;
static int control_value;
static char control_path[192];
static bool refresh_quiet = false;
static bool refresh_wanted;
static ControlFiles *control_files; /* PSRAM: ~4,6 KB */
static ControlFiles *control_scratch; /* PSRAM: montagem da lista */
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

/* Códigos próprios além dos negativos do HTTPClient. */
#define HTTP_TRUNCATED (-100) /* corpo menor que o Content-Length (rede instável) */
#define HTTP_TOO_BIG   (-101) /* corpo não cabe no buffer PSRAM */

/* Destino de writeToStream(): grava no buffer PSRAM (resposta chunked). */
class BufSink : public Stream {
public:
    BufSink(char *buf, size_t cap) : buf_(buf), cap_(cap) {}
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *data, size_t n) override {
        if (len_ + n >= cap_) { overflow_ = true; n = cap_ - 1 - len_; }
        memcpy(buf_ + len_, data, n);
        len_ += n;
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t len() const { return len_; }
    bool overflow() const { return overflow_; }
private:
    char *buf_;
    size_t cap_, len_ = 0;
    bool overflow_ = false;
};

/* Lê o corpo HTTP para o buffer PSRAM. Retorna o código HTTP, ou < 0 em falha.
 * Conexão: no máximo 3 s (impressora offline não segura a fila por 8 s).
 * O corpo é validado contra o Content-Length; chunked é decodificado. */
static int http_do(const char *url, const char *method, const char *payload,
                   int timeout_ms, int *outlen) {
    if (outlen) *outlen = 0;
    if (http_buf) http_buf[0] = 0;
    if (!WiFi.isConnected()) return HTTPC_ERROR_CONNECTION_REFUSED;
    HTTPClient http;
    int t = timeout_ms > 0 ? timeout_ms : 8000;
    if (t > 60000) t = 60000;
    http.setConnectTimeout(t > 3000 ? 3000 : t);
    http.setTimeout(t);
    http.setReuse(false);
    static const char *keys[] = {"Transfer-Encoding"};
    http.collectHeaders(keys, 1);
    if (!http.begin(url)) return -1;
    if (payload) http.addHeader("Content-Type", "application/json");
    int code;
    if (!method || strcmp(method, "GET") == 0) code = http.GET();
    else if (strcmp(method, "POST") == 0) code = http.POST(payload ? payload : "");
    else if (strcmp(method, "PATCH") == 0) code = http.sendRequest("PATCH", (uint8_t *)(payload ? payload : ""), payload ? strlen(payload) : 0);
    else if (strcmp(method, "DELETE") == 0) code = http.sendRequest("DELETE");
    else code = http.POST(payload ? payload : "");
    if (code <= 0) { http.end(); return code < 0 ? code : -1; }

    int len = 0;
    if (http_buf && http_cap) {
        const int size = http.getSize(); /* -1: desconhecido/chunked */
        const bool chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
        if (size >= (int)http_cap) {
            http.end();
            return HTTP_TOO_BIG;
        }
        if (chunked) {
            BufSink sink(http_buf, http_cap);
            int r = http.writeToStream(&sink);
            len = (int)sink.len();
            http_buf[len] = 0;
            if (sink.overflow()) { http.end(); return HTTP_TOO_BIG; }
            if (r < 0) { http.end(); if (outlen) *outlen = len; return HTTP_TRUNCATED; }
        } else {
            WiFiClient *s = http.getStreamPtr();
            unsigned long last = millis();
            const int want = size >= 0 ? size : (int)http_cap - 1;
            while (len < want) {
                int avail = s->available();
                if (avail <= 0) {
                    if (!s->connected()) break;
                    if (millis() - last > (unsigned long)t) break; /* sem bytes novos */
                    delay(4);
                    continue;
                }
                int n = s->read((uint8_t *)http_buf + len, want - len);
                if (n < 0) break;          /* falha do socket (ex.: sem RAM p/ buffer) */
                if (n == 0) { /* antes: parava e truncava o corpo */
                    if (millis() - last > (unsigned long)t) break;
                    delay(4);
                    continue;
                }
                len += n;
                last = millis();
            }
            http_buf[len] = 0;
            if (size >= 0 && len != size) { http.end(); if (outlen) *outlen = len; return HTTP_TRUNCATED; }
            if (size < 0 && len >= (int)http_cap - 1) { http.end(); return HTTP_TOO_BIG; }
        }
    }
    http.end();
    if (outlen) *outlen = len;
    return code;
}

/* Mensagem curta para falhas de transporte. */
static void http_err_msg(int code, char *err, size_t errn) {
    if (!err) return;
    if (code == HTTP_TRUNCATED) cpy(err, errn, "Resposta incompleta da U1 (rede instável). Toque Atualizar.");
    else if (code == HTTP_TOO_BIG) cpy(err, errn, "Resposta grande demais para a tela.");
    else if (code == HTTPC_ERROR_READ_TIMEOUT) cpy(err, errn, "A U1 demorou a responder. Tente de novo.");
    else cpy(err, errn, "Impressora offline / sem conexão");
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
    JsonDocument doc(psram_json());
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
        http_err_msg(code, err, errn);
        return false;
    }
    if (len <= 0 && code >= 200 && code < 300 && strcmp(method, "DELETE") != 0) {
        if (err) cpy(err, errn, "Resposta vazia da U1. Tente de novo.");
        return false;
    }
    /* Moonraker / v2 pode responder 200 com error no envelope. */
    if (http_buf && len > 0) {
        char emsg[160] = {0};
        /* tenta detectar error v2 sem parse completo do array */
        /* Só os campos de erro: a lista de rolos inteira não é materializada aqui. */
        JsonDocument ef(psram_json());
        ef["error"] = true;
        ef["result"]["error"] = true;
        JsonDocument ed(psram_json());
        if (!deserializeJson(ed, http_buf, len, DeserializationOption::Filter(ef))) {
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
    if (code < 0) { http_err_msg(code, err, errn); return false; }
    if (code < 200 || code >= 300) { if (err) snprintf(err, errn, "HTTP %d em %s", code, path); return false; }
    return true;
}

static bool u1_post(const char *ip, uint16_t port, const char *path, const char *payload, int timeout_ms, int *outlen, char *err, size_t errn) {
    char url[320];
    snprintf(url, sizeof(url), "http://%s:%u%s", ip, port, path);
    int code = http_post_url(url, payload, timeout_ms, outlen);
    if (code < 0) { http_err_msg(code, err, errn); return false; }
    if (code < 200 || code >= 300) {
        /* tenta mensagem de erro do Moonraker */
        if (http_buf && *outlen && err) {
            JsonDocument d(psram_json());
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
                "/printer/objects/query?filament_feed%20left&filament_feed%20right&filament_detect&print_task_config&print_stats&webhooks&idle_timeout",
                4000, &len, err, errn))
        return false;
    JsonDocument doc(psram_json());
    if (deserializeJson(doc, http_buf, len)) {
        cpy(err, errn, "Resposta inválida dos feeders");
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
    const char *idle_state = st["idle_timeout"]["state"] | "";
    f->busy_motion = !strcmp(idle_state, "Printing");
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
        cpy(err, errn, "A impressora não está pronta");
        return false;
    }
    return true;
}

static const char *ok_phases[] = {
    "wait_insert", "preload_finish", "load_finish", "unload_finish", "none", "inited", nullptr
};

static bool idle_for_manual(const FeederInfo *f, char *err, size_t errn, bool allow_print) {
    if (!f || !f->valid) { cpy(err, errn, "Status dos feeders indisponível"); return false; }
    /* allow_print: associar rolo é só metadado (spoollink), pode ocorrer durante a impressão.
     * Carga/descarga física continua bloqueada. */
    if (f->busy_print && !allow_print) { cpy(err, errn, "Troca manual indisponível durante impressão ou pausa."); return false; }
    if (f->busy_motion && !allow_print) { cpy(err, errn, "Aguarde o movimento atual da U1 terminar."); return false; }
    for (int i = 0; i < 4; i++) {
        if (!f->ch[i].module_exist) {
            snprintf(err, errn, "Feeder %d indisponível", i + 1);
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
            snprintf(err, errn, "Aguarde a operação atual do feeder %d terminar.", i + 1);
            return false;
        }
    }
    return true;
}

static bool require_external(const char *ip, uint16_t port, char *err, size_t errn) {
    int m = read_external_mode(ip, port);
    if (m == 0) {
        cpy(err, errn, "Não foi possível verificar Filament Detection. Confirme o modo External no Firmware Config.");
        return false;
    }
    if (m != 1) {
        cpy(err, errn, "Selecione Filament Detection = External no Firmware Config antes de associar ou carregar rolos. Leitores internos podem apagar os dados.");
        return false;
    }
    return true;
}

/* ---- REFRESH ---- */
/* quiet = carregamento automático do Início: não mexe na ação/toast. */
static void do_refresh(bool quiet = false) {
    char ip[16] = {0};
    uint16_t port = 7125;
    char err[160] = {0};
    if (!WiFi.isConnected()) {
        inv_publish(INV_OFFLINE, "Sem Wi-Fi");
        if (!quiet) act_set(AR_ERR, "Sem Wi-Fi");
        return;
    }
    if (!endpoint(ip, &port)) {
        inv_publish(INV_OFFLINE, "U1 não encontrada na rede");
        if (!quiet) act_set(AR_ERR, "U1 não encontrada na rede");
        return;
    }
    inv_publish(INV_LOADING, nullptr);
    int len = 0;
    if (!proxy(ip, port, "GET", "/v1/spool?allow_archived=true", nullptr, 12000, &len, err, sizeof(err))) {
        const bool offline = !strncmp(err, "Impressora offline", 18);
        inv_publish(offline ? INV_OFFLINE : INV_ERROR, err[0] ? err : "Falha ao ler o estoque");
        if (!quiet) act_set(AR_ERR, err[0] ? err : "Falha ao ler o estoque");
        return;
    }
    WarehouseSpool *tmp = (WarehouseSpool *)psram_z(sizeof(WarehouseSpool) * WAREHOUSE_MAX);
    if (!tmp) {
        inv_publish(INV_ERROR, "Sem memória para o estoque");
        if (!quiet) act_set(AR_ERR, "Sem memória");
        return;
    }
    int n = spool_parse_list(http_buf, (size_t)len, tmp, WAREHOUSE_MAX, err, sizeof(err));
    if (n < 0) {
        Serial.printf("[inventory] JSON do estoque inválido (%d bytes): %.80s\n", len, http_buf ? http_buf : "");
        free(tmp);
        {
            char base[80];
            cpy(base, sizeof(base), err[0] ? err : "JSON do estoque inválido");
            snprintf(err, sizeof(err), "%s (%d bytes recebidos). Toque Atualizar.", base, len);
        }
        inv_publish(INV_ERROR, err);
        if (!quiet) act_set(AR_ERR, err[0] ? err : "JSON inválido");
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
    if (!quiet) act_set(AR_OK, msg);
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
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 não encontrada na rede"); return; }
    int len = 0;
    if (!proxy(ip, port, "GET", "/v1/vendor", nullptr, 8000, &len, err, sizeof(err))) {
        act_set(AR_ERR, err); return;
    }
    JsonDocument vdoc(psram_json());
    if (deserializeJson(vdoc, http_buf, len)) { act_set(AR_ERR, "Catálogo (vendor) inválido"); return; }
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
    JsonDocument fdoc(psram_json());
    if (deserializeJson(fdoc, http_buf, len)) { act_set(AR_ERR, "Catálogo (filament) inválido"); return; }
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
    act_set(AR_OK, "Catálogo atualizado");
}

/* ---- INSPECT ---- */
static void do_inspect(const char *uid) {
    char norm[24] = {0}, err[80] = {0};
    if (!spool_validate_uid(uid, norm, sizeof(norm), err, sizeof(err))) {
        act_set(AR_ERR, err[0] ? err : "UID NFC inválido");
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
        snprintf(m, sizeof(m), "Tag não cadastrada no Spoolman: %s. Use Novo rolo no Armazém.", norm);
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
        snprintf(m, sizeof(m), "Tag já cadastrada: %s. Este rolo está arquivado.", idn);
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

/* UID normalizado presente numa lista "A,B" (já normalizada pelo parser). */
static bool uid_listed(const char *list, const char *norm) {
    const char *p = list;
    while (p && *p) {
        char tok[24] = {0};
        int k = 0;
        while (*p && *p != ',' && k < 23) tok[k++] = *p++;
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
        char tn[24] = {0};
        spool_normalize_uid(tok, tn, sizeof(tn));
        if (tn[0] && !strcmp(tn, norm)) return true;
    }
    return false;
}

static bool unique_uid(const char *norm, int self, char *err, size_t errn) {
    if (g_inv->status != INV_OK) {
        /* tenta refresh curto: usa o que houver */
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    int n = g_inv->count;
    /* copy ids+uids out to not hold mutex long — small */
    struct { int id; char uids[SPOOL_UIDS_LEN]; bool archived; } *rows =
        (decltype(rows))psram_z(sizeof(*rows) * (size_t)(n > 0 ? n : 1));
    if (!rows) { xSemaphoreGive(mutex); cpy(err, errn, "Sem memória"); return false; }
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
                snprintf(err, errn, "Tag já vinculada ao rolo #%d%s. Nenhuma tag foi transferida.",
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
    JsonDocument doc(psram_json());
    if (deserializeJson(doc, http_buf, len)) { cpy(err, errn, "Campos do Spoolman inválidos"); return false; }
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
    JsonDocument body(psram_json());
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
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 não encontrada"); return; }
    JsonDocument body(psram_json());
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
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 não encontrada"); return; }
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
        act_set(AR_ERR, err[0] ? err : "Rolo inválido"); return;
    }
    if (cur.archived) { act_set(AR_ERR, "Desarquive o rolo antes de vincular NFC."); return; }

    /* preserva extra e une UIDs */
    JsonDocument src(psram_json());
    if (deserializeJson(src, http_buf, len)) { act_set(AR_ERR, "JSON do rolo inválido"); return; }
    JsonObject obj = src.as<JsonObject>();
    if (src["result"].is<JsonObject>()) {
        JsonObject r = src["result"].as<JsonObject>();
        if (r["response"].is<JsonObject>()) obj = r["response"].as<JsonObject>();
    } else if (src["response"].is<JsonObject>()) obj = src["response"].as<JsonObject>();

    if (uid_listed(cur.card_uids, norm)) {
        char h[120];
        snprintf(h, sizeof(h), "Tag %s já estava associada ao rolo #%d.", norm, id);
        act_set(AR_OK, h, id);
        return;
    }
    /* Mantém o formato que o rolo já usa em extra.card_uids:
     *  - lista JSON  -> "[\"A\",\"B\"]" (acrescenta o novo item)
     *  - texto       -> "\"A,B\""        (formato do Spool Factory/SpoolLink)
     * O valor no Spoolman é sempre texto JSON. */
    const char *raw = obj["extra"]["card_uids"] | "";
    char quoted[SPOOL_UIDS_LEN + 64] = {0};
    {
        JsonDocument old(psram_json());
        const bool parsed = raw[0] && !deserializeJson(old, raw);
        if (parsed && old.is<JsonArray>()) {
            old.as<JsonArray>().add(norm);
            if (measureJson(old) >= sizeof(quoted)) { act_set(AR_ERR, "Muitas tags neste rolo."); return; }
            serializeJson(old, quoted, sizeof(quoted));
        } else if (parsed && old.is<const char *>() && old.as<const char *>()[0] == '[') {
            /* texto JSON contendo uma lista: "\"[\\\"A\\\"]\"" */
            JsonDocument inner(psram_json());
            if (deserializeJson(inner, old.as<const char *>()) || !inner.is<JsonArray>()) {
                act_set(AR_ERR, "card_uids do rolo em formato desconhecido."); return;
            }
            inner.as<JsonArray>().add(norm);
            char list[SPOOL_UIDS_LEN + 48] = {0};
            if (measureJson(inner) >= sizeof(list)) { act_set(AR_ERR, "Muitas tags neste rolo."); return; }
            serializeJson(inner, list, sizeof(list));
            JsonDocument q(psram_json());
            q.set(list);
            if (measureJson(q) >= sizeof(quoted)) { act_set(AR_ERR, "Muitas tags neste rolo."); return; }
            serializeJson(q, quoted, sizeof(quoted));
        } else {
            char joined[SPOOL_UIDS_LEN + 24] = {0};
            const char *prev = parsed && old.is<const char *>() ? old.as<const char *>() :
                               parsed ? "" : raw; /* texto cru legado */
            if (prev[0]) snprintf(joined, sizeof(joined), "%s,%s", prev, norm);
            else cpy(joined, sizeof(joined), norm);
            JsonDocument q(psram_json());
            q.set(joined);
            if (measureJson(q) >= sizeof(quoted)) { act_set(AR_ERR, "Muitas tags neste rolo."); return; }
            serializeJson(q, quoted, sizeof(quoted));
        }
    }

    JsonDocument patch(psram_json());
    if (obj["extra"].is<JsonObject>()) {
        for (JsonPair kv : obj["extra"].as<JsonObject>()) {
            patch["extra"][kv.key()] = kv.value();
        }
    }
    patch["extra"]["card_uids"] = quoted;
    char bj[768];
    if (measureJson(patch) >= sizeof(bj)) { act_set(AR_ERR, "Campos extras grandes demais."); return; }
    serializeJson(patch, bj, sizeof(bj));
    len = 0;
    if (!proxy(ip, port, "PATCH", path, bj, 8000, &len, err, sizeof(err))) {
        act_set(AR_ERR, err);
        history_add(err);
        return;
    }
    WarehouseSpool saved{};
    spool_parse_one(http_buf, (size_t)len, &saved, id, err, sizeof(err));
    if (!uid_listed(saved.card_uids, norm)) {
        act_set(AR_ERR, "Vínculo recebido sem NFC confirmado. Atualize o estoque.");
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
    JsonDocument doc(psram_json());
    if (deserializeJson(doc, http_buf, len)) { cpy(err, errn, "Lista de marcas inválida"); return 0; }
    JsonArray arr;
    unwrap_array(doc, arr);
    for (JsonObject v : arr) {
        const char *n = v["name"] | "";
        if (n[0] && strcasecmp(n, name) == 0) return v["id"] | 0;
    }
    JsonDocument body(psram_json());
    body["name"] = name;
    char bj[128];
    serializeJson(body, bj, sizeof(bj));
    len = 0;
    if (!proxy(ip, port, "POST", "/v1/vendor", bj, 8000, &len, err, errn)) return 0;
    JsonDocument rd(psram_json());
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
    JsonDocument doc(psram_json());
    if (deserializeJson(doc, http_buf, len)) { cpy(err, errn, "Lista de filamentos inválida"); return 0; }
    JsonArray arr;
    unwrap_array(doc, arr);
    for (JsonObject f : arr) {
        int vid = 0;
        if (f["vendor"].is<JsonObject>()) vid = f["vendor"]["id"] | 0;
        else vid = f["vendor_id"] | 0;
        if (vid == vendor_id && same_filament(f, r, color0, multi, nc > 1 ? dir : ""))
            return f["id"] | 0;
    }
    JsonDocument body(psram_json());
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
    JsonDocument rd(psram_json());
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
    /* UID opcional: a tela associa a tag depois de criar o rolo. */
    const bool with_uid = r->uid[0] != 0;
    if (with_uid && !spool_validate_uid(r->uid, norm, sizeof(norm), err, sizeof(err))) {
        act_set(AR_ERR, err); return;
    }
    if (r->remaining_g > r->initial_g + 0.01f) {
        act_set(AR_ERR, "Peso restante não pode ser maior que o inicial."); return;
    }
    if (r->initial_g < 0.01f) { act_set(AR_ERR, "Peso inicial inválido."); return; }
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 não encontrada"); return; }
    if (with_uid) {
        if (g_inv->status != INV_OK) do_refresh();
        if (!unique_uid(norm, 0, err, sizeof(err))) { act_set(AR_ERR, err); return; }
        if (!tag_field(ip, port, err, sizeof(err))) { act_set(AR_ERR, err); return; }
    }

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
        if (ncolors > 5) { act_set(AR_ERR, "Use até cinco cores."); return; }
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
            act_set(AR_ERR, "Direção das cores inválida."); return;
        }
        int vid = find_or_create_vendor(ip, port, r->vendor, err, sizeof(err));
        if (vid <= 0) { act_set(AR_ERR, err[0] ? err : "Falha ao criar marca"); return; }
        fid = find_or_create_filament(ip, port, r, vid, colors, dir, err, sizeof(err));
        if (fid <= 0) { act_set(AR_ERR, err[0] ? err : "Falha ao criar filamento"); return; }
    }

    if (with_uid && !unique_uid(norm, 0, err, sizeof(err))) { act_set(AR_ERR, err); return; }

    char quoted[32];
    snprintf(quoted, sizeof(quoted), "\"%s\"", norm);
    JsonDocument body(psram_json());
    body["filament_id"] = fid;
    body["initial_weight"] = r->initial_g;
    body["used_weight"] = r->initial_g - r->remaining_g;
    body["location"] = r->location;
    body["comment"] = r->comment;
    if (with_uid) body["extra"]["card_uids"] = quoted;
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
        act_set(AR_ERR, err[0] ? err : "Cadastro sem resposta válida");
        return;
    }
    if (with_uid && !strstr(saved.card_uids, norm)) {
        act_set(AR_ERR, "Cadastro recebido sem NFC confirmado. Atualize o estoque antes de repetir.");
        return;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_act.saved = saved;
    g_act.spool_id = saved.info.id;
    xSemaphoreGive(mutex);
    char h[140];
    if (with_uid) snprintf(h, sizeof(h), "Rolo #%d cadastrado (NFC %s)", saved.info.id, norm);
    else snprintf(h, sizeof(h), "Rolo #%d cadastrado. Associe a tag NFC.", saved.info.id);
    history_add(h);
    act_set(AR_OK, h, saved.info.id);
    do_refresh();
}

static void do_delete(int id, const char *identity) {
    char ip[16] = {0};
    uint16_t port = 7125;
    char err[160] = {0};
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 não encontrada"); return; }
    /* não apagar se instalado num feeder */
    FeederInfo f{};
    char fe[120] = {0};
    if (parse_feeder_fresh(&f, fe, sizeof(fe), ip, port) && f.valid) {
        for (int i = 0; i < 4; i++) {
            if (f.ch[i].spool_id == id) {
                snprintf(err, sizeof(err), "Rolo #%d está no feeder %d. Remova antes de excluir.", id, i + 1);
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
            act_set(AR_ERR, "Identidade não confere. Atualize o estoque e tente de novo.");
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
    snprintf(h, sizeof(h), "Rolo #%d excluído do Spoolman", id);
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
    if (ch < 0 || ch > 3) { act_set(AR_ERR, "Feeder inválido."); return; }
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 não encontrada"); return; }
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
            snprintf(err, sizeof(err), "Este rolo já está associado ao feeder %d.", i + 1);
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
            act_set(AR_ERR, err[0] ? err : "Rolo não encontrado");
            return;
        }
    }
    char info[480], ie[80];
    if (!spool_build_link_info(&s, info, sizeof(info), ie, sizeof(ie))) {
        act_set(AR_ERR, ie[0] ? ie : "Rolo sem material ou cor.");
        return;
    }
    JsonDocument body(psram_json());
    body["channel"] = ch;
    body["info"] = serialized(info);
    body["message"] = "U1 Factory Display: associação manual";
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
    JsonDocument filter(psram_json()), doc(psram_json());
    JsonDocument fields(psram_json());
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
    cpy(err, errn, "UID sem vínculo único com este rolo ativo no Spoolman.");
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
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 não encontrada."); return; }
    if (!require_external(ip, port, err, sizeof(err)) ||
        !nfc_spool_matches(ip, port, uid, id, err, sizeof(err)) ||
        !parse_feeder_fresh(&f, err, sizeof(err), ip, port) ||
        !idle_for_manual(&f, err, sizeof(err), false)) { act_set(AR_ERR, err); return; }
    const unsigned baseline = feeder_presence(f);
    for (int i = 0; i < 4; ++i) {
        if (f.ch[i].filament_detected && f.ch[i].spool_id == id) {
            act_set(AR_ERR, "Este rolo já está em um alimentador."); return;
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
                act_set(AR_ERR, "Um alimentador está indisponível ou com erro."); return;
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
        act_set(AR_ERR, nfc_auto_cancelled ? "Associação cancelada." : "Prazo de inserção esgotado."); return;
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
            act_set(AR_ERR, "Este rolo já está em outro alimentador."); return;
        }
    }
    JsonDocument command(psram_json());
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
    act_set(AR_ERR, sent ? "U1 não confirmou o rolo e o UID no slot." : "Envio sem confirmação. Confira o slot na U1.");
}

static void do_feed(int ch, bool load) {
    char ip[16] = {0};
    uint16_t port = 7125;
    char err[200] = {0};
    if (ch < 0 || ch > 3) { act_set(AR_ERR, "Feeder inválido."); return; }
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 não encontrada"); return; }
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
            act_set(AR_ERR, "Alimentação automática desativada neste feeder.");
            return;
        }
    }
    char script[48];
    snprintf(script, sizeof(script), "AUTO_FEEDING EXTRUDER=%d %s=1", ch, load ? "LOAD" : "UNLOAD");
    JsonDocument body(psram_json());
    body["script"] = script;
    char bj[80];
    serializeJson(body, bj, sizeof(bj));
    int len = 0;
    const char *expected = load ? "load_finish" : "unload_finish";
    if (!strcmp(f.ch[ch].channel_state, expected)) {
        act_set(AR_OK, load ? "A U1 já informa carga concluída." : "A U1 já informa retirada concluída.");
        return;
    }
    u1_post(ip, port, "/printer/gcode/script", bj, 60000, &len, err, sizeof(err));
    const uint32_t started = millis();
    while (millis() - started < 90000) {
        if (parse_feeder_fresh(&f, err, sizeof(err), ip, port)) {
            const FeederChannel &channel = f.ch[ch];
            if (!strcmp(channel.channel_state, expected) && !strcmp(channel.channel_error, "ok")) {
                char message[112];
                snprintf(message, sizeof(message), "%s confirmada no alimentador %d.",
                         load ? "Carga" : "Retirada", ch + 1);
                history_add(message);
                act_set(AR_OK, message);
                return;
            }
            if (strstr(channel.channel_state, "_fail") || strcmp(channel.channel_error, "ok")) {
                act_set(AR_ERR, "A U1 reportou uma falha no alimentador."); return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    act_set(AR_ERR, "Alimentação sem confirmação. Confira o alimentador na U1.");
}

static bool control_reply(int len) {
    JsonDocument reply(psram_json());
    return len > 0 && !deserializeJson(reply, http_buf, len) &&
           !reply["result"].isNull() && reply["error"].isNull();
}

static void control_list(const char *ip, uint16_t port) {
    char err[128]{};
    int len = 0;
    if (!control_files || !control_scratch) { act_set(AR_ERR, "Sem memória para a lista."); return; }
    ControlFiles &files = *control_scratch;
    memset(&files, 0, sizeof(files));
    /* Só o caminho: a resposta completa (size/modified/permissions) de centenas de
     * arquivos esgotava a RAM interna. */
    JsonDocument filter(psram_json());
    filter["result"][0]["path"] = true;
    JsonDocument doc(psram_json());
    if (!u1_get(ip, port, "/server/files/list?root=gcodes", 6000, &len, err, sizeof(err)) ||
        deserializeJson(doc, http_buf, len, DeserializationOption::Filter(filter)) ||
        !doc["result"].is<JsonArray>()) {
        act_set(AR_ERR, err[0] ? err : "Lista de arquivos indisponível."); return;
    }
    for (JsonObject entry : doc["result"].as<JsonArray>()) {
        const char *path = entry["path"] | "";
        const size_t size = strlen(path);
        if (size < 7 || strcasecmp(path + size - 6, ".gcode")) continue;
        if (size >= sizeof(files.paths[0]) || files.count == ControlFiles::limit) {
            files.more = true;
            continue;
        }
        cpy(files.paths[files.count++], sizeof(files.paths[0]), path);
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    files.version = control_files->version + 1;
    *control_files = files;
    xSemaphoreGive(mutex);
    act_set(AR_OK, files.count ? "Arquivos atualizados." : "Nenhum G-code na U1.");
}

static void do_control() {
    char ip[16]{}, err[160]{};
    uint16_t port = 7125;
    if (!endpoint(ip, &port)) { act_set(AR_ERR, "U1 não encontrada."); return; }
    const ControlOp op = control_op;
    if (op == ControlOp::Files) { control_list(ip, port); return; }
    int len = 0;
    JsonDocument status(psram_json());
    if (!u1_get(ip, port,
                "/printer/objects/query?print_stats=state,filename&idle_timeout=state&webhooks=state&toolhead=homed_axes,position,axis_minimum,axis_maximum",
                4000, &len, err, sizeof(err)) || deserializeJson(status, http_buf, len)) {
        act_set(AR_ERR, err[0] ? err : "Estado atual da U1 indisponível."); return;
    }
    JsonObject st = status["result"]["status"].as<JsonObject>();
    const char *job = st["print_stats"]["state"] | "";
    const char *idle = st["idle_timeout"]["state"] | "";
    const char *ready = st["webhooks"]["state"] | "";
    const char *homed = st["toolhead"]["homed_axes"] | "";
    const bool printing = !strcmp(job, "printing");
    const bool paused = !strcmp(job, "paused");
    const bool idle_job = !strcmp(job, "standby") || !strcmp(job, "complete") ||
                          !strcmp(job, "cancelled");
    if (strcmp(ready, "ready") || !*job) { act_set(AR_ERR, "U1 indisponível."); return; }

    const char *route = "/printer/gcode/script";
    JsonDocument body(psram_json());
    char script[220]{}, payload[300]{};
    int wait_ms = 12000;
    if (op == ControlOp::Pause || op == ControlOp::Resume || op == ControlOp::Cancel) {
        const char *current_file = st["print_stats"]["filename"] | "";
        if (!control_path[0] || strcmp(current_file, control_path) ||
            (op == ControlOp::Pause && !printing) ||
            (op == ControlOp::Resume && !paused) ||
            (op == ControlOp::Cancel && !printing && !paused)) {
            act_set(AR_ERR, "A impressão exibida mudou. Confira o estado da U1."); return;
        }
        route = op == ControlOp::Pause ? "/printer/print/pause" :
                op == ControlOp::Resume ? "/printer/print/resume" : "/printer/print/cancel";
    } else if (op == ControlOp::FanSet) {
        /* Ventiladores podem mudar durante a impressão. */
        static const char *const fans[] = {"", "exhaust_fan", "circulation_fan", "cavity_fan"};
        const int fan = control_value / 1000, pct = control_value % 1000;
        if (fan < 0 || fan > 3 || pct < 0 || pct > 100) { act_set(AR_ERR, "Ventilador inválido."); return; }
        if (fan == 0) snprintf(script, sizeof(script), "M106 S%d", (pct * 255 + 50) / 100);
        else snprintf(script, sizeof(script), "SET_FAN_SPEED FAN=%s SPEED=%.2f", fans[fan], pct / 100.0);
    } else {
        if (!idle_job || (strcmp(idle, "Idle") && strcmp(idle, "Ready"))) {
            act_set(AR_ERR, "Aguarde a impressora ficar ociosa."); return;
        }
        switch (op) {
        case ControlOp::Start:
            if (!control_path[0]) { act_set(AR_ERR, "Selecione um arquivo."); return; }
            route = "/printer/print/start";
            body["filename"] = control_path;
            break;
        case ControlOp::Home: strcpy(script, "G28\nM400"); wait_ms = 60000; break;
        case ControlOp::Cool: strcpy(script, "TURN_OFF_HEATERS"); break;
        case ControlOp::FanOff: strcpy(script, "M107"); break;
        case ControlOp::FanHalf: strcpy(script, "M106 S128"); break;
        case ControlOp::FanFull: strcpy(script, "M106 S255"); break;
        case ControlOp::MotorsOff: strcpy(script, "M84"); break;
        case ControlOp::SelectTool:
            if (control_value < 0 || control_value > 3 || strcmp(homed, "xyz")) {
                act_set(AR_ERR, "Referencie XYZ antes de trocar de cabeçote."); return;
            }
            snprintf(script, sizeof(script), "T%d\nM400", control_value);
            wait_ms = 60000;
            break;
        case ControlOp::Mesh: strcpy(script, "G28\nBED_MESH_CALIBRATE\nM400"); wait_ms = 60000; break;
        case ControlOp::Shaper: strcpy(script, "G28\nSHAPER_CALIBRATE\nM400"); wait_ms = 60000; break;
        case ControlOp::Offsets:
            if (strcmp(homed, "xyz")) { act_set(AR_ERR, "Referencie XYZ antes de calibrar offsets."); return; }
            strcpy(script, "XYZ_OFFSET_CALIBRATE_ALL\nM400"); wait_ms = 60000; break;
        case ControlOp::Bed55: case ControlOp::Bed75:
            snprintf(script, sizeof(script), "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=%d",
                     op == ControlOp::Bed55 ? 55 : 75);
            break;
        case ControlOp::Nozzle200: case ControlOp::Nozzle235: {
            if (control_value < 0 || control_value > 3) { act_set(AR_ERR, "Cabeçote inválido."); return; }
            const char *heaters[] = {"extruder", "extruder1", "extruder2", "extruder3"};
            snprintf(script, sizeof(script), "SET_HEATER_TEMPERATURE HEATER=%s TARGET=%d",
                     heaters[control_value], op == ControlOp::Nozzle200 ? 200 : 235);
            break;
        }
        default: {
            const int step = static_cast<int>(op) - static_cast<int>(ControlOp::XMinus);
            if (step < 0 || step > 5 || strcmp(homed, "xyz")) {
                act_set(AR_ERR, "Referencie XYZ antes de mover."); return;
            }
            const int axis = step / 2;
            const float increment = (step % 2 ? 1 : -1) * (axis == 2 ? 1.0f : 5.0f);
            JsonObject tool = st["toolhead"].as<JsonObject>();
            const float position = tool["position"][axis] | NAN;
            const float lower = tool["axis_minimum"][axis] | NAN;
            const float upper = tool["axis_maximum"][axis] | NAN;
            if (!isfinite(position) || !isfinite(lower) || !isfinite(upper) ||
                position + increment < lower || position + increment > upper) {
                act_set(AR_ERR, "Movimento excede o limite reportado pela U1."); return;
            }
            snprintf(script, sizeof(script),
                     "SAVE_GCODE_STATE NAME=ws_jog\nG91\nG1 %c%.0f F%d\nM400\nRESTORE_GCODE_STATE NAME=ws_jog MOVE=0",
                     "XYZ"[axis], static_cast<double>(increment), axis == 2 ? 240 : 1500);
            break;
        }
        }
    }
    if (script[0]) body["script"] = script;
    serializeJson(body, payload, sizeof(payload));
    if (!u1_post(ip, port, route, payload, wait_ms, &len, err, sizeof(err)) ||
        !control_reply(len)) {
        act_set(AR_ERR, "Sem confirmação da U1. Verifique o estado antes de repetir.");
        return;
    }
    const char *message = script[0] && wait_ms == 60000
        ? "A U1 respondeu ao fim da rotina." : "Comando aceito pela U1.";
    history_add(message);
    act_set(AR_OK, message);
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
    case ACT_CONTROL: do_control(); break;
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
        bool quiet = false;
        if (k == ACT_NONE && refresh_wanted) { k = ACT_REFRESH; quiet = refresh_quiet; }
        if (k == ACT_REFRESH) { refresh_wanted = false; refresh_quiet = false; }
        if (k != ACT_NONE && !quiet) {
            g_act.kind = k;
            g_act.state = AR_RUNNING;
            g_act.message[0] = 0;
            act_seq++;
            g_act.seq = act_seq;
        }
        xSemaphoreGive(mutex);
        if (k == ACT_REFRESH && quiet) do_refresh(true);
        else if (k != ACT_NONE) run_kind(k);
    }
}

static void enqueue(ActKind k) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    /* Atualizar o estoque nunca se perde nem atropela outra ação: fica marcado
     * e roda quando a fila esvaziar (antes, abrir o Armazém durante outra ação
     * deixava a lista vazia com "Nenhum rolo com este filtro"). */
    if (k == ACT_REFRESH && (pending != ACT_NONE || g_act.state == AR_RUNNING)) {
        refresh_wanted = true;
        xSemaphoreGive(mutex);
        if (task_h) xTaskNotifyGive(task_h);
        return;
    }
    if ((g_act.kind == ACT_NFC_AUTO || g_act.kind == ACT_CONTROL) && g_act.state == AR_RUNNING) {
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
    control_files = (ControlFiles *)psram_z(sizeof(ControlFiles));
    control_scratch = (ControlFiles *)psram_z(sizeof(ControlFiles));
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

void inventory_request_refresh(void) {
    if (mutex) { xSemaphoreTake(mutex, portMAX_DELAY); refresh_quiet = false; xSemaphoreGive(mutex); }
    enqueue(ACT_REFRESH);
}

/* Carregamento em segundo plano (Início): sem toast, sem ocupar a ação. */
void inventory_request_refresh_quiet(void) {
    if (!mutex) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!refresh_wanted && pending != ACT_REFRESH) { refresh_wanted = true; refresh_quiet = true; }
    xSemaphoreGive(mutex);
    if (task_h) xTaskNotifyGive(task_h);
}
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

bool control_submit(ControlOp op, int value, const char *path) {
    if (!mutex || !task_h || (path && strlen(path) >= sizeof(control_path))) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool available = g_act.state != AR_RUNNING && pending == ACT_NONE;
    if (available) {
        control_op = op;
        control_value = value;
        cpy(control_path, sizeof(control_path), path);
        pending = g_act.kind = ACT_CONTROL;
        g_act.state = AR_RUNNING;
        g_act.message[0] = 0;
        g_act.seq = ++act_seq;
    }
    xSemaphoreGive(mutex);
    if (available) xTaskNotifyGive(task_h);
    return available;
}

void control_copy_files(ControlFiles &out) {
    if (!mutex || !control_files) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (out.version != control_files->version) out = *control_files;
    xSemaphoreGive(mutex);
}

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
    /* ~50 KB: copiar só quando mudou (antes eram 2 cópias a cada 200 ms). */
    if (fresh) memcpy(out, g_inv, sizeof(*out));
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
