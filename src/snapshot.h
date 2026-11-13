#pragma once
#include "printer_state.h"
#include <stdint.h>
#include <stdbool.h>

#ifndef SNAPSHOT_STR
#define SNAPSHOT_STR 80
#endif

#define SPOOL_LOW_GRAMS 100.0f

typedef struct {
    bool valid;
    int id;
    char name[48];
    char material[24];
    char vendor[24];
    char color_hex[8];   /* RRGGBB, sem '#' */
    float remaining_g;
    bool low;            /* remaining_g < SPOOL_LOW_GRAMS */
} SpoolInfo;

/* 4 canais da U1: filament_feed left (extruder0/1) e right (extruder2/3). */
typedef struct {
    bool module_exist;
    bool filament_detected;
    bool disable_auto;
    char channel_state[24];
    char channel_error[16];
    int spool_id;
    char filament_type[16];
    char vendor[24];
    char color_hex[8];       /* RRGGBB de print_task_config */
    float nozzle_c;
    float nozzle_target_c;
} FeederChannel;

typedef struct {
    bool valid;
    bool printer_ready;
    bool busy_print;          /* printing ou paused */
    FeederChannel ch[4];
} FeederInfo;

typedef struct {
    bool wifi_ok;
    bool moonraker_ok;
    bool discovering;
    char printer_ip[16];
    uint16_t printer_port;
    char scan_ip[16];          /* IP sendo varrido, se houver */
    char status_msg[96];

    PrinterState state;
    bool cancelled;            /* complete mas foi cancelamento */
    char filename[SNAPSHOT_STR];
    float progress;            /* 0..1 */
    int layer;
    int layer_total;
    float remaining_s;
    float nozzle_c;
    float nozzle_target_c;
    float bed_c;
    float bed_target_c;
    float filament_used;
    char print_stats[16];

    int active_extruder;       /* 0-3, toolhead.extruder */

    SpoolInfo spool;           /* rolo do EXTRUSOR ATIVO (print_task_config), não o spool_id global do Moonraker */
    FeederInfo feeder;
    int last_spool_id;         /* para detectar troca */
    bool spool_changed;        /* pulso: UI deve tocar animação de troca */
    uint32_t updated_ms;
} PrinterSnapshot;

#ifdef __cplusplus
extern "C" {
#endif
void snapshot_clear(PrinterSnapshot *s);
#ifdef __cplusplus
}
#endif
