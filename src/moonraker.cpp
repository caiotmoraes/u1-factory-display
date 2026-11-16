/* Cliente HTTP do Moonraker + descoberta de IP + proxy Spoolman.
 *
 * Por que HTTP polling (1 s) e não WebSocket:
 *  - a descoberta na /24 é um GET em /printer/info; o mesmo cliente serve
 *    para tudo, inclusive o POST do proxy Spoolman;
 *  - reconectar um WebSocket a cada IP candidato é mais frágil;
 *  - 1 s é suficiente para um painel de status (progresso, temps, estados).
 *  Um upgrade para WS pode ser feito depois, reusando printer_classify().
 */
#include "moonraker.h"
#include "inventory_logic.h"
#include "secrets.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "psram_json.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

static Preferences prefs;
static SemaphoreHandle_t mutex;
static PrinterSnapshot g_snap;
static uint32_t g_seq = 1;
static bool force_discover = false;

static const uint16_t kPort =
#ifdef MOONRAKER_PORT
    MOONRAKER_PORT
#else
    7125
#endif
    ;

static const char *kSeedIp =
#ifdef MOONRAKER_SEED_IP
    MOONRAKER_SEED_IP
#else
    "192.168.0.4"
#endif
    ;

static void publish(const PrinterSnapshot &s) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    g_snap = s;
    g_seq++;
    xSemaphoreGive(mutex);
}

bool moonraker_copy(PrinterSnapshot *out, uint32_t *seq) {
    if (!out || !mutex) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool fresh = !seq || *seq != g_seq;
    *out = g_snap;
    if (seq) *seq = g_seq;
    xSemaphoreGive(mutex);
    return fresh;
}

void moonraker_force_discover(void) { force_discover = true; }

bool moonraker_endpoint(char *ip, size_t n, uint16_t *port) {
    if (!mutex) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool have = g_snap.printer_ip[0] != 0;
    if (ip && n) {
        strncpy(ip, g_snap.printer_ip, n - 1);
        ip[n - 1] = 0;
    }
    if (port) *port = g_snap.printer_port ? g_snap.printer_port : kPort;
    xSemaphoreGive(mutex);
    return have;
}

static bool http_get(const char *url, String &body, int timeout_ms = 2500) {
    HTTPClient http;
    http.setConnectTimeout(timeout_ms);
    http.setTimeout(timeout_ms);
    http.setReuse(false);
    if (!http.begin(url)) return false;
    int code = http.GET();
    if (code == 200) body = http.getString();
    http.end();
    return code == 200;
}

static bool http_post(const char *url, const char *payload, String &body, int timeout_ms = 3000) {
    HTTPClient http;
    http.setConnectTimeout(timeout_ms);
    http.setTimeout(timeout_ms);
    http.setReuse(false);
    if (!http.begin(url)) return false;
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(payload);
    if (code == 200) body = http.getString();
    http.end();
    return code == 200;
}

/* GET /printer/info — critério de "esta é uma impressora Klipper/Moonraker". */
static bool probe_printer(IPAddress ip, uint16_t port, int timeout_ms = 200) {
    WiFiClient c;
    if (!c.connect(ip, port, timeout_ms)) return false;
    c.setTimeout(1);
    char req[96];
    snprintf(req, sizeof(req),
             "GET /printer/info HTTP/1.1\r\nHost: %u.%u.%u.%u\r\nConnection: close\r\n\r\n",
             ip[0], ip[1], ip[2], ip[3]);
    c.print(req);
    unsigned long start = millis();
    String resp;
    while (millis() - start < (unsigned long)timeout_ms + 300 && c.connected()) {
        while (c.available()) resp += (char)c.read();
        if (resp.indexOf("\r\n\r\n") >= 0 && resp.length() > 32) break;
        delay(5);
    }
    c.stop();
    /* Moonraker responde JSON com "result" e normalmente "state" ou "klipper". */
    return resp.indexOf("200") > 0 && (resp.indexOf("result") >= 0 || resp.indexOf("klipper") >= 0
                                       || resp.indexOf("state") >= 0);
}

static JsonObject root_of(JsonDocument &doc) {
    if (doc["result"].is<JsonObject>()) return doc["result"].as<JsonObject>();
    return doc.as<JsonObject>();
}

static void cpy(char *dst, size_t n, const char *src) {
    if (!dst || n == 0) return;
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, n - 1);
    dst[n - 1] = 0;
}


static void cpy_feeder_ch(FeederChannel *c, JsonObject ex) {
    c->module_exist = ex["module_exist"] | false;
    c->filament_detected = ex["filament_detected"] | false;
    c->disable_auto = ex["disable_auto"] | false;
    cpy(c->channel_state, sizeof(c->channel_state), ex["channel_state"] | "");
    cpy(c->channel_error, sizeof(c->channel_error), ex["channel_error"] | "ok");
}

static void parse_feeders(JsonObject st, FeederInfo &f) {
    memset(&f, 0, sizeof(f));
    const char *names[4][2] = {
        {"filament_feed left", "extruder0"},
        {"filament_feed left", "extruder1"},
        {"filament_feed right", "extruder2"},
        {"filament_feed right", "extruder3"},
    };
    int got = 0;
    for (int i = 0; i < 4; i++) {
        if (!st[names[i][0]].is<JsonObject>()) continue;
        JsonObject side = st[names[i][0]];
        if (!side[names[i][1]].is<JsonObject>()) continue;
        cpy_feeder_ch(&f.ch[i], side[names[i][1]].as<JsonObject>());
        got++;
    }
    if (st["print_task_config"].is<JsonObject>()) {
        JsonObject ptc = st["print_task_config"];
        if (ptc["filament_spool_id"].is<JsonArray>()) {
            JsonArray a = ptc["filament_spool_id"];
            for (int i = 0; i < 4 && i < (int)a.size(); i++)
                f.ch[i].spool_id = a[i].is<int>() ? a[i].as<int>() : 0;
        }
        if (ptc["filament_type"].is<JsonArray>()) {
            JsonArray a = ptc["filament_type"];
            for (int i = 0; i < 4 && i < (int)a.size(); i++)
                cpy(f.ch[i].filament_type, sizeof(f.ch[i].filament_type), a[i] | "");
        }
        if (ptc["filament_vendor"].is<JsonArray>()) {
            JsonArray a = ptc["filament_vendor"];
            for (int i = 0; i < 4 && i < (int)a.size(); i++)
                cpy(f.ch[i].vendor, sizeof(f.ch[i].vendor), a[i] | "");
        }
        if (ptc["filament_color_rgba"].is<JsonArray>()) {
            JsonArray a = ptc["filament_color_rgba"];
            for (int i = 0; i < 4 && i < (int)a.size(); i++) {
                const char *hx = a[i] | "";
                if (hx[0] == '#') hx++;
                char tmp[8] = {0};
                strncpy(tmp, hx, 6);
                for (int k = 0; k < 6; k++) {
                    char c = tmp[k];
                    if (c >= 'a' && c <= 'f') c = (char)(c - 32);
                    if (c) f.ch[i].color_hex[k] = c;
                }
                f.ch[i].color_hex[6] = 0;
            }
        }
    }
    if (st["webhooks"].is<JsonObject>()) {
        const char *ws = st["webhooks"]["state"] | "ready";
        f.printer_ready = (strcasecmp(ws, "ready") == 0);
    } else {
        f.printer_ready = true;
    }
    const char *ps = "";
    if (st["print_stats"].is<JsonObject>()) ps = st["print_stats"]["state"] | "";
    f.busy_print = (strcasecmp(ps, "printing") == 0 || strcasecmp(ps, "paused") == 0);
    f.valid = (got > 0);
}

static bool parse_objects(const String &body, PrinterSnapshot &s) {
    JsonDocument doc(psram_json());
    DeserializationError err = deserializeJson(doc, body);
    if (err) return false;
    JsonObject root = root_of(doc);
    JsonObject st = root["status"].is<JsonObject>() ? root["status"].as<JsonObject>() : root;
    const float display_progress = st["display_status"]["progress"] | -1.0f;
    const float file_progress = st["virtual_sdcard"]["progress"] | 0.0f;
    s.progress = display_progress >= 0.0f && display_progress <= 1.0f
                     ? display_progress : file_progress;
    s.remaining_s = 0;
    if (st["print_stats"].is<JsonObject>()) {
        JsonObject ps = st["print_stats"];
        cpy(s.print_stats, sizeof(s.print_stats), ps["state"] | "");
        cpy(s.filename, sizeof(s.filename), ps["filename"] | "");
        s.filament_used = ps["filament_used"] | 0.0f;
        float duration = ps["print_duration"] | 0.0f;
        float estimated = ps["estimated_time"] | 0.0f; /* pode não existir */
        if (ps["info"].is<JsonObject>()) {
            s.layer = ps["info"]["current_layer"] | 0;
            s.layer_total = ps["info"]["total_layer"] | 0;
        }
        if (estimated > 0 && duration >= 0) s.remaining_s = estimated > duration ? estimated - duration : 0;
        else if (s.progress > 0.01f && duration > 0) s.remaining_s = duration / s.progress - duration;
        s.cancelled = (strcasecmp(s.print_stats, "cancelled") == 0);
    }
    if (st["extruder"].is<JsonObject>()) {
        s.nozzle_c = st["extruder"]["temperature"] | 0.0f;
        s.nozzle_target_c = st["extruder"]["target"] | 0.0f;
    }
    if (st["heater_bed"].is<JsonObject>()) {
        s.bed_c = st["heater_bed"]["temperature"] | 0.0f;
        s.bed_target_c = st["heater_bed"]["target"] | 0.0f;
    }
    if (st["toolhead"].is<JsonObject>()) {
        const char *ex = st["toolhead"]["extruder"] | "extruder";
        s.active_extruder = hud_parse_extruder_name(ex);
    }
    PrinterRawStatus raw{};
    raw.connected = true;
    cpy(raw.webhooks, sizeof(raw.webhooks),
        st["webhooks"].is<JsonObject>() ? (st["webhooks"]["state"] | "ready") : "ready");
    cpy(raw.print_stats, sizeof(raw.print_stats), s.print_stats[0] ? s.print_stats : "standby");
    cpy(raw.idle_timeout, sizeof(raw.idle_timeout),
        st["idle_timeout"].is<JsonObject>() ? (st["idle_timeout"]["state"] | "Idle") : "Idle");
    raw.filament_used = s.filament_used;
    s.state = printer_classify(&raw);
    parse_feeders(st, s.feeder);
    /* temps por extrusor DEPOIS do memset de parse_feeders */
    static const char *ex_key[4] = {"extruder", "extruder1", "extruder2", "extruder3"};
    for (int i = 0; i < 4; i++) {
        if (st[ex_key[i]].is<JsonObject>()) {
            s.feeder.ch[i].nozzle_c = st[ex_key[i]]["temperature"] | 0.0f;
            s.feeder.ch[i].nozzle_target_c = st[ex_key[i]]["target"] | 0.0f;
        }
    }
    if (s.active_extruder < 0 || s.active_extruder > 3) s.active_extruder = 0;
    if (s.feeder.ch[s.active_extruder].nozzle_c > 0.0f ||
        s.feeder.ch[s.active_extruder].nozzle_target_c > 0.0f) {
        s.nozzle_c = s.feeder.ch[s.active_extruder].nozzle_c;
        s.nozzle_target_c = s.feeder.ch[s.active_extruder].nozzle_target_c;
    }
    return true;
}

static bool parse_spoolman_status(const String &body, int *spool_id, bool *connected) {
    JsonDocument doc(psram_json());
    if (deserializeJson(doc, body)) return false;
    JsonObject root = root_of(doc);
    *connected = root["spoolman_connected"] | false;
    if (root["spool_id"].isNull() || !root["spool_id"].is<int>()) {
        *spool_id = 0;
    } else {
        *spool_id = root["spool_id"].as<int>();
    }
    return true;
}

static void parse_spool(const String &body, SpoolInfo &sp, int expected_id) {
    JsonDocument doc(psram_json());
    if (deserializeJson(doc, body)) return;
    JsonObject root = root_of(doc);
    /* use_v2_response: { response: {...}, error: null } */
    JsonObject obj = root;
    if (root["error"].is<JsonObject>() && !root["error"].isNull()) return;
    if (root["response"].is<JsonObject>()) obj = root["response"].as<JsonObject>();

    JsonObject fil = obj["filament"].is<JsonObject>() ? obj["filament"].as<JsonObject>() : obj;
    sp.valid = true;
    sp.id = obj["id"] | expected_id;
    cpy(sp.name, sizeof(sp.name), fil["name"] | "");
    cpy(sp.material, sizeof(sp.material), fil["material"] | "");
    if (fil["vendor"].is<JsonObject>()) cpy(sp.vendor, sizeof(sp.vendor), fil["vendor"]["name"] | "");
    else cpy(sp.vendor, sizeof(sp.vendor), "");
    const char *hex = fil["color_hex"] | "";
    if ((!hex || !hex[0]) && fil["multi_color_hexes"].is<const char *>()) {
        hex = fil["multi_color_hexes"];
    }
    /* aceita "#RRGGBB" ou "RRGGBB" (e pega a primeira de uma lista) */
    char tmp[16] = {0};
    if (hex) {
        if (hex[0] == '#') hex++;
        strncpy(tmp, hex, 6);
    }
    for (int i = 0; i < 6; i++) {
        char c = tmp[i];
        if (c >= 'a' && c <= 'f') c -= 32;
        sp.color_hex[i] = c ? c : 'F';
    }
    sp.color_hex[6] = 0;
    if (obj["remaining_weight"].is<float>() || obj["remaining_weight"].is<int>())
        sp.remaining_g = obj["remaining_weight"].as<float>();
    else
        sp.remaining_g = -1;
    sp.low = (sp.remaining_g >= 0 && sp.remaining_g < SPOOL_LOW_GRAMS);
}

static bool poll_once(PrinterSnapshot &s) {
    char url[384];
    String body;
    snprintf(url, sizeof(url),
             "http://%s:%u/printer/objects/query?print_stats&virtual_sdcard&display_status&extruder&extruder1&extruder2&extruder3&heater_bed&idle_timeout&webhooks&filament_feed%%20left&filament_feed%%20right&filament_detect&print_task_config&toolhead",
             s.printer_ip, s.printer_port);
    if (!http_get(url, body, 2500)) {
        /* fallback se a U1 rejeitar objetos extras */
        snprintf(url, sizeof(url),
                 "http://%s:%u/printer/objects/query?print_stats&virtual_sdcard&display_status&extruder&heater_bed&idle_timeout&webhooks",
                 s.printer_ip, s.printer_port);
        if (!http_get(url, body, 2500)) return false;
    }
    if (!parse_objects(body, s)) return false;
    s.moonraker_ok = true;

    /* Rolo ativo = filament_spool_id do extrusor atual (print_task_config).
     * O /server/spoolman/spool_id da U1 é um id GLOBAL do plugin e costuma
     * vir null mesmo com rolos associados aos 4 bicos — não usar como fonte. */
    int ids[4] = {
        s.feeder.ch[0].spool_id, s.feeder.ch[1].spool_id,
        s.feeder.ch[2].spool_id, s.feeder.ch[3].spool_id
    };
    int spool_id = hud_active_spool_id(ids, s.active_extruder);
    if (spool_id > 0) {
        char payload[160];
        snprintf(payload, sizeof(payload),
                 "{\"request_method\":\"GET\",\"path\":\"/v1/spool/%d\",\"use_v2_response\":true}",
                 spool_id);
        snprintf(url, sizeof(url), "http://%s:%u/server/spoolman/proxy", s.printer_ip, s.printer_port);
        String pb;
        if (http_post(url, payload, pb, 3000)) {
            parse_spool(pb, s.spool, spool_id);
        } else {
            /* fallback sem use_v2_response, caso a versão do Moonraker seja antiga */
            snprintf(payload, sizeof(payload),
                     "{\"request_method\":\"GET\",\"path\":\"/v1/spool/%d\"}", spool_id);
            if (http_post(url, payload, pb, 3000)) parse_spool(pb, s.spool, spool_id);
        }
        if (s.last_spool_id >= 0 && s.last_spool_id != 0 && s.last_spool_id != spool_id && s.spool.valid) {
            s.spool_changed = true;
        }
        s.last_spool_id = spool_id;
    } else {
        s.spool.valid = false;
        if (spool_id == 0) s.last_spool_id = 0;
    }
    return true;
}

static bool discover(PrinterSnapshot &s) {
    s.discovering = true;
    cpy(s.status_msg, sizeof(s.status_msg), "Procurando a U1 na rede...");
    publish(s);

    /* Varredura da /24 do IP local (pedido do projeto; roteadores residenciais são /24). */
    IPAddress self = WiFi.localIP();
    const int first = 1, last = 254;

    /* Ordem: último IP conhecido, seed, depois o resto da /24. */
    char saved[16] = {0};
    prefs.getString("mr_ip", saved, sizeof(saved));
    IPAddress candidates[4];
    int nc = 0;
    auto add = [&](const char *txt) {
        IPAddress ip;
        if (txt && ip.fromString(txt)) candidates[nc++] = ip;
    };
    add(saved);
    add(kSeedIp);

    auto try_ip = [&](IPAddress ip) -> bool {
        if (ip == self) return false;
        snprintf(s.scan_ip, sizeof(s.scan_ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        snprintf(s.status_msg, sizeof(s.status_msg), "Procurando em %s...", s.scan_ip);
        publish(s);
        if (!probe_printer(ip, kPort, 180)) return false;
        snprintf(s.printer_ip, sizeof(s.printer_ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        s.printer_port = kPort;
        prefs.putString("mr_ip", s.printer_ip);
        Serial.printf("[moonraker] encontrada em %s:%u\n", s.printer_ip, s.printer_port);
        return true;
    };

    for (int i = 0; i < nc; i++) if (try_ip(candidates[i])) { s.discovering = false; return true; }

    for (int host = first; host <= last; host++) {
        if (!WiFi.isConnected()) break;
        IPAddress ip(self[0], self[1], self[2], host);
        bool skip = false;
        for (int i = 0; i < nc; i++) if (ip == candidates[i]) skip = true;
        if (skip) continue;
        if (try_ip(ip)) { s.discovering = false; return true; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    s.discovering = false;
    cpy(s.status_msg, sizeof(s.status_msg), "U1 não encontrada na rede");
    s.scan_ip[0] = 0;
    return false;
}

static void task(void *) {
    PrinterSnapshot s;
    snapshot_clear(&s);
    s.printer_port = kPort;
    prefs.getString("mr_ip", s.printer_ip, sizeof(s.printer_ip));
    int fails = 99; /* força descoberta se não houver IP ou a primeira falhar */

    for (;;) {
        s.wifi_ok = WiFi.isConnected();
        s.updated_ms = millis();
        s.spool_changed = false;

        if (!s.wifi_ok) {
            s.moonraker_ok = false;
            s.state = PRINTER_OFFLINE;
            cpy(s.status_msg, sizeof(s.status_msg),
                WiFi.getMode() == WIFI_MODE_AP || WiFi.getMode() == WIFI_MODE_APSTA
                    ? "Conecte-se ao Wi-Fi U1-DISPLAY"
                    : "Sem Wi-Fi");
            publish(s);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (force_discover || s.printer_ip[0] == 0 || fails >= 3) {
            force_discover = false;
            if (!discover(s)) {
                s.moonraker_ok = false;
                s.state = PRINTER_OFFLINE;
                publish(s);
                vTaskDelay(pdMS_TO_TICKS(4000));
                fails = 3;
                continue;
            }
            fails = 0;
        }

        if (poll_once(s)) {
            fails = 0;
            s.moonraker_ok = true;
            if (s.state != PRINTER_OFFLINE) s.status_msg[0] = 0;
            publish(s);
        } else {
            fails++;
            Serial.printf("[moonraker] falha de poll (%d) em %s\n", fails, s.printer_ip);
            if (fails >= 3) {
                s.moonraker_ok = false;
                s.state = PRINTER_OFFLINE;
                cpy(s.status_msg, sizeof(s.status_msg), "Perdeu a U1, vasculhando de novo...");
                publish(s);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void moonraker_init(void) {
    mutex = xSemaphoreCreateMutex();
    snapshot_clear(&g_snap);
    g_snap.printer_port = kPort;
    prefs.begin("u1face", false);
}

void moonraker_start_task(void) {
    xTaskCreatePinnedToCore(task, "moonraker", 12288, nullptr, 3, nullptr, 0);
}
