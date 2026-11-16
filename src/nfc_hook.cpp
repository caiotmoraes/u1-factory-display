#include "nfc_hook.h"
#include <Arduino.h>
#include <PN532.h>
#include <PN532_HSU.h>
#include <atomic>
#include <climits>

namespace {
PN532_HSU transport(Serial0, 44, 43);
PN532 reader(transport);
QueueHandle_t requests;
QueueHandle_t results;
std::atomic<bool> detected{false};
std::atomic<bool> busy{false};
std::atomic<bool> cancelled{false};
constexpr int RECORD_BYTES = 32;
constexpr char PREFIX[] = "Spoolman #";

bool select_tag(char (&uid)[21]) {
    // The library copies the chip-reported UID before returning its length.
    uint8_t bytes[64]{}, length = 0;
    if (!reader.readPassiveTargetID(PN532_MIFARE_ISO14443A, bytes, &length, 450, true) ||
        (length != 4 && length != 7 && length != 10)) return false;
    for (uint8_t i = 0; i < length; ++i) snprintf(uid + 2 * i, 3, "%02X", bytes[i]);
    return true;
}

bool exchange(uint8_t *command, uint8_t length, uint8_t *data, uint8_t expected) {
    uint8_t response[20]{};
    uint8_t size = sizeof(response);
    if (!reader.inDataExchange(command, length, response, &size) || size != expected) return false;
    if (expected) memcpy(data, response, expected);
    return true;
}

bool read_pages(uint8_t page, uint8_t *data) {
    uint8_t cmd[] = {0x30, page};
    return exchange(cmd, sizeof(cmd), data, 16);
}

bool is_ntag() {
    uint8_t version[8]{}, version_cmd[] = {0x60};
    return exchange(version_cmd, sizeof(version_cmd), version, sizeof(version)) &&
           version[1] == 0x04 && version[2] == 0x04 && version[3] == 0x02 &&
           (version[6] == 0x0F || version[6] == 0x11 || version[6] == 0x13);
}

bool read_record(uint8_t *data, bool writing) {
    uint8_t header[16]{};
    if (!read_pages(0, header) || header[12] != 0xE1 || header[13] != 0x10 ||
        header[14] < 4 || (writing && header[15] != 0)) return false;
    return read_pages(4, data) && read_pages(8, data + 16);
}

int parse_record(const uint8_t *data) {
    const unsigned length = data[1];
    constexpr unsigned fixed = 7 + sizeof(PREFIX) - 1;
    if (data[0] != 0x03 || length <= fixed || length + 2 >= RECORD_BYTES ||
        data[length + 2] != 0xFE || data[2] != 0xD1 || data[3] != 1 ||
        data[4] + 4u != length || memcmp(data + 5, "T\x02" "pt", 4) ||
        memcmp(data + 9, PREFIX, sizeof(PREFIX) - 1)) return 0;
    int id = 0;
    for (unsigned i = 2 + fixed; i < 2 + length; ++i) {
        const int digit = data[i] - '0';
        if (digit < 0 || digit > 9 || id > (INT_MAX - digit) / 10) return 0;
        id = id * 10 + digit;
    }
    return id;
}

bool write_page(uint8_t page, const uint8_t *data) {
    uint8_t cmd[] = {0xA2, page, data[0], data[1], data[2], data[3]};
    return exchange(cmd, sizeof(cmd), nullptr, 0);
}

const char *write_record(int id, NfcResult &result) {
    uint8_t existing[RECORD_BYTES]{};
    if (!result.ntag || !read_record(existing, true))
        return "Use uma NTAG213, 215 ou 216 NDEF gravavel.";
    const uint8_t empty[RECORD_BYTES]{};
    const uint8_t empty_ndef[RECORD_BYTES]{0x03, 0x00, 0xFE, 0x00};
    const uint8_t empty_tlv[RECORD_BYTES]{0xFE};
    const int old_id = parse_record(existing);
    if (old_id && old_id != id) return "Tag já gravada para outro rolo.";
    if (!old_id && memcmp(existing, empty, sizeof(existing)) &&
        memcmp(existing, empty_ndef, sizeof(existing)) &&
        memcmp(existing, empty_tlv, sizeof(existing))) return "Tag contem outros dados.";
    if (cancelled.load()) return "Cancelado.";

    uint8_t record[RECORD_BYTES]{0x03, 0, 0xD1, 1, 0, 'T', 2, 'p', 't'};
    const int text_len = snprintf(reinterpret_cast<char *>(record + 9), sizeof(record) - 9,
                                  "%s%d", PREFIX, id);
    record[1] = 7 + text_len;
    record[4] = 3 + text_len;
    record[9 + text_len] = 0xFE;

    // Keep the TLV empty until all trailing pages have been written.
    if (!write_page(4, empty_ndef)) return "Falha de gravação. Releia a tag.";
    for (int page = RECORD_BYTES / 4 - 1; page > 0; --page) {
        if (!write_page(4 + page, record + page * 4)) return "Gravação incompleta. Releia a tag.";
    }
    if (!write_page(4, record)) return "Gravação incompleta. Releia a tag.";
    char verified_uid[21]{};
    if (!select_tag(verified_uid) || strcmp(verified_uid, result.uid) ||
        !read_record(existing, false) || memcmp(record, existing, sizeof(record)))
        return "Gravação sem confirmação. Releia a tag.";
    result.spool_id = id;
    result.ok = true;
    return "Gravação NDEF confirmada.";
}

bool probe() {
    return (reader.getFirmwareVersion() >> 24) == 0x32 &&
           reader.SAMConfig() && reader.setPassiveActivationRetries(0);
}

void service(void *) {
    reader.begin();
    detected = probe();
    int id;
    for (;;) {
        if (xQueueReceive(requests, &id, pdMS_TO_TICKS(3000)) != pdTRUE) {
            if (!detected) detected = probe();
            continue;
        }
        NfcResult result{};
        result.writing = id > 0;
        const char *message = "Sem tag. Aproxime uma tag e tente novamente.";
        detected = probe();
        if (!detected) message = "PN532 sem resposta. Confira a chave UART2 e o modo HSU.";
        else {
            const uint32_t started = millis();
            while (!cancelled && millis() - started < 12000) {
                if (select_tag(result.uid)) {
                    result.ntag = is_ntag();
                    if (id > 0) message = write_record(id, result);
                    else {
                        uint8_t bytes[RECORD_BYTES]{};
                        if (result.ntag && read_record(bytes, false)) result.spool_id = parse_record(bytes);
                        result.ok = true;
                        message = "UID lido.";
                    }
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        snprintf(result.message, sizeof(result.message), "%s", message);
        xQueueOverwrite(results, &result);
        busy = false;
    }
}
}

void nfc_init() {
    requests = xQueueCreate(1, sizeof(int));
    results = xQueueCreate(1, sizeof(NfcResult));
    if (requests && results &&
        // PN532_HSU::receive() faz espera ativa sem ceder a CPU; no core 0 isso
        // matava o IDLE0 de fome e o task watchdog (5 s, panic) reiniciava a placa.
        xTaskCreatePinnedToCore(service, "pn532", 6144, nullptr, 1, nullptr, 1) == pdPASS) return;
    if (requests) vQueueDelete(requests);
    if (results) vQueueDelete(results);
    requests = results = nullptr;
}

bool nfc_present() { return detected.load(); }

bool nfc_request(int spool_id) {
    if (!requests || busy.exchange(true)) return false;
    cancelled = false;
    xQueueReset(results);
    if (xQueueSend(requests, &spool_id, 0) == pdTRUE) return true;
    busy = false;
    return false;
}

bool nfc_result(NfcResult &out) {
    return results && xQueueReceive(results, &out, 0) == pdTRUE;
}

void nfc_cancel() { cancelled = true; }
