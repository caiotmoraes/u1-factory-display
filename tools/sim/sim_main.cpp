/* Simulador de host: renderiza as telas da UI real (LVGL 8.4) em PNG 800x480. */
#include "lvgl.h"
#include "ui.h"
#include "ui_priv.h"
#include "control.h"
#include <stdio.h>

extern uint32_t g_ms;
extern PrinterSnapshot fake_snap;
extern uint32_t fake_seq;
void fake_load_inventory(const char *path);
void fake_set_thumb(const char *path);

static lv_color_t fb[800 * 480];
static lv_color_t draw_buf[800 * 480];
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p) {
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) fb[y * 800 + x] = *p++;
    lv_disp_flush_ready(d);
}

static void run(int ms) {
    for (int t = 0; t < ms; t += 10) { g_ms += 10; lv_tick_inc(10); lv_timer_handler(); }
}

static void shot(const char *path) {
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    run(60);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n800 480\n255\n");
    for (int i = 0; i < 800 * 480; i++) {
        uint16_t c = fb[i].full;
        unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63),
                                (unsigned char)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    fprintf(stderr, "wrote %s\n", path);
}

static void set_printing(void) {
    PrinterSnapshot &s = fake_snap;
    snapshot_clear(&s);
    s.wifi_ok = s.moonraker_ok = true;
    snprintf(s.printer_ip, sizeof(s.printer_ip), "192.168.0.18");
    s.printer_port = 7125;
    s.state = PRINTER_PRINTING;
    snprintf(s.filename, sizeof(s.filename), "suporte_parede_v3.gcode");
    snprintf(s.print_stats, sizeof(s.print_stats), "printing");
    s.progress = 0.62f; s.layer = 112; s.layer_total = 180; s.remaining_s = 5040;
    s.active_extruder = 1;
    s.nozzle_c = 215; s.nozzle_target_c = 215; s.bed_c = 70; s.bed_target_c = 70;
    s.chamber_c = 38; s.chamber_target_c = 0;
    s.fan_part = 1.0f; s.fan_exhaust = 0.5f; s.fan_circulation = 0.0f; s.fan_cavity = 0.25f;
    s.feeder.valid = true;
    const int ids[4] = {33, 38, 24, 29};
    const char *types[4] = {"PETG", "PETG", "PETG", "PLA"};
    const char *hex[4] = {"1A1A1A", "362111", "F2F2F2", "FF6A13"};
    for (int i = 0; i < 4; i++) {
        FeederChannel &c = s.feeder.ch[i];
        c.module_exist = c.filament_detected = true;
        c.spool_id = ids[i];
        snprintf(c.filament_type, sizeof(c.filament_type), "%s", types[i]);
        snprintf(c.vendor, sizeof(c.vendor), "SUNLU");
        snprintf(c.color_hex, sizeof(c.color_hex), "%s", hex[i]);
        c.nozzle_c = i == 1 ? 215 : 140; c.nozzle_target_c = i == 1 ? 215 : 140;
    }
    fake_seq++;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    lv_init();
    static lv_disp_draw_buf_t db;
    lv_disp_draw_buf_init(&db, draw_buf, nullptr, 800 * 480);
    static lv_disp_drv_t dd;
    lv_disp_drv_init(&dd);
    dd.hor_res = 800; dd.ver_res = 480; dd.flush_cb = flush; dd.draw_buf = &db; dd.full_refresh = 1;
    lv_disp_drv_register(&dd);

    history_init();
    history_add("Tag 533F95B4950001 identificada: rolo #38 SUNLU PETG Coffee Brown");
    history_add("Tag 53B992B4950001 associada ao rolo #37");
    history_add("Falha ao ler o estoque: Impressora offline / sem conexão");
    history_add("Rolo #24 ativado no alimentador 3");
    fake_load_inventory(getenv("SPOOLS") ? getenv("SPOOLS") : "/workspace/u1-screen/spools_sample.json");
    fake_set_thumb(getenv("THUMB"));
    set_printing();
    ui_init();
    /* histórico do gráfico: alguns pontos com ruído */
    for (int i = 0; i < 40; i++) {
        fake_snap.nozzle_c = 215 + 1.6f * sinf(i * 0.7f);
        fake_snap.chamber_c = 37.5f + 0.8f * sinf(i * 0.45f + 1);
        fake_seq++;
        run(3010);
    }
    fake_snap.nozzle_c = 215; fake_snap.chamber_c = 38; fake_seq++;
    run(300);
    if (getenv("MEMSTAT")) {
        lv_mem_monitor_t m;
        lv_mem_monitor(&m);
        auto count = [](lv_obj_t *o, auto &self) -> int {
            int n = 1;
            for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) n += self(lv_obj_get_child(o, i), self);
            return n;
        };
        lv_obj_t *scrs[] = {scr_factory, scr_details, scr_settings, scr_nfc, scr_warehouse, scr_history, scr_wifi};
        int tot = 0;
        for (lv_obj_t *o : scrs) tot += count(o, count);
        fprintf(stderr, "objetos LVGL (telas exceto Controle): %d\n", tot);
    }
    char p[512];
    snprintf(p, sizeof(p), "%s/v060_inicio.ppm", out); shot(p);

    ui_go_control(nullptr); run(300);
    snprintf(p, sizeof(p), "%s/v060_controle.ppm", out); shot(p);
    /* aba Temperatura (4ª) */
    extern void sim_control_page(int);
    sim_control_page(3); run(300);
    snprintf(p, sizeof(p), "%s/v060_controle_temperatura.ppm", out); shot(p);

    ui_go_warehouse(nullptr); run(500);
    snprintf(p, sizeof(p), "%s/v060_armazem.ppm", out); shot(p);
    ui_ficha_open(38); run(300);
    snprintf(p, sizeof(p), "%s/v060_ficha.ppm", out); shot(p);
    ui_close_overlays();

    ui_go_nfc(nullptr); run(300);
    snprintf(p, sizeof(p), "%s/v060_nfc.ppm", out); shot(p);
    ui_nfc_open(false); run(300);
    snprintf(p, sizeof(p), "%s/v060_nfc_leitor.ppm", out); shot(p);
    ui_close_overlays();

    ui_go_settings(nullptr); run(300);
    snprintf(p, sizeof(p), "%s/v060_ajustes.ppm", out); shot(p);
    ui_go_wifi(nullptr); run(500);
    snprintf(p, sizeof(p), "%s/v060_wifi.ppm", out); shot(p);
    ui_go_history(nullptr); run(300);
    snprintf(p, sizeof(p), "%s/v060_diario.ppm", out); shot(p);

    ui_go_factory(nullptr);
    ui_confirm("Parada de emergência", "Parar imediatamente a U1? Os motores e aquecedores serão desligados.",
               nullptr, nullptr);
    ui_toast("Ventilador: pedido enviado à U1.", false);
    run(300);
    snprintf(p, sizeof(p), "%s/v060_dialogo.ppm", out); shot(p);
    ui_close_overlays();

    fake_snap.state = PRINTER_OFFLINE; fake_snap.moonraker_ok = false; fake_snap.filename[0] = 0;
    snprintf(fake_snap.status_msg, sizeof(fake_snap.status_msg), "Procurando em 192.168.0.42…");
    fake_set_thumb(nullptr);
    fake_seq++; run(400);
    snprintf(p, sizeof(p), "%s/v060_inicio_offline.ppm", out); shot(p);
    return 0;
}
