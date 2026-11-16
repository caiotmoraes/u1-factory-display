#include "control.h"
#include "strings_pt.h"
#include "ui_priv.h"
#include <HTTPClient.h>
#include <atomic>
#include <esp_heap_caps.h>

namespace {
lv_obj_t *screen, *content, *status_line, *estop_button, *file_list;
lv_obj_t *feeder_labels[4];
lv_obj_t *print_line;
lv_obj_t *heat_line;
lv_obj_t *fan_labels[4];
lv_obj_t *tab_buttons[5];
enum Page { Job, Filament, Move, Heat, Calibration };
Page page;
ControlFiles *alloc_files() {
    // ~4,6 KB: fica na PSRAM para não pesar na RAM interna (Wi-Fi/LVGL).
    void *p = heap_caps_calloc(1, sizeof(ControlFiles), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = calloc(1, sizeof(ControlFiles));
    assert(p);
    return static_cast<ControlFiles *>(p);
}
ControlFiles &files = *alloc_files();
char selection[192];
int heat_tool;
uint32_t rendered_files;
uint32_t last_snapshot;
std::atomic<bool> stop_running{false};
std::atomic<int> stop_code{0};

lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, const char *message, uint32_t color = COLOR_CREAM) {
    lv_obj_t *item = lv_label_create(parent);
    ui_style_label(item, &font_ui_14, color);
    lv_obj_set_pos(item, x, y);
    lv_obj_set_width(item, w);
    lv_label_set_long_mode(item, LV_LABEL_LONG_WRAP);
    lv_label_set_text(item, message);
    return item;
}

void submit(ControlOp op) {
    const bool job = op == ControlOp::Pause || op == ControlOp::Resume || op == ControlOp::Cancel;
    const char *path = job ? selection : op == ControlOp::Start ? selection : nullptr;
    if (!control_submit(op, heat_tool, path)) {
        ui_toast("Outra operação está em andamento.", true);
        return;
    }
    ui_toast("Solicitado a U1...", false);
}

void send(lv_event_t *event) {
    submit(static_cast<ControlOp>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
}

void confirm(lv_event_t *event) {
    const auto op = static_cast<ControlOp>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    const char *message;
    switch (op) {
    case ControlOp::Pause: case ControlOp::Resume: case ControlOp::Cancel:
        ui_cpy(selection, sizeof(selection), ui_snap.filename);
        message = op == ControlOp::Pause ? S_CTL_PAUSE_CONFIRM :
                  op == ControlOp::Resume ? S_CTL_RESUME_CONFIRM : S_CTL_CANCEL_CONFIRM;
        break;
    case ControlOp::Home: message = "Confira se a área de movimento está livre. Referenciar XYZ?"; break;
    case ControlOp::Mesh: message = "Retire peças da mesa, limpe os bicos e confira a placa. Medir nova malha?"; break;
    case ControlOp::Shaper: message = "A U1 vai vibrar nos eixos e salvar os resultados do input shaper."; break;
    case ControlOp::Offsets: message = "Confira mesa livre e bicos limpos. Calibrar os quatro offsets? A rotina pode salvar ajustes."; break;
    case ControlOp::Cool: message = "Desligar os aquecedores com a U1 ociosa?"; break;
    case ControlOp::MotorsOff: message = "Desligar os motores? Será preciso referenciar XYZ de novo."; break;
    case ControlOp::SelectTool: message = "Trocar para o cabeçote selecionado? A U1 vai movimentar o carro."; break;
    default: message = "Aquecer na temperatura selecionada? Mantenha a U1 supervisionada."; break;
    }
    ui_confirm("Confirmar na U1", message, [](void *data) {
        submit(static_cast<ControlOp>(reinterpret_cast<intptr_t>(data)));
    }, reinterpret_cast<void *>(static_cast<intptr_t>(op)));
}

void command(int x, int y, int w, const char *name, ControlOp op, bool confirmation = true) {
    ui_make_btn(content, x, y, w, 40, name, COLOR_CARD, COLOR_MINT,
                confirmation ? confirm : send, reinterpret_cast<void *>(static_cast<intptr_t>(op)));
}

void rebuild_files() {
    if (!file_list) return;
    lv_obj_clean(file_list);
    for (int i = 0; i < files.count; ++i) {
        lv_obj_t *b = lv_list_add_btn(file_list, LV_SYMBOL_FILE, files.paths[i]);
        lv_obj_add_event_cb(b, [](lv_event_t *event) {
            const int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
            if (i < 0 || i >= files.count) return;
            ui_cpy(selection, sizeof(selection), files.paths[i]);
            char question[240];
            snprintf(question, sizeof(question), "Iniciar este arquivo?\n%s", selection);
            ui_confirm("Iniciar impressão", question,
                       [](void *) { submit(ControlOp::Start); }, nullptr);
        }, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }
    if (files.more) lv_list_add_text(file_list, "Mais arquivos disponíveis pela interface web da U1.");
    rendered_files = files.version;
}

void build_page(Page selected) {
    page = selected;
    for (int i = 0; i < 5; ++i) {
        if (!tab_buttons[i]) continue;
        const bool on = i == static_cast<int>(selected);
        lv_obj_set_style_bg_color(tab_buttons[i], ui_C(on ? COLOR_ACCENT : COLOR_SURFACE2), 0);
        lv_obj_set_style_border_width(tab_buttons[i], on ? 0 : 1, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(tab_buttons[i], 0), ui_C(on ? 0xFFFFFF : COLOR_TEXT2), 0);
    }
    lv_obj_clean(content);
    file_list = print_line = heat_line = nullptr;
    memset(feeder_labels, 0, sizeof(feeder_labels));
    memset(fan_labels, 0, sizeof(fan_labels));
    if (page == Job) {
        print_line = label(content, 4, 0, 690, "Impressão atual");
        command(0, 30, 130, S_CTL_PAUSE, ControlOp::Pause);
        command(140, 30, 130, S_CTL_RESUME, ControlOp::Resume);
        command(280, 30, 130, "Cancelar", ControlOp::Cancel);
        command(450, 30, 250, "Listar arquivos", ControlOp::Files, false);
        file_list = lv_list_create(content);
        lv_obj_set_pos(file_list, 0, 82);
        lv_obj_set_size(file_list, 700, 250);
        lv_obj_set_style_radius(file_list, CARD_RADIUS, 0);
        lv_obj_set_style_border_color(file_list, ui_C(COLOR_LINE), 0);
        lv_obj_set_style_text_font(file_list, &font_ui_14, 0);
        control_copy_files(files);
        rebuild_files();
    } else if (page == Filament) {
        for (int i = 0; i < 4; ++i) {
            const int y = 4 + i * 82;
            lv_obj_t *row = ui_make_card(content, 0, y, 700, 70);
            feeder_labels[i] = label(row, 18, 24, 390, "Aguardando…");
            for (int unload = 0; unload < 2; ++unload) {
                const int action = i * 2 + unload;
                ui_make_btn(row, 408 + unload * 140, 14, 130, 40,
                            unload ? "Retirar" : "Carregar", COLOR_CARD, COLOR_MINT,
                            [](lv_event_t *event) {
                    const int a = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
                    char question[118];
                    snprintf(question, sizeof(question), "%s no cabeçote %d? A U1 pode aquecer e mover o filamento.",
                             a % 2 ? "Retirar" : "Carregar", a / 2 + 1);
                    ui_confirm("Filamento", question, [](void *data) {
                        if (inventory_busy()) { ui_toast("Outra operação está em andamento.", true); return; }
                        const int a = static_cast<int>(reinterpret_cast<intptr_t>(data));
                        inventory_request_feed(a / 2, a % 2 == 0);
                    }, reinterpret_cast<void *>(static_cast<intptr_t>(a)));
                }, reinterpret_cast<void *>(static_cast<intptr_t>(action)));
            }
        }
    } else if (page == Move) {
        label(content, 4, 0, 690, "Movimento manual com a U1 ociosa. Passos de 5 mm em X/Y e 1 mm em Z.");
        command(0, 34, 220, "Referenciar XYZ", ControlOp::Home);
        const ControlOp directions[] = {ControlOp::XMinus, ControlOp::XPlus, ControlOp::YMinus, ControlOp::YPlus,
                                        ControlOp::ZMinus, ControlOp::ZPlus};
        const char *names[] = {"X -5 mm", "X +5 mm", "Y -5 mm", "Y +5 mm", "Z -1 mm", "Z +1 mm"};
        for (int i = 0; i < 6; ++i) command(170 * (i % 2), 96 + 56 * (i / 2), 160, names[i], directions[i], false);
        command(380, 34, 320, "Desligar motores", ControlOp::MotorsOff);
    } else if (page == Heat) {
        heat_line = label(content, 4, 0, 690, "Temperaturas");
        command(0, 24, 150, "Mesa 55 °C", ControlOp::Bed55);
        command(160, 24, 150, "Mesa 75 °C", ControlOp::Bed75);
        command(320, 24, 250, "Desligar aquecedores", ControlOp::Cool);
        lv_obj_t *tool = lv_dropdown_create(content);
        lv_obj_set_pos(tool, 0, 72);
        lv_obj_set_size(tool, 172, 40);
        lv_obj_set_style_radius(tool, BTN_RADIUS, 0);
        lv_obj_set_style_border_color(tool, ui_C(COLOR_LINE), 0);
        lv_obj_set_style_bg_color(tool, ui_C(COLOR_SURFACE2), 0);
        lv_obj_set_style_pad_ver(tool, 11, 0);
        lv_dropdown_set_options(tool, "Cabeçote 1\nCabeçote 2\nCabeçote 3\nCabeçote 4");
        lv_dropdown_set_selected(tool, heat_tool);
        lv_obj_add_event_cb(tool, [](lv_event_t *e) {
            heat_tool = lv_dropdown_get_selected(lv_event_get_target(e));
            last_snapshot--;
        }, LV_EVENT_VALUE_CHANGED, nullptr);
        command(184, 72, 150, "Bico 200 °C", ControlOp::Nozzle200);
        command(344, 72, 150, "Bico 235 °C", ControlOp::Nozzle235);
        command(504, 72, 196, "Selecionar", ControlOp::SelectTool);
        /* Ventiladores: peça, exaustão, circulador interno e cavidade. */
        static const int pcts[] = {0, 25, 50, 75, 100};
        for (int f = 0; f < 4; ++f) {
            const int y = 124 + f * 52;
            fan_labels[f] = label(content, 4, y + 11, 200, "--");
            for (int i = 0; i < 5; ++i) {
                char name[8];
                snprintf(name, sizeof(name), "%d%%", pcts[i]);
                ui_make_btn(content, 210 + i * 98, y, 90, 42, name, COLOR_CARD, COLOR_MINT,
                            [](lv_event_t *e) {
                    const int code = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
                    if (!control_submit(ControlOp::FanSet, code))
                        ui_toast("Outra operação está em andamento. Tente de novo.", true);
                    else ui_toast("Ventilador: pedido enviado a U1.", false);
                }, reinterpret_cast<void *>(static_cast<intptr_t>(f * 1000 + pcts[i])));
            }
        }
    } else {
        label(content, 4, 0, 690, "Mantenha a área livre e acompanhe a U1 durante a calibração.");
        command(0, 34, 280, "Malha da mesa", ControlOp::Mesh);
        command(0, 94, 280, "Compensar vibração", ControlOp::Shaper);
        command(0, 154, 280, "Offsets dos 4 cabeçotes", ControlOp::Offsets);
        label(content, 300, 44, 400, "Mede a malha na configuração atual da mesa.");
        label(content, 300, 96, 400, "Executa o input shaper do firmware e salva o resultado.");
        label(content, 300, 164, 400, "Exige XYZ referenciado e os bicos limpos.");
        label(content, 4, 224, 690, "Se a resposta expirar, confira a U1. A rotina pode continuar em execução.");
    }
    last_snapshot--;
}

void estop_task(void *) {
    char ip[16]{}, url[90]{};
    uint16_t port = 7125;
    int code = -1;
    if (moonraker_endpoint(ip, sizeof(ip), &port) && ip[0]) {
        HTTPClient http;
        http.setConnectTimeout(2500);
        http.setTimeout(2500);
        snprintf(url, sizeof(url), "http://%s:%u/printer/emergency_stop", ip, port);
        if (http.begin(url)) {
            code = http.POST("");
            http.end();
        }
    }
    stop_code = code;
    stop_running = false;
    vTaskDelete(nullptr);
}
}

bool control_estop() {
    if (stop_running.exchange(true)) return false;
    if (xTaskCreate(estop_task, "u1-stop", 8192, nullptr, 4, nullptr) == pdPASS) return true;
    stop_running = false;
    return false;
}

bool control_estop_result(int &code) {
    return (code = stop_code.exchange(0)) != 0;
}

void ui_go_control(lv_event_t *) {
    ui_close_overlays();
    lv_scr_load(screen);
    control_ui_tick();
}

void control_ui_build() {
    lv_obj_t *root = ui_make_screen(&screen, NAV_CONTROL);
    ui_page_title(root, "Controle", nullptr);
    status_line = label(root, 4, 46, 690, "Controle remoto", COLOR_TEXT2);
    static const char *const tabs[] = {"Impressão", "Filamento", "Movimento", "Temperatura", "Calibração"};
    for (int i = 0; i < 5; ++i) {
        tab_buttons[i] = ui_make_btn(root, i * 142, 72, 132, 40, tabs[i], COLOR_CARD, COLOR_TEXT2,
                    [](lv_event_t *e) { build_page(static_cast<Page>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)))); },
                    reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_set_style_radius(tab_buttons[i], 20, 0);
    }
    estop_button = ui_make_btn(root, 440, 0, 260, 40, S_CTL_ESTOP, COLOR_RED, COLOR_ON_ACCENT,
                              [](lv_event_t *) {
        ui_confirm(S_CTL_ESTOP, S_CTL_ESTOP_CONFIRM,
                   [](void *) {
                       if (!control_estop()) ui_toast("Não foi possível iniciar a parada remota.", true);
                   }, nullptr);
    }, nullptr);
    content = lv_obj_create(root);
    lv_obj_set_pos(content, 0, 124);
    lv_obj_set_size(content, 700, 332);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    build_page(Job);
}

void control_ui_tick() {
    int result;
    if (control_estop_result(result))
        ui_toast(result >= 200 && result < 300 ? "Parada recebida pela U1." : "Parada sem confirmação. Use o controle físico.",
                 !(result >= 200 && result < 300));
    if (!screen || lv_scr_act() != screen) return;
    if (file_list) {
        control_copy_files(files);
        if (files.version != rendered_files) rebuild_files();
    }
    if (last_snapshot != ui_mr_seq) {
        last_snapshot = ui_mr_seq;
        if (print_line)
            lv_label_set_text_fmt(print_line, "%s  ·  %d%%  ·  %s", printer_state_label_pt(ui_snap.state),
                                  static_cast<int>(ui_snap.progress * 100 + 0.5f),
                                  ui_snap.filename[0] ? ui_snap.filename : "Sem arquivo");
        if (heat_line) {
            const auto &tool = ui_snap.feeder.ch[heat_tool];
            char text[135];
            snprintf(text, sizeof(text), "Mesa %.0f / %.0f °C  ·  Cabeçote %d: %.0f / %.0f °C",
                     ui_snap.bed_c, ui_snap.bed_target_c, heat_tool + 1, tool.nozzle_c, tool.nozzle_target_c);
            lv_label_set_text(heat_line, text);
        }
        static const char *const fan_names[] = {S_FAN_PART, S_FAN_EXHAUST, S_FAN_CIRCULATION, S_FAN_CAVITY};
        const float speeds[] = {ui_snap.fan_part, ui_snap.fan_exhaust, ui_snap.fan_circulation, ui_snap.fan_cavity};
        for (int i = 0; i < 4 && fan_labels[i]; ++i) {
            if (speeds[i] < 0) lv_label_set_text_fmt(fan_labels[i], "%s: --", fan_names[i]);
            else lv_label_set_text_fmt(fan_labels[i], "%s: %d%%", fan_names[i],
                                       static_cast<int>(speeds[i] * 100 + 0.5f));
        }
        for (int i = 0; i < 4 && feeder_labels[i]; ++i) {
            const auto &f = ui_snap.feeder.ch[i];
            lv_label_set_text_fmt(feeder_labels[i], "Cabeçote %d  ·  %s  ·  %s  ·  rolo #%d", i + 1,
                                  !ui_snap.moonraker_ok || !f.module_exist ? "indisponível" :
                                  f.filament_detected ? "presente" : "vazio",
                                  f.filament_type[0] ? f.filament_type : "--", f.spool_id);
        }
    }
    char state[224];
    snprintf(state, sizeof(state), "%s  ·  %s", printer_state_label_pt(ui_snap.state),
             inventory_busy() ? "Operação pendente" : ui_act.kind == ACT_CONTROL && ui_act.message[0]
                 ? ui_act.message : ui_snap.moonraker_ok ? "Moonraker conectado" : "Sem conexão");
    if (strcmp(lv_label_get_text(status_line), state)) lv_label_set_text(status_line, state);
}

#ifdef U1_SIM
void sim_control_page(int p) { build_page(static_cast<Page>(p)); }
#endif
