/* U1 Spool Spec v1: fixtures compartilhadas com o app (spec/fixtures/uids.json). */
#include "spool_parse.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <fstream>
#include <sstream>

static int fails = 0;
static void ok(const std::string &name, bool cond, const std::string &got = "") {
    if (!cond) { printf("FAIL  %s  (obtido: %s)\n", name.c_str(), got.c_str()); fails++; }
    else printf("ok    %s\n", name.c_str());
}

static std::string join(JsonArrayConst a) {
    std::string s;
    for (JsonVariantConst v : a) { if (!s.empty()) s += ","; s += v.as<const char *>(); }
    return s;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "spec/fixtures/uids.json";
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    JsonDocument doc;
    if (deserializeJson(doc, ss.str())) { printf("FAIL  fixtures ilegíveis: %s\n", path); return 1; }

    for (JsonObjectConst c : doc["card_uids_read"].as<JsonArrayConst>()) {
        /* monta um rolo como o Spoolman devolve */
        JsonDocument sp;
        sp["id"] = 1;
        sp["filament"]["material"] = "PLA";
        if (!c["extra"].isNull()) sp["extra"]["card_uids"] = c["extra"];
        for (JsonVariantConst t : c["tags"].as<JsonArrayConst>()) sp["tags"].add<JsonObject>()["uid"] = t;
        std::string js; serializeJson(sp, js);
        WarehouseSpool w; char err[64];
        bool parsed = spool_parse_one(js.c_str(), js.size(), &w, 1, err, sizeof(err));
        std::string name = std::string("read/") + (c["name"] | "?");
        ok(name + "/valid", parsed && join(c["valid"]) == w.card_uids, w.card_uids);
        ok(name + "/invalid", join(c["invalid"]) == w.uids_invalid, w.uids_invalid);
        for (JsonVariantConst v : c["valid"].as<JsonArrayConst>())
            ok(name + "/listed", spool_uid_listed(w.card_uids, v.as<const char *>()));
    }
    for (JsonObjectConst c : doc["card_uids_write"].as<JsonArrayConst>()) {
        std::string name = std::string("write/") + (c["name"] | "?");
        const char *raw = c["extra"] | "";
        char out[SPOOL_UIDS_LEN * 2];
        int rc = spool_uids_canonical(raw, c["add"] | "", out, sizeof(out));
        if (!c["error"].isNull()) { ok(name, rc == -1, std::to_string(rc)); continue; }
        ok(name, rc >= 0 && strcmp(out, c["expect"] | "") == 0, out);
        if (c["already"] | false) ok(name + "/already", rc == 0, std::to_string(rc));
    }
    int i = 0;
    for (JsonObjectConst c : doc["card_uid"].as<JsonArrayConst>()) {
        std::string v; serializeJson(c["value"], v);
        char out[24];
        bool got = spool_card_uid_hex(v.c_str(), out, sizeof(out));
        const char *exp = c["expect"] | "";
        ok("card_uid/" + std::to_string(i++) + " " + v, exp[0] ? (got && !strcmp(out, exp)) : (!got && !out[0]), out);
    }
    /* F1: substring não confirma (4 bytes contido num de 7) */
    ok("F1/substring", !spool_uid_listed("04A1B2C3D4E5F6", "04A1B2C3"));
    printf(fails ? "\n%d FALHAS nas fixtures\n" : "\nFixtures Spec v1 ok.\n", fails);
    return fails ? 1 : 0;
}
