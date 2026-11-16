#include "thumb.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <string.h>
#include "psram_json.h"
/* lodepng vem com o LVGL (LV_USE_PNG=1), compilado como C. */
extern "C" unsigned lodepng_decode32(unsigned char **out, unsigned *w, unsigned *h,
                                     const unsigned char *in, size_t insize);

static const size_t kMaxPng = 200 * 1024;     /* não baixa PNG maior que isto */
static const unsigned kMaxPixels = 512 * 512; /* nem decodifica imagens enormes */

static SemaphoreHandle_t mtx;
static uint16_t *bufs[2];
static lv_img_dsc_t dscs[2];
static int front = -1;          /* buffer exibido; -1 = sem miniatura */
static uint32_t g_seq = 1;
static char cur_file[192];
static uint32_t fail_ms;
static bool cur_ok;

static void *ps_alloc(size_t n) {
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p;
}

static void publish(int idx) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    front = idx;
    g_seq++;
    xSemaphoreGive(mtx);
}

bool thumb_get(const lv_img_dsc_t **dsc, uint32_t *seq) {
    if (!mtx) { *dsc = nullptr; return false; }
    xSemaphoreTake(mtx, portMAX_DELAY);
    bool fresh = *seq != g_seq;
    *seq = g_seq;
    *dsc = front >= 0 ? &dscs[front] : nullptr;
    xSemaphoreGive(mtx);
    return fresh;
}

/* Codifica cada segmento do caminho (mantém '/'). */
static void url_path(char *out, size_t n, const char *in) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < n; ++p) {
        if (isalnum(*p) || strchr("/-_.~", *p)) out[o++] = (char)*p;
        else { out[o++] = '%'; out[o++] = hex[*p >> 4]; out[o++] = hex[*p & 15]; }
    }
    out[o] = 0;
}

/* GET binário para PSRAM. Retorna bytes lidos ou 0. */
static size_t http_get_bin(const char *url, uint8_t **out) {
    *out = nullptr;
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(4000);
    http.setReuse(false);
    if (!http.begin(url)) return 0;
    int code = http.GET();
    size_t got = 0;
    if (code == 200) {
        int len = http.getSize();
        if (len > 0 && (size_t)len <= kMaxPng) {
            uint8_t *buf = (uint8_t *)ps_alloc((size_t)len);
            WiFiClient *s = http.getStreamPtr();
            uint32_t last = millis();
            while (buf && s && got < (size_t)len && millis() - last < 4000) {
                int a = s->available();
                if (a > 0) {
                    int r = s->read(buf + got, min((size_t)a, (size_t)len - got));
                    if (r > 0) { got += (size_t)r; last = millis(); }
                } else if (!s->connected()) {
                    break;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
            }
            if (buf && got == (size_t)len) *out = buf;
            else { heap_caps_free(buf); got = 0; }
        }
    }
    http.end();
    return got;
}

static bool pick_thumb(const char *ip, uint16_t port, const char *file, char *rel, size_t n) {
    char enc[400], url[520];
    url_path(enc, sizeof(enc), file);
    snprintf(url, sizeof(url), "http://%s:%u/server/files/metadata?filename=%s", ip, port, enc);
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(3000);
    http.setReuse(false);
    if (!http.begin(url)) return false;
    int code = http.GET();
    bool ok = false;
    if (code == 200) {
        String body = http.getString();
        JsonDocument filter;
        filter["result"]["thumbnails"][0]["width"] = true;
        filter["result"]["thumbnails"][0]["height"] = true;
        filter["result"]["thumbnails"][0]["size"] = true;
        filter["result"]["thumbnails"][0]["relative_path"] = true;
        JsonDocument doc(psram_json());
        if (!deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
            /* menor miniatura com pelo menos 96 px; senão a maior que couber */
            int best = -1, best_w = 0;
            JsonArray a = doc["result"]["thumbnails"];
            for (size_t i = 0; i < a.size(); i++) {
                int w = a[i]["width"] | 0, h = a[i]["height"] | 0, sz = a[i]["size"] | 0;
                if (w <= 0 || h <= 0 || (unsigned)(w * h) > kMaxPixels || (size_t)sz > kMaxPng) continue;
                bool better = best < 0 ||
                    (w >= 96 && (best_w < 96 || w < best_w)) ||
                    (w < 96 && best_w < 96 && w > best_w);
                if (better) { best = (int)i; best_w = w; }
            }
            if (best >= 0) {
                const char *r = a[best]["relative_path"] | "";
                /* relative_path é relativo à pasta do G-code */
                const char *slash = strrchr(file, '/');
                if (slash) snprintf(rel, n, "%.*s/%s", (int)(slash - file), file, r);
                else snprintf(rel, n, "%s", r);
                ok = r[0] != 0;
            }
        }
    }
    http.end();
    return ok;
}

static bool load(const char *ip, uint16_t port, const char *file) {
    char rel[384], enc[512], url[600];
    if (!pick_thumb(ip, port, file, rel, sizeof(rel))) return false;
    url_path(enc, sizeof(enc), rel);
    snprintf(url, sizeof(url), "http://%s:%u/server/files/gcodes/%s", ip, port, enc);
    uint8_t *png = nullptr;
    size_t n = http_get_bin(url, &png);
    if (!n) return false;
    unsigned char *rgba = nullptr;
    unsigned w = 0, h = 0;
    unsigned err = lodepng_decode32(&rgba, &w, &h, png, n);
    heap_caps_free(png);
    if (err || !rgba || !w || !h || w * h > kMaxPixels) { lv_mem_free(rgba); return false; }

    const int idx = front == 0 ? 1 : 0;
    uint16_t *dst = bufs[idx];
    /* encaixa mantendo a proporção, centralizado, vizinho mais próximo */
    const float sc = fminf((float)THUMB_W / w, (float)THUMB_H / h);
    const int dw = (int)(w * sc), dh = (int)(h * sc);
    const int ox = (THUMB_W - dw) / 2, oy = (THUMB_H - dh) / 2;
    const int br = (THUMB_BG >> 16) & 255, bg = (THUMB_BG >> 8) & 255, bb = THUMB_BG & 255;
    for (int y = 0; y < THUMB_H; y++) {
        for (int x = 0; x < THUMB_W; x++) {
            int r = br, g = bg, b = bb;
            if (x >= ox && x < ox + dw && y >= oy && y < oy + dh) {
                unsigned sx = (unsigned)((x - ox) / sc), sy = (unsigned)((y - oy) / sc);
                if (sx >= w) sx = w - 1;
                if (sy >= h) sy = h - 1;
                const unsigned char *p = rgba + 4 * (sy * w + sx);
                const int a = p[3];
                r = (p[0] * a + br * (255 - a)) / 255;
                g = (p[1] * a + bg * (255 - a)) / 255;
                b = (p[2] * a + bb * (255 - a)) / 255;
            }
            dst[y * THUMB_W + x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
        if ((y & 31) == 31) vTaskDelay(1);
    }
    lv_mem_free(rgba);   /* lodepng aloca com lv_mem_alloc */
    publish(idx);
    return true;
}

void thumb_update(const char *ip, uint16_t port, const char *filename) {
    if (!mtx) {
        mtx = xSemaphoreCreateMutex();
        for (int i = 0; i < 2; i++) {
            bufs[i] = (uint16_t *)ps_alloc(THUMB_W * THUMB_H * 2);
            dscs[i].header.always_zero = 0;
            dscs[i].header.cf = LV_IMG_CF_TRUE_COLOR;
            dscs[i].header.w = THUMB_W;
            dscs[i].header.h = THUMB_H;
            dscs[i].data_size = THUMB_W * THUMB_H * 2;
            dscs[i].data = (const uint8_t *)bufs[i];
        }
    }
    if (!bufs[0] || !bufs[1] || !ip || !ip[0]) return;
    if (!filename || !filename[0]) {
        if (cur_file[0]) { cur_file[0] = 0; cur_ok = false; publish(-1); }
        return;
    }
    const bool same = strcmp(filename, cur_file) == 0;
    if (same && (cur_ok || millis() - fail_ms < 60000)) return;
    if (!same && front >= 0) publish(-1);   /* arquivo novo: some a miniatura antiga */
    strncpy(cur_file, filename, sizeof(cur_file) - 1);
    cur_file[sizeof(cur_file) - 1] = 0;
    cur_ok = load(ip, port, filename);
    if (!cur_ok) fail_ms = millis();
    Serial.printf("[thumb] %s: %s\n", filename, cur_ok ? "ok" : "sem miniatura");
}
