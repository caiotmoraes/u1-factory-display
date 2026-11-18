/* Imprime os pedidos que o firmware monta para vincular extrusor → rolo
 * (mesmas funções do inventory.cpp). Usado por scripts/test_protocol.py.
 * Uso: link_request <channel 0-3> <spool_id>
 * Saída: 1) caminho do POST gcode  2) caminho do PATCH no proxy  3) corpo do PATCH */
#include "inventory_logic.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "uso: %s <channel> <spool_id>\n", argv[0]); return 2; }
    const int ch = atoi(argv[1]), id = atoi(argv[2]);
    char script[64], enc[128], loc[65], body[160];
    if (!spoollink_set_spool_script(ch, id, script, sizeof(script)) ||
        !url_encode_component(script, enc, sizeof(enc)) ||
        !spool_u1_location(ch, loc, sizeof(loc)) ||
        !spool_location_patch_json(loc, body, sizeof(body))) {
        fprintf(stderr, "pedido inválido\n");
        return 1;
    }
    printf("/printer/gcode/script?script=%s\n/v1/spool/%d\n%s\n", enc, id, body);
    return 0;
}
