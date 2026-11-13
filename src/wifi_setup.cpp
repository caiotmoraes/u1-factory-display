#include "wifi_setup.h"
#include <string.h>
#include <stdio.h>
#include "secrets.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

static Preferences prefs;
static WebServer server(80);
static DNSServer dns;
static bool portal = false;

static const char *AP_SSID = "U1-DISPLAY";
static const char *AP_PASS = "u1display"; /* senha do portal, documentada no README */

static const char PAGE[] = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>U1 Factory Display — Wi-Fi</title>
<style>
body{font-family:sans-serif;background:#080f23;color:#f9f2de;margin:0;padding:24px}
h1{font-size:20px;color:#59ecc5}
input,button{font-size:16px;width:100%;padding:12px;margin:8px 0;box-sizing:border-box;border:0}
button{background:#59ecc5;color:#081725;font-weight:700}
.card{background:#111d37;padding:16px;max-width:420px}
</style></head><body>
<div class="card">
<h1>U1 Factory Display</h1>
<p>Informe o Wi-Fi da casa. A tela guarda a senha só na própria placa (NVS).</p>
<form method="POST" action="/save">
<input name="ssid" placeholder="Nome da rede (SSID)" required>
<input name="pass" type="password" placeholder="Senha">
<button type="submit">Salvar e conectar</button>
</form>
</div></body></html>
)HTML";

static void handle_root() { server.send(200, "text/html; charset=utf-8", PAGE); }

static void handle_save() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    ssid.trim();
    if (ssid.isEmpty()) { server.send(400, "text/plain", "SSID vazio"); return; }
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    server.send(200, "text/html; charset=utf-8",
                "<p>Salvo. A tela vai reiniciar e tentar conectar.</p>");
    delay(500);
    ESP.restart();
}

static void start_portal() {
    portal = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    dns.start(53, "*", WiFi.softAPIP());
    server.on("/", handle_root);
    server.on("/save", HTTP_POST, handle_save);
    server.onNotFound(handle_root);
    server.begin();
    Serial.printf("[wifi] portal AP %s  (senha %s)  IP %s\n",
                  AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
    xTaskCreate([](void *) {
        for (;;) {
            dns.processNextRequest();
            server.handleClient();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }, "portal", 4096, nullptr, 2, nullptr);
}

static bool try_sta(const char *ssid, const char *pass, uint32_t ms) {
    if (!ssid || !ssid[0]) return false;
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("u1-display");
    WiFi.begin(ssid, pass && pass[0] ? pass : nullptr);
    Serial.printf("[wifi] conectando em %s...\n", ssid);
    uint32_t start = millis();
    while (millis() - start < ms) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[wifi] OK %s  IP %s\n", ssid, WiFi.localIP().toString().c_str());
            return true;
        }
        delay(200);
    }
    Serial.println("[wifi] falhou");
    WiFi.disconnect(true);
    return false;
}

void wifi_setup_start(void) {
    prefs.begin("u1wifi", false);
    String nvs_ssid = prefs.getString("ssid", "");
    String nvs_pass = prefs.getString("pass", "");
    const char *ssid = nvs_ssid.length() ? nvs_ssid.c_str() : WIFI_SSID;
    const char *pass = nvs_ssid.length() ? nvs_pass.c_str() : WIFI_PASSWORD;
    if (try_sta(ssid, pass, 12000)) {
        portal = false;
        return;
    }
    start_portal();
}

bool wifi_is_portal(void) { return portal; }


bool wifi_is_connected(void) { return WiFi.status() == WL_CONNECTED; }

void wifi_current(char *ssid, size_t sn, char *ip, size_t in) {
    if (ssid && sn) {
        ssid[0] = 0;
        if (WiFi.status() == WL_CONNECTED) {
            String s = WiFi.SSID();
            strncpy(ssid, s.c_str(), sn - 1);
            ssid[sn - 1] = 0;
        } else if (portal) {
            strncpy(ssid, AP_SSID, sn - 1);
            ssid[sn - 1] = 0;
        }
    }
    if (ip && in) {
        ip[0] = 0;
        if (WiFi.status() == WL_CONNECTED) {
            String a = WiFi.localIP().toString();
            strncpy(ip, a.c_str(), in - 1);
            ip[in - 1] = 0;
        } else if (portal) {
            String a = WiFi.softAPIP().toString();
            strncpy(ip, a.c_str(), in - 1);
            ip[in - 1] = 0;
        }
    }
}

void wifi_prepare_scan(void) {
    wifi_mode_t m = WiFi.getMode();
    if (m == WIFI_MODE_AP || m == WIFI_AP) {
        WiFi.mode(WIFI_AP_STA);
    }
}

bool wifi_scan_start(void) {
    wifi_prepare_scan();
    WiFi.scanDelete();
    int r = WiFi.scanNetworks(true, false);
    return r == WIFI_SCAN_RUNNING || r >= 0;
}

int wifi_scan_poll(void) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return -1;
    if (n == WIFI_SCAN_FAILED) return -2;
    return n;
}

int wifi_scan_copy(WifiNet *out, int max) {
    if (!out || max <= 0) return 0;
    int n = WiFi.scanComplete();
    if (n < 0) return n;
    int w = 0;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        int32_t rssi = WiFi.RSSI(i);
        bool lock = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        int found = -1;
        for (int j = 0; j < w; j++) {
            if (strcmp(out[j].ssid, ssid.c_str()) == 0) { found = j; break; }
        }
        if (found >= 0) {
            if (rssi > out[found].rssi) {
                out[found].rssi = rssi;
                out[found].locked = lock;
            }
            continue;
        }
        if (w >= max) continue;
        strncpy(out[w].ssid, ssid.c_str(), WIFI_SSID_LEN - 1);
        out[w].ssid[WIFI_SSID_LEN - 1] = 0;
        out[w].rssi = rssi;
        out[w].locked = lock;
        w++;
    }
    for (int i = 1; i < w; i++) {
        WifiNet key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < key.rssi) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    WiFi.scanDelete();
    return w;
}

void wifi_save_and_restart(const char *ssid, const char *pass) {
    if (!ssid || !ssid[0]) return;
    prefs.putString("ssid", String(ssid));
    prefs.putString("pass", String(pass ? pass : ""));
    Serial.printf("[wifi] salvo SSID \"%s\", reiniciando\n", ssid);
    delay(400);
    ESP.restart();
}
