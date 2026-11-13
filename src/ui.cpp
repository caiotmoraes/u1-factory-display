#include "ui_priv.h"

#ifndef LV_COLOR_MAKE
#define LV_COLOR_MAKE(r,g,b) lv_color_make(r,g,b)
#endif

PrinterSnapshot ui_snap;
InventorySnapshot *ui_inv = nullptr;
CatalogSnapshot *ui_cat = nullptr;
ActionResult ui_act;
uint32_t ui_inv_seq = 0, ui_cat_seq = 0, ui_act_seq = 0, ui_mr_seq = 0;
lv_obj_t *scr_factory, *scr_details, *scr_warehouse, *scr_history, *scr_wifi;
StockFilter ui_filter;
bool ui_compact = false;

static lv_obj_t *scene;
static lv_obj_t *img_bg, *img_toolhead, *img_carriage, *img_part, *img_part_clip;
static lv_obj_t *night, *error_veil;
static lv_obj_t *bay_hl[4], *bay_num_bg[4], *bay_num[4], *bay_spool[4], *bay_plate[4], *bay_mat[4];
static uint8_t *bay_recolor[4];
static lv_img_dsc_t bay_dsc[4];
static uint32_t bay_rgb_cache[4];
static int bay_cache_valid[4];
static lv_obj_t *hud, *lbl_state, *lbl_sub, *bar, *lbl_bar, *lbl_camada, *lbl_restam;
static lv_obj_t *lbl_bico, *lbl_mesa, *lbl_spool, *lbl_spool_title, *lbl_alert, *lbl_hint, *lbl_ip;
static lv_obj_t *swatch;
static lv_obj_t *lbl_f[4], *sw_f[4];
static lv_obj_t *card_bico, *card_mesa;
static lv_obj_t *det_title, *det_body;
static lv_obj_t *confetti[16];
static lv_obj_t *toast_box, *toast_lbl;
static uint32_t toast_until = 0;

static int th_x = U1_TOOLHEAD_X;

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
    if (!s || !s[0]) return 0x59ECC5;
    unsigned v = 0;
    sscanf(s, "%x", &v);
    return v & 0xFFFFFFu;
}

void ui_style_label(lv_obj_t *o, const lv_font_t *font, uint32_t color) {
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, ui_C(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
}

lv_obj_t *ui_make_panel(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, ui_C(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_90, 0);
    lv_obj_set_style_border_color(p, ui_C(COLOR_MINT), 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_all(p, 8, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

lv_obj_t *ui_make_btn(lv_obj_t *parent, int x, int y, int w, int h,
                      const char *txt, uint32_t bg, uint32_t fg,
                      lv_event_cb_t cb, void *ud) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_bg_color(b, ui_C(bg), 0);
    lv_obj_set_style_bg_color(b, ui_C(COLOR_GOLD), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, ui_C(COLOR_MINT), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 2, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt ? txt : "");
    ui_style_label(l, &font_press_start_12, fg);
    lv_obj_center(l);
    return b;
}

static void tab_clicked(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    if (id == 0) ui_go_factory(e);
    else if (id == 1) ui_go_warehouse(e);
    else if (id == 2) ui_go_history(e);
    else ui_go_wifi(e);
}

void ui_add_tabs(lv_obj_t *parent, int active) {
    struct { int x, w; const char *name; } t[4] = {
        {TAB_WIFI_X, TAB_WIFI_W, "WIFI"},
        {TAB_FAB_X,  TAB_FAB_W,  "FABRICA"},
        {TAB_ARM_X,  TAB_ARM_W,  "ARMAZEM"},
        {TAB_DIA_X,  TAB_DIA_W,  "DIARIO"},
    };
    /* active: 0 factory, 1 warehouse, 2 history, 3 wifi — visual index 1,2,3,0 */
    int vis[4] = {3, 0, 1, 2}; /* tab i corresponds to vis */
    (void)vis;
    int active_i[4] = {3, 0, 1, 2};
    /* Map: we pass active 0=fab 1=arm 2=dia 3=wifi, tab visual order WIFI,FAB,ARM,DIA */
    int highlight[4] = {3, 0, 1, 2};
    for (int i = 0; i < 4; i++) {
        /* i: 0 WIFI (active==3), 1 FAB (0), 2 ARM (1), 3 DIA (2) */
        int act_id = (i == 0) ? 3 : (i - 1);
        bool on = (act_id == active);
        lv_obj_t *b = lv_btn_create(parent);
        lv_obj_set_pos(b, t[i].x, TAB_Y);
        lv_obj_set_size(b, t[i].w, TAB_H);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_bg_color(b, ui_C(on ? COLOR_MINT : COLOR_CARD), 0);
        lv_obj_set_style_border_color(b, ui_C(COLOR_MINT), 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, tab_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)act_id);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, t[i].name);
        ui_style_label(l, &font_press_start_12, on ? 0x081725 : COLOR_MINT);
        lv_obj_center(l);
    }
    (void)highlight; (void)active_i;
}

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
        lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, 0);
        lv_obj_set_size(kb, SCREEN_W, 180);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
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
    lv_obj_set_size(overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(overlay, ui_C(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_add_event_cb(overlay, overlay_swallow, LV_EVENT_CLICKED, nullptr);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *box = ui_make_panel(overlay, (SCREEN_W - w) / 2, (SCREEN_H - h) / 2 - 10, w, h);
    lv_obj_add_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    if (title && title[0]) {
        lv_obj_t *t = lv_label_create(box);
        ui_style_label(t, &font_press_start_16, COLOR_MINT);
        lv_label_set_text(t, title);
        lv_obj_set_pos(t, 4, 2);
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
     * O callback TEM que ser definido DEPOIS de criar a caixa — senão
     * CONFIRMAR fecha o diálogo e não chama nada (Ativar rolo nunca
     * chegava à U1). */
    lv_obj_t *box = ui_overlay_box(title ? title : "CONFIRMAR", 560, 240);
    confirm_ok = ok;
    confirm_ud = ud;
    lv_obj_t *b = lv_label_create(box);
    ui_style_label(b, &lv_font_montserrat_14, COLOR_CREAM);
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, 530);
    lv_obj_set_pos(b, 8, 40);
    lv_label_set_text(b, msg ? msg : "");
    ui_make_btn(box, 40, 180, 200, 36, "CANCELAR", COLOR_CARD, COLOR_MUTED, confirm_no, nullptr);
    ui_make_btn(box, 300, 180, 200, 36, "CONFIRMAR", COLOR_MINT, 0x081725, confirm_yes, nullptr);
}

void ui_toast(const char *msg, bool err) {
    if (!toast_box || !msg) return;
    lv_label_set_text(toast_lbl, msg);
    lv_obj_set_style_bg_color(toast_box, ui_C(err ? 0x4A1E16 : COLOR_CARD), 0);
    lv_obj_set_style_border_color(toast_box, ui_C(err ? COLOR_ALERT : COLOR_MINT), 0);
    lv_obj_clear_flag(toast_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_box);
    toast_until = millis() + (err ? 5000 : 2800);
}

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
void ui_go_details(lv_event_t *e) {
    if (lv_event_get_target(e) != scr_factory && lv_scr_act() != scr_factory) return;
    if (lv_scr_act() == scr_details) { lv_scr_load(scr_factory); return; }
    /* preenche corpo */
    char buf[768];
    PrinterSnapshot *s = &ui_snap;
    char feeders[256] = {0};
    for (int i = 0; i < 4; i++) {
        const FeederChannel *c = &s->feeder.ch[i];
        char line[64];
        snprintf(line, sizeof(line), "  F%d%s: #%d %s %s\n",
                 i + 1, (i == s->active_extruder) ? "*" : "",
                 c->spool_id, c->vendor, c->filament_type);
        strncat(feeders, line, sizeof(feeders) - strlen(feeders) - 1);
    }
    snprintf(buf, sizeof(buf),
             "Arquivo: %s\nEstado: %s\nProgresso: %.0f%%\nCamada: %d / %d\n"
             "Bico %d: %.0f / %.0f C\nMesa: %.0f / %.0f C\n"
             "Rolo ativo: %s #%d %s %s  %.0f g\nFeeders:\n%sU1: %s:%u",
             s->filename[0] ? s->filename : "--",
             printer_state_label_pt(s->state),
             (double)(s->progress * 100.0f),
             s->layer, s->layer_total,
             s->active_extruder + 1,
             (double)s->nozzle_c, (double)s->nozzle_target_c,
             (double)s->bed_c, (double)s->bed_target_c,
             s->spool.valid ? s->spool.vendor : "--",
             s->spool.valid ? s->spool.id : 0,
             s->spool.valid ? s->spool.material : "",
             s->spool.valid ? s->spool.name : "",
             s->spool.valid ? (double)s->spool.remaining_g : 0.0,
             feeders,
             s->printer_ip[0] ? s->printer_ip : "--",
             (unsigned)s->printer_port);
    lv_label_set_text(det_body, buf);
    lv_scr_load(scr_details);
}

static uint32_t active_filament_rgb(const PrinterSnapshot *s) {
    if (s->spool.valid && s->spool.color_hex[0])
        return ui_parse_hex(s->spool.color_hex);
    int ae = s->active_extruder;
    if (ae >= 0 && ae < 4 && s->feeder.ch[ae].color_hex[0])
        return ui_parse_hex(s->feeder.ch[ae].color_hex);
    return 0x59ECC5;
}

/* Recolore pixels com alpha=254 (marcador de filamento).
 * Cabelo/sombra marrom ou quase-preto NÃO é recolorido: o RGB original
 * permanece e o alpha vira 255 (opaco). Magenta/rosa (B relativamente
 * alto) é a bobina e recebe a cor do filamento ativo. */
static void recolor_into(uint8_t *dst, const uint8_t *src, int npix, uint16_t c565) {
    memcpy(dst, src, (size_t)npix * 3);
    uint8_t *p = dst;
    for (int i = 0; i < npix; i++, p += 3) {
        if (p[2] != 254) continue;
        uint16_t pix = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
        int R = (int)((pix >> 11) & 0x1F) << 3;
        int G = (int)((pix >> 5) & 0x3F) << 2;
        int B = (int)(pix & 0x1F) << 3;
        /* só cabelo/vermelho-marrom (B < 0.2 R). Sombra magenta do
         * enrolamento (B relativamente alto) deve ser recolorida. */
        if (R > 40 && (B * 5) < R && G <= R) {
            p[2] = 255;
            continue;
        }
        p[0] = (uint8_t)(c565 & 0xFF);
        p[1] = (uint8_t)(c565 >> 8);
        p[2] = 255;
    }
}

static uint16_t rgb_to_565(uint32_t rgb) {
    return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x1F));
}

static const lv_img_dsc_t *bay_colored(int i, uint32_t rgb) {
    if (i < 0 || i >= 4 || !bay_recolor[i]) return &img_u1_spool;
    if (bay_cache_valid[i] && bay_rgb_cache[i] == rgb) return &bay_dsc[i];
    recolor_into(bay_recolor[i], img_u1_spool.data,
                 img_u1_spool.header.w * img_u1_spool.header.h, rgb_to_565(rgb));
    bay_dsc[i] = img_u1_spool;
    bay_dsc[i].data = bay_recolor[i];
    bay_rgb_cache[i] = rgb;
    bay_cache_valid[i] = 1;
    return &bay_dsc[i];
}

static const lv_coord_t k_bay_x[4] = {U1_SLOT0_X, U1_SLOT1_X, U1_SLOT2_X, U1_SLOT3_X};
static const lv_coord_t k_bay_w[4] = {U1_SLOT0_W, U1_SLOT1_W, U1_SLOT2_W, U1_SLOT3_W};

static void update_bay(int i, const FeederChannel *c, bool on,
                       const char *material, const char *color_hex) {
    if (!bay_spool[i]) return;
    bool empty = (c->spool_id <= 0 && !c->filament_detected &&
                  !(material && material[0]) && !(color_hex && color_hex[0]));
    if (on) lv_obj_clear_flag(bay_hl[i], LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(bay_hl[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(bay_num[i], ui_C(on ? COLOR_MINT : COLOR_CREAM), 0);
    lv_obj_set_style_border_color(bay_plate[i], ui_C(on ? COLOR_MINT : 0x2A3C5C), 0);
    if (bay_num_bg[i])
        lv_obj_set_style_border_color(bay_num_bg[i], ui_C(on ? COLOR_MINT : 0x2A3C5C), 0);
    if (empty) {
        lv_obj_add_flag(bay_spool[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(bay_mat[i], "--");
        lv_obj_set_style_text_color(bay_mat[i], ui_C(COLOR_MUTED), 0);
    } else {
        uint32_t rgb = (color_hex && color_hex[0]) ? ui_parse_hex(color_hex)
                      : (c->filament_detected ? 0x445066 : 0xCCCCCC);
        lv_obj_clear_flag(bay_spool[i], LV_OBJ_FLAG_HIDDEN);
        lv_img_set_src(bay_spool[i], bay_colored(i, rgb));
        char mat[8];
        if (material && material[0]) {
            int k = 0;
            for (; material[k] && k < 4; k++) mat[k] = material[k];
            mat[k] = 0;
        } else {
            mat[0] = '-'; mat[1] = '-'; mat[2] = 0;
        }
        lv_label_set_text(bay_mat[i], mat);
        lv_obj_set_style_text_color(bay_mat[i], ui_C(on ? COLOR_MINT : COLOR_CREAM), 0);
    }
}

static void fmt_time(char *b, size_t n, float sec) {
    if (sec < 0) { snprintf(b, n, "--"); return; }
    int s = (int)(sec + 0.5f);
    int h = s / 3600, m = (s % 3600) / 60;
    if (h > 0) snprintf(b, n, "%dh %02dmin", h, m);
    else snprintf(b, n, "%dmin", m);
}

static void refresh_text(const PrinterSnapshot *s) {
    lv_label_set_text(lbl_state, printer_state_label_pt(s->state));
    const char *sub = "";
    if (s->state == PRINTER_IDLE) sub = "FABRICA DORMINDO";
    else if (s->state == PRINTER_OFFLINE) sub = s->status_msg[0] ? s->status_msg : "SEM CONEXAO";
    else if (s->state == PRINTER_COMPLETE && s->cancelled) sub = "CANCELADA";
    else if (s->state == PRINTER_HEATING) sub = "AQUECENDO O BICO";
    else if (s->filename[0] && (s->state == PRINTER_PRINTING || s->state == PRINTER_PAUSED)) sub = s->filename;
    else if (s->discovering) sub = s->status_msg;
    else if (s->filename[0]) sub = s->filename;
    lv_label_set_text(lbl_sub, sub);

    int pct = (int)(s->progress * 100.0f + 0.5f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    char tmp[96];
    snprintf(tmp, sizeof(tmp), "%d%%", pct);
    lv_label_set_text(lbl_bar, tmp);

    if (s->layer_total > 0)
        snprintf(tmp, sizeof(tmp), "CAMADA  %d / %d", s->layer, s->layer_total);
    else
        snprintf(tmp, sizeof(tmp), "CAMADA  --");
    lv_label_set_text(lbl_camada, tmp);

    char tr[24];
    fmt_time(tr, sizeof(tr), s->remaining_s);
    if (s->state == PRINTER_PRINTING || s->state == PRINTER_HEATING)
        snprintf(tmp, sizeof(tmp), "RESTAM  %s", tr);
    else
        snprintf(tmp, sizeof(tmp), "RESTAM  --");
    lv_label_set_text(lbl_restam, tmp);

    int ae = s->active_extruder;
    if (ae < 0 || ae > 3) ae = 0;
    snprintf(tmp, sizeof(tmp), "BICO %d\n%.0f / %.0f C", ae + 1,
             (double)s->nozzle_c, (double)s->nozzle_target_c);
    lv_label_set_text(lbl_bico, tmp);
    snprintf(tmp, sizeof(tmp), "MESA\n%.0f / %.0f C",
             (double)s->bed_c, (double)s->bed_target_c);
    lv_label_set_text(lbl_mesa, tmp);

    const FeederChannel *fc = (s->feeder.valid) ? &s->feeder.ch[ae] : nullptr;
    int sid = fc ? fc->spool_id : 0;
    const WarehouseSpool *ws = (ui_inv && sid > 0) ? inventory_find(ui_inv, sid) : nullptr;

    snprintf(tmp, sizeof(tmp), "ROLO ATIVO   BICO %d", ae + 1);
    lv_label_set_text(lbl_spool_title, tmp);

    const char *vendor = "";
    const char *material = "";
    const char *name = "";
    const char *chex = "";
    float rem = -1;
    bool low = false;
    bool has_fil = false;
    if (s->spool.valid) {
        vendor = s->spool.vendor;
        material = s->spool.material;
        name = s->spool.name;
        chex = s->spool.color_hex;
        rem = s->spool.remaining_g;
        low = s->spool.low;
        has_fil = true;
        if (ws && rem < 0) rem = ws->info.remaining_g;
        if (ws && !name[0]) name = ws->info.name;
    } else if (ws) {
        vendor = ws->info.vendor;
        material = ws->info.material;
        name = ws->info.name;
        chex = ws->info.color_hex;
        rem = ws->info.remaining_g;
        low = ws->info.low;
        has_fil = true;
    } else if (fc && (fc->filament_type[0] || fc->vendor[0] || fc->filament_detected)) {
        vendor = fc->vendor;
        material = fc->filament_type;
        chex = fc->color_hex;
        has_fil = fc->filament_detected || fc->filament_type[0];
    }

    if (sid > 0 && has_fil) {
        if (rem >= 0)
            snprintf(tmp, sizeof(tmp), "#%d  %s\n%s  %s\n%.0f g",
                     sid, material[0] ? material : "?",
                     vendor[0] ? vendor : "",
                     name[0] ? name : "",
                     (double)rem);
        else
            snprintf(tmp, sizeof(tmp), "#%d  %s\n%s  %s",
                     sid, material[0] ? material : "?",
                     vendor[0] ? vendor : "",
                     name[0] ? name : "");
        lv_label_set_text(lbl_spool, tmp);
        uint32_t rgb = ui_parse_hex(chex[0] ? chex : "59ECC5");
        lv_obj_set_style_bg_color(swatch, ui_C(rgb), 0);
        lv_obj_clear_flag(swatch, LV_OBJ_FLAG_HIDDEN);
        if (low) {
            lv_label_set_text(lbl_alert, "FILAMENTO BAIXO");
            lv_obj_clear_flag(lbl_alert, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(lbl_alert, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (has_fil) {
        snprintf(tmp, sizeof(tmp), "%s  %s\nSEM ROLO ASSOCIADO",
                 vendor[0] ? vendor : "Generic",
                 material[0] ? material : "");
        lv_label_set_text(lbl_spool, tmp);
        uint32_t rgb = ui_parse_hex(chex[0] ? chex : "FFFFFF");
        lv_obj_set_style_bg_color(swatch, ui_C(rgb), 0);
        lv_obj_clear_flag(swatch, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_alert, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(lbl_spool, "SEM ROLO ATIVO");
        lv_obj_add_flag(swatch, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_alert, LV_OBJ_FLAG_HIDDEN);
    }

    /* quatro feeders */
    for (int i = 0; i < 4; i++) {
        const FeederChannel *c = &s->feeder.ch[i];
        bool on = (i == ae);
        const WarehouseSpool *w = (ui_inv && c->spool_id > 0) ? inventory_find(ui_inv, c->spool_id) : nullptr;
        const char *v = (w && w->info.vendor[0]) ? w->info.vendor : (c->vendor[0] ? c->vendor : "");
        const char *m = (w && w->info.material[0]) ? w->info.material : (c->filament_type[0] ? c->filament_type : "");
        float g = (w && w->info.remaining_g >= 0) ? w->info.remaining_g : -1;
        const char *cx = (w && w->info.color_hex[0]) ? w->info.color_hex : c->color_hex;
        if (c->spool_id > 0) {
            if (g >= 0)
                snprintf(tmp, sizeof(tmp), "F%d  %s %s  %.0fg", i + 1,
                         v[0] ? v : "", m[0] ? m : "?", (double)g);
            else
                snprintf(tmp, sizeof(tmp), "F%d  %s %s  #%d", i + 1,
                         v[0] ? v : "", m[0] ? m : "?", c->spool_id);
        } else if (c->filament_detected || m[0]) {
            snprintf(tmp, sizeof(tmp), "F%d  %s %s  --", i + 1,
                     v[0] ? v : "Generic", m[0] ? m : "");
        } else {
            snprintf(tmp, sizeof(tmp), "F%d  vazio", i + 1);
        }
        lv_label_set_text(lbl_f[i], tmp);
        lv_obj_set_style_text_color(lbl_f[i], ui_C(on ? COLOR_MINT : COLOR_CREAM), 0);
        uint32_t rgb = cx[0] ? ui_parse_hex(cx) : (c->filament_detected ? 0x445066 : 0x1A2438);
        lv_obj_set_style_bg_color(sw_f[i], ui_C(rgb), 0);
        lv_obj_set_style_border_color(sw_f[i], ui_C(on ? COLOR_MINT : COLOR_CREAM), 0);
        lv_obj_set_style_border_width(sw_f[i], on ? 2 : 1, 0);
        update_bay(i, c, on, m, cx);
    }

    if (s->printer_ip[0] && s->moonraker_ok)
        snprintf(tmp, sizeof(tmp), "U1  %s", s->printer_ip);
    else if (s->discovering)
        snprintf(tmp, sizeof(tmp), "%s", s->scan_ip[0] ? s->scan_ip : "...");
    else
        snprintf(tmp, sizeof(tmp), "U1  --");
    lv_label_set_text(lbl_ip, tmp);
    (void)lbl_hint;
}

static void tick_anim(void) {
    const PrinterSnapshot *s = &ui_snap;

    /* carro + bico: posição atualizada em tick_toolhead (50 ms) */

    float prog = s->progress;
    if (s->state == PRINTER_COMPLETE) prog = 1.0f;
    if (s->state == PRINTER_IDLE || s->state == PRINTER_OFFLINE || s->state == PRINTER_ERROR)
        prog = 0.0f;
    int vis = (int)(prog * (float)U1_PART_H + 0.5f);
    if (vis < 0) vis = 0;
    if (vis > U1_PART_H) vis = U1_PART_H;
    if (vis < 2) {
        lv_obj_add_flag(img_part_clip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(img_part_clip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(img_part_clip, U1_PART_X, U1_PART_Y + (U1_PART_H - vis));
        lv_obj_set_size(img_part_clip, U1_PART_W, vis);
        lv_obj_set_pos(img_part, 0, vis - U1_PART_H);
    }

    /* LEDs da arte de fundo — overlays menta removidos (piscaavam fora do lugar) */

    bool night_on = (s->state == PRINTER_IDLE || s->state == PRINTER_OFFLINE);
    if (night_on) lv_obj_clear_flag(night, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(night, LV_OBJ_FLAG_HIDDEN);
    if (s->state == PRINTER_ERROR) lv_obj_clear_flag(error_veil, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(error_veil, LV_OBJ_FLAG_HIDDEN);

    bool party = (s->state == PRINTER_COMPLETE && !s->cancelled);
    for (int i = 0; i < 16; i++) {
        if (!party) { lv_obj_add_flag(confetti[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(confetti[i], LV_OBJ_FLAG_HIDDEN);
        int y = (int)lv_obj_get_y(confetti[i]) + 6 + (i % 3);
        int x = (int)lv_obj_get_x(confetti[i]);
        if (y > SCENE_H) y = -(i * 7) % 80;
        if (x < 0 || x > SCENE_W - 6) x = (i * 47) % (SCENE_W - 8);
        lv_obj_set_pos(confetti[i], x, y);
    }
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
    if (dirty) refresh_text(&ui_snap);
    tick_anim();

    /* ação do estoque: toast */
    uint32_t aseq = ui_act_seq;
    if (inventory_copy_action(&ui_act, &aseq) && aseq != ui_act_seq) {
        ui_act_seq = aseq;
        if (ui_act.state == AR_OK && ui_act.message[0]) ui_toast(ui_act.message, false);
        else if (ui_act.state == AR_ERR && ui_act.message[0]) ui_toast(ui_act.message, true);
    }
    ui_warehouse_tick();
    ui_wifi_tick();

    if (toast_box && toast_until && millis() > toast_until) {
        lv_obj_add_flag(toast_box, LV_OBJ_FLAG_HIDDEN);
        toast_until = 0;
    }
}

static lv_obj_t *make_mini_card(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *c = ui_make_panel(parent, x, y, w, h);
    lv_obj_set_style_bg_color(c, ui_C(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, ui_C(0x2A3C5C), 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static void add_factory_tabs(lv_obj_t *parent) {
    struct { int x, w; const char *name; int act; } t[4] = {
        {FTAB_WIFI_X, FTAB_WIFI_W, "WIFI",    3},
        {FTAB_FAB_X,  FTAB_FAB_W,  "FABRICA", 0},
        {FTAB_ARM_X,  FTAB_ARM_W,  "ARMAZEM", 1},
        {FTAB_DIA_X,  FTAB_DIA_W,  "DIARIO",  2},
    };
    for (int i = 0; i < 4; i++) {
        bool on = (t[i].act == 0);
        lv_obj_t *b = lv_btn_create(parent);
        lv_obj_set_pos(b, t[i].x, FTAB_Y);
        lv_obj_set_size(b, t[i].w, FTAB_H);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_bg_color(b, ui_C(on ? COLOR_MINT : COLOR_CARD), 0);
        lv_obj_set_style_border_color(b, ui_C(COLOR_MINT), 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, tab_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)t[i].act);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, t[i].name);
        ui_style_label(l, &font_press_start_12, on ? 0x081725 : COLOR_MINT);
        lv_obj_center(l);
    }
}

static void build_hud(lv_obj_t *parent) {
    hud = lv_obj_create(parent);
    lv_obj_set_pos(hud, HUD_X, HUD_Y);
    lv_obj_set_size(hud, HUD_W, HUD_H);
    lv_obj_set_style_bg_color(hud, ui_C(COLOR_NAVY), 0);
    lv_obj_set_style_bg_opa(hud, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hud, 0, 0);
    lv_obj_set_style_pad_all(hud, 0, 0);
    lv_obj_set_style_radius(hud, 0, 0);
    lv_obj_clear_flag(hud, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hud, ui_go_details, LV_EVENT_CLICKED, nullptr);
    /* divisor mint entre painel e cena */
    lv_obj_t *div = lv_obj_create(hud);
    lv_obj_set_pos(div, HUD_W - 3, 0);
    lv_obj_set_size(div, 3, HUD_H);
    lv_obj_set_style_bg_color(div, ui_C(COLOR_MINT), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);
    lv_obj_set_style_pad_all(div, 0, 0);
    lv_obj_clear_flag(div, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    add_factory_tabs(hud);

    lbl_state = lv_label_create(hud);
    ui_style_label(lbl_state, &font_press_start_16, COLOR_MINT);
    lv_obj_set_pos(lbl_state, 16, 48);

    lbl_sub = lv_label_create(hud);
    ui_style_label(lbl_sub, &font_press_start_12, COLOR_GOLD);
    lv_label_set_long_mode(lbl_sub, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_sub, HUD_W - 36);
    lv_obj_set_pos(lbl_sub, 16, 72);

    lbl_bar = lv_label_create(hud);
    ui_style_label(lbl_bar, &font_press_start_16, COLOR_CREAM);
    lv_obj_set_pos(lbl_bar, 16, 98);

    bar = lv_bar_create(hud);
    lv_obj_set_pos(bar, 16, 124);
    lv_obj_set_size(bar, HUD_W - 36, 18);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, ui_C(0x0A1630), 0);
    lv_obj_set_style_bg_color(bar, ui_C(COLOR_MINT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);

    lbl_restam = lv_label_create(hud);
    ui_style_label(lbl_restam, &font_press_start_16, COLOR_CREAM);
    lv_obj_set_pos(lbl_restam, 16, 152);

    lbl_camada = lv_label_create(hud);
    ui_style_label(lbl_camada, &font_press_start_12, COLOR_MUTED);
    lv_obj_set_pos(lbl_camada, 16, 180);

    card_bico = make_mini_card(hud, 16, 206, 178, 64);
    lbl_bico = lv_label_create(card_bico);
    ui_style_label(lbl_bico, &font_press_start_12, COLOR_CREAM);
    lv_obj_set_pos(lbl_bico, 8, 8);

    card_mesa = make_mini_card(hud, 206, 206, 178, 64);
    lbl_mesa = lv_label_create(card_mesa);
    ui_style_label(lbl_mesa, &font_press_start_12, COLOR_CREAM);
    lv_obj_set_pos(lbl_mesa, 8, 8);

    lbl_spool_title = lv_label_create(hud);
    ui_style_label(lbl_spool_title, &font_press_start_12, COLOR_GOLD);
    lv_obj_set_pos(lbl_spool_title, 16, 284);

    swatch = lv_obj_create(hud);
    lv_obj_set_pos(swatch, 16, 308);
    lv_obj_set_size(swatch, 28, 28);
    lv_obj_set_style_radius(swatch, 0, 0);
    lv_obj_set_style_border_width(swatch, 2, 0);
    lv_obj_set_style_border_color(swatch, ui_C(COLOR_CREAM), 0);
    lv_obj_set_style_pad_all(swatch, 0, 0);
    lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lbl_spool = lv_label_create(hud);
    ui_style_label(lbl_spool, &font_press_start_12, COLOR_CREAM);
    lv_obj_set_pos(lbl_spool, 52, 306);

    lbl_alert = lv_label_create(hud);
    ui_style_label(lbl_alert, &font_press_start_12, COLOR_ALERT);
    lv_obj_set_pos(lbl_alert, 16, 358);
    lv_obj_add_flag(lbl_alert, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 4; i++) {
        int y = 378 + i * 18;
        sw_f[i] = lv_obj_create(hud);
        lv_obj_set_pos(sw_f[i], 16, y + 2);
        lv_obj_set_size(sw_f[i], 12, 12);
        lv_obj_set_style_radius(sw_f[i], 0, 0);
        lv_obj_set_style_border_width(sw_f[i], 1, 0);
        lv_obj_set_style_border_color(sw_f[i], ui_C(COLOR_CREAM), 0);
        lv_obj_set_style_pad_all(sw_f[i], 0, 0);
        lv_obj_set_style_bg_color(sw_f[i], ui_C(0x1A2438), 0);
        lv_obj_clear_flag(sw_f[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lbl_f[i] = lv_label_create(hud);
        ui_style_label(lbl_f[i], &font_press_start_12, COLOR_CREAM);
        lv_label_set_long_mode(lbl_f[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl_f[i], HUD_W - 52);
        lv_obj_set_pos(lbl_f[i], 34, y);
        lv_label_set_text(lbl_f[i], "");
    }

    lbl_ip = lv_label_create(hud);
    ui_style_label(lbl_ip, &font_press_start_12, COLOR_MUTED);
    lv_obj_set_pos(lbl_ip, 16, 454);
    lbl_hint = lbl_ip;
}

static void tick_toolhead(void) {
    const PrinterSnapshot *s = &ui_snap;
    bool show_motion = (s->state == PRINTER_PRINTING ||
                        s->state == PRINTER_HEATING ||
                        s->state == PRINTER_BUSY);
    if (show_motion) {
        int span = U1_TOOLHEAD_TRAVEL;
        if (span < 8) span = 70;
        /* ping-pong com smoothstep; ciclo completo ~7 s (ida+volta) */
        const uint32_t half = 3500;
        uint32_t now = lv_tick_get();
        uint32_t cyc = now % (half * 2);
        float u = (cyc < half) ? (cyc / (float)half) : (2.0f - cyc / (float)half);
        u = u * u * (3.0f - 2.0f * u);
        th_x = U1_TOOLHEAD_X_MIN + (int)(u * (float)span + 0.5f);
    } else if (s->state == PRINTER_PAUSED) {
        /* congela na última posição */
    } else {
        th_x = U1_TOOLHEAD_X;
    }
    int dx = th_x - U1_TOOLHEAD_X;
    if (img_toolhead)
        lv_obj_set_pos(img_toolhead, U1_TOOLHEAD_X + dx, U1_TOOLHEAD_Y);
    if (img_carriage)
        lv_obj_set_pos(img_carriage, U1_CARRIAGE_X + dx, U1_CARRIAGE_Y);
}

static void on_anim(lv_timer_t *) {
    tick_toolhead();
}

static void build_bays(lv_obj_t *parent) {
    for (int i = 0; i < 4; i++) {
        int x = k_bay_x[i], w = k_bay_w[i];
        /* Gaveta inferior: widgets nos 4 compartimentos já pintados.
         * Arte de fundo intacta; só os widgets dos 4 slots. */
        bay_hl[i] = lv_obj_create(parent);
        lv_obj_set_pos(bay_hl[i], x - 2, U1_SLOT_HL_Y);
        lv_obj_set_size(bay_hl[i], w + 4, U1_SLOT_HL_H);
        lv_obj_set_style_bg_opa(bay_hl[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(bay_hl[i], ui_C(COLOR_MINT), 0);
        lv_obj_set_style_border_width(bay_hl[i], 2, 0);
        lv_obj_set_style_radius(bay_hl[i], 0, 0);
        lv_obj_set_style_pad_all(bay_hl[i], 0, 0);
        lv_obj_clear_flag(bay_hl[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(bay_hl[i], LV_OBJ_FLAG_HIDDEN);

        /* fundo escuro atrás do número (dourado no amarelo da tampa sumia) */
        int nx = x + (w - 20) / 2;
        bay_num_bg[i] = lv_obj_create(parent);
        lv_obj_set_pos(bay_num_bg[i], nx, U1_SLOT_NUM_Y);
        lv_obj_set_size(bay_num_bg[i], 20, 18);
        lv_obj_set_style_bg_color(bay_num_bg[i], ui_C(0x080F23), 0);
        lv_obj_set_style_bg_opa(bay_num_bg[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(bay_num_bg[i], ui_C(0x2A3C5C), 0);
        lv_obj_set_style_border_width(bay_num_bg[i], 1, 0);
        lv_obj_set_style_radius(bay_num_bg[i], 0, 0);
        lv_obj_set_style_pad_all(bay_num_bg[i], 0, 0);
        lv_obj_clear_flag(bay_num_bg[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        bay_num[i] = lv_label_create(bay_num_bg[i]);
        ui_style_label(bay_num[i], &font_press_start_16, COLOR_CREAM);
        char nb[2] = { (char)('1' + i), 0 };
        lv_label_set_text(bay_num[i], nb);
        lv_obj_center(bay_num[i]);
        lv_obj_clear_flag(bay_num[i], LV_OBJ_FLAG_CLICKABLE);

        /* plaquinha do material abaixo do número, ainda no interior da gaveta */
        int pw = 48;
        int px = x + (w - pw) / 2;
        bay_plate[i] = lv_obj_create(parent);
        lv_obj_set_pos(bay_plate[i], px, U1_SLOT_PLATE_Y);
        lv_obj_set_size(bay_plate[i], pw, 14);
        lv_obj_set_style_bg_color(bay_plate[i], ui_C(0x0C1930), 0);
        lv_obj_set_style_bg_opa(bay_plate[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(bay_plate[i], ui_C(0x2A3C5C), 0);
        lv_obj_set_style_border_width(bay_plate[i], 1, 0);
        lv_obj_set_style_radius(bay_plate[i], 0, 0);
        lv_obj_set_style_pad_all(bay_plate[i], 0, 0);
        lv_obj_clear_flag(bay_plate[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        bay_mat[i] = lv_label_create(bay_plate[i]);
        ui_style_label(bay_mat[i], &font_press_start_12, COLOR_MUTED);
        lv_label_set_text(bay_mat[i], "--");
        lv_obj_center(bay_mat[i]);

        bay_spool[i] = lv_img_create(parent);
        lv_img_set_src(bay_spool[i], &img_u1_spool);
        lv_obj_set_pos(bay_spool[i],
                       x + (w - IMG_U1_SPOOL_W) / 2,
                       U1_SLOT_SPOOL_Y);
        lv_obj_clear_flag(bay_spool[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(bay_spool[i], LV_OBJ_FLAG_HIDDEN);
        /* placa e número ficam à frente do rolo (criados antes: move_foreground) */
        lv_obj_move_foreground(bay_plate[i]);
        lv_obj_move_foreground(bay_num_bg[i]);
        lv_obj_move_foreground(bay_hl[i]);
    }
}

void ui_init(void) {
    stock_filter_clear(&ui_filter);
    {
        size_t bcap = (size_t)IMG_U1_SPOOL_W * IMG_U1_SPOOL_H * 3 + 32;
        for (int i = 0; i < 4; i++) {
            bay_recolor[i] = (uint8_t *)heap_caps_malloc(bcap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!bay_recolor[i]) bay_recolor[i] = (uint8_t *)malloc(bcap);
            bay_cache_valid[i] = 0;
            bay_rgb_cache[i] = 0xFFFFFFFF;
        }
    }
    ui_inv = (InventorySnapshot *)heap_caps_malloc(sizeof(InventorySnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ui_inv) ui_inv = (InventorySnapshot *)malloc(sizeof(InventorySnapshot));
    if (ui_inv) memset(ui_inv, 0, sizeof(*ui_inv));
    ui_cat = (CatalogSnapshot *)heap_caps_malloc(sizeof(CatalogSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ui_cat) ui_cat = (CatalogSnapshot *)malloc(sizeof(CatalogSnapshot));
    if (ui_cat) memset(ui_cat, 0, sizeof(*ui_cat));

    snapshot_clear(&ui_snap);

    /* ---------- fábrica ---------- */
    scr_factory = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_factory, ui_C(COLOR_NAVY), 0);
    lv_obj_clear_flag(scr_factory, LV_OBJ_FLAG_SCROLLABLE);

    /* cena à direita: clip 400x480, fundo 800 deslocado de -SCENE_CROP_X */
    scene = lv_obj_create(scr_factory);
    lv_obj_set_pos(scene, SCENE_X, 0);
    lv_obj_set_size(scene, SCENE_W, SCENE_H);
    lv_obj_set_style_bg_color(scene, ui_C(COLOR_NAVY), 0);
    lv_obj_set_style_bg_opa(scene, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scene, 0, 0);
    lv_obj_set_style_pad_all(scene, 0, 0);
    lv_obj_set_style_radius(scene, 0, 0);
    lv_obj_clear_flag(scene, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scene, ui_go_details, LV_EVENT_CLICKED, nullptr);

    img_bg = lv_img_create(scene);
    lv_img_set_src(img_bg, &img_bg_factory);
    lv_obj_set_pos(img_bg, -SCENE_CROP_X, 0);
    lv_obj_clear_flag(img_bg, LV_OBJ_FLAG_CLICKABLE);

    /* gaveta inferior (baias do topo ficam na arte) */
    build_bays(scene);

    /* peça (clip de baixo para cima) */
    img_part_clip = lv_obj_create(scene);
    lv_obj_set_pos(img_part_clip, U1_PART_X, U1_PART_Y);
    lv_obj_set_size(img_part_clip, U1_PART_W, U1_PART_H);
    lv_obj_set_style_bg_opa(img_part_clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(img_part_clip, 0, 0);
    lv_obj_set_style_pad_all(img_part_clip, 0, 0);
    lv_obj_set_style_radius(img_part_clip, 0, 0);
    lv_obj_clear_flag(img_part_clip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    img_part = lv_img_create(img_part_clip);
    lv_img_set_src(img_part, &img_u1_part);
    lv_obj_set_pos(img_part, 0, 0);
    lv_obj_clear_flag(img_part, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(img_part_clip, LV_OBJ_FLAG_HIDDEN);

    img_toolhead = lv_img_create(scene);
    lv_img_set_src(img_toolhead, &img_u1_toolhead);
    lv_obj_set_pos(img_toolhead, U1_TOOLHEAD_X, U1_TOOLHEAD_Y);
    lv_obj_clear_flag(img_toolhead, LV_OBJ_FLAG_CLICKABLE);

    /* gantry do teto — à frente das baias, mesmo dx do bico */
    img_carriage = lv_img_create(scene);
    lv_img_set_src(img_carriage, &img_u1_carriage);
    lv_obj_set_pos(img_carriage, U1_CARRIAGE_X, U1_CARRIAGE_Y);
    lv_obj_clear_flag(img_carriage, LV_OBJ_FLAG_CLICKABLE);

    night = lv_obj_create(scene);
    lv_obj_set_size(night, SCENE_W, SCENE_H);
    lv_obj_set_pos(night, 0, 0);
    lv_obj_set_style_bg_color(night, ui_C(COLOR_NIGHT), 0);
    lv_obj_set_style_bg_opa(night, LV_OPA_50, 0);
    lv_obj_set_style_border_width(night, 0, 0);
    lv_obj_set_style_radius(night, 0, 0);
    lv_obj_set_style_pad_all(night, 0, 0);
    lv_obj_clear_flag(night, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(night, LV_OBJ_FLAG_HIDDEN);

    error_veil = lv_obj_create(scene);
    lv_obj_set_size(error_veil, SCENE_W, SCENE_H);
    lv_obj_set_pos(error_veil, 0, 0);
    lv_obj_set_style_bg_color(error_veil, lv_color_make(80, 0, 0), 0);
    lv_obj_set_style_bg_opa(error_veil, LV_OPA_30, 0);
    lv_obj_set_style_border_width(error_veil, 0, 0);
    lv_obj_set_style_radius(error_veil, 0, 0);
    lv_obj_set_style_pad_all(error_veil, 0, 0);
    lv_obj_clear_flag(error_veil, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(error_veil, LV_OBJ_FLAG_HIDDEN);

    static const uint32_t conf_colors[4] = {COLOR_MINT, COLOR_GOLD, COLOR_ALERT, COLOR_CREAM};
    for (int i = 0; i < 16; i++) {
        confetti[i] = lv_obj_create(scene);
        lv_obj_set_size(confetti[i], 5, 8);
        lv_obj_set_pos(confetti[i], (i * 47) % (SCENE_W - 8), (i * 19) % 200);
        lv_obj_set_style_bg_color(confetti[i], ui_C(conf_colors[i % 4]), 0);
        lv_obj_set_style_border_width(confetti[i], 0, 0);
        lv_obj_set_style_radius(confetti[i], 0, 0);
        lv_obj_set_style_pad_all(confetti[i], 0, 0);
        lv_obj_clear_flag(confetti[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(confetti[i], LV_OBJ_FLAG_HIDDEN);
    }

    build_hud(scr_factory);
    /* abas da fábrica ficam no painel esquerdo (add_factory_tabs) */

    toast_box = ui_make_panel(lv_layer_top(), 160, 430, 480, 40);
    lv_obj_set_style_bg_opa(toast_box, LV_OPA_90, 0);
    toast_lbl = lv_label_create(toast_box);
    ui_style_label(toast_lbl, &lv_font_montserrat_14, COLOR_CREAM);
    lv_label_set_long_mode(toast_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(toast_lbl, 460);
    lv_obj_center(toast_lbl);
    lv_obj_add_flag(toast_box, LV_OBJ_FLAG_HIDDEN);

    /* ---------- detalhes ---------- */
    scr_details = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_details, ui_C(COLOR_NAVY), 0);
    lv_obj_clear_flag(scr_details, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr_details, ui_go_factory, LV_EVENT_CLICKED, nullptr);
    det_title = lv_label_create(scr_details);
    ui_style_label(det_title, &font_press_start_16, COLOR_MINT);
    lv_label_set_text(det_title, "DETALHES DA IMPRESSAO");
    lv_obj_set_pos(det_title, 20, 48);
    det_body = lv_label_create(scr_details);
    ui_style_label(det_body, &font_press_start_12, COLOR_CREAM);
    lv_label_set_long_mode(det_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(det_body, 760);
    lv_obj_set_pos(det_body, 20, 88);
    lv_obj_t *back = lv_label_create(scr_details);
    ui_style_label(back, &font_press_start_12, COLOR_MUTED);
    lv_label_set_text(back, "toque para voltar a fabrica");
    lv_obj_set_pos(back, 20, 440);
    ui_add_tabs(scr_details, 0);

    /* ---------- armazem / diario / wifi ---------- */
    scr_warehouse = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_warehouse, ui_C(COLOR_NAVY), 0);
    lv_obj_clear_flag(scr_warehouse, LV_OBJ_FLAG_SCROLLABLE);
    ui_warehouse_build();

    scr_history = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_history, ui_C(COLOR_NAVY), 0);
    lv_obj_clear_flag(scr_history, LV_OBJ_FLAG_SCROLLABLE);
    ui_history_build();

    scr_wifi = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_wifi, ui_C(COLOR_NAVY), 0);
    lv_obj_clear_flag(scr_wifi, LV_OBJ_FLAG_SCROLLABLE);
    ui_wifi_build();

    lv_scr_load(scr_factory);
    lv_timer_create(on_timer, 200, nullptr);
    lv_timer_create(on_anim, 50, nullptr);
    refresh_text(&ui_snap);
    tick_anim();
    tick_toolhead();
}
