#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRINTER_OFFLINE = 0,
    PRINTER_IDLE,
    PRINTER_HEATING,
    PRINTER_PRINTING,
    PRINTER_PAUSED,
    PRINTER_COMPLETE,
    PRINTER_ERROR,
    PRINTER_BUSY,
    PRINTER_STATE_COUNT
} PrinterState;

/* Campos crus usados pela máquina de estados (inspirada no Klippyface). */
typedef struct {
    bool connected;
    char webhooks[16];       /* ready / startup / shutdown / error */
    char print_stats[16];    /* standby / printing / paused / complete / cancelled / error */
    char idle_timeout[16];   /* Idle / Ready / Printing */
    float filament_used;     /* mm, 0 = ainda não extrudou */
} PrinterRawStatus;

PrinterState printer_classify(const PrinterRawStatus *raw);
const char *printer_state_id(PrinterState s);       /* inglês, estável */
const char *printer_state_label_pt(PrinterState s); /* rótulo da HUD */

#ifdef __cplusplus
}
#endif
