#pragma once
#include "snapshot.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void moonraker_init(void);
void moonraker_start_task(void);
/* Cópia thread-safe do último snapshot. Retorna true se há dados novos
 * desde a última chamada com o mesmo *seq (passe 0 na primeira). */
bool moonraker_copy(PrinterSnapshot *out, uint32_t *seq);
/* Força uma nova varredura da /24 (por exemplo após mudar de rede). */
void moonraker_force_discover(void);
/* IP/porta conhecidos da U1 (mesmo se o último poll falhou). */
bool moonraker_endpoint(char *ip, size_t n, uint16_t *port);

#ifdef __cplusplus
}
#endif
