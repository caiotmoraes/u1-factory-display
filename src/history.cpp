#include "history.h"
#include "inventory_logic.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <SPIFFS.h>
#include <FS.h>
#define HIST_PATH "/history.txt"
#endif

static char lines[HISTORY_MAX][HISTORY_LINE];
static int count = 0;
static bool inited = false;

static void cpy(char *d, size_t n, const char *s) {
    if (!d || n == 0) return;
    if (!s) { d[0] = 0; return; }
    strncpy(d, s, n - 1);
    d[n - 1] = 0;
}

#ifdef ARDUINO
static void persist(void) {
    File f = SPIFFS.open(HIST_PATH, FILE_WRITE);
    if (!f) return;
    for (int i = 0; i < count; i++) {
        f.println(lines[i]);
    }
    f.close();
}

static void load(void) {
    count = 0;
    if (!SPIFFS.exists(HIST_PATH)) return;
    File f = SPIFFS.open(HIST_PATH, FILE_READ);
    if (!f) return;
    while (f.available() && count < HISTORY_MAX) {
        String s = f.readStringUntil('\n');
        s.trim();
        if (s.length() == 0) continue;
        cpy(lines[count], HISTORY_LINE, s.c_str());
        count++;
    }
    f.close();
}
#endif

void history_init(void) {
    if (inited) return;
    inited = true;
    count = 0;
#ifdef ARDUINO
    if (!SPIFFS.begin(true)) {
        Serial.println("[history] SPIFFS indisponivel; so memoria");
        return;
    }
    load();
    Serial.printf("[history] %d registros\n", count);
#endif
}

int history_count(void) { return count; }

bool history_get(int i, char *out, size_t n) {
    if (!out || n == 0) return false;
    out[0] = 0;
    if (i < 0 || i >= count) return false;
    cpy(out, n, lines[i]);
    return true;
}

void history_add(const char *text) {
    if (!text || !text[0]) return;
    /* timestamp local dd/MM HH:mm:ss */
    char stamp[24] = "--/-- --:--:--";
    time_t now = time(nullptr);
    struct tm tm;
    if (now != (time_t)-1 && localtime_r(&now, &tm)) {
        snprintf(stamp, sizeof(stamp), "%02d/%02d %02d:%02d:%02d",
                 tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
#ifdef ARDUINO
    else {
        /* sem NTP: usa millis como hh:mm:ss relativo */
        unsigned long ms = millis();
        unsigned long s = ms / 1000;
        snprintf(stamp, sizeof(stamp), "--/-- %02lu:%02lu:%02lu",
                 (s / 3600) % 24, (s / 60) % 60, s % 60);
    }
#endif
    char line[HISTORY_LINE];
    snprintf(line, sizeof(line), "%s · %s", stamp, text);

    if (count >= HISTORY_MAX) count = HISTORY_MAX - 1;
    memmove(&lines[1], &lines[0], (size_t)count * HISTORY_LINE);
    cpy(lines[0], HISTORY_LINE, line);
    count++;
#ifdef ARDUINO
    persist();
#endif
}

int history_filter(const char *query, const char *type, int *idx, int max) {
    if (!idx || max <= 0) return 0;
    int m = 0;
    for (int i = 0; i < count && m < max; i++) {
        if (type && type[0] && strcmp(history_event_type(lines[i]), type) != 0) continue;
        if (query && query[0] && !stock_matches_query(lines[i], query)) continue;
        idx[m++] = i;
    }
    return m;
}
