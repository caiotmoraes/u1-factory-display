#include "inventory_logic.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>

static void cpy(char *dst, size_t n, const char *src) {
    if (!dst || n == 0) return;
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, n - 1);
    dst[n - 1] = 0;
}

/* Minúsculas ASCII + remove combiners UTF-8 (não faz NFD completo:
 * para entrada já em Latin-1/UTF-8 composto, mapeia letras acentuadas
 * comuns do português para a base). */
static unsigned char fold(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c + 32);
    return c;
}

static void normalize_pt(const char *in, char *out, size_t n) {
    if (!out || n == 0) return;
    size_t k = 0;
    const unsigned char *p = (const unsigned char *)in;
    if (!p) { out[0] = 0; return; }
    while (*p && k + 1 < n) {
        /* UTF-8 Latin-1 supplement: C3 80-BF → À-ÿ */
        if (p[0] == 0xC3 && p[1]) {
            unsigned char s = p[1];
            char base = 0;
            switch (s) {
                case 0x80: case 0x81: case 0x82: case 0x83: case 0x84:
                case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4:
                    base = 'a'; break;
                case 0x87: case 0xA7: base = 'c'; break;
                case 0x88: case 0x89: case 0x8A: case 0x8B:
                case 0xA8: case 0xA9: case 0xAA: case 0xAB:
                    base = 'e'; break;
                case 0x8C: case 0x8D: case 0x8E: case 0x8F:
                case 0xAC: case 0xAD: case 0xAE: case 0xAF:
                    base = 'i'; break;
                case 0x91: case 0xB1: base = 'n'; break;
                case 0x92: case 0x93: case 0x94: case 0x95: case 0x96:
                case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6:
                    base = 'o'; break;
                case 0x99: case 0x9A: case 0x9B: case 0x9C:
                case 0xB9: case 0xBA: case 0xBB: case 0xBC:
                    base = 'u'; break;
                default: break;
            }
            if (base) {
                out[k++] = base;
                p += 2;
                continue;
            }
        }
        /* Latin-1 byte (se alguém mandar ISO-8859-1) */
        unsigned char c = *p++;
        if (c >= 0xC0) {
            const char *b =
                "aaaaaaeceeeeiiiidnooooo-ouuuuy-aaaaaaeceeeeiiiidnooooo-ouuuuyy";
            /* 0xC0 is index 0 */
            out[k++] = b[c - 0xC0];
            continue;
        }
        if (c < 0x80) out[k++] = (char)fold(c);
        /* ignora outros bytes UTF-8 (continuação já tratada no C3) */
    }
    out[k] = 0;
}

bool stock_matches_query(const char *text, const char *query) {
    if (!query || !query[0]) return true;
    char qn[160], tn[384];
    normalize_pt(query, qn, sizeof(qn));
    normalize_pt(text ? text : "", tn, sizeof(tn));
    /* todas as palavras */
    char *p = qn;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char *e = p;
        while (*e && *e != ' ' && *e != '\t') e++;
        char save = *e;
        *e = 0;
        if (p[0] && !strstr(tn, p)) return false;
        *e = save;
        p = save ? e + 1 : e;
    }
    return true;
}

void stock_color_name(const char *color_hex, char *out, size_t n) {
    if (!out || n == 0) return;
    char hex[8];
    spool_parse_color(color_hex, hex);
    unsigned r = 0, g = 0, b = 0;
    sscanf(hex, "%02x%02x%02x", &r, &g, &b);
    struct { const char *name; int r, g, b; } names[] = {
        {"Preto", 18, 19, 24},
        {"Branco", 247, 247, 244},
        {"Cinza", 139, 144, 150},
        {"Prata", 195, 201, 207},
        {"Azul", 20, 99, 211},
        {"Azul-marinho", 0, 55, 118},
        {"Ciano", 25, 203, 219},
        {"Verde", 45, 163, 89},
        {"Verde-lima", 165, 224, 48},
        {"Amarelo", 255, 222, 28},
        {"Dourado", 223, 166, 42},
        {"Laranja", 255, 75, 0},
        {"Vermelho", 212, 38, 38},
        {"Rosa", 238, 99, 155},
        {"Roxo", 137, 72, 182},
        {"Marrom", 125, 75, 42},
        {"Bege", 215, 191, 153},
    };
    int best = 0;
    long score = 1L << 30;
    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        long dr = (long)r - names[i].r, dg = (long)g - names[i].g, db = (long)b - names[i].b;
        long d = dr * dr + dg * dg + db * db;
        if (d < score) { score = d; best = i; }
    }
    cpy(out, n, names[best].name);
}

void stock_filter_clear(StockFilter *f) {
    if (!f) return;
    memset(f, 0, sizeof(*f));
    f->sort = STOCK_SORT_ID;
}

static int icmp(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b) {
        int ca = fold((unsigned char)*a), cb = fold((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return fold((unsigned char)*a) - fold((unsigned char)*b);
}

static int cmp_spool(const WarehouseSpool *a, const WarehouseSpool *b, StockSort sort) {
    if (sort == STOCK_SORT_WEIGHT) {
        float wa = (a->info.remaining_g >= 0) ? a->info.remaining_g : 1e30f;
        float wb = (b->info.remaining_g >= 0) ? b->info.remaining_g : 1e30f;
        if (wa < wb) return -1;
        if (wa > wb) return 1;
        return a->info.id - b->info.id;
    }
    if (sort == STOCK_SORT_MATERIAL) {
        int c = icmp(a->info.material, b->info.material);
        return c ? c : (a->info.id - b->info.id);
    }
    if (sort == STOCK_SORT_BRAND) {
        int c = icmp(a->info.vendor, b->info.vendor);
        return c ? c : (a->info.id - b->info.id);
    }
    return a->info.id - b->info.id;
}

int stock_apply_filter(const WarehouseSpool *in, int n, const StockFilter *f,
                       int *idx, int max_out) {
    if (!in || !idx || max_out <= 0 || n <= 0) return 0;
    StockFilter def;
    if (!f) { stock_filter_clear(&def); f = &def; }
    int m = 0;
    for (int i = 0; i < n && m < max_out; i++) {
        const WarehouseSpool *s = &in[i];
        if (!f->include_archived && s->archived) continue;
        if (f->material[0] && strcmp(s->info.material, f->material) != 0) continue;
        if (f->brand[0] && strcmp(s->info.vendor, f->brand) != 0) continue;
        if (f->location[0] && strcmp(s->location, f->location) != 0) continue;
        if (f->low_stock) {
            if (s->info.remaining_g < 0 || s->info.remaining_g > STOCK_LOW_FILTER_G) continue;
        }
        if (f->query[0]) {
            char cname[32];
            stock_color_name(s->info.color_hex, cname, sizeof(cname));
            char blob[256];
            snprintf(blob, sizeof(blob), "%d %s %s %s %s %s",
                     s->info.id, s->info.name, s->info.material, s->info.vendor,
                     cname, s->location);
            if (!stock_matches_query(blob, f->query)) continue;
        }
        idx[m++] = i;
    }
    /* insertion sort nos índices (n pequeno) */
    for (int i = 1; i < m; i++) {
        int v = idx[i], j = i - 1;
        while (j >= 0 && cmp_spool(&in[idx[j]], &in[v], f->sort) > 0) {
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = v;
    }
    return m;
}

int stock_unique_field(const WarehouseSpool *in, int n, int field,
                       char *out, int max, size_t elem_size) {
    if (!in || !out || max <= 0 || elem_size < 2) return 0;
    int c = 0;
    for (int i = 0; i < n; i++) {
        const char *v = "";
        if (field == 0) v = in[i].info.material;
        else if (field == 1) v = in[i].info.vendor;
        else v = in[i].location;
        if (!v || !v[0]) continue;
        bool seen = false;
        for (int k = 0; k < c; k++) {
            if (strcmp(out + k * elem_size, v) == 0) { seen = true; break; }
        }
        if (seen) continue;
        if (c >= max) break;
        strncpy(out + c * elem_size, v, elem_size - 1);
        out[c * elem_size + elem_size - 1] = 0;
        c++;
    }
    /* sort case-insensitive */
    for (int i = 1; i < c; i++) {
        char tmp[128];
        if (elem_size > sizeof(tmp)) break;
        memcpy(tmp, out + i * elem_size, elem_size);
        int j = i - 1;
        while (j >= 0 && icmp(out + j * elem_size, tmp) > 0) {
            memcpy(out + (j + 1) * elem_size, out + j * elem_size, elem_size);
            j--;
        }
        memcpy(out + (j + 1) * elem_size, tmp, elem_size);
    }
    return c;
}

StockSummary stock_summary(const WarehouseSpool *in, int n) {
    StockSummary s;
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < n; i++) {
        if (in[i].archived) { s.archived++; continue; }
        s.active++;
        if (in[i].info.remaining_g >= 0) {
            s.known_weight_g += in[i].info.remaining_g > 0 ? in[i].info.remaining_g : 0;
            s.known_count++;
            if (in[i].info.remaining_g <= STOCK_LOW_FILTER_G) s.low200++;
        } else {
            s.unknown_weight++;
        }
    }
    return s;
}

static int has_ci(const char *t, const char *needle) {
    /* busca ASCII case-insensitive; também versões sem acento */
    if (!t || !needle) return 0;
    char a[256], b[64];
    normalize_pt(t, a, sizeof(a));
    normalize_pt(needle, b, sizeof(b));
    return strstr(a, b) != nullptr;
}

const char *history_event_type(const char *text) {
    /* ordem do app: problem, association, read, other */
    if (has_ci(text, "falha") || has_ci(text, "erro") || has_ci(text, "expir")
        || has_ci(text, "nao confirmad") || has_ci(text, "não confirmad")
        || has_ci(text, "cancelad") || has_ci(text, "sem conex"))
        return "problem";
    if (has_ci(text, "associad") || has_ci(text, "associacao") || has_ci(text, "associação"))
        return "association";
    if (has_ci(text, "nfc") || has_ci(text, "tag") || has_ci(text, "identificad")
        || has_ci(text, "leitura"))
        return "read";
    return "other";
}

const char *history_event_label_pt(const char *type) {
    if (type && strcmp(type, "association") == 0) return "ASSOCIACAO";
    if (type && strcmp(type, "read") == 0) return "NFC";
    if (type && strcmp(type, "problem") == 0) return "ATENCAO";
    return "REGISTRO";
}

bool spool_build_link_info(const WarehouseSpool *s, char *json, size_t n, char *err, size_t en) {
    if (err && en) err[0] = 0;
    if (!s || !json || n < 32) {
        if (err) cpy(err, en, "rolo invalido");
        return false;
    }
    if (!s->info.material[0]) {
        if (err) cpy(err, en, "Rolo sem material cadastrado.");
        return false;
    }
    const char *vendor = s->info.vendor[0] ? s->info.vendor : "Generic";
    /* cores: multi_color ou color_hex, até 5 */
    char colors[5][8];
    int nc = 0;
    const char *src = s->multi_color[0] ? s->multi_color : s->info.color_hex;
    const char *p = src;
    while (*p && nc < 5) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        char tmp[16] = {0};
        int k = 0;
        if (*p == '#') p++;
        while (*p && *p != ',' && *p != ' ' && k < 15) tmp[k++] = *p++;
        spool_parse_color(tmp, colors[nc]);
        /* valida 6 hex */
        int ok = 1;
        for (int i = 0; i < 6; i++) {
            char c = colors[nc][i];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) ok = 0;
        }
        if (!ok) {
            if (err) cpy(err, en, "Cor invalida no cadastro do rolo.");
            return false;
        }
        nc++;
    }
    if (nc == 0) {
        if (err) cpy(err, en, "Rolo sem cor cadastrada.");
        return false;
    }
    unsigned rgb1 = 0;
    sscanf(colors[0], "%x", &rgb1);
    unsigned long argb = (255UL << 24) | rgb1;
    /* CARD_UID fica array vazio — o app também manda vazio no spoollink */
    char rgb_fields[256] = {0};
    for (int i = 0; i < nc; i++) {
        unsigned v = 0;
        sscanf(colors[i], "%x", &v);
        char piece[48];
        snprintf(piece, sizeof(piece), ",\"RGB_%d\":%u", i + 1, v);
        strncat(rgb_fields, piece, sizeof(rgb_fields) - strlen(rgb_fields) - 1);
    }
    /* escapa vendor e material mínimos (aspas) */
    auto esc = [](const char *s, char *o, size_t n) {
        size_t k = 0;
        for (; s && *s && k + 2 < n; s++) {
            if (*s == '"' || *s == '\\') {
                if (k + 3 >= n) break;
                o[k++] = '\\';
            }
            o[k++] = *s;
        }
        o[k] = 0;
    };
    char ev[80], em[40];
    esc(vendor, ev, sizeof(ev));
    esc(s->info.material, em, sizeof(em));
    int wr = snprintf(json, n,
                      "{\"VENDOR\":\"%s\",\"MAIN_TYPE\":\"%s\",\"SPOOL_ID\":%d,"
                      "\"OFFICIAL\":true,\"ALPHA\":255,\"CARD_UID\":[]%s,"
                      "\"COLOR_NUMS\":%d,\"ARGB_COLOR\":%lu}",
                      ev, em, s->info.id, rgb_fields, nc, argb);
    if (wr < 0 || (size_t)wr >= n) {
        if (err) cpy(err, en, "info do spoollink grande demais");
        return false;
    }
    return true;
}

int hud_parse_extruder_name(const char *name) {
    if (!name || strncmp(name, "extruder", 8) != 0) return 0;
    if (name[8] == 0) return 0;
    if (name[8] >= '1' && name[8] <= '3' && name[9] == 0) return name[8] - '0';
    return 0;
}

static const WarehouseSpool *find_id(const WarehouseSpool *items, int count, int id) {
    if (!items || id <= 0) return NULL;
    for (int i = 0; i < count; i++)
        if (items[i].info.id == id) return &items[i];
    return NULL;
}

float hud_channel_grams(const WarehouseSpool *items, int count, int spool_id,
                        int active_id, float active_g) {
    if (spool_id <= 0) return -1;
    if (active_id == spool_id && active_g >= 0) return active_g;
    const WarehouseSpool *w = find_id(items, count, spool_id);
    return (w && w->info.remaining_g >= 0) ? w->info.remaining_g : -1;
}

bool hud_inventory_missing(const WarehouseSpool *items, int count, const int ids[4]) {
    for (int i = 0; i < 4; i++)
        if (ids[i] > 0 && !find_id(items, count, ids[i])) return true;
    return false;
}

uint32_t hud_mapping_key(const int ids[4]) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 4; i++) { h ^= (uint32_t)ids[i]; h *= 16777619u; }
    return h;
}

int hud_active_spool_id(const int filament_spool_id[4], int active_extruder) {
    if (!filament_spool_id) return 0;
    if (active_extruder < 0 || active_extruder > 3) active_extruder = 0;
    int id = filament_spool_id[active_extruder];
    return id > 0 ? id : 0;
}
