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

using namespace esp_panel::drivers;
using namespace esp_panel::board;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("=== U1 Factory Display  v0.4.3  (Waveshare ESP32-S3-Touch-LCD-7) ===");

    Serial.println("[board] init");
    Board *board = new Board();
    board->init();
    if (!board->begin()) {
        Serial.println("[board] begin FALHOU");
        return;
    }

    Serial.println("[lvgl] init");
    lvgl_port_init(board->getLCD(), board->getTouch());

    history_init();
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
    delay(250);
}
