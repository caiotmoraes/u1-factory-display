/* Teste de host da máquina de estados (sem Arduino). */
#include "printer_state.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails = 0;
static void expect(const char *name, PrinterState got, PrinterState want) {
    if (got != want) {
        printf("FAIL  %s: got %s, want %s\n", name, printer_state_id(got), printer_state_id(want));
        fails++;
    } else {
        printf("ok    %s\n", name);
    }
}

static PrinterRawStatus R(bool c, const char *wh, const char *ps, const char *idle, float fil) {
    PrinterRawStatus r{};
    r.connected = c;
    strncpy(r.webhooks, wh, sizeof(r.webhooks) - 1);
    strncpy(r.print_stats, ps, sizeof(r.print_stats) - 1);
    strncpy(r.idle_timeout, idle, sizeof(r.idle_timeout) - 1);
    r.filament_used = fil;
    return r;
}

int main() {
    PrinterRawStatus r;
    r = R(false, "ready", "standby", "Idle", 0);   expect("offline", printer_classify(&r), PRINTER_OFFLINE);
    r = R(true, "ready", "standby", "Idle", 0);    expect("idle", printer_classify(&r), PRINTER_IDLE);
    r = R(true, "ready", "printing", "Printing", 0); expect("heating", printer_classify(&r), PRINTER_HEATING);
    r = R(true, "ready", "printing", "Printing", 12.5f); expect("printing", printer_classify(&r), PRINTER_PRINTING);
    r = R(true, "ready", "paused", "Printing", 50); expect("paused", printer_classify(&r), PRINTER_PAUSED);
    r = R(true, "ready", "complete", "Ready", 80); expect("complete-until-idle", printer_classify(&r), PRINTER_COMPLETE);
    r = R(true, "ready", "complete", "Idle", 80); expect("complete-then-idle", printer_classify(&r), PRINTER_IDLE);
    r = R(true, "ready", "cancelled", "Ready", 10); expect("cancelled-until-idle", printer_classify(&r), PRINTER_COMPLETE);
    r = R(true, "ready", "cancelled", "Idle", 10); expect("cancelled-then-idle", printer_classify(&r), PRINTER_IDLE);
    r = R(true, "error", "standby", "Idle", 0);    expect("webhooks-error", printer_classify(&r), PRINTER_ERROR);
    r = R(true, "shutdown", "printing", "Printing", 1); expect("webhooks-shutdown", printer_classify(&r), PRINTER_ERROR);
    r = R(true, "ready", "error", "Idle", 0);     expect("print-error", printer_classify(&r), PRINTER_ERROR);
    r = R(true, "ready", "standby", "Printing", 0); expect("busy-homing", printer_classify(&r), PRINTER_BUSY);
    r = R(true, "READY", "PRINTING", "PRINTING", 1); expect("case-insensitive", printer_classify(&r), PRINTER_PRINTING);
    r = R(true, "ready", "printing", "Printing", 0.01f); expect("tiny-filament-still-heating", printer_classify(&r), PRINTER_HEATING);
    r = R(true, "ready", "printing", "Printing", 0.06f); expect("just-extruded", printer_classify(&r), PRINTER_PRINTING);

    if (strcmp(printer_state_label_pt(PRINTER_PRINTING), "IMPRIMINDO") != 0) {
        printf("FAIL  label pt\n"); fails++;
    } else printf("ok    label pt\n");

    printf(fails ? "\n%d FALHA(S)\n" : "\nTodos os testes passaram.\n", fails);
    return fails ? 1 : 0;
}
