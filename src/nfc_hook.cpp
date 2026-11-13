#include "nfc_hook.h"
#include "spool_parse.h"
#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#endif

void nfc_init(void) {
#ifdef ARDUINO
    Serial.println("[nfc] gancho pronto; PN532 ainda nao ligado");
#endif
}

bool nfc_present(void) { return false; }

bool nfc_poll(char *uid, size_t n) {
    if (uid && n) uid[0] = 0;
    /* TODO: quando o PN532 estiver no SPI (GPIO 11/12/13 + CS livre) ou
     * UART1, ler o UID aqui, validar com spool_validate_uid() e devolver
     * true uma vez por aproximação (debounce). */
    (void)uid; (void)n;
    return false;
}
