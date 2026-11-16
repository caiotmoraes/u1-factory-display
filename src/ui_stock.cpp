#include "ui_priv.h"
#include <esp_heap_caps.h>

/* ---- ARMAZÉM, FICHA, FORMULÁRIOS, DIÁRIO, WIFI ---- */

static lv_obj_t *wh_status, *wh_summary, *wh_list, *wh_search;
static lv_obj_t *wh_mat, *wh_brand, *wh_loc, *wh_sort;
static lv_obj_t *wh_chip_arch, *wh_chip_low, *wh_chip_comp;
static lv_obj_t *wh_busy;
static bool wh_loaded = false;
static int filt_idx[WAREHOUSE_MAX];
static int filt_n = 0;
static char mat_opts[24][24], brand_opts[24][24], loc_opts[16][SPOOL_LOCATION_LEN];
static int n_mat = 0, n_brand = 0, n_loc = 0;
static int ficha_id = 0;
static int form_channel = 0;
static bool form_load = true;

/* widgets da ficha / formulários (válidos só com overlay aberto) */
static lv_obj_t *ta_location, *ta_comment, *ta_weight, *cb_weight;
static lv_obj_t *ta_uid, *ta_vendor, *ta_fname, *ta_material, *ta_colors;
static lv_obj_t *ta_density, *ta_diam, *ta_init, *ta_rem, *ta_delete;
static lv_obj_t *dd_filament, *dd_dir, *cb_newfil;
static lv_obj_t *hist_search, *hist_type, *hist_list;
static lv_obj_t *wifi_cur, *wifi_list;
static bool wifi_scanning = false;
static uint32_t wifi_opened_ms = 0;

static void ta_focus(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED)
        ui_show_kb((lv_obj_t *)lv_event_get_target(e));
    else if (lv_event_get_code(e) == LV_EVENT_DEFOCUSED)
        ui_hide_kb();
}

static lv_obj_t *make_ta(lv_obj_t *parent, int x, int y, int w, int h, const char *ph, int maxlen, bool oneline) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_pos(ta, x, y);
    lv_obj_set_size(ta, w, h);
    lv_textarea_set_one_line(ta, oneline);
    lv_textarea_set_max_length(ta, maxlen);
    lv_textarea_set_placeholder_text(ta, ph ? ph : "");
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(ta, ui_C(0x0A1630), 0);
    lv_obj_set_style_text_color(ta, ui_C(COLOR_CREAM), 0);
    lv_obj_set_style_border_color(ta, ui_C(COLOR_MINT), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_pad_all(ta, 4, 0);
    lv_obj_add_event_cb(ta, ta_focus, LV_EVENT_ALL, nullptr);
    return ta;
}

static lv_obj_t *make_dd(lv_obj_t *parent, int x, int y, int w, const char *opts) {
    lv_obj_t *d = lv_dropdown_create(parent);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_size(d, w, 32);
    lv_dropdown_set_options(d, opts ? opts : "Todos");
    lv_obj_set_style_text_font(d, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(d, ui_C(COLOR_CARD), 0);
    lv_obj_set_style_text_color(d, ui_C(COLOR_CREAM), 0);
    lv_obj_set_style_border_color(d, ui_C(COLOR_MINT), 0);
    lv_obj_set_style_border_width(d, 1, 0);
    lv_obj_set_style_radius(d, 0, 0);
    return d;
}

static lv_obj_t *chip(lv_obj_t *parent, int x, int y, int w, const char *txt, lv_event_cb_t cb, bool *state) {
    lv_obj_t *b = ui_make_btn(parent, x, y, w, 28, txt, COLOR_CARD, COLOR_MINT, cb, state);
    return b;
}

static void set_chip(lv_obj_t *b, bool on) {
    lv_obj_set_style_bg_color(b, ui_C(on ? COLOR_MINT : COLOR_CARD), 0);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    if (l) lv_obj_set_style_text_color(l, ui_C(on ? 0x081725 : COLOR_MINT), 0);
}

/* ---------- lista ---------- */
static void apply_and_fill(void);

static void card_click(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    if (id > 0) ui_ficha_open(id);
}

static bool stock_filter_is_default(const StockFilter *f) {
    return !f->query[0] && !f->material[0] && !f->brand[0] && !f->location[0] &&
           !f->low_stock && !f->include_archived;
}

static void fill_list(void) {
    if (!wh_list || !ui_inv) return;
    lv_obj_clean(wh_list);
    filt_n = stock_apply_filter(ui_inv->items, ui_inv->count, &ui_filter, filt_idx, WAREHOUSE_MAX);
    if (ui_inv->status == INV_LOADING || ui_inv->status == INV_IDLE) {
        lv_obj_t *l = lv_label_create(wh_list);
        ui_style_label(l, &font_press_start_12, COLOR_MUTED);
        lv_label_set_text(l, "Carregando estoque...");
        lv_obj_set_pos(l, 12, 16);
        return;
    }
    if (ui_inv->status == INV_ERROR || ui_inv->status == INV_OFFLINE) {
        lv_obj_t *l = lv_label_create(wh_list);
        ui_style_label(l, &lv_font_montserrat_14, COLOR_ALERT);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, 740);
        lv_obj_set_pos(l, 12, 16);
        char msg[200];
        snprintf(msg, sizeof(msg), "%s\nToque ATUALIZA para tentar de novo.",
                 ui_inv->status == INV_OFFLINE
                     ? (ui_inv->error_msg[0] ? ui_inv->error_msg : "Impressora offline / sem conexao")
                     : (ui_inv->error_msg[0] ? ui_inv->error_msg : "Falha ao carregar o estoque."));
        lv_label_set_text(l, msg);
        return;
    }
    if (filt_n == 0) {
        lv_obj_t *l = lv_label_create(wh_list);
        ui_style_label(l, &font_press_start_12, COLOR_MUTED);
        int visible = 0;
        for (int i = 0; i < ui_inv->count; i++) visible += !ui_inv->items[i].archived;
        lv_label_set_text(l, ui_inv->count == 0 ? "Nenhum rolo cadastrado no Spoolman." :
                             stock_filter_is_default(&ui_filter) && visible == 0
                                 ? "So ha rolos arquivados. Toque ARQUIVADOS."
                                 : "Nenhum rolo com este filtro.");
        lv_obj_set_pos(l, 12, 16);
        return;
    }
    const int cw = ui_compact ? 760 : 376;
    const int ch = ui_compact ? 44 : 96;
    const int gap = 8;
    const int cols = ui_compact ? 1 : 2;
    for (int i = 0; i < filt_n; i++) {
        const WarehouseSpool *s = &ui_inv->items[filt_idx[i]];
        int col = i % cols;
        int row = i / cols;
        int x = col * (cw + gap);
        int y = row * (ch + gap);
        lv_obj_t *card = lv_obj_create(wh_list);
        lv_obj_set_pos(card, x, y);
        lv_obj_set_size(card, cw, ch);
        bool active = (s->info.id == ui_inv->active_id && s->info.id > 0);
        lv_obj_set_style_bg_color(card, ui_C(COLOR_CARD), 0);
        lv_obj_set_style_bg_opa(card, s->archived ? LV_OPA_50 : LV_OPA_90, 0);
        lv_obj_set_style_border_color(card, ui_C(active ? COLOR_GOLD : COLOR_MINT), 0);
        lv_obj_set_style_border_width(card, active ? 3 : 2, 0);
        lv_obj_set_style_radius(card, 0, 0);
        lv_obj_set_style_pad_all(card, 6, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(card, card_click, LV_EVENT_CLICKED, (void *)(intptr_t)s->info.id);

        lv_obj_t *sw = lv_obj_create(card);
        lv_obj_set_pos(sw, 2, ui_compact ? 8 : 8);
        lv_obj_set_size(sw, ui_compact ? 20 : 28, ui_compact ? 20 : 28);
        lv_obj_set_style_bg_color(sw, ui_C(ui_parse_hex(s->info.color_hex)), 0);
        lv_obj_set_style_border_width(sw, 1, 0);
        lv_obj_set_style_border_color(sw, ui_C(COLOR_CREAM), 0);
        lv_obj_set_style_radius(sw, 0, 0);
        lv_obj_set_style_pad_all(sw, 0, 0);
        lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        char line[160];
        if (ui_compact) {
            if (s->info.remaining_g >= 0)
                snprintf(line, sizeof(line), "#%d  %s %s  %s   %.0fg%s",
                         s->info.id, s->info.vendor, s->info.material,
                         s->info.name, (double)s->info.remaining_g,
                         active ? "  EM USO" : (s->archived ? "  arq." : ""));
            else
                snprintf(line, sizeof(line), "#%d  %s %s  %s%s",
                         s->info.id, s->info.vendor, s->info.material, s->info.name,
                         active ? "  EM USO" : "");
            lv_obj_t *lb = lv_label_create(card);
            ui_style_label(lb, &lv_font_montserrat_14, s->info.low ? COLOR_ALERT : COLOR_CREAM);
            lv_label_set_long_mode(lb, LV_LABEL_LONG_DOT);
            lv_obj_set_width(lb, cw - 50);
            lv_obj_set_pos(lb, 30, 10);
            lv_label_set_text(lb, line);
        } else {
            lv_obj_t *t1 = lv_label_create(card);
            ui_style_label(t1, &font_press_start_12, COLOR_MINT);
            snprintf(line, sizeof(line), "%s  %s", s->info.material[0] ? s->info.material : "?",
                     s->info.vendor);
            lv_label_set_text(t1, line);
            lv_obj_set_pos(t1, 40, 4);
            lv_obj_t *t2 = lv_label_create(card);
            ui_style_label(t2, &lv_font_montserrat_14, COLOR_CREAM);
            lv_label_set_long_mode(t2, LV_LABEL_LONG_DOT);
            lv_obj_set_width(t2, cw - 56);
            lv_obj_set_pos(t2, 40, 26);
            lv_label_set_text(t2, s->info.name[0] ? s->info.name : "--");
            lv_obj_t *t3 = lv_label_create(card);
            ui_style_label(t3, &font_press_start_12, s->info.low ? COLOR_ALERT : COLOR_MUTED);
            if (s->info.remaining_g >= 0)
                snprintf(line, sizeof(line), "#%d   %.0f g", s->info.id, (double)s->info.remaining_g);
            else
                snprintf(line, sizeof(line), "#%d   -- g", s->info.id);
            lv_label_set_text(t3, line);
            lv_obj_set_pos(t3, 40, 58);
            if (s->location[0]) {
                lv_obj_t *t4 = lv_label_create(card);
                ui_style_label(t4, &lv_font_montserrat_14, COLOR_MUTED);
                lv_label_set_long_mode(t4, LV_LABEL_LONG_DOT);
                lv_obj_set_width(t4, 140);
                lv_obj_set_pos(t4, cw - 160, 58);
                lv_label_set_text(t4, s->location);
            }
            if (active) {
                lv_obj_t *badge = lv_label_create(card);
                ui_style_label(badge, &font_press_start_12, COLOR_GOLD);
                lv_label_set_text(badge, "EM USO");
                lv_obj_set_pos(badge, cw - 90, 6);
            } else if (s->archived) {
                lv_obj_t *badge = lv_label_create(card);
                ui_style_label(badge, &font_press_start_12, COLOR_MUTED);
                lv_label_set_text(badge, "ARQ.");
                lv_obj_set_pos(badge, cw - 70, 6);
            }
        }
    }
    StockSummary sm = stock_summary(ui_inv->items, ui_inv->count);
    char sum[160];
    snprintf(sum, sizeof(sum), "%d ativos  |  %.0f g  |  %d <=200 g  |  %d arquivados  |  mostrando %d",
             sm.active, (double)sm.known_weight_g, sm.low200, sm.archived, filt_n);
    if (wh_summary) lv_label_set_text(wh_summary, sum);
}

static void rebuild_dropdowns(void) {
    if (!ui_inv || !wh_mat) return;
    n_mat = stock_unique_field(ui_inv->items, ui_inv->count, 0, (char *)mat_opts, 24, 24);
    n_brand = stock_unique_field(ui_inv->items, ui_inv->count, 1, (char *)brand_opts, 24, 24);
    n_loc = stock_unique_field(ui_inv->items, ui_inv->count, 2, (char *)loc_opts, 16, SPOOL_LOCATION_LEN);
    char buf[1024];
    auto fill = [&](lv_obj_t *dd, const char *cur, char *arr, int n, size_t es) {
        strcpy(buf, "Todos");
        int sel = 0;
        size_t used = strlen(buf);
        for (int i = 0; i < n; i++) {
            const char *v = arr + i * (int)es;
            if (!v[0]) continue;
            const size_t vl = strnlen(v, es);
            if (used + 1 + vl >= sizeof(buf)) break; /* antes: strcat sem limite (16 locais x 64) */
            buf[used++] = '\n';
            memcpy(buf + used, v, vl);
            used += vl;
            buf[used] = 0;
            if (cur && cur[0] && strcmp(cur, v) == 0) sel = i + 1;
        }
        lv_dropdown_set_options(dd, buf);
        lv_dropdown_set_selected(dd, sel);
    };
    fill(wh_mat, ui_filter.material, (char *)mat_opts, n_mat, 24);
    fill(wh_brand, ui_filter.brand, (char *)brand_opts, n_brand, 24);
    fill(wh_loc, ui_filter.location, (char *)loc_opts, n_loc, SPOOL_LOCATION_LEN);
}

static void read_filters_from_ui(void) {
    if (wh_search) {
        const char *q = lv_textarea_get_text(wh_search);
        ui_cpy(ui_filter.query, sizeof(ui_filter.query), q ? q : "");
    }
    char tmp[SPOOL_LOCATION_LEN] = {0};
    auto pick = [](lv_obj_t *dd, char *dst, size_t n) {
        dst[0] = 0;
        if (!dd) return;
        if (lv_dropdown_get_selected(dd) == 0) return;
        lv_dropdown_get_selected_str(dd, dst, (uint32_t)n);
        if (strcmp(dst, "Todos") == 0) dst[0] = 0;
    };
    pick(wh_mat, ui_filter.material, sizeof(ui_filter.material));
    pick(wh_brand, ui_filter.brand, sizeof(ui_filter.brand));
    pick(wh_loc, tmp, sizeof(tmp));
    ui_cpy(ui_filter.location, sizeof(ui_filter.location), tmp);
    if (wh_sort) ui_filter.sort = (StockSort)lv_dropdown_get_selected(wh_sort);
}

static void on_filter_change(lv_event_t *) {
    read_filters_from_ui();
    fill_list();
}

static void on_chip_arch(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    ui_filter.include_archived = !ui_filter.include_archived;
    set_chip(wh_chip_arch, ui_filter.include_archived);
    fill_list();
}
static void on_chip_low(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    ui_filter.low_stock = !ui_filter.low_stock;
    set_chip(wh_chip_low, ui_filter.low_stock);
    fill_list();
}
static void on_chip_comp(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    ui_compact = !ui_compact;
    set_chip(wh_chip_comp, ui_compact);
    fill_list();
}
static void on_chip_clear(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    stock_filter_clear(&ui_filter);
    ui_compact = false;
    if (wh_search) lv_textarea_set_text(wh_search, "");
    if (wh_mat) lv_dropdown_set_selected(wh_mat, 0);
    if (wh_brand) lv_dropdown_set_selected(wh_brand, 0);
    if (wh_loc) lv_dropdown_set_selected(wh_loc, 0);
    if (wh_sort) lv_dropdown_set_selected(wh_sort, 0);
    set_chip(wh_chip_arch, false);
    set_chip(wh_chip_low, false);
    set_chip(wh_chip_comp, false);
    fill_list();
}

static void on_refresh(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    if (inventory_busy()) { ui_toast("Aguarde a acao em curso.", false); return; }
    if (wh_status) lv_label_set_text(wh_status, "Atualizando...");
    inventory_request_refresh();
}

static void on_novo(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    ui_new_spool_open();
}

void ui_warehouse_on_show(void) {
    /* Recarrega se nunca carregou, se falhou ou se a lista tem mais de 2 min. */
    const bool stale = ui_inv && ui_inv->status == INV_OK && millis() - ui_inv->fetched_ms > 120000;
    if (!wh_loaded || stale || (ui_inv && ui_inv->status != INV_OK && ui_inv->status != INV_LOADING)) {
        inventory_request_refresh();
        wh_loaded = true;
    }
    fill_list();
}

/* ---------- NFC (PN532) ---------- */
enum NfcMode { NFC_LOOKUP, NFC_BIND, NFC_WRITE };
static lv_obj_t *nfc_target;      /* objeto cuja exclusão cancela a leitura */
static NfcMode nfc_mode;
static int nfc_bind_id;           /* rolo alvo do NFC_BIND */
static lv_obj_t *nfc_status;
static lv_obj_t *nfc_dd;
static lv_obj_t *nfc_adv;
static lv_obj_t *nfc_link_btn;
static lv_obj_t *nfc_auto_button;
static lv_obj_t *nfc_uid_ta;
static int nfc_dd_ids[WAREHOUSE_MAX + 1];
static int nfc_dd_n;
static char nfc_last_uid[24];
static char nfc_matched_uid[21];
static int nfc_matched_spool;
static bool nfc_auto_active;
static uint32_t nfc_auto_seq;
static bool new_spool_wait;
static uint32_t new_spool_seq;

static void open_nfc_for(int spool_id, const char *intro);

static void spool_line(const WarehouseSpool *s, char *out, size_t n) {
    char cname[32];
    stock_color_name(s->info.color_hex, cname, sizeof(cname));
    snprintf(out, n, "#%d %s %s %s", s->info.id,
             s->info.vendor[0] ? s->info.vendor : "?",
             s->info.material[0] ? s->info.material : "?", cname);
}

static bool uid_in_list(const char *list, const char *norm) {
    const char *p = list;
    while (p && *p) {
        char tok[24] = {0};
        int k = 0;
        while (*p && *p != ',' && k < 23) tok[k++] = *p++;
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
        char tn[24] = {0};
        spool_normalize_uid(tok, tn, sizeof(tn));
        if (tn[0] && !strcmp(tn, norm)) return true;
    }
    return false;
}

/* Rolo do estoque local com este UID (normalizado), ou nullptr. */
static const WarehouseSpool *spool_by_uid(const char *norm, int *count) {
    const WarehouseSpool *found = nullptr;
    int n = 0;
    if (ui_inv && ui_inv->status == INV_OK) {
        for (int i = 0; i < ui_inv->count; i++) {
            if (uid_in_list(ui_inv->items[i].card_uids, norm)) {
                if (!found) found = &ui_inv->items[i];
                n++;
            }
        }
    }
    if (count) *count = n;
    return found;
}

static void nfc_set_status(const char *text) {
    if (nfc_status) lv_label_set_text(nfc_status, text);
}

static void cancel_nfc_on_delete(lv_event_t *e) {
    if (lv_event_get_target(e) == nfc_target) {
        nfc_cancel();
        nfc_target = nullptr;
    }
}

static bool scan_nfc(lv_obj_t *target, NfcMode mode, int spool_id) {
    if (nfc_auto_active) {
        ui_toast("Aguarde a associacao ou feche o menu para cancelar.", false);
        return false;
    }
    if (!nfc_request(mode == NFC_WRITE ? spool_id : 0)) {
        ui_toast("Leitor ocupado. Tente novamente.", true);
        return false;
    }
    nfc_mode = mode;
    nfc_bind_id = mode == NFC_BIND ? spool_id : 0;
    nfc_target = target;
    lv_obj_add_event_cb(target, cancel_nfc_on_delete, LV_EVENT_DELETE, nullptr);
    if (nfc_link_btn) lv_obj_add_flag(nfc_link_btn, LV_OBJ_FLAG_HIDDEN);
    nfc_set_status(mode == NFC_WRITE ? "Aproxime uma NTAG vazia e mantenha no leitor..."
                                     : "Aproxime a tag do leitor e mantenha ate o fim...");
    return true;
}

static int nfc_selected_id(void) {
    if (!nfc_dd) return 0;
    const uint16_t i = lv_dropdown_get_selected(nfc_dd);
    return i < nfc_dd_n ? nfc_dd_ids[i] : 0;
}

struct BindReq { int id; char uid[24]; };
static BindReq bind_req;

static void do_bind_confirmed(void *) {
    inventory_request_bind(bind_req.id, bind_req.uid);
    ui_toast("Associando tag no Spoolman...", false);
}

/* Confere duplicidade no estoque local e pede confirmação. */
static void ask_bind(int id, const char *raw_uid) {
    char norm[24] = {0}, err[80] = {0};
    if (id <= 0) { nfc_set_status("Escolha um rolo na lista."); return; }
    if (!spool_validate_uid(raw_uid, norm, sizeof(norm), err, sizeof(err))) {
        nfc_set_status(err[0] ? err : "UID invalido.");
        return;
    }
    int count = 0;
    const WarehouseSpool *owner = spool_by_uid(norm, &count);
    char msg[200];
    if (owner && owner->info.id == id) {
        snprintf(msg, sizeof(msg), "A tag %s ja esta associada ao rolo #%d.", norm, id);
        nfc_set_status(msg);
        if (!nfc_status) ui_toast(msg, false);
        return;
    }
    if (owner) {
        snprintf(msg, sizeof(msg), "Tag %s ja associada ao rolo #%d. Nenhuma tag foi transferida.",
                 norm, owner->info.id);
        nfc_set_status(msg);
        if (!nfc_status) ui_toast(msg, true);
        return;
    }
    bind_req.id = id;
    ui_cpy(bind_req.uid, sizeof(bind_req.uid), norm);
    const WarehouseSpool *s = inventory_find(ui_inv, id);
    char line[96];
    if (s) spool_line(s, line, sizeof(line));
    else snprintf(line, sizeof(line), "#%d", id);
    snprintf(msg, sizeof(msg), "Associar a tag %s ao rolo %s? Os UIDs ja gravados no rolo serao mantidos.",
             norm, line);
    ui_confirm("ASSOCIAR TAG", msg, do_bind_confirmed, nullptr);
}

static void show_lookup(const NfcResult &r) {
    char norm[24] = {0};
    spool_normalize_uid(r.uid, norm, sizeof(norm));
    ui_cpy(nfc_last_uid, sizeof(nfc_last_uid), norm);
    int count = 0;
    const WarehouseSpool *s = spool_by_uid(norm, &count);
    char text[320];
    nfc_matched_spool = 0;
    if (nfc_auto_button) lv_obj_add_state(nfc_auto_button, LV_STATE_DISABLED);
    if (!ui_inv || ui_inv->status != INV_OK) {
        snprintf(text, sizeof(text), "UID: %s\nEstoque nao carregado (%s). Toque ATUALIZA no Armazem.",
                 norm, ui_inv && ui_inv->error_msg[0] ? ui_inv->error_msg : "sem dados");
    } else if (!s) {
        snprintf(text, sizeof(text), "UID: %s\nTag nao associada a nenhum rolo.\n"
                 "Escolha o rolo abaixo e toque ASSOCIAR ESTA TAG.", norm);
        if (nfc_link_btn) lv_obj_clear_flag(nfc_link_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        char line[96], rem[24];
        spool_line(s, line, sizeof(line));
        if (s->info.remaining_g >= 0) snprintf(rem, sizeof(rem), "%.0f g", s->info.remaining_g);
        else snprintf(rem, sizeof(rem), "desconhecido");
        snprintf(text, sizeof(text), "UID: %s\nRolo %s%s\nRestante: %s%s%s", norm, line,
                 s->archived ? " (arquivado)" : "", rem,
                 r.spool_id > 0 ? "\nNDEF na tag: Spoolman #" : "",
                 "");
        if (r.spool_id > 0) {
            size_t l = strlen(text);
            snprintf(text + l, sizeof(text) - l, "%d", r.spool_id);
        }
        if (count > 1) {
            size_t l = strlen(text);
            snprintf(text + l, sizeof(text) - l, "\nAtencao: UID repetido em %d rolos.", count);
        }
        if (!s->archived && count == 1 && r.ntag) {
            nfc_matched_spool = s->info.id;
            ui_cpy(nfc_matched_uid, sizeof(nfc_matched_uid), norm);
            if (nfc_auto_button) lv_obj_clear_state(nfc_auto_button, LV_STATE_DISABLED);
        }
        /* seleciona o rolo encontrado na lista */
        for (int i = 0; nfc_dd && i < nfc_dd_n; i++)
            if (nfc_dd_ids[i] == s->info.id) lv_dropdown_set_selected(nfc_dd, i);
    }
    nfc_set_status(text);
}

static void nfc_tick() {
    NfcResult result{};
    if (nfc_result(result) && nfc_target) {
        lv_obj_remove_event_cb(nfc_target, cancel_nfc_on_delete);
        nfc_target = nullptr;
        if (!result.ok || !result.uid[0]) {
            nfc_set_status(result.message);
            if (!nfc_status) ui_toast(result.message, true);
        } else if (nfc_mode == NFC_LOOKUP) {
            show_lookup(result);
        } else if (nfc_mode == NFC_BIND) {
            ask_bind(nfc_bind_id, result.uid);
        } else {
            char text[200];
            snprintf(text, sizeof(text), "%s\nUID: %s", result.message, result.uid);
            nfc_set_status(text);
        }
    }
    if (nfc_status && nfc_auto_active && ui_act_seq != nfc_auto_seq &&
        ui_act.kind == ACT_NFC_AUTO) {
        nfc_auto_seq = ui_act_seq;
        if (ui_act.message[0]) lv_label_set_text(nfc_status, ui_act.message);
        if (ui_act.state == AR_OK || ui_act.state == AR_ERR) nfc_auto_active = false;
    }
    /* Rolo recém-cadastrado: oferece associar a tag logo em seguida. */
    if (new_spool_wait && ui_act_seq != new_spool_seq && ui_act.kind == ACT_CREATE &&
        (ui_act.state == AR_OK || ui_act.state == AR_ERR)) {
        new_spool_wait = false;
        if (ui_act.state == AR_OK && ui_act.spool_id > 0) {
            char intro[120];
            snprintf(intro, sizeof(intro), "Rolo #%d cadastrado.\nAproxime a tag e toque LER E ASSOCIAR.",
                     ui_act.spool_id);
            open_nfc_for(ui_act.spool_id, intro);
        }
    }
}

static void build_spool_options(int preselect) {
    nfc_dd_n = 0;
    const size_t cap = (WAREHOUSE_MAX + 1) * 72;
    char *opts = (char *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!opts) return;
    opts[0] = 0;
    size_t used = 0;
    int sel = 0;
    bool have_pre = false;
    if (ui_inv && ui_inv->status == INV_OK) {
        for (int i = 0; i < ui_inv->count && nfc_dd_n < WAREHOUSE_MAX; i++) {
            const WarehouseSpool *s = &ui_inv->items[i];
            if (s->archived || s->info.id <= 0) continue;
            char line[72];
            spool_line(s, line, sizeof(line));
            used += snprintf(opts + used, cap - used, "%s%s", nfc_dd_n ? "\n" : "", line);
            if (s->info.id == preselect) { sel = nfc_dd_n; have_pre = true; }
            nfc_dd_ids[nfc_dd_n++] = s->info.id;
        }
    }
    if (preselect > 0 && !have_pre) { /* rolo novo ainda fora da lista local */
        char line[72];
        if (ui_act.saved.info.id == preselect) spool_line(&ui_act.saved, line, sizeof(line));
        else snprintf(line, sizeof(line), "#%d (novo)", preselect);
        used += snprintf(opts + used, cap - used, "%s%s", nfc_dd_n ? "\n" : "", line);
        sel = nfc_dd_n;
        nfc_dd_ids[nfc_dd_n++] = preselect;
    }
    if (!nfc_dd_n) snprintf(opts, cap, "Sem rolos carregados");
    lv_dropdown_set_options(nfc_dd, opts);
    lv_dropdown_set_selected(nfc_dd, sel);
    heap_caps_free(opts);
}

static void open_nfc_for(int spool_id, const char *intro) {
    lv_obj_t *box = ui_overlay_box("NFC", 720, 430);
    nfc_matched_spool = 0;
    nfc_last_uid[0] = 0;
    nfc_status = lv_label_create(box);
    lv_obj_add_event_cb(nfc_status, [](lv_event_t *) {
        if (nfc_auto_active) inventory_cancel_nfc_auto();
        nfc_auto_active = false;
        nfc_status = nfc_dd = nfc_adv = nfc_link_btn = nfc_auto_button = nfc_uid_ta = nullptr;
    }, LV_EVENT_DELETE, nullptr);
    ui_style_label(nfc_status, &lv_font_montserrat_14, COLOR_CREAM);
    lv_label_set_long_mode(nfc_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(nfc_status, 690);
    lv_obj_set_pos(nfc_status, 8, 34);
    lv_label_set_text(nfc_status, intro ? intro :
                      nfc_present() ? "PN532 conectado. Toque LER TAG ou escolha um rolo para associar."
                                    : "Aguardando o PN532 (UART2).");

    ui_make_btn(box, 8, 128, 190, 36, "LER TAG", COLOR_MINT, 0x081725,
                [](lv_event_t *) { scan_nfc(nfc_status, NFC_LOOKUP, 0); }, nullptr);
    nfc_link_btn = ui_make_btn(box, 210, 128, 300, 36, "ASSOCIAR ESTA TAG", COLOR_GOLD, 0x081725,
                               [](lv_event_t *) { ask_bind(nfc_selected_id(), nfc_last_uid); }, nullptr);
    lv_obj_add_flag(nfc_link_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *l = lv_label_create(box);
    ui_style_label(l, &lv_font_montserrat_14, COLOR_MUTED);
    lv_label_set_text(l, "Rolo do Spoolman:");
    lv_obj_set_pos(l, 8, 176);
    nfc_dd = make_dd(box, 8, 196, 470, "");
    build_spool_options(spool_id);
    if (ui_inv && ui_inv->status != INV_OK && ui_inv->status != INV_LOADING) inventory_request_refresh();
    ui_make_btn(box, 490, 196, 210, 36, "LER E ASSOCIAR", COLOR_CARD, COLOR_MINT,
                [](lv_event_t *) {
                    const int id = nfc_selected_id();
                    if (id <= 0) { nfc_set_status("Escolha um rolo na lista."); return; }
                    scan_nfc(nfc_status, NFC_BIND, id);
                }, nullptr);

    ui_make_btn(box, 8, 370, 170, 36, "AVANCADO", COLOR_CARD, COLOR_MUTED, [](lv_event_t *) {
        if (!nfc_adv) return;
        if (lv_obj_has_flag(nfc_adv, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(nfc_adv, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(nfc_adv, LV_OBJ_FLAG_HIDDEN);
    }, nullptr);
    ui_make_btn(box, 530, 370, 170, 36, "FECHAR", COLOR_CARD, COLOR_MUTED,
                [](lv_event_t *) { ui_close_overlays(); }, nullptr);

    /* Opções avançadas, escondidas por padrão. */
    nfc_adv = lv_obj_create(box);
    lv_obj_set_pos(nfc_adv, 0, 244);
    lv_obj_set_size(nfc_adv, 704, 120);
    lv_obj_set_style_bg_opa(nfc_adv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nfc_adv, 0, 0);
    lv_obj_set_style_pad_all(nfc_adv, 0, 0);
    lv_obj_clear_flag(nfc_adv, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(nfc_adv, LV_OBJ_FLAG_HIDDEN);
    nfc_uid_ta = make_ta(nfc_adv, 8, 6, 250, 36, "UID manual (AABBCCDD)", 22, true);
    ui_make_btn(nfc_adv, 268, 6, 210, 36, "ASSOCIAR UID", COLOR_CARD, COLOR_MINT, [](lv_event_t *) {
        if (nfc_uid_ta) ask_bind(nfc_selected_id(), lv_textarea_get_text(nfc_uid_ta));
    }, nullptr);
    ui_make_btn(nfc_adv, 8, 56, 250, 36, "GRAVAR NDEF NA TAG", COLOR_CARD, COLOR_MINT, [](lv_event_t *) {
        const int id = nfc_selected_id();
        if (id <= 0) { nfc_set_status("Escolha um rolo na lista."); return; }
        scan_nfc(nfc_status, NFC_WRITE, id);
    }, nullptr);
    nfc_auto_button = ui_make_btn(nfc_adv, 268, 56, 300, 36, "INSERIR NO ALIMENTADOR",
                                  COLOR_CARD, COLOR_MINT, [](lv_event_t *) {
        if (!inventory_request_nfc_auto(nfc_matched_spool, nfc_matched_uid)) {
            nfc_set_status("Estoque ocupado. Tente novamente.");
            return;
        }
        nfc_auto_active = true;
        nfc_auto_seq = ui_act_seq;
        lv_obj_add_state(nfc_auto_button, LV_STATE_DISABLED);
        nfc_set_status("Verificando os alimentadores. Aguarde para inserir.");
    }, nullptr);
    lv_obj_add_state(nfc_auto_button, LV_STATE_DISABLED);
}

static void open_nfc(lv_event_t *) { open_nfc_for(0, nullptr); }

void ui_warehouse_tick(void) {
    nfc_tick();
    if (!ui_inv) return;
    uint32_t seq = ui_inv_seq;
    if (inventory_copy(ui_inv, &seq) && seq != ui_inv_seq) {
        ui_inv_seq = seq;
        if (wh_status) {
            if (ui_inv->status == INV_LOADING) lv_label_set_text(wh_status, "Carregando...");
            else if (ui_inv->status == INV_OK) lv_label_set_text(wh_status, "Estoque atualizado");
            else if (ui_inv->status == INV_OFFLINE) lv_label_set_text(wh_status, "Impressora offline");
            else lv_label_set_text(wh_status, ui_inv->error_msg);
        }
        rebuild_dropdowns();
        fill_list();
        /* Menu NFC aberto: atualiza a lista de rolos mantendo a escolha. */
        if (nfc_dd && ui_inv->status == INV_OK) build_spool_options(nfc_selected_id());
    }
    if (wh_busy) {
        if (inventory_busy()) lv_obj_clear_flag(wh_busy, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(wh_busy, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_warehouse_build(void) {
    lv_obj_t *scr = scr_warehouse;
    lv_obj_t *t = lv_label_create(scr);
    ui_style_label(t, &font_press_start_16, COLOR_MINT);
    lv_label_set_text(t, "ARMAZEM DE ROLOS");
    lv_obj_set_pos(t, 12, 48);

    ui_make_btn(scr, 12, 8, 100, 28, "ATUALIZA", COLOR_CARD, COLOR_MINT, on_refresh, nullptr);
    /* ATUALIZA is under tabs? tabs are at y=8 right side. Left is free. Move: */
    /* actually tabs occupy 392+. x=12 is fine. But I already put ATUALIZA at 12,8 overlapping title.
       Put buttons at y=48 right of title. Recreate - I'll set pos after: */
    /* The two buttons were created at 12,8 — delete approach: just also create at right places.
       Let me not create at 12,8. I'll leave and add properly: */
    ui_make_btn(scr, 420, 46, 70, 28, "NOVO", COLOR_MINT, 0x081725, on_novo, nullptr);
    ui_make_btn(scr, 502, 46, 110, 28, "NFC", COLOR_CARD, COLOR_MINT, open_nfc, nullptr);

    wh_status = lv_label_create(scr);
    ui_style_label(wh_status, &font_press_start_12, COLOR_MUTED);
    lv_obj_set_pos(wh_status, 12, 72);
    lv_label_set_text(wh_status, "");

    wh_search = make_ta(scr, 12, 92, 280, 34, "Buscar...", 78, true);
    lv_obj_add_event_cb(wh_search, on_filter_change, LV_EVENT_VALUE_CHANGED, nullptr);

    wh_mat = make_dd(scr, 300, 92, 120, "Todos");
    wh_brand = make_dd(scr, 428, 92, 130, "Todos");
    wh_loc = make_dd(scr, 566, 92, 120, "Todos");
    wh_sort = make_dd(scr, 694, 92, 98, "Id\nPeso\nMaterial\nMarca");
    lv_obj_add_event_cb(wh_mat, on_filter_change, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(wh_brand, on_filter_change, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(wh_loc, on_filter_change, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(wh_sort, on_filter_change, LV_EVENT_VALUE_CHANGED, nullptr);

    wh_chip_arch = chip(scr, 12, 132, 130, "ARQUIVADOS", on_chip_arch, nullptr);
    wh_chip_low  = chip(scr, 150, 132, 110, "<=200 g", on_chip_low, nullptr);
    wh_chip_comp = chip(scr, 268, 132, 120, "COMPACTA", on_chip_comp, nullptr);
    chip(scr, 396, 132, 100, "LIMPAR", on_chip_clear, nullptr);

    wh_summary = lv_label_create(scr);
    ui_style_label(wh_summary, &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_pos(wh_summary, 510, 136);
    lv_label_set_text(wh_summary, "");

    wh_busy = lv_label_create(scr);
    ui_style_label(wh_busy, &font_press_start_12, COLOR_GOLD);
    lv_label_set_text(wh_busy, "...");
    lv_obj_set_pos(wh_busy, 360, 52);
    lv_obj_add_flag(wh_busy, LV_OBJ_FLAG_HIDDEN);

    wh_list = lv_obj_create(scr);
    lv_obj_set_pos(wh_list, 8, 168);
    lv_obj_set_size(wh_list, 784, 304);
    lv_obj_set_style_bg_opa(wh_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wh_list, 0, 0);
    lv_obj_set_style_pad_all(wh_list, 4, 0);
    lv_obj_set_style_radius(wh_list, 0, 0);
    lv_obj_set_scroll_dir(wh_list, LV_DIR_VER);

    ui_add_tabs(scr, 1);
}

/* ---------- FICHA ---------- */
static const WarehouseSpool *ficha_spool(void) {
    return inventory_find(ui_inv, ficha_id);
}

static void do_save_edit(void *) {
    if (inventory_busy()) { ui_toast("Aguarde.", false); return; }
    const char *loc = ta_location ? lv_textarea_get_text(ta_location) : "";
    const char *com = ta_comment ? lv_textarea_get_text(ta_comment) : "";
    bool setw = cb_weight && lv_obj_has_state(cb_weight, LV_STATE_CHECKED);
    float rem = 0;
    if (setw && ta_weight) rem = strtof(lv_textarea_get_text(ta_weight), nullptr);
    inventory_request_update(ficha_id, loc, com, setw, rem);
    ui_toast("Salvando...", false);
}

static void ask_save_edit(lv_event_t *) {
    ui_confirm("SALVAR",
               "Salvar no Spoolman? Isso nao movimenta a impressora.",
               do_save_edit, nullptr);
}

static void open_bind(lv_event_t *) {
    open_nfc_for(ficha_id, "Aproxime a tag deste rolo e toque LER E ASSOCIAR.");
}

static void do_assign_ok(void *) {
    inventory_request_assign(form_channel, ficha_id);
    ui_toast("Associando ao feeder...", false);
}
static void do_feed_ok(void *) {
    inventory_request_feed(form_channel, form_load);
    ui_toast(form_load ? "Carregando..." : "Retirando...", false);
}

static void pick_channel_then(bool assign, bool load, lv_event_t *) {
    form_load = load;
    lv_obj_t *box = ui_overlay_box(assign ? "ATIVAR ROLO" : (load ? "CARREGAR" : "RETIRAR"), 600, 340);
    if (ui_inv && ui_inv->external_mode == 2) {
        lv_obj_t *w = lv_label_create(box);
        ui_style_label(w, &lv_font_montserrat_14, COLOR_ALERT);
        lv_label_set_long_mode(w, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(w, 560);
        lv_obj_set_pos(w, 8, 36);
        lv_label_set_text(w, "Aviso: Filament Detection nao esta em External. Leitores internos podem apagar os dados.");
    }
    lv_obj_t *h = lv_label_create(box);
    ui_style_label(h, &lv_font_montserrat_14, COLOR_CREAM);
    lv_obj_set_pos(h, 8, 80);
    lv_label_set_text(h, "Escolha o feeder (1-4):");
for (int i = 0; i < 4; i++) {
        char lab[40];
        const FeederChannel *fc = ui_snap.feeder.valid ? &ui_snap.feeder.ch[i] : nullptr;
        snprintf(lab, sizeof(lab), "F%d %s", i + 1,
                 (fc && fc->filament_detected) ? "fil." : (fc && !fc->module_exist) ? "n/a" : "");
        int x = 16 + i * 140;
        /* store assign/load in user: (assign?100:0)+(load?10:0)+ch  */
        int token = (assign ? 100 : 0) + (load ? 10 : 0) + i;
        ui_make_btn(box, x, 120, 128, 48, lab, COLOR_CARD, COLOR_MINT,
                    [](lv_event_t *e) {
                        int tok = (int)(intptr_t)lv_event_get_user_data(e);
                        int ch = tok % 10;
                        bool asg = tok >= 100;
                        bool ld = ((tok / 10) % 10) == 1;
                        form_channel = ch;
                        form_load = ld;
                        if (asg) {
                            ui_confirm("ATIVAR ROLO",
                                       "Confirme que este e o rolo que esta no feeder escolhido. A associacao e enviada a U1.",
                                       do_assign_ok, nullptr);
                        } else {
                            ui_confirm(ld ? "CARREGAR" : "RETIRAR",
                                       "A U1 pode aquecer o bico e movimentar o filamento. Continuar?",
                                       do_feed_ok, nullptr);
                        }
                    }, (void *)(intptr_t)token);
    }
    ui_make_btn(box, 16, 270, 160, 36, "CANCELAR", COLOR_CARD, COLOR_MUTED,
                [](lv_event_t *){ ui_close_overlays(); }, nullptr);
}

static void on_assign(lv_event_t *e) { pick_channel_then(true, false, e); }
static void on_load(lv_event_t *e) { pick_channel_then(false, true, e); }
static void on_unload(lv_event_t *e) { pick_channel_then(false, false, e); }

static void do_delete_ok(void *) {
    const WarehouseSpool *s = ficha_spool();
    if (!s) { ui_toast("Rolo nao encontrado.", true); return; }
    const char *typed = ta_delete ? lv_textarea_get_text(ta_delete) : "";
    char expect[16];
    snprintf(expect, sizeof(expect), "%d", s->info.id);
    if (strcmp(typed, expect) != 0) {
        ui_toast("Digite o id do rolo para confirmar.", true);
        return;
    }
    char idn[160];
    spool_identity(s, idn, sizeof(idn));
    inventory_request_delete(s->info.id, idn);
    ui_toast("Excluindo...", false);
}

static void open_delete(lv_event_t *) {
    const WarehouseSpool *s = ficha_spool();
    if (!s) return;
    char idn[160], msg[280];
    spool_identity(s, idn, sizeof(idn));
    snprintf(msg, sizeof(msg), "Excluir %s?\nDigite o id %d para confirmar. Isso nao pode ser desfeito.", idn, s->info.id);
    lv_obj_t *box = ui_overlay_box("EXCLUIR ROLO", 600, 300);
    lv_obj_t *h = lv_label_create(box);
    ui_style_label(h, &lv_font_montserrat_14, COLOR_CREAM);
    lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(h, 570);
    lv_obj_set_pos(h, 8, 40);
    lv_label_set_text(h, msg);
    ta_delete = make_ta(box, 8, 140, 200, 36, "id", 8, true);
    lv_textarea_set_accepted_chars(ta_delete, "0123456789");
    ui_make_btn(box, 8, 220, 180, 36, "CANCELAR", COLOR_CARD, COLOR_MUTED,
                [](lv_event_t *){ ui_close_overlays(); }, nullptr);
    ui_make_btn(box, 300, 220, 220, 36, "EXCLUIR", COLOR_ALERT, 0x081725,
                [](lv_event_t *){
                    ui_confirm("EXCLUIR",
                               "Confirma a exclusao definitiva deste rolo no Spoolman?",
                               do_delete_ok, nullptr);
                }, nullptr);
}

void ui_ficha_open(int id) {
    if (!ui_inv) return;
    const WarehouseSpool *s = inventory_find(ui_inv, id);
    if (!s) { ui_toast("Rolo nao encontrado. Atualize o estoque.", true); return; }
    ficha_id = id;
    lv_obj_t *box = ui_overlay_box("FICHA DO ROLO", 760, 440);
    /* header */
    lv_obj_t *sw = lv_obj_create(box);
    lv_obj_set_pos(sw, 8, 36);
    lv_obj_set_size(sw, 36, 36);
    lv_obj_set_style_bg_color(sw, ui_C(ui_parse_hex(s->info.color_hex)), 0);
    lv_obj_set_style_border_width(sw, 1, 0);
    lv_obj_set_style_border_color(sw, ui_C(COLOR_CREAM), 0);
    lv_obj_set_style_radius(sw, 0, 0);
    lv_obj_set_style_pad_all(sw, 0, 0);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);

    char line[200], idn[160];
    spool_identity(s, idn, sizeof(idn));
    lv_obj_t *tt = lv_label_create(box);
    ui_style_label(tt, &font_press_start_12, COLOR_GOLD);
    lv_obj_set_pos(tt, 52, 36);
    lv_label_set_text(tt, idn);

    snprintf(line, sizeof(line), "Peso: %.0f / %.0f g   usado %.0f   %s%s  NFC: %s",
             s->info.remaining_g, s->initial_g, s->used_g,
             s->archived ? "ARQUIVADO  " : "",
             (s->info.id == ui_inv->active_id) ? "EM USO  " : "",
             s->card_uids[0] ? s->card_uids : "--");
    lv_obj_t *sub = lv_label_create(box);
    ui_style_label(sub, &lv_font_montserrat_14, COLOR_MUTED);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(sub, 700);
    lv_obj_set_pos(sub, 52, 58);
    lv_label_set_text(sub, line);

    lv_obj_t *l1 = lv_label_create(box);
    ui_style_label(l1, &font_press_start_12, COLOR_MUTED);
    lv_label_set_text(l1, "LOCAL");
    lv_obj_set_pos(l1, 8, 100);
    ta_location = make_ta(box, 8, 118, 280, 34, "local", 64, true);
    lv_textarea_set_text(ta_location, s->location);

    lv_obj_t *l2 = lv_label_create(box);
    ui_style_label(l2, &font_press_start_12, COLOR_MUTED);
    lv_label_set_text(l2, "COMENTARIO");
    lv_obj_set_pos(l2, 300, 100);
    ta_comment = make_ta(box, 300, 118, 430, 34, "comentario", 200, true);
    lv_textarea_set_text(ta_comment, s->comment);

    cb_weight = lv_checkbox_create(box);
    lv_checkbox_set_text(cb_weight, "Atualizar peso restante (g)");
    lv_obj_set_style_text_font(cb_weight, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cb_weight, ui_C(COLOR_CREAM), 0);
    lv_obj_set_pos(cb_weight, 8, 162);
    ta_weight = make_ta(box, 340, 156, 120, 34, "g", 8, true);
    if (s->info.remaining_g >= 0) {
        char g[16];
        snprintf(g, sizeof(g), "%.0f", (double)s->info.remaining_g);
        lv_textarea_set_text(ta_weight, g);
    }
    /* desmarcado por padrao: nao sobrescreve o consumo ao vivo */

    ui_make_btn(box, 8, 210, 180, 36, "SALVAR", COLOR_MINT, 0x081725, ask_save_edit, nullptr);
    ui_make_btn(box, 200, 210, 170, 36, "NFC", COLOR_CARD, COLOR_MINT, open_bind, nullptr);
    ui_make_btn(box, 380, 210, 170, 36, "ATIVAR", COLOR_GOLD, 0x081725, on_assign, nullptr);
    ui_make_btn(box, 560, 210, 170, 36, "CARREGAR", COLOR_CARD, COLOR_MINT, on_load, nullptr);
    ui_make_btn(box, 380, 258, 170, 36, "RETIRAR", COLOR_CARD, COLOR_MINT, on_unload, nullptr);
    ui_make_btn(box, 560, 258, 170, 36, "EXCLUIR", COLOR_CARD, COLOR_ALERT, open_delete, nullptr);
    ui_make_btn(box, 8, 370, 180, 36, "VOLTAR", COLOR_CARD, COLOR_MUTED,
                [](lv_event_t *){ ui_close_overlays(); }, nullptr);
}

/* ---------- NOVO ROLO ---------- */
static void do_create_ok(void *) {
    CreateSpoolReq r;
    memset(&r, 0, sizeof(r));
    ui_cpy(r.uid, sizeof(r.uid), ta_uid ? lv_textarea_get_text(ta_uid) : "");
    ui_cpy(r.location, sizeof(r.location), ta_location ? lv_textarea_get_text(ta_location) : "");
    ui_cpy(r.comment, sizeof(r.comment), ta_comment ? lv_textarea_get_text(ta_comment) : "");
    r.initial_g = ta_init ? strtof(lv_textarea_get_text(ta_init), nullptr) : 1000;
    r.remaining_g = ta_rem ? strtof(lv_textarea_get_text(ta_rem), nullptr) : r.initial_g;
    r.density = ta_density ? strtof(lv_textarea_get_text(ta_density), nullptr) : 1.24f;
    r.diameter = ta_diam ? strtof(lv_textarea_get_text(ta_diam), nullptr) : 1.75f;
    bool isnew = cb_newfil && lv_obj_has_state(cb_newfil, LV_STATE_CHECKED);
    if (isnew) {
        r.filament_id = 0;
        ui_cpy(r.vendor, sizeof(r.vendor), ta_vendor ? lv_textarea_get_text(ta_vendor) : "");
        ui_cpy(r.name, sizeof(r.name), ta_fname ? lv_textarea_get_text(ta_fname) : "");
        ui_cpy(r.material, sizeof(r.material), ta_material ? lv_textarea_get_text(ta_material) : "");
        ui_cpy(r.colors, sizeof(r.colors), ta_colors ? lv_textarea_get_text(ta_colors) : "");
        if (dd_dir) {
            char d[16] = {0};
            lv_dropdown_get_selected_str(dd_dir, d, sizeof(d));
            ui_cpy(r.direction, sizeof(r.direction), d);
        } else {
            ui_cpy(r.direction, sizeof(r.direction), "coaxial");
        }
    } else {
        /* selected filament id stored as option "id: name" */
        r.filament_id = 0;
        if (dd_filament) {
            char opt[80] = {0};
            lv_dropdown_get_selected_str(dd_filament, opt, sizeof(opt));
            r.filament_id = atoi(opt);
        }
        if (r.filament_id <= 0) {
            ui_toast("Escolha um filamento ou marque Novo filamento.", true);
            return;
        }
    }
    new_spool_wait = true;
    new_spool_seq = ui_act_seq;
    inventory_request_create(&r);
    ui_toast("Cadastrando rolo...", false);
}

void ui_new_spool_open(void) {
    if (!inventory_busy()) inventory_request_catalog();
    lv_obj_t *box = ui_overlay_box("NOVO ROLO", 760, 440);
    ta_uid = nullptr; /* a tag é associada depois de salvar (botao Associar tag) */
    lv_obj_t *u = lv_label_create(box);
    ui_style_label(u, &lv_font_montserrat_14, COLOR_MUTED);
    lv_label_set_text(u, "A tag NFC e associada logo depois de salvar.");
    lv_obj_set_pos(u, 8, 40);

    lv_obj_t *f = lv_label_create(box);
    ui_style_label(f, &font_press_start_12, COLOR_MUTED);
    lv_label_set_text(f, "FILAMENTO");
    lv_obj_set_pos(f, 8, 98);

    /* opções do catálogo, se já tiver */
    char opts[2048] = "Escolha...";
    if (ui_cat) {
        uint32_t cs = ui_cat_seq;
        inventory_copy_catalog(ui_cat, &cs);
        ui_cat_seq = cs;
        opts[0] = 0;
        strcat(opts, "Escolha...");
        for (int i = 0; i < ui_cat->n_filaments && (int)strlen(opts) < 1900; i++) {
            char line[80];
            snprintf(line, sizeof(line), "\n%d: %s %s %s",
                     ui_cat->filaments[i].id,
                     ui_cat->filaments[i].vendor,
                     ui_cat->filaments[i].material,
                     ui_cat->filaments[i].name);
            strcat(opts, line);
        }
    }
    dd_filament = make_dd(box, 8, 116, 400, opts);

    cb_newfil = lv_checkbox_create(box);
    lv_checkbox_set_text(cb_newfil, "Novo filamento");
    lv_obj_set_style_text_font(cb_newfil, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cb_newfil, ui_C(COLOR_CREAM), 0);
    lv_obj_set_pos(cb_newfil, 420, 118);

    ta_vendor = make_ta(box, 8, 156, 180, 32, "Marca", 64, true);
    ta_fname = make_ta(box, 196, 156, 200, 32, "Nome", 64, true);
    ta_material = make_ta(box, 404, 156, 120, 32, "PLA", 24, true);
    ta_colors = make_ta(box, 532, 156, 200, 32, "RRGGBB[,..]", 80, true);

    ta_density = make_ta(box, 8, 196, 100, 32, "1.24", 8, true);
    lv_textarea_set_text(ta_density, "1.24");
    ta_diam = make_ta(box, 116, 196, 100, 32, "1.75", 8, true);
    lv_textarea_set_text(ta_diam, "1.75");
    dd_dir = make_dd(box, 224, 196, 160, "coaxial\nlongitudinal");

    lv_obj_t *w = lv_label_create(box);
    ui_style_label(w, &font_press_start_12, COLOR_MUTED);
    lv_label_set_text(w, "PESO inicial / restante (g)");
    lv_obj_set_pos(w, 8, 236);
    ta_init = make_ta(box, 8, 254, 120, 32, "1000", 8, true);
    lv_textarea_set_text(ta_init, "1000");
    ta_rem = make_ta(box, 140, 254, 120, 32, "1000", 8, true);
    lv_textarea_set_text(ta_rem, "1000");

    ta_location = make_ta(box, 280, 254, 200, 32, "Local", 64, true);
    ta_comment = make_ta(box, 490, 254, 240, 32, "Comentario", 200, true);

    ui_make_btn(box, 8, 370, 180, 36, "CANCELAR", COLOR_CARD, COLOR_MUTED,
                [](lv_event_t *){ ui_close_overlays(); }, nullptr);
    ui_make_btn(box, 500, 370, 230, 36, "SALVAR", COLOR_MINT, 0x081725,
                [](lv_event_t *){
                    ui_confirm("NOVO ROLO",
                               "Salvar no Spoolman? Isso nao movimenta a impressora.",
                               do_create_ok, nullptr);
                }, nullptr);
}

/* ---------- DIARIO ---------- */
static void hist_refill(void) {
    if (!hist_list) return;
    lv_obj_clean(hist_list);
    const char *q = hist_search ? lv_textarea_get_text(hist_search) : "";
    const char *type = "";
    char tbuf[16] = {0};
    if (hist_type) {
        int s = lv_dropdown_get_selected(hist_type);
        const char *types[] = {"", "problem", "association", "read", "other"};
        if (s >= 0 && s < 5) type = types[s];
        (void)tbuf;
    }
    int idx[HISTORY_MAX];
    int n = history_filter(q, type, idx, HISTORY_MAX);
    if (n == 0) {
        lv_obj_t *l = lv_label_create(hist_list);
        ui_style_label(l, &font_press_start_12, COLOR_MUTED);
        lv_label_set_text(l, "Nenhum evento.");
        lv_obj_set_pos(l, 8, 8);
        return;
    }
    for (int i = 0; i < n; i++) {
        char line[HISTORY_LINE];
        history_get(idx[i], line, sizeof(line));
        const char *et = history_event_type(line);
        uint32_t col = COLOR_CREAM;
        if (strcmp(et, "problem") == 0) col = COLOR_ALERT;
        else if (strcmp(et, "association") == 0) col = COLOR_GOLD;
        else if (strcmp(et, "read") == 0) col = COLOR_MINT;
        lv_obj_t *row = lv_obj_create(hist_list);
        lv_obj_set_pos(row, 0, i * 36);
        lv_obj_set_size(row, 760, 34);
        lv_obj_set_style_bg_color(row, ui_C(COLOR_CARD), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_80, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lb = lv_label_create(row);
        ui_style_label(lb, &lv_font_montserrat_14, col);
        lv_label_set_long_mode(lb, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lb, 740);
        lv_label_set_text(lb, line);
    }
}

static void on_hist_change(lv_event_t *) { hist_refill(); }

void ui_history_on_show(void) { hist_refill(); }

void ui_history_build(void) {
    lv_obj_t *scr = scr_history;
    lv_obj_t *t = lv_label_create(scr);
    ui_style_label(t, &font_press_start_16, COLOR_MINT);
    lv_label_set_text(t, "DIARIO");
    lv_obj_set_pos(t, 12, 48);

    hist_search = make_ta(scr, 12, 88, 400, 34, "Buscar no diario...", 78, true);
    lv_obj_add_event_cb(hist_search, on_hist_change, LV_EVENT_VALUE_CHANGED, nullptr);
    hist_type = make_dd(scr, 424, 88, 200, "Todos\nProblema\nAssociacao\nLeitura\nOutros");
    lv_obj_add_event_cb(hist_type, on_hist_change, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *note = lv_label_create(scr);
    ui_style_label(note, &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_pos(note, 12, 128);
    lv_label_set_text(note, "Historico local desta tela (60 linhas). Compartilhar diagnostico nao esta disponivel neste painel.");

    hist_list = lv_obj_create(scr);
    lv_obj_set_pos(hist_list, 12, 160);
    lv_obj_set_size(hist_list, 776, 308);
    lv_obj_set_style_bg_opa(hist_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hist_list, 0, 0);
    lv_obj_set_style_pad_all(hist_list, 0, 0);
    lv_obj_set_style_radius(hist_list, 0, 0);
    lv_obj_set_scroll_dir(hist_list, LV_DIR_VER);

    ui_add_tabs(scr, 2);
}

/* ---------- WIFI ---------- */
static char pending_ssid[WIFI_SSID_LEN];

static void do_wifi_save(void *) {
    const char *pass = ta_uid ? lv_textarea_get_text(ta_uid) : ""; /* reuse ta_uid as password */
    wifi_save_and_restart(pending_ssid, pass);
}

static void open_wifi_pass(const char *ssid, bool locked) {
    ui_cpy(pending_ssid, sizeof(pending_ssid), ssid);
    lv_obj_t *box = ui_overlay_box("CONECTAR", 560, 280);
    lv_obj_t *h = lv_label_create(box);
    ui_style_label(h, &lv_font_montserrat_14, COLOR_CREAM);
    lv_obj_set_pos(h, 8, 40);
    char line[96];
    snprintf(line, sizeof(line), "Rede: %s", ssid);
    lv_label_set_text(h, line);
    ta_uid = make_ta(box, 8, 90, 500, 36, locked ? "Senha" : "(rede aberta)", 63, true);
    if (locked) lv_textarea_set_password_mode(ta_uid, true);
    ui_make_btn(box, 8, 200, 180, 36, "CANCELAR", COLOR_CARD, COLOR_MUTED,
                [](lv_event_t *){ ui_close_overlays(); }, nullptr);
    ui_make_btn(box, 300, 200, 220, 36, "SALVAR", COLOR_MINT, 0x081725,
                [](lv_event_t *){
                    char msg[160];
                    snprintf(msg, sizeof(msg), "Salvar \"%s\" e reiniciar a tela?", pending_ssid);
                    ui_confirm("WIFI", msg, do_wifi_save, nullptr);
                }, nullptr);
}

static void wifi_net_click(lv_event_t *e) {
    lv_event_stop_bubbling(e);
    WifiNet *n = (WifiNet *)lv_event_get_user_data(e);
    /* user_data can't hold the struct after fill — we store ssid in a static table */
    (void)n;
}

static WifiNet wifi_nets[WIFI_NET_MAX];
static int wifi_nn = 0;

static void wifi_fill(void) {
    if (!wifi_list) return;
    lv_obj_clean(wifi_list);
    if (wifi_scanning) {
        lv_obj_t *l = lv_label_create(wifi_list);
        ui_style_label(l, &font_press_start_12, COLOR_MUTED);
        lv_label_set_text(l, "Varrendo redes 2.4 GHz...");
        lv_obj_set_pos(l, 8, 8);
        return;
    }
    if (wifi_nn <= 0) {
        lv_obj_t *l = lv_label_create(wifi_list);
        ui_style_label(l, &font_press_start_12, COLOR_MUTED);
        lv_label_set_text(l, "Nenhuma rede. Toque em VARRER.");
        lv_obj_set_pos(l, 8, 8);
        return;
    }
    for (int i = 0; i < wifi_nn; i++) {
        lv_obj_t *row = lv_obj_create(wifi_list);
        lv_obj_set_pos(row, 0, i * 48);
        lv_obj_set_size(row, 760, 44);
        lv_obj_set_style_bg_color(row, ui_C(COLOR_CARD), 0);
        lv_obj_set_style_border_color(row, ui_C(COLOR_MINT), 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, [](lv_event_t *e) {
            int i = (int)(intptr_t)lv_event_get_user_data(e);
            if (i >= 0 && i < wifi_nn) open_wifi_pass(wifi_nets[i].ssid, wifi_nets[i].locked);
        }, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        char line[96];
        int bars = wifi_nets[i].rssi > -55 ? 4 : wifi_nets[i].rssi > -70 ? 3 : wifi_nets[i].rssi > -80 ? 2 : 1;
        snprintf(line, sizeof(line), "%s   %s   %d dBm  %s",
                 wifi_nets[i].ssid,
                 wifi_nets[i].locked ? "lock" : "aberta",
                 (int)wifi_nets[i].rssi,
                 bars >= 4 ? "****" : bars == 3 ? "***" : bars == 2 ? "**" : "*");
        lv_obj_t *lb = lv_label_create(row);
        ui_style_label(lb, &lv_font_montserrat_14, COLOR_CREAM);
        lv_label_set_text(lb, line);
        lv_obj_center(lb);
    }
}

static void wifi_show_current(void) {
    if (!wifi_cur) return;
    char ssid[WIFI_SSID_LEN] = {0}, ip[20] = {0};
    wifi_current(ssid, sizeof(ssid), ip, sizeof(ip));
    char line[160];
    if (ssid[0])
        snprintf(line, sizeof(line), "Agora: %s    IP %s%s", ssid, ip[0] ? ip : "--",
                 wifi_is_portal() ? "    (portal U1-DISPLAY)" : "");
    else
        snprintf(line, sizeof(line), "Desconectado. Portal de fallback: U1-DISPLAY (senha u1display).");
    lv_label_set_text(wifi_cur, line);
}

static void on_wifi_scan(lv_event_t *e) {
    if (e) lv_event_stop_bubbling(e);
    wifi_show_current();
    if (wifi_scan_start()) {
        wifi_scanning = true;
        wifi_fill();
    } else {
        ui_toast("Nao foi possivel varrer as redes.", true);
    }
}

void ui_wifi_on_show(void) {
    wifi_show_current();
    wifi_opened_ms = millis();
    on_wifi_scan(nullptr);
}

void ui_wifi_tick(void) {
    if (!wifi_scanning) return;
    if (lv_scr_act() != scr_wifi) return;
    int n = wifi_scan_poll();
    if (n == -1) return;
    wifi_scanning = false;
    if (n < 0) {
        wifi_nn = 0;
        ui_toast("Falha na varredura.", true);
    } else {
        wifi_nn = wifi_scan_copy(wifi_nets, WIFI_NET_MAX);
    }
    wifi_fill();
}

void ui_wifi_build(void) {
    lv_obj_t *scr = scr_wifi;
    lv_obj_t *t = lv_label_create(scr);
    ui_style_label(t, &font_press_start_16, COLOR_MINT);
    lv_label_set_text(t, "WI-FI");
    lv_obj_set_pos(t, 12, 48);

    wifi_cur = lv_label_create(scr);
    ui_style_label(wifi_cur, &lv_font_montserrat_14, COLOR_CREAM);
    lv_obj_set_pos(wifi_cur, 12, 88);
    lv_label_set_text(wifi_cur, "");

    lv_obj_t *note = lv_label_create(scr);
    ui_style_label(note, &lv_font_montserrat_14, COLOR_GOLD);
    lv_obj_set_pos(note, 12, 118);
    lv_label_set_text(note, "Esta placa so fala 2.4 GHz. Redes 5 GHz nao aparecem. A senha fica so na NVS da tela.");

    ui_make_btn(scr, 12, 150, 140, 36, "VARRER", COLOR_MINT, 0x081725, on_wifi_scan, nullptr);

    wifi_list = lv_obj_create(scr);
    lv_obj_set_pos(wifi_list, 12, 200);
    lv_obj_set_size(wifi_list, 776, 268);
    lv_obj_set_style_bg_opa(wifi_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_list, 0, 0);
    lv_obj_set_style_pad_all(wifi_list, 0, 0);
    lv_obj_set_style_radius(wifi_list, 0, 0);
    lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);

    ui_add_tabs(scr, 3);
}
