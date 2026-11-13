#pragma once
/* Gancho para o PN532 (ainda sem hardware).
 *
 * NÃO use o conector I2C da placa (GPIO 8/9): o endereço 0x24 do PN532
 * conflita com o CH422G. Caminhos seguros, a escolher antes de soldar:
 *  1. SPI — MOSI/SCK/MISO nos GPIOs 11/12/13; CS livre (nunca EXIO4/SD).
 *  2. UART1 em dois GPIOs livres (fora da matriz RGB e de 8, 9, 4).
 *  3. Segundo host I2C da S3 em GPIOs que não sejam 8/9.
 *
 * nfc_poll() é non-blocking e hoje sempre retorna false. Quando o módulo
 * chegar, implementar a leitura do UID (4, 7 ou 10 bytes) aqui, e a UI
 * (cadastro / vínculo / LER NFC) já chama este gancho a cada tick.
 */
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

void nfc_init(void);
/* true se o hardware foi detectado nesta sessão. */
bool nfc_present(void);
/* Se uma tag nova foi lida, escreve o UID normalizado (8/14/20 hex) e
 * retorna true. Sem módulo ou sem tag: false. */
bool nfc_poll(char *uid, size_t n);

#ifdef __cplusplus
}
#endif
