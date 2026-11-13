#include "spool_parse.h"
#include <ArduinoJson.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
namespace {
struct PsramAllocator : ArduinoJson::Allocator {
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
PsramAllocator &psram_alloc() {
    static PsramAllocator a;
    return a;
}
}
#define SPOOL_DOC() JsonDocument(&psram_alloc())
#else
#define SPOOL_DOC() JsonDocument()
#endif

static void cpy(char *dst, size_t n, const char *src) {
    if (!dst || n == 0) return;
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, n - 1);
    dst[n - 1] = 0;
}

void spool_parse_color(const char *hex, char out[8]) {
    memset(out, 0, 8);
    const char *h = hex ? hex : "";
    if (h[0] == '#') h++;
    char tmp[16] = {0};
    /* primeira de uma lista "aabbcc,ddeeff" */
    strncpy(tmp, h, 15);
    for (int i = 0; tmp[i]; i++) {
        if (tmp[i] == ',' || tmp[i] == ' ' || tmp[i] == ';') { tmp[i] = 0; break; }
    }
    int n = 0;
    for (int i = 0; tmp[i] && n < 6; i++) {
        char c = tmp[i];
        if (c >= 'a' && c <= 'f') c = (char)(c - 32);
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) out[n++] = c;
    }
    if (n != 6) {
        /* fallback mint da paleta */
        memcpy(out, "59ECC5", 7);
    } else {
        out[6] = 0;
    }
}

int spool_normalize_uid(const char *raw, char *out, size_t n) {
    if (!out || n == 0) return 0;
    out[0] = 0;
    if (!raw) return 0;
    size_t k = 0;
    for (const char *p = raw; *p && k + 1 < n; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'f') c = (char)(c - 32);
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) out[k++] = c;
    }
    out[k] = 0;
    return (int)k;
}

bool spool_validate_uid(const char *raw, char *normalized, size_t n, char *err, size_t en) {
    if (err && en) err[0] = 0;
    if (!raw || !raw[0]) {
        if (err) cpy(err, en, "UID NFC vazio.");
        return false;
    }
    /* o bruto só pode ter hex, ':', espaços e hífen — como no app */
    for (const char *p = raw; *p; p++) {
        unsigned char c = (unsigned char)*p;
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')
                  || c == ':' || c == ' ' || c == '-';
        if (!ok) {
            if (err) cpy(err, en, "UID NFC invalido.");
            return false;
        }
    }
    char tmp[48];
    int digits = spool_normalize_uid(raw, tmp, sizeof(tmp));
    if (digits != 8 && digits != 14 && digits != 20) {
        if (err) cpy(err, en, "UID NFC deve ter 4, 7 ou 10 bytes.");
        return false;
    }
    if (normalized && n) cpy(normalized, n, tmp);
    return true;
}

void spool_parse_uids_from_extra(const char *card_uids_raw, char *out, size_t n) {
    if (!out || n == 0) return;
    out[0] = 0;
    if (!card_uids_raw || !card_uids_raw[0]) return;
    /* Pode vir "\"AA,BB\"", "AA,BB", "[\"AA\",\"BB\"]" */
    const char *s = card_uids_raw;
    char buf[256];
    cpy(buf, sizeof(buf), s);
    /* tira aspas externas repetidas */
    size_t L = strlen(buf);
    while (L >= 2 && buf[0] == '"' && buf[L - 1] == '"') {
        memmove(buf, buf + 1, L - 2);
        buf[L - 2] = 0;
        L -= 2;
    }
    /* se for array JSON simples, troca [ ] " por espaço */
    for (char *p = buf; *p; p++) {
        if (*p == '[' || *p == ']' || *p == '"' || *p == '\'' ) *p = ' ';
        if (*p == ';') *p = ',';
    }
    char acc[SPOOL_UIDS_LEN];
    acc[0] = 0;
    char *save = nullptr;
    /* strtok-like */
    char *tok = buf;
    while (*tok) {
        while (*tok == ' ' || *tok == ',' || *tok == '\t') tok++;
        if (!*tok) break;
        char *end = tok;
        while (*end && *end != ',' && *end != ' ' && *end != '\t') end++;
        char c = *end;
        *end = 0;
        char uid[24];
        spool_normalize_uid(tok, uid, sizeof(uid));
        if (uid[0]) {
            int d = (int)strlen(uid);
            if (d == 8 || d == 14 || d == 20) {
                if (acc[0]) strncat(acc, ",", sizeof(acc) - strlen(acc) - 1);
                strncat(acc, uid, sizeof(acc) - strlen(acc) - 1);
            }
        }
        *end = c;
        tok = c ? end + 1 : end;
        (void)save;
    }
    cpy(out, n, acc);
}

void spool_identity(const WarehouseSpool *s, char *out, size_t n) {
    if (!out || n == 0) return;
    out[0] = 0;
    if (!s) return;
    snprintf(out, n, "#%d · %s %s · %s|%d",
             s->info.id,
             s->info.vendor,
             s->info.material,
             s->info.name,
             s->filament_id);
}

static bool is_num(JsonVariantConst v) {
    return !v.isNull() && !v.isUnbound()
           && !v.is<const char *>() && !v.is<JsonObjectConst>() && !v.is<JsonArrayConst>()
           && (v.is<float>() || v.is<double>() || v.is<int>() || v.is<long>()
               || v.is<unsigned>() || v.is<int64_t>() || v.is<uint64_t>());
}

static bool extract_error(JsonVariantConst node, char *msg, size_t n) {
    if (node.isNull() || node.isUnbound()) return false;
    if (node.is<JsonObjectConst>()) {
        JsonObjectConst o = node.as<JsonObjectConst>();
        const char *m = o["message"] | "";
        if (!m[0]) m = o["error"] | "";
        if (msg && n) {
            if (m[0]) cpy(msg, n, m);
            else cpy(msg, n, "erro do Spoolman");
        }
        return true;
    }
    if (node.is<const char *>()) {
        const char *m = node.as<const char *>();
        if (m && m[0]) {
            if (msg && n) cpy(msg, n, m);
            return true;
        }
    }
    return false;
}

static void fill_from_obj(JsonObjectConst obj, WarehouseSpool *w, int expected_id) {
    memset(w, 0, sizeof(*w));
    w->initial_g = -1;
    w->used_g = -1;
    w->info.remaining_g = -1;
    w->filament_weight = -1;
    w->density = -1;
    w->diameter = -1;

    JsonObjectConst fil = obj;
    if (obj["filament"].is<JsonObjectConst>()) fil = obj["filament"].as<JsonObjectConst>();

    w->info.valid = true;
    w->info.id = obj["id"] | expected_id;
    w->filament_id = fil["id"] | 0;
    cpy(w->info.name, sizeof(w->info.name), fil["name"] | "");
    cpy(w->info.material, sizeof(w->info.material), fil["material"] | "");
    if (fil["vendor"].is<JsonObjectConst>())
        cpy(w->info.vendor, sizeof(w->info.vendor), fil["vendor"]["name"] | "");
    else
        w->info.vendor[0] = 0;

    const char *hex = fil["color_hex"] | "";
    if ((!hex || !hex[0]) && fil["multi_color_hexes"].is<const char *>())
        hex = fil["multi_color_hexes"];
    spool_parse_color(hex, w->info.color_hex);
    cpy(w->multi_color, sizeof(w->multi_color), fil["multi_color_hexes"] | "");

    if (is_num(obj["remaining_weight"]))
        w->info.remaining_g = obj["remaining_weight"].as<float>();
    if (is_num(obj["initial_weight"]))
        w->initial_g = obj["initial_weight"].as<float>();
    else if (is_num(fil["weight"]))
        w->initial_g = fil["weight"].as<float>();
    if (is_num(obj["used_weight"]))
        w->used_g = obj["used_weight"].as<float>();
    if (is_num(fil["weight"]))
        w->filament_weight = fil["weight"].as<float>();
    if (is_num(fil["density"]))
        w->density = fil["density"].as<float>();
    if (is_num(fil["diameter"]))
        w->diameter = fil["diameter"].as<float>();

    w->info.low = (w->info.remaining_g >= 0 && w->info.remaining_g < SPOOL_LOW_GRAMS);
    w->archived = obj["archived"] | false;
    cpy(w->location, sizeof(w->location), obj["location"] | "");
    cpy(w->comment, sizeof(w->comment), obj["comment"] | "");
    cpy(w->lot, sizeof(w->lot), obj["lot_nr"] | "");

    /* extra.card_uids — vários formatos */
    char raw[192] = {0};
    if (obj["extra"].is<JsonObjectConst>()) {
        JsonVariantConst cu = obj["extra"]["card_uids"];
        if (cu.is<const char *>()) cpy(raw, sizeof(raw), cu.as<const char *>());
        else if (cu.is<JsonArrayConst>()) {
            JsonArrayConst a = cu.as<JsonArrayConst>();
            size_t off = 0;
            for (JsonVariantConst it : a) {
                const char *u = it.as<const char *>();
                if (!u) continue;
                if (off + 2 < sizeof(raw)) {
                    if (off) raw[off++] = ',';
                    strncpy(raw + off, u, sizeof(raw) - off - 1);
                    off = strlen(raw);
                }
            }
        }
    }
    /* também tags[{uid}] */
    if (obj["tags"].is<JsonArrayConst>()) {
        for (JsonVariantConst t : obj["tags"].as<JsonArrayConst>()) {
            if (!t.is<JsonObjectConst>()) continue;
            const char *u = t["uid"] | "";
            if (u[0]) {
                size_t L = strlen(raw);
                if (L + 2 < sizeof(raw)) {
                    if (L) raw[L] = ',', raw[L + 1] = 0;
                    strncat(raw, u, sizeof(raw) - strlen(raw) - 1);
                }
            }
        }
    }
    spool_parse_uids_from_extra(raw, w->card_uids, sizeof(w->card_uids));
}

static JsonVariantConst unwrap(JsonDocument &doc, char *err, size_t en, bool *had_err) {
    *had_err = false;
    JsonVariantConst root = doc.as<JsonVariantConst>();
    /* array cru (GET /v1/spool direto, ou result já desembrulhado) */
    if (root.is<JsonArrayConst>()) return root;
    if (root["result"].is<JsonArrayConst>()) return root["result"];
    if (root["result"].is<JsonObjectConst>())
        root = root["result"];
    if (extract_error(root["error"], err, en)) { *had_err = true; return root; }
    if (root["response"].is<JsonArrayConst>() || root["response"].is<JsonObjectConst>()) {
        return root["response"];
    }
    /* response: null sem error — lista vazia */
    if (root["response"].isNull() && !root["response"].isUnbound())
        return root["response"];
    return root;
}

bool spool_fill(const char *obj_json, size_t len, WarehouseSpool *out, int expected_id) {
    if (!obj_json || !out) return false;
    JsonDocument doc = SPOOL_DOC();
    if (deserializeJson(doc, obj_json, len)) return false;
    if (!doc.is<JsonObject>()) return false;
    fill_from_obj(doc.as<JsonObject>(), out, expected_id);
    return out->info.valid;
}

bool spool_parse_one(const char *json, size_t len, WarehouseSpool *out, int expected_id,
                     char *error_msg, size_t error_msg_len) {
    if (error_msg && error_msg_len) error_msg[0] = 0;
    if (!json || !out) {
        if (error_msg) cpy(error_msg, error_msg_len, "JSON vazio");
        return false;
    }
    JsonDocument doc = SPOOL_DOC();
    DeserializationError e = deserializeJson(doc, json, len);
    if (e) {
        if (error_msg) cpy(error_msg, error_msg_len, "Falha ao ler o JSON");
        return false;
    }
    bool had_err = false;
    JsonVariantConst payload = unwrap(doc, error_msg, error_msg_len, &had_err);
    if (had_err) return false;
    if (!payload.is<JsonObjectConst>()) {
        if (error_msg) cpy(error_msg, error_msg_len, "Resposta sem ficha do rolo");
        return false;
    }
    fill_from_obj(payload.as<JsonObjectConst>(), out, expected_id);
    return out->info.valid;
}

int spool_parse_list(const char *json, size_t len,
                     WarehouseSpool *out, int max,
                     char *error_msg, size_t error_msg_len) {
    if (error_msg && error_msg_len) error_msg[0] = 0;
    if (!json || !out || max <= 0) {
        if (error_msg) cpy(error_msg, error_msg_len, "parametros invalidos");
        return -1;
    }
    JsonDocument doc = SPOOL_DOC();
    DeserializationError e = deserializeJson(doc, json, len);
    if (e) {
        if (error_msg) cpy(error_msg, error_msg_len, "Falha ao ler o JSON do estoque");
        return -1;
    }
    bool had_err = false;
    JsonVariantConst payload = unwrap(doc, error_msg, error_msg_len, &had_err);
    if (had_err) return -1;
    if (payload.isNull()) {
        /* lista vazia ou response null sem error */
        return 0;
    }
    if (!payload.is<JsonArrayConst>()) {
        /* um único objeto? */
        if (payload.is<JsonObjectConst>()) {
            fill_from_obj(payload.as<JsonObjectConst>(), &out[0], 0);
            return out[0].info.valid ? 1 : 0;
        }
        if (error_msg) cpy(error_msg, error_msg_len, "Resposta do estoque nao e uma lista");
        return -1;
    }
    int n = 0;
    for (JsonVariantConst it : payload.as<JsonArrayConst>()) {
        if (n >= max) break;
        if (!it.is<JsonObjectConst>()) continue;
        fill_from_obj(it.as<JsonObjectConst>(), &out[n], 0);
        if (out[n].info.valid && out[n].info.id != 0) n++;
    }
    return n;
}

bool spool_parse_active_id(const char *json, size_t len, int *id, bool *spoolman_connected) {
    if (id) *id = 0;
    if (spoolman_connected) *spoolman_connected = false;
    if (!json) return false;
    JsonDocument doc = SPOOL_DOC();
    if (deserializeJson(doc, json, len)) return false;
    JsonVariantConst root = doc.as<JsonVariantConst>();
    if (root["result"].is<JsonObjectConst>()) root = root["result"];
    bool ok = false;
    if (!root["spool_id"].isNull() && is_num(root["spool_id"])) {
        if (id) *id = root["spool_id"].as<int>();
        ok = true;
    } else if (root["spool_id"].isNull()) {
        if (id) *id = 0;
        ok = true;
    }
    if (!root["spoolman_connected"].isUnbound()) {
        if (spoolman_connected) *spoolman_connected = root["spoolman_connected"] | false;
        ok = true;
    }
    return ok;
}

/* Percorre um objeto de grupos { "grupo": { "items": [ {id, current}, ... ] } }. */
static int settings_scan_groups(JsonObject root) {
    for (JsonPair kv : root) {
        JsonObject group = kv.value().as<JsonObject>();
        if (group.isNull()) continue;
        JsonArray items = group["items"].as<JsonArray>();
        if (items.isNull()) continue;
        for (JsonObject it : items) {
            const char *id = it["id"] | "";
            if (id && strcmp(id, "rfid") == 0) {
                const char *cur = it["current"] | "";
                if (!cur || !cur[0]) return 0;
                return (strcasecmp(cur, "external") == 0) ? 1 : 2;
            }
        }
    }
    return 0;
}

int settings_parse_rfid_mode(const char *json, size_t len) {
    if (!json || len == 0) return 0;

    JsonDocument filter;
    /* curinga *: qualquer nome de grupo; [0] aplica-se a todos os items. */
    filter["*"]["items"][0]["id"] = true;
    filter["*"]["items"][0]["current"] = true;
    filter["result"]["*"]["items"][0]["id"] = true;
    filter["result"]["*"]["items"][0]["current"] = true;

    JsonDocument doc = SPOOL_DOC();
    DeserializationError err = deserializeJson(doc, json, len,
                                               DeserializationOption::Filter(filter));
    if (err) {
        /* fallback: documento completo (PSRAM no aparelho; 30 KB cabe) */
        JsonDocument full = SPOOL_DOC();
        if (deserializeJson(full, json, len)) return 0;
        JsonObject root = full.as<JsonObject>();
        if (full["result"].is<JsonObject>()) root = full["result"].as<JsonObject>();
        return settings_scan_groups(root);
    }
    JsonObject root = doc.as<JsonObject>();
    if (doc["result"].is<JsonObject>()) root = doc["result"].as<JsonObject>();
    return settings_scan_groups(root);
}

