#include "control.h"
#include "ui_priv.h"
#include <HTTPClient.h>
#include <atomic>

namespace {
lv_obj_t *screen, *content, *status_line, *estop_button, *file_list;
lv_obj_t *feeder_labels[4];
lv_obj_t *print_line;
lv_obj_t *heat_line;
enum Page { Job, Filament, Move, Heat, Calibration };
Page page;
ControlFiles files;
char selection[192];
int heat_tool;
uint32_t rendered_files;
uint32_t last_snapshot;
std::atomic<bool> stop_running{false};
std::atomic<int> stop_code{0};

lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, const char *message, uint32_t color = COLOR_CREAM) {
    lv_obj_t *item = lv_label_create(parent);
    ui_style_label(item, &lv_font_montserrat_14, color);
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
        ui_toast("Outra operacao esta em andamento.", true);
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
        message = op == ControlOp::Pause ? "Pausar a impressao atual?" :
                  op == ControlOp::Resume ? "Retomar a impressao atual?" : "Cancelar a impressao atual?";
        break;
    case ControlOp::Home: message = "Confira se a area de movimento esta livre. Referenciar XYZ?"; break;
    case ControlOp::Mesh: message = "Retire pecas da mesa, limpe os bicos e confira a placa. Medir nova malha?"; break;
    case ControlOp::Shaper: message = "A U1 vai vibrar nos eixos e salvar os resultados do input shaper."; break;
    case ControlOp::Offsets: message = "Confira mesa livre e bicos limpos. Calibrar os quatro offsets? A rotina pode salvar ajustes."; break;
    case ControlOp::Cool: message = "Desligar os aquecedores com a U1 ociosa?"; break;
    case ControlOp::MotorsOff: message = "Desligar os motores? Será preciso referenciar XYZ de novo."; break;
    case ControlOp::SelectTool: message = "Trocar para o cabeçote selecionado? A U1 vai movimentar o carro."; break;
    default: message = "Aquecer na temperatura selecionada? Mantenha a U1 supervisionada."; break;
    }
    ui_confirm("CONFIRMAR NA U1", message, [](void *data) {
        submit(static_cast<ControlOp>(reinterpret_cast<intptr_t>(data)));
    }, reinterpret_cast<void *>(static_cast<intptr_t>(op)));
}

void command(int x, int y, int w, const char *name, ControlOp op, bool confirmation = true) {
    ui_make_btn(content, x, y, w, 36, name, COLOR_CARD, COLOR_MINT,
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
            ui_confirm("INICIAR IMPRESSAO", question,
                       [](void *) { submit(ControlOp::Start); }, nullptr);
        }, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }
    if (files.more) lv_list_add_text(file_list, "Mais arquivos disponiveis pela interface web da U1.");
    rendered_files = files.version;
}

void build_page(Page selected) {
    page = selected;
    lv_obj_clean(content);
    file_list = print_line = heat_line = nullptr;
    memset(feeder_labels, 0, sizeof(feeder_labels));
    if (page == Job) {
        print_line = label(content, 12, 0, 740, "Impressao atual");
        command(12, 48, 138, "PAUSAR", ControlOp::Pause);
        command(160, 48, 142, "RETOMAR", ControlOp::Resume);
        command(312, 48, 156, "CANCELAR", ControlOp::Cancel);
        command(510, 48, 240, "LISTAR ARQUIVOS", ControlOp::Files, false);
        file_list = lv_list_create(content);
        lv_obj_set_pos(file_list, 12, 96);
        lv_obj_set_size(file_list, 738, 244);
        control_copy_files(files);
        rebuild_files();
    } else if (page == Filament) {
        for (int i = 0; i < 4; ++i) {
            const int y = 12 + i * 75;
            feeder_labels[i] = label(content, 12, y + 7, 410, "Aguardando...");
            for (int unload = 0; unload < 2; ++unload) {
                const int action = i * 2 + unload;
                ui_make_btn(content, 420 + unload * 164, y, 154, 36,
                            unload ? "RETIRAR" : "CARREGAR", COLOR_CARD, COLOR_MINT,
                            [](lv_event_t *event) {
                    const int a = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
                    char question[118];
                    snprintf(question, sizeof(question), "%s no cabeçote %d? A U1 pode aquecer e mover o filamento.",
                             a % 2 ? "Retirar" : "Carregar", a / 2 + 1);
                    ui_confirm("FILAMENTO", question, [](void *data) {
                        if (inventory_busy()) { ui_toast("Outra operacao esta em andamento.", true); return; }
                        const int a = static_cast<int>(reinterpret_cast<intptr_t>(data));
                        inventory_request_feed(a / 2, a % 2 == 0);
                    }, reinterpret_cast<void *>(static_cast<intptr_t>(a)));
                }, reinterpret_cast<void *>(static_cast<intptr_t>(action)));
            }
        }
    } else if (page == Move) {
        label(content, 12, 2, 742, "Movimento manual com a U1 ociosa. Passos de 5 mm em X/Y e 1 mm em Z.");
        command(12, 44, 220, "REFERENCIAR XYZ", ControlOp::Home);
        const ControlOp directions[] = {ControlOp::XMinus, ControlOp::XPlus, ControlOp::YMinus, ControlOp::YPlus,
                                        ControlOp::ZMinus, ControlOp::ZPlus};
        const char *names[] = {"X -5 mm", "X +5 mm", "Y -5 mm", "Y +5 mm", "Z -1 mm", "Z +1 mm"};
        for (int i = 0; i < 6; ++i) command(12 + 170 * (i % 2), 103 + 53 * (i / 2), 148, names[i], directions[i], false);
        command(374, 44, 320, "DESLIGAR MOTORES", ControlOp::MotorsOff);
    } else if (page == Heat) {
        heat_line = label(content, 12, 0, 730, "Temperaturas");
        label(content, 12, 49, 730, "Mesa");
        command(12, 75, 160, "MESA 55 C", ControlOp::Bed55);
        command(183, 75, 160, "MESA 75 C", ControlOp::Bed75);
        label(content, 12, 127, 730, "Cabeçote para aquecimento");
        lv_obj_t *tool = lv_dropdown_create(content);
        lv_obj_set_pos(tool, 12, 151);
        lv_obj_set_width(tool, 172);
        lv_dropdown_set_options(tool, "Cabeçote 1\nCabeçote 2\nCabeçote 3\nCabeçote 4");
        lv_dropdown_set_selected(tool, heat_tool);
        lv_obj_add_event_cb(tool, [](lv_event_t *e) {
            heat_tool = lv_dropdown_get_selected(lv_event_get_target(e));
            last_snapshot--;
        }, LV_EVENT_VALUE_CHANGED, nullptr);
        command(196, 151, 174, "BICO 200 C", ControlOp::Nozzle200);
        command(382, 151, 174, "BICO 235 C", ControlOp::Nozzle235);
        command(567, 151, 171, "SELECIONAR", ControlOp::SelectTool);
        command(12, 205, 323, "DESLIGAR AQUECEDORES", ControlOp::Cool);
        label(content, 12, 252, 330, "Ventilação da peça");
        command(12, 280, 152, "VENT. 0%", ControlOp::FanOff, false);
        command(178, 280, 152, "VENT. 50%", ControlOp::FanHalf, false);
        command(344, 280, 152, "VENT. 100%", ControlOp::FanFull, false);
    } else {
        label(content, 12, 2, 735, "Mantenha a área livre e acompanhe a U1 durante a calibração.");
        command(12, 48, 295, "MALHA DA MESA", ControlOp::Mesh);
        command(12, 108, 295, "COMPENSAR VIBRACAO", ControlOp::Shaper);
        command(12, 168, 295, "OFFSETS 4 CABECOTES", ControlOp::Offsets);
        label(content, 327, 52, 412, "Mede a malha na configuração atual da mesa.");
        label(content, 327, 109, 412, "Executa o input shaper do firmware e salva o resultado.");
        label(content, 327, 171, 412, "Exige XYZ referenciado e os bicos limpos.");
        label(content, 12, 244, 723, "Se a resposta expirar, confira a U1. A rotina pode continuar em execução.");
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
    if (xTaskCreate(estop_task, "u1-stop", 6144, nullptr, 4, nullptr) == pdPASS) return true;
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
    screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, ui_C(COLOR_NAVY), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_add_tabs(screen, 4);
    status_line = label(screen, 12, 46, 752, "Controle remoto", COLOR_GOLD);
    const char *tabs[] = {"IMPRESSAO", "FILAMENTO", "MOVIMENTO", "TEMPERATURA", "CALIBRACAO"};
    for (int i = 0; i < 5; ++i)
        ui_make_btn(screen, 12 + i * 155, 78, 149, 35, tabs[i], COLOR_CARD, COLOR_MINT,
                    [](lv_event_t *e) { build_page(static_cast<Page>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)))); },
                    reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    estop_button = ui_make_btn(screen, 12, 8, 238, 29, "PARADA DE EMERGENCIA", 0x7A222C, COLOR_CREAM,
                              [](lv_event_t *) {
        ui_confirm("PARADA DE EMERGENCIA",
                   "Parar imediatamente a U1? Os motores e aquecedores serão desligados.",
                   [](void *) {
                       if (!control_estop()) ui_toast("Nao foi possivel iniciar a parada remota.", true);
                   }, nullptr);
    }, nullptr);
    content = lv_obj_create(screen);
    lv_obj_set_pos(content, 10, 126);
    lv_obj_set_size(content, 780, 348);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    build_page(Job);
}

void control_ui_tick() {
    int result;
    if (control_estop_result(result))
        ui_toast(result >= 200 && result < 300 ? "Parada recebida pela U1." : "Parada sem confirmacao. Use o controle fisico.",
                 !(result >= 200 && result < 300));
    if (!screen || lv_scr_act() != screen) return;
    if (file_list) {
        control_copy_files(files);
        if (files.version != rendered_files) rebuild_files();
    }
    if (last_snapshot != ui_mr_seq) {
        last_snapshot = ui_mr_seq;
        if (print_line)
            lv_label_set_text_fmt(print_line, "%s  |  %d%%  |  %s", printer_state_label_pt(ui_snap.state),
                                  static_cast<int>(ui_snap.progress * 100 + 0.5f),
                                  ui_snap.filename[0] ? ui_snap.filename : "Sem arquivo");
        if (heat_line) {
            const auto &tool = ui_snap.feeder.ch[heat_tool];
            char text[135];
            snprintf(text, sizeof(text), "Mesa %.0f / %.0f C  |  Cabeçote %d: %.0f / %.0f C",
                     ui_snap.bed_c, ui_snap.bed_target_c, heat_tool + 1, tool.nozzle_c, tool.nozzle_target_c);
            lv_label_set_text(heat_line, text);
        }
        for (int i = 0; i < 4 && feeder_labels[i]; ++i) {
            const auto &f = ui_snap.feeder.ch[i];
            lv_label_set_text_fmt(feeder_labels[i], "Cabeçote %d  |  %s  |  %s  |  rolo #%d", i + 1,
                                  !ui_snap.moonraker_ok || !f.module_exist ? "indisponivel" :
                                  f.filament_detected ? "presente" : "vazio",
                                  f.filament_type[0] ? f.filament_type : "--", f.spool_id);
        }
    }
    char state[224];
    snprintf(state, sizeof(state), "%s  |  %s", printer_state_label_pt(ui_snap.state),
             inventory_busy() ? "Operacao pendente" : ui_act.kind == ACT_CONTROL && ui_act.message[0]
                 ? ui_act.message : ui_snap.moonraker_ok ? "Moonraker conectado" : "Sem conexao");
    if (strcmp(lv_label_get_text(status_line), state)) lv_label_set_text(status_line, state);
}
