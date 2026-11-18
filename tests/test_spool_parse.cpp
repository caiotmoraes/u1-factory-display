/* Testes de host do parser Spoolman / envelope Moonraker v2. */
#include "spool_parse.h"
#include "inventory_logic.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails = 0;
static void ok(const char *name, bool cond) {
    if (!cond) { printf("FAIL  %s\n", name); fails++; }
    else printf("ok    %s\n", name);
}

static const char *V2_ONE =
    "{\"result\":{\"response\":{"
    "\"id\":40,\"remaining_weight\":540.0,\"initial_weight\":1000.0,"
    "\"location\":\"Prateleira A\",\"comment\":\"bom\","
    "\"archived\":false,"
    "\"filament\":{\"id\":7,\"name\":\"Volt\",\"material\":\"PLA\","
    "\"color_hex\":\"D5A62A\",\"vendor\":{\"name\":\"Generic\"},\"weight\":1000}"
    "},\"error\":null}}";

static const char *V2_LIST =
    "{\"result\":{\"response\":["
    "{\"id\":41,\"remaining_weight\":72.0,\"initial_weight\":1000,"
    "\"location\":\"Prateleira B\",\"archived\":false,"
    "\"filament\":{\"id\":2,\"name\":\"Masterprint\",\"material\":\"PETG\","
    "\"color_hex\":\"#FF4000\",\"vendor\":{\"name\":\"Masterprint\"}}},"
    "{\"id\":40,\"remaining_weight\":540.0,\"initial_weight\":1000,"
    "\"location\":\"Prateleira A\",\"archived\":false,"
    "\"filament\":{\"id\":7,\"name\":\"Volt\",\"material\":\"PLA\","
    "\"color_hex\":\"D5A62A\",\"vendor\":{\"name\":\"Generic\"}}},"
    "{\"id\":55,\"remaining_weight\":40.0,\"archived\":true,"
    "\"filament\":{\"name\":\"Vermelho\",\"material\":\"ABS\","
    "\"color_hex\":\"C0392B\",\"vendor\":{\"name\":\"Generic\"}}}"
    "],\"error\":null}}";

static const char *V2_ERR =
    "{\"result\":{\"response\":null,\"error\":{\"status_code\":404,\"message\":\"No spool with ID 9\"}}}";

static const char *RAW_ARRAY =
    "[{\"id\":18,\"remaining_weight\":188,\"filament\":{\"name\":\"Mint Silk\","
    "\"material\":\"PLA\",\"multi_color_hexes\":\"59ECC5,FFFFFF\","
    "\"vendor\":{\"name\":\"Foo\"}}}]";

static const char *TOP_RESPONSE =
    "{\"response\":[{\"id\":1,\"remaining_weight\":10,"
    "\"filament\":{\"name\":\"X\",\"material\":\"PLA\",\"color_hex\":\"112233\","
    "\"vendor\":{\"name\":\"V\"}}}],\"error\":null}";

/* v0.6.1: dados reais da U1 (16/11) — 4 rolos no Spoolman, filament_spool_id = [40,28,24,25] */
static const char *REAL4 =
    "{\"result\":{\"response\":["
    "{\"id\":40,\"remaining_weight\":512.7,\"archived\":false,\"filament\":{\"material\":\"PETG\",\"color_hex\":\"1A1A1A\",\"vendor\":{\"name\":\"SUNLU\"}}},"
    "{\"id\":28,\"remaining_weight\":977.1,\"archived\":false,\"filament\":{\"material\":\"PETG\",\"color_hex\":\"362111\",\"vendor\":{\"name\":\"SUNLU\"}}},"
    "{\"id\":24,\"remaining_weight\":578.3,\"archived\":false,\"filament\":{\"material\":\"PLA\",\"color_hex\":\"FFFFFF\",\"vendor\":{\"name\":\"SUNLU\"}}},"
    "{\"id\":25,\"remaining_weight\":787.7,\"archived\":false,\"filament\":{\"material\":\"PETG\",\"color_hex\":\"FF6A13\",\"vendor\":{\"name\":\"SUNLU\"}}}"
    "],\"error\":null}}";

static bool near(float a, float b) { return a > b - 0.05f && a < b + 0.05f; }

static const char *STATUS =
    "{\"result\":{\"spoolman_connected\":true,\"spool_id\":40}}";

int main(void) {
    WarehouseSpool sp;
    char err[96];

    /* --- um rolo, envelope v2 --- */
    ok("v2-one", spool_parse_one(V2_ONE, strlen(V2_ONE), &sp, 40, err, sizeof(err)));
    ok("v2-one-id", sp.info.id == 40);
    ok("v2-one-mat", strcmp(sp.info.material, "PLA") == 0);
    ok("v2-one-vendor", strcmp(sp.info.vendor, "Generic") == 0);
    ok("v2-one-name", strcmp(sp.info.name, "Volt") == 0);
    ok("v2-one-color", strcmp(sp.info.color_hex, "D5A62A") == 0);
    ok("v2-one-g", sp.info.remaining_g > 539.5f && sp.info.remaining_g < 540.5f);
    ok("v2-one-init", sp.initial_g > 999.f);
    ok("v2-one-loc", strcmp(sp.location, "Prateleira A") == 0);
    ok("v2-one-low", sp.info.low == false);
    ok("v2-one-fil-id", sp.filament_id == 7);

    /* --- lista --- */
    WarehouseSpool list[16];
    int n = spool_parse_list(V2_LIST, strlen(V2_LIST), list, 16, err, sizeof(err));
    ok("v2-list-n", n == 3);
    ok("v2-list-41", list[0].info.id == 41 && list[0].info.low == true);
    ok("v2-list-color-hash", strcmp(list[0].info.color_hex, "FF4000") == 0);
    ok("v2-list-archived", list[2].archived == true && list[2].info.id == 55);

    /* --- erro v2 --- */
    ok("v2-err", !spool_parse_one(V2_ERR, strlen(V2_ERR), &sp, 9, err, sizeof(err)));
    ok("v2-err-msg", strstr(err, "No spool") != NULL);
    ok("v2-err-list", spool_parse_list(V2_ERR, strlen(V2_ERR), list, 16, err, sizeof(err)) < 0);

    /* --- array cru + multi_color --- */
    n = spool_parse_list(RAW_ARRAY, strlen(RAW_ARRAY), list, 16, err, sizeof(err));
    ok("raw-array", n == 1 && list[0].info.id == 18);
    ok("multi-color", strcmp(list[0].info.color_hex, "59ECC5") == 0);
    ok("multi-stored", strstr(list[0].multi_color, "59ECC5") != NULL);

    /* --- response no topo --- */
    n = spool_parse_list(TOP_RESPONSE, strlen(TOP_RESPONSE), list, 16, err, sizeof(err));
    ok("top-response", n == 1 && list[0].info.id == 1);

    /* --- JSON inválido --- */
    ok("bad-json", spool_parse_list("{nope", 5, list, 16, err, sizeof(err)) < 0);

    /* --- lista vazia --- */
    const char *empty = "{\"result\":{\"response\":[],\"error\":null}}";
    ok("empty-list", spool_parse_list(empty, strlen(empty), list, 16, err, sizeof(err)) == 0);

    /* --- spool_id --- */
    int id = -1;
    bool conn = false;
    ok("status", spool_parse_active_id(STATUS, strlen(STATUS), &id, &conn) && id == 40 && conn);
    const char *nil = "{\"result\":{\"spoolman_connected\":true,\"spool_id\":null}}";
    ok("status-null", spool_parse_active_id(nil, strlen(nil), &id, &conn) && id == 0 && conn);

    /* --- cor --- */
    char col[8];
    spool_parse_color("#abCDef", col);
    ok("color-hash", strcmp(col, "ABCDEF") == 0);
    spool_parse_color("ff4000,111111", col);
    ok("color-first", strcmp(col, "FF4000") == 0);
    spool_parse_color("", col);
    ok("color-fallback", strcmp(col, "59ECC5") == 0);

    /* --- UID --- */
    char uid[24], uerr[64];
    ok("uid-ok-4", spool_validate_uid("04:A1:B2:C3", uid, sizeof(uid), uerr, sizeof(uerr))
                   && strcmp(uid, "04A1B2C3") == 0);
    ok("uid-ok-7", spool_validate_uid("04A1B2C3D4E5F6", uid, sizeof(uid), uerr, sizeof(uerr))
                   && strlen(uid) == 14);
    ok("uid-bad-len", !spool_validate_uid("AABBCC", uid, sizeof(uid), uerr, sizeof(uerr)));
    ok("uid-bad-char", !spool_validate_uid("GGHHIIJJ", uid, sizeof(uid), uerr, sizeof(uerr)));
    ok("uid-empty", !spool_validate_uid("", uid, sizeof(uid), uerr, sizeof(uerr)));

    spool_parse_uids_from_extra("\"04A1B2C3D4E5F6\"", uid, sizeof(uid));
    ok("uids-quoted", strcmp(uid, "04A1B2C3D4E5F6") == 0);
    char uids[96];
    spool_parse_uids_from_extra("\"\\\"04A1B2C3,AABBCCDD\\\"\"", uids, sizeof(uids));
    /* após tirar aspas: 04A1B2C3,AABBCCDD — AABBCCDD tem 8 dígitos, ok */
    ok("uids-json-quote", strstr(uids, "04A1B2C3") != NULL);

    /* identity */
    WarehouseSpool idsp;
    memset(&idsp, 0, sizeof(idsp));
    idsp.info.id = 41;
    strcpy(idsp.info.vendor, "Masterprint");
    strcpy(idsp.info.material, "PETG");
    strcpy(idsp.info.name, "Masterprint");
    idsp.filament_id = 2;
    char ident[128];
    spool_identity(&idsp, ident, sizeof(ident));
    ok("identity", strcmp(ident, "#41 · Masterprint PETG · Masterprint|2") == 0);

    /* peso ausente */
    const char *now = "{\"id\":3,\"filament\":{\"name\":\"n\",\"material\":\"PLA\",\"color_hex\":\"000000\"}}";
    ok("no-weight", spool_fill(now, strlen(now), &sp, 3) && sp.info.remaining_g < 0 && !sp.info.low);

    /* --- filtros --- */
    n = spool_parse_list(V2_LIST, strlen(V2_LIST), list, 16, err, sizeof(err));
    StockFilter f;
    stock_filter_clear(&f);
    int idx[16];
    int m = stock_apply_filter(list, n, &f, idx, 16);
    ok("filter-hide-arch", m == 2); /* 41 e 40, 55 arquivado */
    f.include_archived = true;
    m = stock_apply_filter(list, n, &f, idx, 16);
    ok("filter-show-arch", m == 3);
    stock_filter_clear(&f);
    strcpy(f.material, "PETG");
    m = stock_apply_filter(list, n, &f, idx, 16);
    ok("filter-mat", m == 1 && list[idx[0]].info.id == 41);
    stock_filter_clear(&f);
    f.low_stock = true;
    m = stock_apply_filter(list, n, &f, idx, 16);
    ok("filter-200g", m == 1 && list[idx[0]].info.id == 41); /* 72g; 40g está arquivado */
    stock_filter_clear(&f);
    strcpy(f.query, "petg branco"); /* "branco" não deve casar com Masterprint PETG */
    m = stock_apply_filter(list, n, &f, idx, 16);
    ok("filter-query-miss", m == 0);
    strcpy(f.query, "volt");
    m = stock_apply_filter(list, n, &f, idx, 16);
    ok("filter-query-hit", m == 1 && list[idx[0]].info.id == 40);
    strcpy(f.query, "prateleira");
    m = stock_apply_filter(list, n, &f, idx, 16);
    ok("filter-query-loc", m == 2);
    /* acentos */
    ok("query-accent", stock_matches_query("Prateleira A", "prateleira"));
    ok("query-accent2", stock_matches_query("Associação manual", "associacao"));

    stock_filter_clear(&f);
    f.sort = STOCK_SORT_WEIGHT;
    f.include_archived = true;
    m = stock_apply_filter(list, n, &f, idx, 16);
    ok("sort-weight", m == 3 && list[idx[0]].info.id == 55 && list[idx[1]].info.id == 41);

    StockSummary sum = stock_summary(list, n);
    ok("summary-active", sum.active == 2);
    ok("summary-arch", sum.archived == 1);
    ok("summary-low", sum.low200 == 1);
    ok("summary-kg", sum.known_weight_g > 610 && sum.known_weight_g < 614);

    char cname[32];
    stock_color_name("D5A62A", cname, sizeof(cname));
    ok("color-name-gold", strcmp(cname, "Dourado") == 0);
    stock_color_name("FF4000", cname, sizeof(cname));
    ok("color-name-orange", strcmp(cname, "Laranja") == 0);

    ok("hist-problem", strcmp(history_event_type("Falha ao associar o rolo"), "problem") == 0);
    ok("hist-problem-accent", strcmp(history_event_type("Sem conexão com a U1"), "problem") == 0);
    ok("hist-assoc", strcmp(history_event_type("Rolo #40 associado ao feeder 1"), "association") == 0);
    ok("hist-nfc", strcmp(history_event_type("Tag NFC 04A1B2C3 lida"), "read") == 0);
    ok("hist-other", strcmp(history_event_type("Estoque atualizado"), "other") == 0);
    ok("hist-label", strcmp(history_event_label_pt("problem"), "ATENCAO") == 0);

    /* spoollink info */
    char info[512], ierr[80];
    memset(&sp, 0, sizeof(sp));
    sp.info.id = 40;
    strcpy(sp.info.material, "PLA");
    strcpy(sp.info.vendor, "Generic");
    strcpy(sp.info.color_hex, "D5A62A");
    ok("link-info", spool_build_link_info(&sp, info, sizeof(info), ierr, sizeof(ierr)));
    ok("link-vendor", strstr(info, "\"VENDOR\":\"Generic\"") != NULL);
    ok("link-type", strstr(info, "\"MAIN_TYPE\":\"PLA\"") != NULL);
    ok("link-id", strstr(info, "\"SPOOL_ID\":40") != NULL);
    ok("link-rgb", strstr(info, "\"RGB_1\":") != NULL);
    ok("link-color-nums", strstr(info, "\"COLOR_NUMS\":1") != NULL);
    WarehouseSpool bad = sp;
    bad.info.material[0] = 0;
    ok("link-no-mat", !spool_build_link_info(&bad, info, sizeof(info), ierr, sizeof(ierr)));

    /* unique fields */
    char mats[8][24];
    int um = stock_unique_field(list, n, 0, (char *)mats, 8, 24);
    ok("unique-mat", um == 3); /* PETG, PLA, ABS — sorted ABS, PETG, PLA? A, P, P */

    /* HUD: spool ativo vem do extrusor, não do spool_id global do Moonraker */
    int ids[4] = {40, 38, 0, 17};
    ok("hud-ex-name-0", hud_parse_extruder_name("extruder") == 0);
    ok("hud-ex-name-2", hud_parse_extruder_name("extruder2") == 2);
    ok("hud-ex-name-3", hud_parse_extruder_name("extruder3") == 3);
    ok("hud-ex-name-bad", hud_parse_extruder_name("hotend") == 0);
    ok("hud-ex-null", hud_parse_extruder_name(NULL) == 0);
    /* bico 3 (índice 2) sem associação — o caso do rolo branco SUNLU */
    ok("hud-bico3-sem-rolo", hud_active_spool_id(ids, 2) == 0);
    ok("hud-bico1", hud_active_spool_id(ids, 0) == 40);
    ok("hud-bico2", hud_active_spool_id(ids, 1) == 38);
    ok("hud-bico4", hud_active_spool_id(ids, 3) == 17);
    ok("hud-ae-clamp", hud_active_spool_id(ids, 99) == 40); /* cai no 0 */

    /* payload exato de Ativar rolo no bico 3 (SUNLU White PLA #24) */
    WarehouseSpool sunlu{};
    sunlu.info.id = 24;
    strcpy(sunlu.info.material, "PLA");
    strcpy(sunlu.info.vendor, "SUNLU");
    strcpy(sunlu.info.name, "White");
    strcpy(sunlu.info.color_hex, "FFFFFF");
    ok("link-sunlu", spool_build_link_info(&sunlu, info, sizeof(info), ierr, sizeof(ierr)));
    ok("link-sunlu-vendor", strstr(info, "\"VENDOR\":\"SUNLU\"") != NULL);
    ok("link-sunlu-type", strstr(info, "\"MAIN_TYPE\":\"PLA\"") != NULL);
    ok("link-sunlu-id", strstr(info, "\"SPOOL_ID\":24") != NULL);
    ok("link-sunlu-rgb", strstr(info, "\"RGB_1\":16777215") != NULL);
    ok("link-sunlu-argb", strstr(info, "\"ARGB_COLOR\":4294967295") != NULL);


    /* firmware-config /settings: Filament Detection (item id "rfid") */
    const char *ext =
        "{\"snapmaker_components\":{\"items\":["
        "{\"id\":\"language\",\"current\":\"pt\"},"
        "{\"id\":\"rfid\",\"current\":\"external\"}]}}";
    ok("rfid-external", settings_parse_rfid_mode(ext, strlen(ext)) == 1);

    const char *openr =
        "{\"general\":{\"items\":[{\"id\":\"rfid\",\"current\":\"openrfid\"}]}}";
    ok("rfid-openrfid", settings_parse_rfid_mode(openr, strlen(openr)) == 2);

    const char *openr_case =
        "{\"general\":{\"items\":[{\"id\":\"rfid\",\"current\":\"OpenRFID\"}]}}";
    ok("rfid-openrfid-case", settings_parse_rfid_mode(openr_case, strlen(openr_case)) == 2);

    const char *wrapped =
        "{\"result\":{\"snapmaker_components\":{\"items\":["
        "{\"id\":\"rfid\",\"current\":\"external\"}]}}}";
    ok("rfid-wrapped", settings_parse_rfid_mode(wrapped, strlen(wrapped)) == 1);

    ok("rfid-empty", settings_parse_rfid_mode("{}", 2) == 0);
    ok("rfid-null", settings_parse_rfid_mode(NULL, 0) == 0);
    ok("rfid-bad", settings_parse_rfid_mode("not-json", 8) == 0);

    /* documento ~30 KB (tamanho real da U1: 30066) com o rfid enterrado no fim */
    {
        const size_t MIN = 30066;
        char *fat = (char *)malloc(MIN + 512);
        ok("rfid-fat-alloc", fat != NULL);
        if (fat) {
            size_t n = 0;
            const char *head = "{\"misc\":{\"items\":[";
            memcpy(fat + n, head, strlen(head)); n += strlen(head);
            int i = 0;
            while (n + 160 < MIN + 128) {
                if (i) fat[n++] = ',';
                int w = snprintf(fat + n, 160,
                    "{\"id\":\"dummy_%d\",\"current\":\"val_%d\","
                    "\"label\":\"Setting %d with a long description to pad the body\"}",
                    i, i, i);
                if (w < 0) break;
                n += (size_t)w;
                i++;
            }
            const char *tail = "]},\"snapmaker_components\":{\"items\":["
                               "{\"id\":\"rfid\",\"current\":\"openrfid\"}]}}";
            memcpy(fat + n, tail, strlen(tail) + 1);
            n += strlen(tail);
            ok("rfid-fat-size", n >= MIN);
            ok("rfid-fat-openrfid", settings_parse_rfid_mode(fat, n) == 2);
            char *pcur = strstr(fat, "\"current\":\"openrfid\"");
            ok("rfid-fat-found", pcur != NULL);
            if (pcur) {
                /* "openrfid" e "external" têm 8 caracteres */
                memcpy(pcur, "\"current\":\"external\"", 20);
                ok("rfid-fat-external", settings_parse_rfid_mode(fat, n) == 1);
            }
            printf("    firmware-config fixture %zu bytes, %d dummy items\n", n, i);
            free(fat);
        }
    }

    /* --- v0.6.1: peso de T1–T4 no Início (bug: só T1 tinha peso) --- */
    {
        const int ids[4] = {40, 28, 24, 25};
        WarehouseSpool inv4[8];
        /* estoque ainda não carregado: só o rolo ativo (#40, lido por id) tem peso */
        ok("hud-g-sem-estoque-t1", near(hud_channel_grams(NULL, 0, 40, 40, 512.7f), 512.7f));
        ok("hud-g-sem-estoque-t2", hud_channel_grams(NULL, 0, 28, 40, 512.7f) < 0);
        ok("hud-missing-sem-estoque", hud_inventory_missing(NULL, 0, ids));
        int n4 = spool_parse_list(REAL4, strlen(REAL4), inv4, 8, err, sizeof(err));
        ok("hud-real-n", n4 == 4);
        ok("hud-missing-carregado", !hud_inventory_missing(inv4, n4, ids));
        /* spool_id do Moonraker = 28 (só o ativo); os demais vêm do estoque */
        const float want[4] = {512.7f, 977.1f, 578.3f, 787.7f};
        for (int i = 0; i < 4; i++) {
            char nm[32];
            snprintf(nm, sizeof(nm), "hud-g-T%d", i + 1);
            ok(nm, near(hud_channel_grams(inv4, n4, ids[i], 28, 977.1f), want[i]));
        }
        /* ativo com leitura mais nova prevalece; rolo fora do estoque = -1 */
        ok("hud-g-ativo-novo", near(hud_channel_grams(inv4, n4, 28, 28, 970.0f), 970.0f));
        ok("hud-g-ativo-sem-peso", near(hud_channel_grams(inv4, n4, 28, 28, -1), 977.1f));
        ok("hud-g-desconhecido", hud_channel_grams(inv4, n4, 99, 28, 977.1f) < 0);
        ok("hud-g-sem-id", hud_channel_grams(inv4, n4, 0, 28, 977.1f) < 0);
        const int ids2[4] = {40, 28, 24, 99};
        ok("hud-missing-novo-id", hud_inventory_missing(inv4, n4, ids2));
        ok("hud-key-muda", hud_mapping_key(ids) != hud_mapping_key(ids2));
    }

    /* ---- SET_SPOOL_ID + local no Spoolman (bug 2026-11-18) ---- */
    {
        char b[96], e[160];
        ok("setspool-T2", spoollink_set_spool_script(1, 15, b, sizeof(b)) &&
                          !strcmp(b, "SET_SPOOL_ID CHANNEL=1 SPOOL_ID=15"));
        ok("setspool-T4", spoollink_set_spool_script(3, 12, b, sizeof(b)) &&
                          !strcmp(b, "SET_SPOOL_ID CHANNEL=3 SPOOL_ID=12"));
        ok("setspool-T1-0based", spoollink_set_spool_script(0, 13, b, sizeof(b)) &&
                                 !strcmp(b, "SET_SPOOL_ID CHANNEL=0 SPOOL_ID=13"));
        ok("setspool-canal-4", !spoollink_set_spool_script(4, 15, b, sizeof(b)) && !b[0]);
        ok("setspool-canal-neg", !spoollink_set_spool_script(-1, 15, b, sizeof(b)));
        ok("setspool-id-0", !spoollink_set_spool_script(1, 0, b, sizeof(b)));
        ok("setspool-buf", !spoollink_set_spool_script(1, 15, b, 10));
        ok("urlenc-script", url_encode_component("SET_SPOOL_ID CHANNEL=1 SPOOL_ID=15", e, sizeof(e)) &&
                            !strcmp(e, "SET_SPOOL_ID%20CHANNEL%3D1%20SPOOL_ID%3D15"));
        ok("urlenc-reservados", url_encode_component("a&b?c#d+é", e, sizeof(e)) &&
                                !strcmp(e, "a%26b%3Fc%23d%2B%C3%A9"));
        ok("urlenc-buf", !url_encode_component("SET_SPOOL_ID CHANNEL=1", e, 12) && !e[0]);
        ok("loc-T2", spool_u1_location(1, b, sizeof(b)) && !strcmp(b, "Snapmaker U1 - Extrusor 2"));
        ok("loc-T4", spool_u1_location(3, b, sizeof(b)) && !strcmp(b, "Snapmaker U1 - Extrusor 4"));
        ok("loc-canal-4", !spool_u1_location(4, b, sizeof(b)));
        ok("patch-json", spool_location_patch_json("Snapmaker U1 - Extrusor 2", e, sizeof(e)) &&
                         !strcmp(e, "{\"location\":\"Snapmaker U1 - Extrusor 2\"}"));
        ok("patch-json-escape", spool_location_patch_json("Arm\xc3\xa1rio \"A\"\\1\n", e, sizeof(e)) &&
                                !strcmp(e, "{\"location\":\"Arm\xc3\xa1rio \\\"A\\\"\\\\1\\u000a\"}"));
        ok("patch-json-buf", !spool_location_patch_json("Snapmaker U1 - Extrusor 2", e, 20));
        const char *ok_body = "{\"result\":\"ok\"}";
        ok("mr-ok", !moonraker_error_message(ok_body, strlen(ok_body), e, sizeof(e)));
        ok("mr-vazio", !moonraker_error_message("", 0, e, sizeof(e)));
        const char *eb = "{\"error\":{\"code\":400,\"message\":\"Unknown command:\\\"SET_SPOOL_ID\\\"\",\"traceback\":\"\"}}";
        ok("mr-erro-obj", moonraker_error_message(eb, strlen(eb), e, sizeof(e)) &&
                          !strcmp(e, "Unknown command:\"SET_SPOOL_ID\""));
        const char *es = "{\"error\":\"bad json\"}";
        ok("mr-erro-str", moonraker_error_message(es, strlen(es), e, sizeof(e)) && !strcmp(e, "bad json"));
        const char *er = "{\"result\":{\"error\":{\"message\":\"x\"}}}";
        ok("mr-erro-result", moonraker_error_message(er, strlen(er), e, sizeof(e)) && !strcmp(e, "x"));
        ok("mr-html", moonraker_error_message("<html>502</html>", 16, e, sizeof(e)) && e[0]);
    }

    printf(fails ? "\n%d FALHA(S)\n" : "\nTodos os testes de spool/filtro passaram.\n", fails);
    return fails ? 1 : 0;
}
