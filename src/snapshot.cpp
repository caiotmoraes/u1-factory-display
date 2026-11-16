#include "snapshot.h"
#include <string.h>

void snapshot_clear(PrinterSnapshot *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->state = PRINTER_OFFLINE;
    s->last_spool_id = -1;
    s->printer_port = 7125;
    s->fan_part = s->fan_exhaust = s->fan_circulation = s->fan_cavity = -1;
    s->chamber_c = -1000;
    s->chamber_target_c = 0;
}
