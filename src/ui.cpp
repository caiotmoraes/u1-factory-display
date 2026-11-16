/* UI v0.6 — tema claro ("widgets claros"): barra lateral, Início com cartões
 * de progresso, filamentos, temperaturas e ventiladores. Tudo aqui roda na
 * task do LVGL; dados chegam por cópias (moonraker_copy / inventory_copy). */
#include "ui_priv.h"
#include "psram_json.h"
#include "control.h"
#include "thumb.h"

PrinterSnapshot ui_snap;
InventorySnapshot *ui_inv = nullptr;
CatalogSnapshot *ui_cat = nullptr;
ActionResult &ui_act = *psram_calloc_obj<ActionResult>();
uint32_t ui_inv_seq = 0, ui_cat_seq = 0, ui_act_seq = 0, ui_mr_seq = 0;
lv_obj_t *scr_factory, *scr_details, *scr_warehouse, *scr_history, *scr_wifi, *scr_nfc, *scr_settings;
StockFilter ui_filter;
bool ui_compact = false;

/* ---- Início ---- */
static lv_obj_t *thumb_box, *thumb_img, *thumb_ph;
static lv_obj_t *lbl_pct, *lbl_pct_sign, *lbl_restam, *bar, *lbl_layer, *lbl_file, *state_dot, *lbl_state;
static lv_obj_t *fil_card[4], *fil_dot[4], *fil_mat[4], *fil_g[4], *fil_id[4];
static lv_obj_t *spool_outer[4], *spool_arc[4], *spool_hub[4], *spool_hole[4];
static lv_obj_t *t_title[3], *t_val[3], *t_unit[3], *t_sub[3];
static lv_obj_t *chart_noz, *chart_cham;
static lv_chart_series_t *ser_noz, *ser_cham;
static lv_obj_t *fan_sw[2], *fan_pct[2];
static uint32_t fan_hold_until[2];
static uint32_t thumb_seq = 0, last_sample_ms = 0;

/* ---- detalhes / ajustes / nfc ---- */
static lv_obj_t *det_body;
static lv_obj_t *set_wifi, *set_u1, *set_about, *nfc_reader;
static lv_obj_t *toast_box, *toast_lbl;
static uint32_t toast_until = 0;

/* overlays */
static lv_obj_t *overlay, *kb;
static void (*confirm_ok)(void *);
static void *confirm_ud;

lv_color_t ui_C(uint32_t rgb) {
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void ui_cpy(char *d, size_t n, const char *s) {
    if (!d || n == 0) return;
    if (!s) { d[0] = 0; return; }
    strncpy(d, s, n - 1);
    d[n - 1] = 0;
}

uint32_t ui_parse_hex(const char *s) {
    if (!s || !s[0]) return COLOR_ACCENT;
    unsigned v = 0;
    sscanf(s, "%x", &v);
    return v & 0xFFFFFFu;
}

static int luma(uint32_t rgb) {
    return (int)(((rgb >> 16) & 255) * 299 + ((rgb >> 8) & 255) * 587 + (rgb & 255) * 114) / 1000;
}

static void plain(lv_obj_t *o) {
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_style_label(lv_obj_t *o, const lv_font_t *font, uint32_t color) {
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, ui_C(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
}

lv_obj_t *ui_label(lv_obj_t *parent, int x, int y, const lv_font_t *font, uint32_t color, const char *txt) {
    lv_obj_t *l = lv_label_create(parent);
    ui_style_label(l, font, color);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, txt ? txt : "");
    return l;
}

/* Cartão branco arredondado, borda suave (sem sombra: sem cache de sombra
 * no lv_conf, sombras grandes custariam FPS). */
lv_obj_t *ui_make_card(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, ui_C(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, ui_C(COLOR_LINE), 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_radius(p, CARD_RADIUS, 0);
    lv_obj_set_style_shadow_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

lv_obj_t *ui_make_panel(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *p = ui_make_card(parent, x, y, w, h);
    lv_obj_set_style_pad_all(p, 12, 0);
    return p;
}

lv_obj_t *ui_make_btn(lv_obj_t *parent, int x, int y, int w, int h,
                      const char *txt, uint32_t bg, uint32_t fg,
                      lv_event_cb_t cb, void *ud) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    const bool flat = bg == COLOR_SURFACE;
    if (fg == COLOR_ON_ACCENT) fg = luma(bg) < 175 ? 0xFFFFFF : COLOR_TEXT;
    lv_obj_set_style_radius(b, BTN_RADIUS, 0);
    lv_obj_set_style_bg_color(b, ui_C(flat ? COLOR_SURFACE2 : bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, flat ? ui_C(COLOR_ACCENT_BG) : lv_color_darken(ui_C(bg), LV_OPA_20),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, ui_C(COLOR_LINE), 0);
    lv_obj_set_style_border_width(b, flat ? 1 : 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 2, 0);
    lv_obj_set_style_opa(b, LV_OPA_50, LV_STATE_DISABLED);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt ? txt : "");
    ui_style_label(l, h >= 34 ? &font_ui_16 : &font_ui_14, fg);
    lv_obj_center(l);
    return b;
}

/* ---------- barra lateral ---------- */
static void nav_clicked(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case NAV_HOME: ui_go_factory(e); break;
    case NAV_CONTROL: ui_go_control(e); break;
    case NAV_STOCK: ui_go_warehouse(e); break;
    case NAV_NFC: ui_go_nfc(e); break;
    default: ui_go_settings(e); break;
    }
}

static void add_sidebar(lv_obj_t *scr, int active) {
    lv_obj_t *bar = ui_make_card(scr, SIDEBAR_X, SIDEBAR_Y, SIDEBAR_W, SIDEBAR_H);
    lv_obj_set_style_radius(bar, 20, 0);
    static const char *const icons[NAV_COUNT] = {ICON_HOME, ICON_SLIDERS, ICON_CUBE, ICON_NFC, ICON_COG};
    for (int i = 0; i < NAV_COUNT; i++) {
        const bool on = i == active;
        lv_obj_t *b = lv_btn_create(bar);
        lv_obj_set_pos(b, 8, 22 + i * 86);
        lv_obj_set_size(b, 48, 48);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_style_bg_color(b, ui_C(COLOR_ACCENT_BG), 0);
        lv_obj_set_style_bg_opa(b, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_add_event_cb(b, nav_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(b);
        ui_style_label(l, &font_icon_26, on ? COLOR_ACCENT : COLOR_TEXT2);
        lv_label_set_text(l, icons[i]);
        lv_obj_center(l);
    }
}

lv_obj_t *ui_make_screen(lv_obj_t **scr, int active) {
    *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(*scr, ui_C(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(*scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(*scr, LV_OBJ_FLAG_SCROLLABLE);
    add_sidebar(*scr, active);
    lv_obj_t *c = lv_obj_create(*scr);
    plain(c);
    lv_obj_set_pos(c, CONTENT_X, CONTENT_Y);
    lv_obj_set_size(c, CONTENT_W, CONTENT_H);
    return c;
}

/* Título de página (28 px) e, opcionalmente, botão "voltar". */
lv_obj_t *ui_page_title(lv_obj_t *content, const char *title, lv_event_cb_t back) {
    int x = 4;
    if (back) {
        lv_obj_t *b = ui_make_btn(content, 0, 2, 40, 36, ICON_BACK, COLOR_SURFACE, COLOR_TEXT2, back, nullptr);
        lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &font_icon_18, 0);
        x = 52;
    }
    return ui_label(content, x, 2, &font_ui_28, COLOR_TEXT, title);
}

/* ---------- teclado, diálogos, toast ---------- */
void ui_hide_kb(void) {
    if (kb) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(kb, nullptr);
    }
}

static void kb_ready(lv_event_t *e) {
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) ui_hide_kb();
}

void ui_show_kb(lv_obj_t *ta) {
    if (!kb) {
        kb = lv_keyboard_create(lv_layer_top());
        lv_obj_set_style_text_font(kb, &font_ui_16, 0);
        lv_obj_set_size(kb, SCREEN_W, 200);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(kb, ui_C(0xE4E2DD), 0);
        lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
        lv_obj_add_event_cb(kb, kb_ready, LV_EVENT_ALL, nullptr);
    }
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(kb);
}

void ui_close_overlays(void) {
    ui_hide_kb();
    if (overlay) {
        lv_obj_del(overlay);
        overlay = nullptr;
    }
    confirm_ok = nullptr;
    confirm_ud = nullptr;
}

static void overlay_swallow(lv_event_t *e) { lv_event_stop_bubbling(e); }

lv_obj_t *ui_overlay_box(const char *title, int w, int h) {
    ui_close_overlays();
    overlay = lv_obj_create(lv_layer_top());
    plain(overlay);
    lv_obj_set_size(overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(overlay, ui_C(0x1E2430), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_40, 0);
    lv_obj_add_event_cb(overlay, overlay_swallow, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *box = ui_make_panel(overlay, (SCREEN_W - w) / 2, (SCREEN_H - h) / 2 - 10, w, h);
    lv_obj_set_style_radius(box, 20, 0);
    lv_obj_set_style_pad_all(box, 8, 0);   /* as fichas foram desenhadas para 8 px */
    lv_obj_set_style_pad_top(box, 12, 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    if (title && title[0]) {
        lv_obj_t *t = ui_label(box, 4, 0, &font_ui_20, COLOR_TEXT, title);
        (void)t;
    }
    return box;
}

static void confirm_yes(lv_event_t *) {
    void (*fn)(void *) = confirm_ok;
    void *ud = confirm_ud;
    ui_close_overlays();
    if (fn) fn(ud);
}
static void confirm_no(lv_event_t *) { ui_close_overlays(); }

void ui_confirm(const char *title, const char *msg, void (*ok)(void *), void *ud) {
    /* ui_overlay_box() chama ui_close_overlays(), que zera confirm_ok.
     * O callback TEM que ser definido DEPOIS de criar a caixa. */
    lv_obj_t *box = ui_overlay_box(title ? title : "Confirmar", 560, 250);
    confirm_ok = ok;
    confirm_ud = ud;
    lv_obj_t *b = ui_label(box, 4, 40, &font_ui_16, COLOR_TEXT2, msg);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, 520);
    ui_make_btn(box, 24, 176, 230, 42, "Cancelar", COLOR_SURFACE, COLOR_TEXT2, confirm_no, nullptr);
    ui_make_btn(box, 282, 176, 230, 42, "Confirmar", COLOR_ACCENT, COLOR_ON_ACCENT, confirm_yes, nullptr);
}

void ui_toast(const char *msg, bool err) {
    if (!toast_box || !msg) return;
    lv_label_set_text(toast_lbl, msg);
    lv_obj_set_style_bg_color(toast_box, ui_C(err ? COLOR_RED_BG : COLOR_SURFACE), 0);
    lv_obj_set_style_border_color(toast_box, ui_C(err ? COLOR_RED : COLOR_LINE), 0);
    lv_obj_set_style_text_color(toast_lbl, ui_C(err ? 0x9A3B2F : COLOR_TEXT), 0);
    lv_obj_clear_flag(toast_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_box);
    toast_until = millis() + (err ? 5000 : 2800);
}

/* ---------- navegação ---------- */
void ui_go_factory(lv_event_t *) {
    ui_close_overlays();
    lv_scr_load(scr_factory);
}
void ui_go_warehouse(lv_event_t *) {
    ui_close_overlays();
    lv_scr_load(scr_warehouse);
    ui_warehouse_on_show();
}
void ui_go_history(lv_event_t *) {
    ui_close_overlays();
    lv_scr_load(scr_history);
    ui_history_on_show();
}
void ui_go_wifi(lv_event_t *) {
    ui_close_overlays();
    lv_scr_load(scr_wifi);
    ui_wifi_on_show();
}
static void settings_refresh(void);
void ui_go_settings(lv_event_t *) {
    ui_close_overlays();
    lv_scr_load(scr_settings);
    settings_refresh();
}
static void nfc_screen_refresh(void);
void ui_go_nfc(lv_event_t *) {
    ui_close_overlays();
    lv_scr_load(scr_nfc);
    nfc_screen_refresh();
    /* o leitor precisa do estoque para mostrar o rolo da tag */
    if (ui_inv && ui_inv->status != INV_OK && ui_inv->status != INV_LOADING && !inventory_busy())
        inventory_request_refresh();
}

static const char *state_text(PrinterState st, bool cancelled) {
    switch (st) {
    case PRINTER_OFFLINE: return "Offline";
    case PRINTER_IDLE: return "Ociosa";
    case PRINTER_HEATING: return "Aquecendo";
    case PRINTER_PRINTING: return "Imprimindo";
    case PRINTER_PAUSED: return "Pausada";
    case PRINTER_COMPLETE: return cancelled ? "Cancelada" : "Concluída";
    case PRINTER_ERROR: return "Erro";
    case PRINTER_BUSY: return "Ocupada";
    default: return "—";
    }
}

void ui_go_details(lv_event_t *) {
    char buf[768];
    PrinterSnapshot *s = &ui_snap;
    char feeders[256] = {0};
    for (int i = 0; i < 4; i++) {
        const FeederChannel *c = &s->feeder.ch[i];
        char line[64];
        snprintf(line, sizeof(line), "   T%d%s: #%d %s %s\n",
                 i + 1, (i == s->active_extruder) ? " (ativo)" : "",
                 c->spool_id, c->vendor, c->filament_type);
        strncat(feeders, line, sizeof(feeders) - strlen(feeders) - 1);
    }
    snprintf(buf, sizeof(buf),
             "Arquivo: %s\nEstado: %s\nProgresso: %.0f%%\nCamada: %d / %d\n"
             "Bico T%d: %.0f / %.0f °C\nMesa: %.0f / %.0f °C\n"
             "Rolo ativo: %s #%d %s %s  %.0f g\nAlimentadores:\n%sU1: %s:%u",
             s->filename[0] ? s->filename : "—",
             state_text(s->state, s->cancelled),
             (double)(s->progress * 100.0f),
             s->layer, s->layer_total,
             s->active_extruder + 1,
             (double)s->nozzle_c, (double)s->nozzle_target_c,
             (double)s->bed_c, (double)s->bed_target_c,
             s->spool.valid ? s->spool.vendor : "—",
             s->spool.valid ? s->spool.id : 0,
             s->spool.valid ? s->spool.material : "",
             s->spool.valid ? s->spool.name : "",
             s->spool.valid ? (double)s->spool.remaining_g : 0.0,
             feeders,
             s->printer_ip[0] ? s->printer_ip : "—",
             (unsigned)s->printer_port);
    lv_label_set_text(det_body, buf);
    ui_close_overlays();
    lv_scr_load(scr_details);
}

/* ---------- Início: atualização ---------- */
/* Só invalida (redesenha) o que mudou: o Moonraker publica 1x por segundo. */
static void set_txt(lv_obj_t *l, const char *t) {
    if (strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t);
}
static void set_txt_color(lv_obj_t *o, uint32_t rgb) {
    lv_color_t c = ui_C(rgb);
    if (lv_obj_get_style_text_color(o, 0).full != c.full) lv_obj_set_style_text_color(o, c, 0);
}
static void set_bg(lv_obj_t *o, uint32_t rgb) {
    lv_color_t c = ui_C(rgb);
    if (lv_obj_get_style_bg_color(o, 0).full != c.full) lv_obj_set_style_bg_color(o, c, 0);
}
static void fmt_time(char *b, size_t n, float sec) {
    if (sec < 0) { snprintf(b, n, "—"); return; }
    int s = (int)(sec + 0.5f);
    int h = s / 3600, m = (s % 3600) / 60;
    if (h > 0) snprintf(b, n, "%dh %02dmin", h, m);
    else snprintf(b, n, "%dmin", m);
}

static void set_temp(int i, float c, float target, bool have) {
    char v[16], sub[48];
    if (!have) {
        set_txt(t_val[i], "--");
        snprintf(sub, sizeof(sub), i == 2 ? "sem sensor" : "sem dados");
    } else {
        snprintf(v, sizeof(v), "%.0f", (double)c);
        set_txt(t_val[i], v);
        if (target > 0.5f) snprintf(sub, sizeof(sub), "alvo %.0f °C", (double)target);
        else snprintf(sub, sizeof(sub), i == 2 ? "ambiente" : "desligado");
    }
    set_txt(t_sub[i], sub);
    lv_obj_align_to(t_unit[i], t_val[i], LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -8);
}

static void update_spool(int i, uint32_t rgb, bool empty, bool active) {
    static uint32_t cache[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    const uint32_t key = (rgb & 0xFFFFFF) | (empty ? 1u << 24 : 0) | (active ? 1u << 25 : 0);
    if (cache[i] == key) return;
    cache[i] = key;
    lv_obj_set_style_border_color(fil_card[i], ui_C(active ? COLOR_ACCENT : COLOR_LINE), 0);
    lv_obj_set_style_border_width(fil_card[i], active ? 2 : 1, 0);
    if (empty) {
        lv_obj_set_style_bg_color(spool_outer[i], ui_C(COLOR_SURFACE2), 0);
        lv_obj_set_style_border_color(spool_outer[i], ui_C(COLOR_LINE), 0);
        lv_obj_add_flag(spool_arc[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(fil_dot[i], ui_C(COLOR_LINE), 0);
        return;
    }
    lv_color_t c = ui_C(rgb);
    lv_obj_set_style_bg_color(spool_outer[i], c, 0);
    lv_obj_set_style_border_color(spool_outer[i],
        luma(rgb) > 200 ? ui_C(0xD5D8DE) : lv_color_darken(c, LV_OPA_30), 0);
    lv_obj_clear_flag(spool_arc[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_arc_color(spool_arc[i], luma(rgb) > 200 ? ui_C(0xC9CDD4) : lv_color_lighten(c, LV_OPA_40),
                               LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(fil_dot[i], c, 0);
    lv_obj_set_style_border_width(fil_dot[i], luma(rgb) > 200 ? 1 : 0, 0);
}

static void refresh_fans(const PrinterSnapshot *s) {
    const float sp[2] = {s->fan_exhaust, s->fan_circulation};
    for (int i = 0; i < 2; i++) {
        if (!fan_sw[i]) continue;
        char t[16];
        if (sp[i] < 0 || !s->moonraker_ok) {
            lv_obj_add_state(fan_sw[i], LV_STATE_DISABLED);
            lv_obj_clear_state(fan_sw[i], LV_STATE_CHECKED);
            set_txt(fan_pct[i], s->moonraker_ok ? "ausente" : "--");
            continue;
        }
        lv_obj_clear_state(fan_sw[i], LV_STATE_DISABLED);
        snprintf(t, sizeof(t), "%d%%", (int)(sp[i] * 100 + 0.5f));
        set_txt(fan_pct[i], t);
        if ((int32_t)(millis() - fan_hold_until[i]) < 0) continue; /* usuário acabou de mexer */
        if (sp[i] > 0.01f) lv_obj_add_state(fan_sw[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(fan_sw[i], LV_STATE_CHECKED);
    }
}

static void refresh_home(const PrinterSnapshot *s) {
    char tmp[128];
    const bool job = s->state == PRINTER_PRINTING || s->state == PRINTER_PAUSED ||
                     s->state == PRINTER_HEATING || s->state == PRINTER_COMPLETE;
    int pct = (int)(s->progress * 100.0f + 0.5f);
    if (s->state == PRINTER_COMPLETE && !s->cancelled) pct = 100;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (job) {
        snprintf(tmp, sizeof(tmp), "%d", pct);
        set_txt(lbl_pct, tmp);
        lv_obj_clear_flag(lbl_pct_sign, LV_OBJ_FLAG_HIDDEN);
    } else {
        set_txt(lbl_pct, "--");
        lv_obj_add_flag(lbl_pct_sign, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align_to(lbl_pct_sign, lbl_pct, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -8);
    lv_bar_set_value(bar, job ? pct : 0, LV_ANIM_OFF);

    char tr[24];
    fmt_time(tr, sizeof(tr), s->remaining_s);
    switch (s->state) {
    case PRINTER_PRINTING: snprintf(tmp, sizeof(tmp), "Restam %s", tr); break;
    case PRINTER_HEATING: snprintf(tmp, sizeof(tmp), "Aquecendo o bico…"); break;
    case PRINTER_PAUSED: snprintf(tmp, sizeof(tmp), "Pausada · restam %s", tr); break;
    case PRINTER_COMPLETE: snprintf(tmp, sizeof(tmp), s->cancelled ? "Impressão cancelada" : "Impressão concluída"); break;
    case PRINTER_IDLE: snprintf(tmp, sizeof(tmp), "Pronta para imprimir"); break;
    case PRINTER_ERROR: snprintf(tmp, sizeof(tmp), "Erro na U1"); break;
    case PRINTER_OFFLINE: snprintf(tmp, sizeof(tmp), "Sem conexão"); break;
    default: snprintf(tmp, sizeof(tmp), "%s", state_text(s->state, s->cancelled)); break;
    }
    set_txt(lbl_restam, tmp);
    if (job && s->layer_total > 0) snprintf(tmp, sizeof(tmp), "Camada %d / %d", s->layer, s->layer_total);
    else tmp[0] = 0;
    set_txt(lbl_layer, tmp);
    set_txt(lbl_file, s->filename[0] ? s->filename : "Nenhum arquivo");

    uint32_t dot = COLOR_MUTED;
    if (s->state == PRINTER_PRINTING || s->state == PRINTER_HEATING) dot = COLOR_ACCENT;
    else if (s->state == PRINTER_PAUSED || s->state == PRINTER_BUSY) dot = COLOR_AMBER;
    else if (s->state == PRINTER_ERROR) dot = COLOR_RED;
    else if (s->state == PRINTER_IDLE || s->state == PRINTER_COMPLETE) dot = COLOR_GREEN;
    set_bg(state_dot, dot);
    if (s->state == PRINTER_OFFLINE)
        snprintf(tmp, sizeof(tmp), "%s", s->status_msg[0] ? s->status_msg :
                 s->discovering ? "Procurando a U1…" : "U1 offline");
    else
        snprintf(tmp, sizeof(tmp), "%s · U1 %s", state_text(s->state, s->cancelled),
                 s->printer_ip[0] ? s->printer_ip : "—");
    set_txt(lbl_state, tmp);

    /* filamentos T1-T4 */
    int ae = s->active_extruder;
    if (ae < 0 || ae > 3) ae = 0;
    for (int i = 0; i < 4; i++) {
        const FeederChannel *c = &s->feeder.ch[i];
        const WarehouseSpool *w = (ui_inv && c->spool_id > 0) ? inventory_find(ui_inv, c->spool_id) : nullptr;
        const char *m = (w && w->info.material[0]) ? w->info.material : c->filament_type;
        float g = (w && w->info.remaining_g >= 0) ? w->info.remaining_g : -1;
        if (s->spool.valid && s->spool.id == c->spool_id && s->spool.remaining_g >= 0) g = s->spool.remaining_g;
        const char *cx = (w && w->info.color_hex[0]) ? w->info.color_hex : c->color_hex;
        const bool empty = c->spool_id <= 0 && !c->filament_detected && !m[0] && !cx[0];
        set_txt(fil_mat[i], empty ? "vazio" : (m[0] ? m : "—"));
        if (g >= 0) snprintf(tmp, sizeof(tmp), "%.0f g", (double)g);
        else snprintf(tmp, sizeof(tmp), empty ? "" : "— g");
        set_txt(fil_g[i], tmp);
        set_txt_color(fil_g[i], g >= 0 && g < SPOOL_LOW_GRAMS ? COLOR_RED : COLOR_TEXT2);
        if (c->spool_id > 0) snprintf(tmp, sizeof(tmp), "#%d", c->spool_id);
        else tmp[0] = 0;
        set_txt(fil_id[i], tmp);
        uint32_t rgb = cx[0] ? ui_parse_hex(cx) : 0x9AA1AD;
        update_spool(i, rgb, empty, s->moonraker_ok && i == ae);
    }

    /* temperaturas */
    snprintf(tmp, sizeof(tmp), "BICO T%d", ae + 1);
    set_txt(t_title[0], tmp);
    set_temp(0, s->nozzle_c, s->nozzle_target_c, s->moonraker_ok);
    set_temp(1, s->bed_c, s->bed_target_c, s->moonraker_ok);
    set_temp(2, s->chamber_c, s->chamber_target_c, s->moonraker_ok && s->chamber_c > -999);
    refresh_fans(s);
}

static void chart_push(lv_obj_t *chart, lv_chart_series_t *ser, float v, bool have) {
    lv_chart_set_next_value(chart, ser, have ? (lv_coord_t)(v * 10) : LV_CHART_POINT_NONE);
    lv_coord_t lo = 32767, hi = -32768;
    const uint16_t n = lv_chart_get_point_count(chart);
    lv_coord_t *a = lv_chart_get_y_array(chart, ser);
    for (uint16_t i = 0; i < n; i++) {
        if (a[i] == LV_CHART_POINT_NONE) continue;
        if (a[i] < lo) lo = a[i];
        if (a[i] > hi) hi = a[i];
    }
    if (lo > hi) { lo = 0; hi = 10; }
    lv_coord_t pad = (hi - lo) / 4 + 50; /* ±5 °C no mínimo: ruído não vira serrote */
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, lo - pad, hi + pad);
    lv_chart_refresh(chart);
}

static void tick_home(void) {
    const lv_img_dsc_t *d = nullptr;
    if (thumb_get(&d, &thumb_seq)) {
        if (d) {
            lv_img_cache_invalidate_src(d);
            lv_img_set_src(thumb_img, d);
            lv_obj_clear_flag(thumb_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(thumb_ph, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(thumb_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(thumb_ph, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (millis() - last_sample_ms >= 3000) {
        last_sample_ms = millis();
        chart_push(chart_noz, ser_noz, ui_snap.nozzle_c, ui_snap.moonraker_ok);
        chart_push(chart_cham, ser_cham, ui_snap.chamber_c, ui_snap.moonraker_ok && ui_snap.chamber_c > -999);
    }
}

static void fan_toggled(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    /* ventilador 1 = exaustão, 2 = circulação (mesma codificação do Controle) */
    if (!control_submit(ControlOp::FanSet, (i + 1) * 1000 + (on ? 100 : 0))) {
        ui_toast("Outra operação está em andamento. Tente de novo.", true);
        if (on) lv_obj_clear_state(sw, LV_STATE_CHECKED);
        else lv_obj_add_state(sw, LV_STATE_CHECKED);
        return;
    }
    fan_hold_until[i] = millis() + 4000;
    ui_toast(on ? (i ? "Ligando a circulação…" : "Ligando a exaustão…")
                : (i ? "Desligando a circulação…" : "Desligando a exaustão…"), false);
}

static void on_timer(lv_timer_t *) {
    bool dirty = false;
    uint32_t seq = ui_mr_seq;
    if (moonraker_copy(&ui_snap, &seq) || seq != ui_mr_seq) {
        ui_mr_seq = seq;
        dirty = true;
    }
    if (ui_inv) {
        uint32_t iseq = ui_inv_seq;
        inventory_copy(ui_inv, &iseq);
        if (iseq != ui_inv_seq) {
            ui_inv_seq = iseq;
            dirty = true;
        }
    }
    if (dirty) refresh_home(&ui_snap);
    tick_home();

    /* ação do estoque: toast */
    uint32_t aseq = ui_act_seq;
    if (inventory_copy_action(&ui_act, &aseq) && aseq != ui_act_seq) {
        ui_act_seq = aseq;
        if (ui_act.state == AR_OK && ui_act.message[0]) ui_toast(ui_act.message, false);
        else if (ui_act.state == AR_ERR && ui_act.message[0]) ui_toast(ui_act.message, true);
    }
    ui_warehouse_tick();
    ui_wifi_tick();
    control_ui_tick();
    if (dirty && lv_scr_act() == scr_settings) settings_refresh();
    if (lv_scr_act() == scr_nfc) nfc_screen_refresh();

    if (toast_box && toast_until && millis() > toast_until) {
        lv_obj_add_flag(toast_box, LV_OBJ_FLAG_HIDDEN);
        toast_until = 0;
    }
}

/* ---------- Início: construção ---------- */
static lv_obj_t *circle(lv_obj_t *parent, int cx, int cy, int d, uint32_t bg) {
    lv_obj_t *o = lv_obj_create(parent);
    plain(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, ui_C(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *spark(lv_obj_t *parent, int x, int y, lv_chart_series_t **ser) {
    lv_obj_t *ch = lv_chart_create(parent);
    plain(ch);
    lv_obj_set_pos(ch, x, y);
    lv_obj_set_size(ch, 142, 52);
    lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(ch, 0, 0);
    lv_chart_set_point_count(ch, 40);
    lv_chart_set_update_mode(ch, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_line_width(ch, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(ch, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(ch, LV_OBJ_FLAG_CLICKABLE);
    *ser = lv_chart_add_series(ch, ui_C(COLOR_ACCENT), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(ch, *ser, LV_CHART_POINT_NONE);
    return ch;
}

static void build_home(void) {
    lv_obj_t *c = ui_make_screen(&scr_factory, NAV_HOME);

    /* progresso (toque = detalhes) */
    lv_obj_t *pc = ui_make_card(c, 0, 0, 352, 228);
    lv_obj_add_flag(pc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pc, ui_go_details, LV_EVENT_CLICKED, nullptr);
    thumb_box = lv_obj_create(pc);
    plain(thumb_box);
    lv_obj_set_pos(thumb_box, 16, 16);
    lv_obj_set_size(thumb_box, THUMB_W + 8, THUMB_H + 8);
    lv_obj_set_style_radius(thumb_box, 12, 0);
    lv_obj_set_style_bg_color(thumb_box, ui_C(THUMB_BG), 0);
    lv_obj_set_style_bg_opa(thumb_box, LV_OPA_COVER, 0);
    lv_obj_clear_flag(thumb_box, LV_OBJ_FLAG_CLICKABLE);
    thumb_img = lv_img_create(thumb_box);
    lv_obj_set_pos(thumb_img, 4, 4);
    lv_obj_add_flag(thumb_img, LV_OBJ_FLAG_HIDDEN);
    thumb_ph = lv_label_create(thumb_box);
    ui_style_label(thumb_ph, &font_icon_26, 0xB4BAC4);
    lv_label_set_text(thumb_ph, ICON_CUBE);
    lv_obj_center(thumb_ph);

    lbl_pct = ui_label(pc, 176, 16, &font_ui_48, COLOR_TEXT, "--");
    lbl_pct_sign = ui_label(pc, 240, 40, &font_ui_28, COLOR_TEXT2, "%");
    lbl_restam = ui_label(pc, 176, 80, &font_ui_16, COLOR_TEXT2, "");
    lv_label_set_long_mode(lbl_restam, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_restam, 164);
    bar = lv_bar_create(pc);
    lv_obj_set_pos(bar, 176, 112);
    lv_obj_set_size(bar, 156, 10);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, ui_C(0xE5E8EE), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, ui_C(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 5, 0);
    lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lbl_layer = ui_label(pc, 176, 132, &font_ui_14, COLOR_MUTED, "");
    lbl_file = ui_label(pc, 16, 170, &font_ui_16, COLOR_TEXT, "");
    lv_label_set_long_mode(lbl_file, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_file, 320);
    state_dot = circle(pc, 21, 205, 10, COLOR_MUTED);
    lbl_state = ui_label(pc, 34, 196, &font_ui_14, COLOR_TEXT2, "");
    lv_label_set_long_mode(lbl_state, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_state, 300);

    /* filamentos */
    lv_obj_t *fc = ui_make_card(c, 364, 0, 336, 228);
    lv_obj_t *ft = ui_label(fc, 0, 12, &font_ui_14, COLOR_TEXT2, "FILAMENTOS");
    lv_obj_set_style_text_letter_space(ft, 1, 0);
    lv_obj_align(ft, LV_ALIGN_TOP_MID, 0, 10);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *s = lv_obj_create(fc);
        plain(s);
        fil_card[i] = s;
        lv_obj_set_pos(s, 10 + i * 80, 36);
        lv_obj_set_size(s, 74, 180);
        lv_obj_set_style_radius(s, 12, 0);
        lv_obj_set_style_bg_color(s, ui_C(COLOR_SURFACE2), 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s, ui_C(COLOR_LINE), 0);
        lv_obj_set_style_border_width(s, 1, 0);
        lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
        fil_dot[i] = circle(s, 15, 17, 12, COLOR_LINE);
        lv_obj_set_style_border_color(fil_dot[i], ui_C(0xC9CDD4), 0);
        char t[4];
        snprintf(t, sizeof(t), "T%d", i + 1);
        ui_label(s, 26, 8, &font_ui_16, COLOR_TEXT, t);
        fil_mat[i] = ui_label(s, 8, 32, &font_ui_14, COLOR_TEXT, "—");
        lv_label_set_long_mode(fil_mat[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(fil_mat[i], 62);
        fil_g[i] = ui_label(s, 8, 50, &font_ui_14, COLOR_TEXT2, "");
        /* rolo vetorial: disco na cor do filamento, brilho em arco, cubo e furo */
        spool_outer[i] = circle(s, 36, 116, 62, 0x9AA1AD);
        lv_obj_set_style_border_width(spool_outer[i], 3, 0);
        spool_arc[i] = lv_arc_create(s);
        plain(spool_arc[i]);
        lv_obj_set_size(spool_arc[i], 46, 46);
        lv_obj_set_pos(spool_arc[i], 36 - 23, 116 - 23);
        lv_arc_set_bg_angles(spool_arc[i], 0, 0);
        lv_arc_set_angles(spool_arc[i], 200, 290);
        lv_obj_set_style_arc_width(spool_arc[i], 3, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(spool_arc[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(spool_arc[i], true, LV_PART_INDICATOR);
        lv_obj_remove_style(spool_arc[i], nullptr, LV_PART_KNOB);
        lv_obj_clear_flag(spool_arc[i], LV_OBJ_FLAG_CLICKABLE);
        spool_hub[i] = circle(s, 36, 116, 26, 0xF4F5F7);
        lv_obj_set_style_border_width(spool_hub[i], 2, 0);
        lv_obj_set_style_border_color(spool_hub[i], ui_C(0xD5D8DE), 0);
        spool_hole[i] = circle(s, 36, 116, 9, 0xB9BEC7);
        fil_id[i] = ui_label(s, 0, 154, &font_ui_14, COLOR_MUTED, "");
        lv_obj_set_width(fil_id[i], 74);
        lv_obj_set_style_text_align(fil_id[i], LV_TEXT_ALIGN_CENTER, 0);
    }

    /* temperaturas + ventiladores */
    lv_obj_t *bc = ui_make_card(c, 0, 240, 700, 216);
    static const char *const titles[3] = {"BICO T1", "MESA", "CÂMARA"};
    static const uint32_t vcol[3] = {COLOR_TEMP, COLOR_GREEN, COLOR_TEMP};
    for (int i = 0; i < 4; i++) {
        if (i) {
            lv_obj_t *dv = lv_obj_create(bc);
            plain(dv);
            lv_obj_set_pos(dv, i * 175, 24);
            lv_obj_set_size(dv, 1, 168);
            lv_obj_set_style_bg_color(dv, ui_C(COLOR_LINE), 0);
            lv_obj_set_style_bg_opa(dv, LV_OPA_COVER, 0);
        }
        if (i == 3) break;
        const int x0 = i * 175 + 20;
        t_title[i] = ui_label(bc, x0, 18, &font_ui_14, COLOR_TEXT2, titles[i]);
        lv_obj_set_style_text_letter_space(t_title[i], 1, 0);
        t_val[i] = ui_label(bc, x0, 40, &font_ui_48, vcol[i], "--");
        t_unit[i] = ui_label(bc, x0 + 80, 64, &font_ui_20, vcol[i], "°C");
        t_sub[i] = ui_label(bc, x0, 102, &font_ui_14, COLOR_MUTED, "");
    }
    chart_noz = spark(bc, 14, 136, &ser_noz);
    chart_cham = spark(bc, 2 * 175 + 14, 136, &ser_cham);
    lv_obj_t *th = ui_label(bc, 175 + 62, 132, &font_icon_26, COLOR_GREEN, ICON_THERMO);
    (void)th;
    lv_obj_t *base = lv_obj_create(bc);
    plain(base);
    lv_obj_set_pos(base, 175 + 52, 172);
    lv_obj_set_size(base, 34, 2);
    lv_obj_set_style_bg_color(base, ui_C(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);

    const int fx = 3 * 175 + 9;
    lv_obj_t *vt = ui_label(bc, fx + 8, 18, &font_ui_14, COLOR_TEXT2, "VENTILADORES");
    lv_obj_set_style_text_letter_space(vt, 1, 0);
    static const char *const fnames[2] = {"Exaustão", "Circulação"};
    static const char *const ficons[2] = {ICON_WIND, ICON_SYNC};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *row = lv_obj_create(bc);
        plain(row);
        lv_obj_set_pos(row, fx, 46 + i * 74);
        lv_obj_set_size(row, 156, 62);
        lv_obj_set_style_radius(row, 31, 0);
        lv_obj_set_style_bg_color(row, ui_C(COLOR_SURFACE2), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, ui_C(COLOR_LINE), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *ic = circle(row, 20, 31, 30, COLOR_SURFACE);
        lv_obj_t *il = lv_label_create(ic);
        ui_style_label(il, &font_icon_18, COLOR_TEXT2);
        lv_label_set_text(il, ficons[i]);
        lv_obj_center(il);
        lv_obj_t *nl = ui_label(row, 40, 12, &font_ui_14, COLOR_TEXT, fnames[i]);
        (void)nl;
        fan_pct[i] = ui_label(row, 40, 32, &font_ui_14, COLOR_MUTED, "--");
        fan_sw[i] = lv_switch_create(row);
        lv_obj_set_size(fan_sw[i], 36, 20);
        lv_obj_set_pos(fan_sw[i], 114, 21);
        lv_obj_set_style_bg_color(fan_sw[i], ui_C(0xD9DCE2), 0);
        lv_obj_set_style_bg_color(fan_sw[i], ui_C(COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(fan_sw[i], fan_toggled, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
        lv_obj_add_state(fan_sw[i], LV_STATE_DISABLED);
    }
    lv_obj_t *more = ui_label(bc, fx + 8, 190, &font_ui_14, COLOR_MUTED, "Mais em Controle");
    (void)more;
}

/* ---------- detalhes ---------- */
static void build_details(void) {
    lv_obj_t *c = ui_make_screen(&scr_details, NAV_HOME);
    ui_page_title(c, "Detalhes da impressão", ui_go_factory);
    lv_obj_t *card = ui_make_panel(c, 0, 56, CONTENT_W, CONTENT_H - 56);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    det_body = ui_label(card, 8, 4, &font_ui_16, COLOR_TEXT, "");
    lv_label_set_long_mode(det_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(det_body, CONTENT_W - 48);
    lv_obj_set_style_text_line_space(det_body, 6, 0);
}

/* ---------- Ajustes ---------- */
static lv_obj_t *settings_card(lv_obj_t *c, int x, int y, int w, int h, const char *icon, const char *title) {
    lv_obj_t *k = ui_make_card(c, x, y, w, h);
    lv_obj_t *i = ui_label(k, 18, 18, &font_icon_18, COLOR_ACCENT, icon);
    (void)i;
    ui_label(k, 50, 15, &font_ui_20, COLOR_TEXT, title);
    return k;
}

static lv_obj_t *settings_text(lv_obj_t *k, int y, int w) {
    lv_obj_t *l = ui_label(k, 18, y, &font_ui_14, COLOR_TEXT2, "");
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, w - 36);
    lv_obj_set_style_text_line_space(l, 4, 0);
    return l;
}

static void settings_refresh(void) {
    if (!set_wifi) return;
    char ssid[WIFI_SSID_LEN] = {0}, ip[20] = {0}, t[200];
    wifi_current(ssid, sizeof(ssid), ip, sizeof(ip));
    if (ssid[0]) snprintf(t, sizeof(t), "Rede: %s\nIP da tela: %s%s", ssid, ip[0] ? ip : "—",
                          wifi_is_portal() ? "\nPortal U1-DISPLAY ativo" : "");
    else snprintf(t, sizeof(t), "Desconectado.\nPortal de reserva: U1-DISPLAY");
    lv_label_set_text(set_wifi, t);
    const PrinterSnapshot *s = &ui_snap;
    snprintf(t, sizeof(t), "Endereço: %s:%u\nEstado: %s\nMoonraker: %s",
             s->printer_ip[0] ? s->printer_ip : "—", (unsigned)s->printer_port,
             state_text(s->state, s->cancelled),
             s->moonraker_ok ? "conectado" : s->discovering ? "procurando…" : "sem conexão");
    lv_label_set_text(set_u1, t);
    snprintf(t, sizeof(t), "U1 Factory Display v0.6.0\nRAM interna livre: %u KB\nPSRAM livre: %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    lv_label_set_text(set_about, t);
}

static void build_settings(void) {
    lv_obj_t *c = ui_make_screen(&scr_settings, NAV_SETTINGS);
    ui_page_title(c, "Ajustes", nullptr);
    lv_obj_t *k = settings_card(c, 0, 56, 344, 192, ICON_WIFI, "Wi-Fi");
    set_wifi = settings_text(k, 54, 344);
    ui_make_btn(k, 18, 134, 308, 42, "Redes Wi-Fi", COLOR_SURFACE, COLOR_ACCENT, ui_go_wifi, nullptr);

    k = settings_card(c, 356, 56, 344, 192, ICON_PRINT, "Impressora U1");
    set_u1 = settings_text(k, 54, 344);
    ui_make_btn(k, 18, 134, 308, 42, "Procurar a U1 de novo", COLOR_SURFACE, COLOR_ACCENT, [](lv_event_t *) {
        moonraker_force_discover();
        ui_toast("Procurando a U1 na rede…", false);
    }, nullptr);

    k = settings_card(c, 0, 260, 344, 196, ICON_BOOK, "Diário");
    lv_obj_t *d = settings_text(k, 54, 344);
    lv_label_set_text(d, "Leituras NFC, associações, trocas de rolo e problemas registrados nesta tela.");
    ui_make_btn(k, 18, 138, 308, 42, "Abrir o diário", COLOR_SURFACE, COLOR_ACCENT, ui_go_history, nullptr);

    k = settings_card(c, 356, 260, 344, 196, ICON_INFO, "Sobre");
    set_about = settings_text(k, 54, 344);
    ui_make_btn(k, 18, 138, 308, 42, "Detalhes da impressão", COLOR_SURFACE, COLOR_ACCENT, ui_go_details, nullptr);
}

/* ---------- NFC ---------- */
static void nfc_screen_refresh(void) {
    if (!nfc_reader) return;
    const char *t = nfc_present() ? "Leitor PN532 conectado." : "Aguardando o leitor PN532 (UART).";
    if (strcmp(lv_label_get_text(nfc_reader), t)) lv_label_set_text(nfc_reader, t);
}

static void build_nfc(void) {
    lv_obj_t *c = ui_make_screen(&scr_nfc, NAV_NFC);
    ui_page_title(c, "NFC", nullptr);
    lv_obj_t *k = ui_make_card(c, 0, 56, CONTENT_W, 220);
    ui_label(k, 24, 22, &font_icon_26, COLOR_ACCENT, ICON_TAG);
    ui_label(k, 66, 22, &font_ui_20, COLOR_TEXT, "Tags dos rolos");
    nfc_reader = ui_label(k, 66, 50, &font_ui_14, COLOR_TEXT2, "");
    lv_obj_t *d = ui_label(k, 24, 84, &font_ui_16, COLOR_TEXT2,
        "Aproxime a tag para ver o rolo associado no Spoolman, ou associe uma tag nova a um rolo.");
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(d, CONTENT_W - 48);
    ui_make_btn(k, 24, 150, 310, 48, "Ler tag", COLOR_ACCENT, COLOR_ON_ACCENT,
                [](lv_event_t *) { ui_nfc_open(true); }, nullptr);
    ui_make_btn(k, 352, 150, 324, 48, "Associar tag a um rolo", COLOR_SURFACE, COLOR_ACCENT,
                [](lv_event_t *) { ui_nfc_open(false); }, nullptr);

    lv_obj_t *k2 = ui_make_card(c, 0, 288, CONTENT_W, 168);
    ui_label(k2, 24, 20, &font_icon_18, COLOR_ACCENT, ICON_CUBE);
    ui_label(k2, 56, 18, &font_ui_20, COLOR_TEXT, "Dica");
    lv_obj_t *t = ui_label(k2, 24, 54, &font_ui_14, COLOR_TEXT2,
        "No Armazém, abra a ficha de um rolo e toque NFC para associar a tag direto a ele. "
        "Gravar NDEF e inserir no alimentador ficam em Avançado, dentro do leitor.");
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, CONTENT_W - 48);
    lv_obj_set_style_text_line_space(t, 4, 0);
    ui_make_btn(k2, 24, 110, 220, 42, "Abrir o Armazém", COLOR_SURFACE, COLOR_ACCENT, ui_go_warehouse, nullptr);
}

void ui_init(void) {
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(disp, ui_C(COLOR_ACCENT), ui_C(COLOR_AMBER), false, &font_ui_14);
    lv_disp_set_theme(disp, th);

    stock_filter_clear(&ui_filter);
    snapshot_clear(&ui_snap);
    ui_inv = (InventorySnapshot *)heap_caps_calloc(1, sizeof(InventorySnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ui_inv) ui_inv = (InventorySnapshot *)calloc(1, sizeof(InventorySnapshot));
    ui_cat = (CatalogSnapshot *)heap_caps_calloc(1, sizeof(CatalogSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ui_cat) ui_cat = (CatalogSnapshot *)calloc(1, sizeof(CatalogSnapshot));

    build_home();
    build_details();
    build_settings();
    build_nfc();

    toast_box = ui_make_card(lv_layer_top(), 190, 418, 420, 46);
    lv_obj_set_style_radius(toast_box, 23, 0);
    toast_lbl = ui_label(toast_box, 0, 0, &font_ui_14, COLOR_TEXT, "");
    lv_label_set_long_mode(toast_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(toast_lbl, 392);
    lv_obj_set_style_text_align(toast_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(toast_lbl);
    lv_obj_add_flag(toast_box, LV_OBJ_FLAG_HIDDEN);

    ui_warehouse_build(ui_make_screen(&scr_warehouse, NAV_STOCK));
    ui_history_build(ui_make_screen(&scr_history, NAV_SETTINGS));
    ui_wifi_build(ui_make_screen(&scr_wifi, NAV_SETTINGS));
    control_ui_build();

    lv_scr_load(scr_factory);
    lv_timer_create(on_timer, 200, nullptr);
    refresh_home(&ui_snap);
}
