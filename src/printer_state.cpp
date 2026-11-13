#include "printer_state.h"
#include <string.h>
#include <ctype.h>

static int ieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        unsigned ca = (unsigned char)*a++, cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) return 0;
    }
    return *a == *b;
}

PrinterState printer_classify(const PrinterRawStatus *raw) {
    if (!raw || !raw->connected) return PRINTER_OFFLINE;

    /* 1. erro (Klipper caiu ou a impressão falhou) */
    if (ieq(raw->webhooks, "shutdown") || ieq(raw->webhooks, "error")) return PRINTER_ERROR;
    if (ieq(raw->print_stats, "error")) return PRINTER_ERROR;

    /* 2. aquecendo: print já começou, ainda não extrudou */
    if (ieq(raw->print_stats, "printing") && raw->filament_used <= 0.05f) return PRINTER_HEATING;

    /* 3. imprimindo */
    if (ieq(raw->print_stats, "printing")) return PRINTER_PRINTING;

    /* 4. pausada */
    if (ieq(raw->print_stats, "paused")) return PRINTER_PAUSED;

    /* 5. concluída / cancelada até o idle_timeout virar Idle */
    if (ieq(raw->print_stats, "complete") || ieq(raw->print_stats, "cancelled")) {
        if (ieq(raw->idle_timeout, "Idle")) return PRINTER_IDLE;
        return PRINTER_COMPLETE;
    }

    /* 6. ocupada: sem job, mas o Klipper está rodando G-code (home, macros) */
    if (ieq(raw->idle_timeout, "Printing")) return PRINTER_BUSY;

    return PRINTER_IDLE;
}

const char *printer_state_id(PrinterState s) {
    switch (s) {
        case PRINTER_OFFLINE:   return "offline";
        case PRINTER_IDLE:      return "idle";
        case PRINTER_HEATING:   return "heating";
        case PRINTER_PRINTING:  return "printing";
        case PRINTER_PAUSED:    return "paused";
        case PRINTER_COMPLETE:  return "complete";
        case PRINTER_ERROR:     return "error";
        case PRINTER_BUSY:      return "busy";
        default:                return "unknown";
    }
}

const char *printer_state_label_pt(PrinterState s) {
    switch (s) {
        case PRINTER_OFFLINE:   return "OFFLINE";
        case PRINTER_IDLE:      return "OCIOSA";
        case PRINTER_HEATING:   return "AQUECENDO";
        case PRINTER_PRINTING:  return "IMPRIMINDO";
        case PRINTER_PAUSED:    return "PAUSADA";
        case PRINTER_COMPLETE:  return "CONCLUÍDA";
        case PRINTER_ERROR:     return "ERRO";
        case PRINTER_BUSY:      return "OCUPADA";
        default:                return "????";
    }
}
