#pragma once
#include <stdint.h>

struct NfcResult {
    char uid[21];
    int spool_id;
    bool ok;
    bool writing;
    bool ntag;
    char message[112];
};

void nfc_init();
bool nfc_present();
// spool_id == 0 reads a tag. A positive ID writes a verified NDEF record.
bool nfc_request(int spool_id);
bool nfc_result(NfcResult &out);
void nfc_cancel();
