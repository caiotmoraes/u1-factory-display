#pragma once
/* Compartilhado entre ui.cpp / ui_stock.cpp — não incluir fora da UI. */
#include "ui.h"
#include "layout.h"
#include "moonraker.h"
#include "inventory.h"
#include "inventory_logic.h"
#include "history.h"
#include "nfc_hook.h"
#include "wifi_setup.h"
#include "fonts.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <Arduino.h>
#include <esp_heap_caps.h>

#ifndef LV_COLOR_MAKE
#define LV_COLOR_MAKE(r,g,b) lv_color_make(r,g,b)
#endif

lv_color_t ui_C(uint32_t rgb);
void ui_style_label(lv_obj_t *o, const lv_font_t *font, uint32_t color);
lv_obj_t *ui_make_panel(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *ui_make_card(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *ui_label(lv_obj_t *parent, int x, int y, const lv_font_t *font, uint32_t color, const char *txt);
/* Tela com barra lateral; devolve o contêiner de conteúdo (700x456 em 88,12). */
enum { NAV_HOME = 0, NAV_CONTROL, NAV_STOCK, NAV_NFC, NAV_SETTINGS, NAV_COUNT };
lv_obj_t *ui_make_screen(lv_obj_t **scr, int active);
lv_obj_t *ui_page_title(lv_obj_t *content, const char *title, lv_event_cb_t back);
lv_obj_t *ui_make_btn(lv_obj_t *parent, int x, int y, int w, int h,
                      const char *txt, uint32_t bg, uint32_t fg,
                      lv_event_cb_t cb, void *ud);
void ui_go_factory(lv_event_t *e);
void ui_go_warehouse(lv_event_t *e);
void ui_go_history(lv_event_t *e);
void ui_go_wifi(lv_event_t *e);
void ui_go_details(lv_event_t *e);
void ui_go_settings(lv_event_t *e);
void ui_go_nfc(lv_event_t *e);
void ui_go_control(lv_event_t *e);

void ui_toast(const char *msg, bool err);
void ui_confirm(const char *title, const char *msg, void (*ok)(void *), void *ud);
void ui_close_overlays(void);
void ui_show_kb(lv_obj_t *ta);
void ui_hide_kb(void);
lv_obj_t *ui_overlay_box(const char *title, int w, int h);
uint32_t ui_parse_hex(const char *s);
void ui_cpy(char *d, size_t n, const char *s);

extern PrinterSnapshot ui_snap;
extern InventorySnapshot *ui_inv;
extern CatalogSnapshot *ui_cat;
extern ActionResult &ui_act; /* PSRAM (~6 KB) */
extern uint32_t ui_inv_seq, ui_cat_seq, ui_act_seq, ui_mr_seq;
extern lv_obj_t *scr_factory, *scr_details, *scr_warehouse, *scr_history, *scr_wifi, *scr_nfc, *scr_settings;
extern StockFilter ui_filter;
extern bool ui_compact;

void ui_warehouse_build(lv_obj_t *content);
void ui_warehouse_on_show(void);
void ui_warehouse_tick(void);
void ui_ficha_open(int id);
void ui_history_build(lv_obj_t *content);
void ui_history_on_show(void);
void ui_wifi_build(lv_obj_t *content);
void ui_wifi_on_show(void);
void ui_wifi_tick(void);
void ui_new_spool_open(void);
/* Abre o leitor NFC (overlay); read = já começa a ler uma tag. */
void ui_nfc_open(bool read);
