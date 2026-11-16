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

/* ---- U1 Spool Spec v1: extra.card_uids ---- */

namespace {
struct UidAcc {
    char *valid; size_t vn; int nvalid;
    char *invalid; size_t in;
    bool overflow;
};

bool uid_sep(char c) {
    return c == ',' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '[' || c == ']' || c == '"' || c == '\'';
}

bool list_has(const char *list, const char *tok) {
    size_t L = strlen(tok);
    for (const char *p = list; p && *p;) {
        const char *e = strchr(p, ',');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n == L && !memcmp(p, tok, L)) return true;
        p = e ? e + 1 : nullptr;
    }
    return false;
}

void list_add(char *list, size_t n, const char *tok, bool *overflow) {
    size_t cur = strlen(list), L = strlen(tok);
    if (cur + (cur ? 1 : 0) + L + 1 > n) { if (overflow) *overflow = true; return; }
    if (cur) list[cur++] = ',';
    memcpy(list + cur, tok, L + 1);
}

void acc_token(UidAcc &a, const char *tok, size_t len) {
    if (!len) return;
    char raw[48];
    if (len >= sizeof(raw)) len = sizeof(raw) - 1;
    memcpy(raw, tok, len);
    raw[len] = 0;
    bool chars_ok = true;
    for (const char *p = raw; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isxdigit(c) || c == ':' || c == '-')) { chars_ok = false; break; }
    }
    char norm[48];
    int d = spool_normalize_uid(raw, norm, sizeof(norm));
    if (chars_ok && (d == 8 || d == 14 || d == 20)) {
        if (!list_has(a.valid, norm)) {
            bool of = false;
            list_add(a.valid, a.vn, norm, &of);
            if (of) a.overflow = true; else a.nvalid++;
        }
    } else if (a.invalid && a.in && !list_has(a.invalid, raw)) {
        bool of = false;
        list_add(a.invalid, a.in, raw, &of);
    }
}

void acc_split(UidAcc &a, const char *s) {
    while (s && *s) {
        while (*s && uid_sep(*s)) s++;
        const char *e = s;
        while (*e && !uid_sep(*e)) e++;
        acc_token(a, s, (size_t)(e - s));
        s = e;
    }
}

void acc_value(UidAcc &a, const char *s, int depth) {
    if (!s) return;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return;
    if (depth < 2 && (*s == '"' || *s == '[')) {
        JsonDocument d = SPOOL_DOC();
        if (!deserializeJson(d, s)) {
            if (d.is<JsonArrayConst>()) {
                for (JsonVariantConst it : d.as<JsonArrayConst>())
                    if (it.is<const char *>()) acc_split(a, it.as<const char *>());
                return;
            }
            if (d.is<const char *>()) { acc_value(a, d.as<const char *>(), depth + 1); return; }
        }
    }
    acc_split(a, s);
}
} // namespace

int spool_uids_parse(const char *raw, char *valid, size_t vn, char *invalid, size_t in) {
    char vtmp[SPOOL_UIDS_LEN] = {0};
    UidAcc a{valid ? valid : vtmp, valid ? vn : sizeof(vtmp), 0, invalid, in, false};
    if (a.valid && a.vn) a.valid[0] = 0;
    if (invalid && in) invalid[0] = 0;
    acc_value(a, raw, 0);
    return a.nvalid;
}

void spool_parse_uids_from_extra(const char *card_uids_raw, char *out, size_t n) {
    if (!out || n == 0) return;
    spool_uids_parse(card_uids_raw, out, n, nullptr, 0);
}

bool spool_uid_listed(const char *list, const char *uid) {
    char norm[48];
    if (!list || !uid) return false;
    spool_normalize_uid(uid, norm, sizeof(norm));
    if (!norm[0]) return false;
    return list_has(list, norm);
}

int spool_uids_canonical(const char *raw, const char *add, char *out, size_t n) {
    if (out && n) out[0] = 0;
    char norm[48];
    char tmp[64];
    if (!spool_validate_uid(add, norm, sizeof(norm), tmp, sizeof(tmp))) return -2;
    char valid[SPOOL_UIDS_LEN + 24] = {0}, invalid[SPOOL_UIDS_LEN] = {0};
    int nv = spool_uids_parse(raw, valid, sizeof(valid), invalid, sizeof(invalid));
    int rc = 1;
    if (list_has(valid, norm)) rc = 0;
    else {
        if (nv >= SPOOL_UIDS_MAX) return -1;
        bool of = false;
        list_add(valid, sizeof(valid), norm, &of);
        if (of) return -1;
    }
    char joined[sizeof(valid) + sizeof(invalid) + 2];
    snprintf(joined, sizeof(joined), "%s%s%s", valid, invalid[0] ? "," : "", invalid);
    JsonDocument q = SPOOL_DOC();
    q.set(joined);
    if (!out || measureJson(q) >= n) return -1;
    serializeJson(q, out, n);
    return rc;
}

bool spool_card_uid_hex(const char *json_value, char *out, size_t n) {
    if (!out || n < 21) return false;
    out[0] = 0;
    if (!json_value || !json_value[0]) return false;
    JsonDocument d = SPOOL_DOC();
    if (deserializeJson(d, json_value)) return false;
    unsigned bytes[10];
    int count = 0;
    if (d.is<JsonArrayConst>()) {
        for (JsonVariantConst v : d.as<JsonArrayConst>()) {
            if (count >= 10 || !v.is<long>()) return false;
            long b = v.as<long>();
            if (b < 0 || b > 255) return false;
            bytes[count++] = (unsigned)b;
        }
    } else if (d.is<const char *>()) {
        const char *s = d.as<const char *>();
        while (*s) {
            while (*s == ' ' || *s == ',' || *s == '\t') s++;
            if (!*s) break;
            if (!isdigit((unsigned char)*s) || count >= 10) return false;
            long b = 0;
            int digits = 0;
            while (isdigit((unsigned char)*s)) { b = b * 10 + (*s++ - '0'); if (++digits > 3) return false; }
            if (*s && *s != ' ' && *s != ',' && *s != '\t') return false;
            if (b > 255) return false;
            bytes[count++] = (unsigned)b;
        }
    } else return false;
    if (count != 4 && count != 7 && count != 10) return false;
    bool any = false;
    for (int i = 0; i < count; i++) any |= bytes[i] != 0;
    if (!any) return false;
    for (int i = 0; i < count; i++) snprintf(out + i * 2, 3, "%02X", bytes[i]);
    return true;
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

    /* extra.card_uids — leitura tolerante (Spec v1) + tags[].uid */
    char raw[SPOOL_UIDS_LEN * 2] = {0};
    if (obj["extra"].is<JsonObjectConst>()) {
        JsonVariantConst cu = obj["extra"]["card_uids"];
        if (cu.is<const char *>()) cpy(raw, sizeof(raw), cu.as<const char *>());
        else if (cu.is<JsonArrayConst>()) serializeJson(cu, raw, sizeof(raw));
    }
    spool_uids_parse(raw, w->card_uids, sizeof(w->card_uids), w->uids_invalid, sizeof(w->uids_invalid));
    if (obj["tags"].is<JsonArrayConst>()) {
        char tags[SPOOL_UIDS_LEN] = {0};
        size_t off = 0;
        for (JsonVariantConst t : obj["tags"].as<JsonArrayConst>()) {
            const char *u = t.is<JsonObjectConst>() ? (t["uid"] | "") : "";
            size_t L = strlen(u);
            if (!L || off + L + 2 >= sizeof(tags)) continue;
            if (off) tags[off++] = ',';
            memcpy(tags + off, u, L + 1);
            off += L;
        }
        char tv[SPOOL_UIDS_LEN] = {0};
        spool_uids_parse(tags, tv, sizeof(tv), nullptr, 0);
        for (char *p = tv; *p;) {
            char *e = strchr(p, ',');
            if (e) *e = 0;
            if (!spool_uid_listed(w->card_uids, p)) {
                bool of = false;
                list_add(w->card_uids, sizeof(w->card_uids), p, &of);
            }
            if (!e) break;
            p = e + 1;
        }
    }
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

