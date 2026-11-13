#include "snapshot.h"
#include <string.h>

void snapshot_clear(PrinterSnapshot *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->state = PRINTER_OFFLINE;
    s->last_spool_id = -1;
    s->printer_port = 7125;
}
