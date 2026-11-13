#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_NET_MAX 24
#define WIFI_SSID_LEN 33

typedef struct {
    char ssid[WIFI_SSID_LEN];
    int32_t rssi;
    bool locked;
} WifiNet;

void wifi_setup_start(void);          /* tenta STA; se falhar, sobe o portal */
bool wifi_is_portal(void);
bool wifi_is_connected(void);
/* SSID e IP atuais (STA). Se portal, ssid = AP "U1-FABRICA". */
void wifi_current(char *ssid, size_t sn, char *ip, size_t in);

/* Scan assíncrono. Em modo AP, sobe AP+STA (2.4 GHz). */
void wifi_prepare_scan(void);
bool wifi_scan_start(void);
/* -1 = ainda varrendo, -2 = não iniciado, >=0 = quantidade (já dedup). */
int  wifi_scan_poll(void);
/* Copia resultados (SSID único, melhor RSSI), ordenados por RSSI desc.
 * Chamar quando poll >= 0. Libera a varredura do driver. */
int  wifi_scan_copy(WifiNet *out, int max);

/* Grava NVS namespace u1wifi (ssid/pass) e reinicia. */
void wifi_save_and_restart(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif
