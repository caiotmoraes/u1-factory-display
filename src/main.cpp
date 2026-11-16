#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "wifi_setup.h"
#include "moonraker.h"
#include "inventory.h"
#include "history.h"
#include "nfc_hook.h"
#include "ui.h"
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>

/* Diagnóstico de reinício: sobrevive ao reset (não à falta de energia). */
struct ResetDiag {
    uint32_t magic;
    uint32_t uptime_s;
    uint32_t min_internal;
    uint32_t free_internal;
};
static RTC_NOINIT_ATTR ResetDiag rtc_diag;
static constexpr uint32_t DIAG_MAGIC = 0x55314644;

static const char *reset_name(esp_reset_reason_t r) {
    switch (r) {
    case ESP_RST_PANIC: return "PANIC (crash)";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT (alimentacao)";
    default: return nullptr;
    }
}

static void report_previous_reset() {
    const esp_reset_reason_t r = esp_reset_reason();
    const char *name = reset_name(r);
    Serial.printf("[boot] reset_reason=%d\n", (int)r);
    if (name) {
        char line[160];
        if (rtc_diag.magic == DIAG_MAGIC)
            snprintf(line, sizeof(line), "Reinicio inesperado: %s apos %lu s. RAM interna livre %lu KB (min %lu KB).",
                     name, (unsigned long)rtc_diag.uptime_s, (unsigned long)(rtc_diag.free_internal / 1024),
                     (unsigned long)(rtc_diag.min_internal / 1024));
        else
            snprintf(line, sizeof(line), "Reinicio inesperado: %s.", name);
        Serial.println(line);
        history_add(line);
    }
    rtc_diag = ResetDiag{DIAG_MAGIC, 0, 0, 0};
}

using namespace esp_panel::drivers;
using namespace esp_panel::board;

void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    delay(200);
    Serial.println();
    Serial.println("=== U1 Factory Display  v0.5.1  (Waveshare ESP32-S3-Touch-LCD-7) ===");

    Serial.println("[board] init");
    Board *board = new Board();
    board->init();
    if (!board->begin()) {
        Serial.println("[board] begin FALHOU");
        return;
    }

    Serial.println("[lvgl] init");
    lvgl_port_init(board->getLCD(), board->getTouch(), board->getBacklight());

    history_init();
    report_previous_reset();
    nfc_init();
    inventory_init();

    lvgl_port_lock(-1);
    ui_init();
    lvgl_port_unlock();

    Serial.println("[wifi] start");
    wifi_setup_start();

    Serial.println("[moonraker] start");
    moonraker_init();
    moonraker_start_task();

    Serial.println("[inventory] start");
    inventory_start_task();
    Serial.println("[boot] ok");
}

void loop() {
    rtc_diag.uptime_s = millis() / 1000;
    rtc_diag.free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    rtc_diag.min_internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    static uint32_t last_log;
    if (millis() - last_log > 60000) {
        last_log = millis();
        Serial.printf("[heap] interna livre %u (min %u, maior bloco %u) | PSRAM livre %u\n",
                      (unsigned)rtc_diag.free_internal, (unsigned)rtc_diag.min_internal,
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
    delay(250);
}
