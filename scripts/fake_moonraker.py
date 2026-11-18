#!/usr/bin/env python3
"""Moonraker mínimo para testar o protocolo (objetos, spoolman/status, proxy).
Uso: python3 scripts/fake_moonraker.py [--port 7125] [--scenario printing]
Cenários: idle, heating, printing, paused, complete, error, low_spool, change_spool
"""
from __future__ import annotations
import argparse, json, re, threading, time
from urllib.parse import parse_qs, urlsplit
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SCENARIO = "printing"
SPOOL_ID = 40
REMOTE_FILES = ("controle/teste.gcode",)
REMOTE_JOB = None

def objects(scenario: str) -> dict:
    base = {
        "webhooks": {"state": "ready"},
        "print_stats": {
            "state": "standby", "filename": "", "print_duration": 0,
            "filament_used": 0, "info": {"current_layer": 0, "total_layer": 0},
        },
        "virtual_sdcard": {"progress": 0},
        "display_status": {"progress": 0, "message": ""},
        "extruder": {"temperature": 24.0, "target": 0.0},
        "heater_bed": {"temperature": 24.0, "target": 0.0},
        "idle_timeout": {"state": "Idle"},
        "toolhead": {"homed_axes": "xyz", "position": [20, 20, 20, 0],
                     "axis_minimum": [0, 0, 0, 0], "axis_maximum": [270, 270, 270, 0]},
        "filament_feed left": {
            "extruder0": {"module_exist": True, "filament_detected": True,
                          "channel_state": "wait_insert", "channel_error": "ok", "disable_auto": False},
            "extruder1": {"module_exist": True, "filament_detected": False,
                          "channel_state": "wait_insert", "channel_error": "ok", "disable_auto": False},
        },
        "filament_feed right": {
            "extruder2": {"module_exist": True, "filament_detected": False,
                          "channel_state": "wait_insert", "channel_error": "ok", "disable_auto": False},
            "extruder3": {"module_exist": True, "filament_detected": False,
                          "channel_state": "wait_insert", "channel_error": "ok", "disable_auto": False},
        },
        "filament_detect": {"info": [{"CARD_UID": []}, {"CARD_UID": []}, {"CARD_UID": []}, {"CARD_UID": []}]},
        "print_task_config": {
            "filament_type": ["PLA", "NONE", "NONE", "NONE"],
            "filament_spool_id": [40, 0, 0, 0],
        },
    }
    if scenario == "heating":
        base["print_stats"].update(state="printing", filename="benchy.gcode",
                                    print_duration=12, filament_used=0,
                                    info={"current_layer": 0, "total_layer": 210})
        base["extruder"].update(temperature=180, target=215)
        base["heater_bed"].update(temperature=50, target=60)
        base["idle_timeout"]["state"] = "Printing"
        base["virtual_sdcard"]["progress"] = 0
    elif scenario == "printing":
        base["print_stats"].update(state="printing", filename="benchy.gcode",
                                    print_duration=5020, filament_used=3400,
                                    info={"current_layer": 142, "total_layer": 210},
                                    estimated_time=5020 + 4980)
        base["virtual_sdcard"]["progress"] = 0.67
        base["display_status"]["progress"] = 0.67
        base["extruder"].update(temperature=215, target=215)
        base["heater_bed"].update(temperature=60, target=60)
        base["idle_timeout"]["state"] = "Printing"
    elif scenario == "paused":
        objects_p = objects("printing")
        objects_p["result"]["status"]["print_stats"]["state"] = "paused"
        return objects_p
    elif scenario == "complete":
        base["print_stats"].update(state="complete", filename="benchy.gcode",
                                    print_duration=8100, filament_used=8200,
                                    info={"current_layer": 210, "total_layer": 210})
        base["virtual_sdcard"]["progress"] = 1.0
        base["extruder"].update(temperature=80, target=0)
        base["heater_bed"].update(temperature=40, target=0)
        base["idle_timeout"]["state"] = "Ready"
    elif scenario == "error":
        base["print_stats"]["state"] = "error"
        base["webhooks"]["state"] = "ready"
    elif scenario == "idle":
        pass
    # Estado mutável (SET_SPOOL_ID, filament_detect/set, inserção simulada).
    base["print_task_config"]["filament_spool_id"] = list(STATE["spool_ids"])
    for i, det in enumerate(STATE["detected"]):
        side = "filament_feed left" if i < 2 else "filament_feed right"
        ex = base[side]["extruder%d" % i]
        if det is not None:
            ex["filament_detected"] = det
            if det:
                ex["channel_state"] = "preload_finish"
    base["filament_detect"]["info"] = [dict(x) for x in STATE["detect_info"]]
    return {"result": {"status": base, "eventtime": 0}}

SPOOL = {
    40: {
        "id": 40, "remaining_weight": 540.0, "initial_weight": 1000.0, "used_weight": 460.0,
        "location": "Prateleira A", "comment": "", "archived": False,
        "filament": {
            "id": 1, "name": "Volt", "material": "PLA", "color_hex": "D5A62A",
            "vendor": {"id": 1, "name": "Generic"},
        },
        "extra": {"card_uids": '"AABBCCDD"'},
    },
    41: {
        "id": 41, "remaining_weight": 72.0, "initial_weight": 1000.0, "used_weight": 928.0,
        "location": "Caixa 2", "comment": "quase no fim", "archived": False,
        "filament": {
            "id": 2, "name": "Masterprint", "material": "PETG", "color_hex": "FF4000",
            "vendor": {"id": 2, "name": "Masterprint"},
        },
        "extra": {},
    },
    42: {
        "id": 42, "remaining_weight": 800.0, "initial_weight": 800.0, "used_weight": 0.0,
        "location": "", "comment": "arquivado", "archived": True,
        "filament": {
            "id": 1, "name": "Volt", "material": "PLA", "color_hex": "D5A62A",
            "vendor": {"id": 1, "name": "Generic"},
        },
        "extra": {},
    },
}

VENDORS = {
    1: {"id": 1, "name": "Generic"},
    2: {"id": 2, "name": "Masterprint"},
}
FILAMENTS = {
    1: {"id": 1, "name": "Volt", "material": "PLA", "color_hex": "D5A62A",
        "density": 1.24, "diameter": 1.75, "weight": 1000,
        "vendor": {"id": 1, "name": "Generic"}, "vendor_id": 1},
    2: {"id": 2, "name": "Masterprint", "material": "PETG", "color_hex": "FF4000",
        "density": 1.27, "diameter": 1.75, "weight": 1000,
        "vendor": {"id": 2, "name": "Masterprint"}, "vendor_id": 2},
}
FIELDS = [{"key": "card_uids", "name": "Card UIDs", "field_type": "text", "order": 1}]
NEXT_ID = {"spool": 43, "vendor": 3, "filament": 3}
LAST_SPOOLINK = None
LAST_GCODE = None
RFID_MODE = "openrfid"
# Estado observável pelos testes (GET /_fake/state, POST /_fake/config).
# Como na U1 real (paxx + SpoolLink, 2026-11-18): filament_detect/set só põe o
# UID no detector; quem grava filament_spool_id é o SET_SPOOL_ID.
STATE = {
    "spool_ids": [40, 0, 0, 0],
    "detected": [None, None, None, None],   # None = usa o cenário
    "detect_info": [{"CARD_UID": []} for _ in range(4)],
    "gcode_log": [],     # {"script", "source": "query"|"body", "raw_query"}
    "detect_log": [],
    "patch_log": [],     # {"path", "body" (texto cru)}
    "fail_set_spool": "",      # mensagem de erro do SpoolLink (HTTP 400)
    "fail_spoolman_patch": "", # mensagem de erro do Spoolman (v2 error)
}
SET_SPOOL_RE = re.compile(r"^SET_SPOOL_ID CHANNEL=(-?\d+) SPOOL_ID=(-?\d+)$")
SETTINGS_PAD = 30066

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):  # quieter
        print("[fake]", self.address_string(), fmt % args)

    def _json(self, obj, code=200):
        data = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/printer/info":
            return self._json({"result": {"state": "ready", "hostname": "lava"}})
        if path == "/_fake/state":
            return self._json(STATE)
        if path == "/printer/objects/query":
            obj = objects(SCENARIO)
            if REMOTE_JOB is not None:
                obj["result"]["status"]["print_stats"]["filename"] = REMOTE_JOB
            return self._json(obj)
        if path == "/server/files/list":
            return self._json({"result": [{"path": name, "size": 1024} for name in REMOTE_FILES]})
        if path == "/server/spoolman/status":
            return self._json({"result": {
                "spoolman_connected": True,
                "spool_id": SPOOL_ID,
            }})
        if path == "/server/spoolman/spool_id":
            return self._json({"result": {"spool_id": SPOOL_ID}})
        # Na U1 real /firmware-config é o nginx :80; Moonraker :7125 devolve 404.
        self._json({"error": "not found"}, 404)

    def do_POST(self):
        global SCENARIO, REMOTE_JOB
        path = self.path.split("?", 1)[0]
        n = int(self.headers.get("Content-Length", "0") or 0)
        raw = self.rfile.read(n) if n else b"{}"
        try:
            body = json.loads(raw.decode() or "{}")
        except json.JSONDecodeError:
            return self._json({"error": "bad json"}, 400)
        if path == "/printer/print/start":
            filename = body.get("filename")
            if SCENARIO in ("heating", "printing", "paused") or filename not in REMOTE_FILES:
                return self._json({"error": "cannot start"}, 409)
            REMOTE_JOB, SCENARIO = filename, "printing"
            return self._json({"result": "ok"})
        job_actions = {
            "/printer/print/pause": (("heating", "printing"), "paused"),
            "/printer/print/resume": (("paused",), "printing"),
            "/printer/print/cancel": (("heating", "printing", "paused"), "idle"),
        }
        if path in job_actions:
            allowed, next_state = job_actions[path]
            if SCENARIO not in allowed:
                return self._json({"error": "incorrect state"}, 409)
            SCENARIO = next_state
            return self._json({"result": "ok"})
        if path == "/printer/emergency_stop":
            SCENARIO = "error"
            return self._json({"result": "ok"})
        if path == "/_fake/config":
            for k, v in body.items():
                if k in STATE:
                    STATE[k] = v
            return self._json({"result": "ok"})
        if path == "/printer/filament_detect/set":
            ch = body.get("channel")
            if not isinstance(ch, int) or not 0 <= ch <= 3:
                return self._json({"error": {"code": 400, "message": "invalid channel"}}, 400)
            STATE["detect_log"].append(body)
            STATE["detect_info"][ch] = dict(body.get("info") or {})
            return self._json({"result": "ok"})
        if path == "/printer/spoollink/set":
            global LAST_SPOOLINK
            LAST_SPOOLINK = body
            return self._json({"result": "ok"})
        if path == "/printer/gcode/script":
            global LAST_GCODE
            raw_query = urlsplit(self.path).query
            q = parse_qs(raw_query, keep_blank_values=True)
            if "script" in q:
                script, source = q["script"][0], "query"
            else:
                script, source = body.get("script", ""), "body"
            LAST_GCODE = script
            STATE["gcode_log"].append({"script": script, "source": source, "raw_query": raw_query})
            if script.split(" ", 1)[0] == "SET_SPOOL_ID":
                m = SET_SPOOL_RE.match(script)
                if not m:
                    return self._json({"error": {"code": 400, "message": "Malformed command '%s'" % script}}, 400)
                ch, sid = int(m.group(1)), int(m.group(2))
                if not 0 <= ch <= 3:
                    return self._json({"error": {"code": 400, "message": "SpoolLink: CHANNEL must be 0-3"}}, 400)
                if STATE["fail_set_spool"]:
                    return self._json({"error": {"code": 400, "message": STATE["fail_set_spool"]}}, 400)
                STATE["spool_ids"][ch] = sid
            return self._json({"result": "ok"})
        if path == "/server/spoolman/proxy":
            method = (body.get("request_method") or "GET").upper()
            p = body.get("path", "")
            use_v2 = body.get("use_v2_response", False)
            raw_body = body.get("body")
            if isinstance(raw_body, str):
                try:
                    raw_body = json.loads(raw_body)
                except json.JSONDecodeError:
                    raw_body = {}
            if not raw_body:
                raw_body = {}

            def v2(obj, err=None, code=200):
                if use_v2:
                    return self._json({"result": {"response": obj, "error": err}}, code)
                if err:
                    return self._json({"error": err}, code if code != 200 else 400)
                return self._json({"result": obj})

            # list
            if method == "GET" and (p.startswith("/v1/spool?") or p == "/v1/spool"):
                q = parse_qs(urlsplit(p).query, keep_blank_values=True)
                rows = list(SPOOL.values())
                if "location" in q:
                    # Spoolman: busca parcial, sem diferenciar maiúsculas.
                    term = q["location"][0].lower()
                    rows = [s for s in rows if term in (s.get("location") or "").lower()]
                return v2(rows)
            if method == "GET" and p == "/v1/vendor":
                return v2(list(VENDORS.values()))
            if method == "GET" and p == "/v1/filament":
                return v2(list(FILAMENTS.values()))
            if method == "GET" and p == "/v1/field/spool":
                return v2(FIELDS)
            if method == "POST" and p == "/v1/field/spool/card_uids":
                FIELDS.append({"key": "card_uids", "name": raw_body.get("name", "Card UIDs"),
                               "field_type": raw_body.get("field_type", "text"), "order": 1})
                return v2(FIELDS[-1])
            if method == "POST" and p == "/v1/vendor":
                nid = NEXT_ID["vendor"]; NEXT_ID["vendor"] += 1
                v = {"id": nid, "name": raw_body.get("name", "X")}
                VENDORS[nid] = v
                return v2(v)
            if method == "POST" and p == "/v1/filament":
                nid = NEXT_ID["filament"]; NEXT_ID["filament"] += 1
                vid = raw_body.get("vendor_id", 0)
                vend = VENDORS.get(vid, {"id": vid, "name": "?"})
                f = {"id": nid, **{k: raw_body.get(k) for k in
                     ("name","material","color_hex","density","diameter","weight",
                      "multi_color_hexes","multi_color_direction")},
                     "vendor": vend, "vendor_id": vid}
                FILAMENTS[nid] = f
                return v2(f)
            if method == "POST" and p == "/v1/spool":
                nid = NEXT_ID["spool"]; NEXT_ID["spool"] += 1
                fid = raw_body.get("filament_id")
                fil = FILAMENTS.get(fid, {"id": fid, "name": "?", "material": "PLA",
                                          "color_hex": "FFFFFF", "vendor": {"name": "Generic"}})
                sp = {"id": nid, "filament": fil, "filament_id": fid,
                      "initial_weight": raw_body.get("initial_weight", 1000),
                      "used_weight": raw_body.get("used_weight", 0),
                      "remaining_weight": raw_body.get("initial_weight", 1000) - raw_body.get("used_weight", 0),
                      "location": raw_body.get("location", ""),
                      "comment": raw_body.get("comment", ""),
                      "archived": False,
                      "extra": raw_body.get("extra", {})}
                SPOOL[nid] = sp
                return v2(sp)
            if p.startswith("/v1/spool/"):
                try:
                    sid = int(p.split("?",1)[0].rsplit("/", 1)[-1])
                except ValueError:
                    sid = 0
                spool = SPOOL.get(sid)
                if method == "GET":
                    if not spool:
                        err = {"status_code": 404, "message": f"No spool with ID {sid}"}
                        return v2(None, err)
                    return v2(spool)
                if method == "PATCH":
                    STATE["patch_log"].append({"path": p, "body": body.get("body")})
                    if STATE["fail_spoolman_patch"]:
                        return v2(None, {"status_code": 500, "message": STATE["fail_spoolman_patch"]})
                    if not spool:
                        return v2(None, {"message": f"No spool with ID {sid}"})
                    for k in ("location", "comment", "remaining_weight", "extra"):
                        if k in raw_body:
                            spool[k] = raw_body[k]
                    return v2(spool)
                if method == "DELETE":
                    if sid in SPOOL:
                        del SPOOL[sid]
                        return v2({})
                    return v2(None, {"message": f"No spool with ID {sid}"})
            return self._json({"error": "invalid proxy request"}, 400)
        self._json({"error": "not found"}, 404)

def settings_payload():
    """JSON de /firmware-config/api/settings, ~SETTINGS_PAD bytes (real U1: 30066)."""
    items = [
        {"id": "language", "current": "en", "label": "Language"},
        {"id": "rfid", "current": RFID_MODE, "label": "Filament Detection",
         "options": ["external", "openrfid", "none"]},
    ]
    body = {"snapmaker_components": {"items": items}, "general": {"items": []}}
    raw = json.dumps(body, separators=(",", ":"))
    pad = SETTINGS_PAD - len(raw)
    if pad > 16:
        # preenche um grupo extra para chegar ao tamanho real
        filler = "x" * pad
        body["general"]["items"].append({
            "id": "_pad", "current": filler, "label": "padding",
        })
        raw = json.dumps(body, separators=(",", ":"))
    return body


class NginxHandler(BaseHTTPRequestHandler):
    """Porta 80 (nginx) na U1 real: só firmware-config."""
    def log_message(self, fmt, *args):
        print("[fake-http]", self.address_string(), fmt % args)

    def _json(self, obj, code=200):
        data = json.dumps(obj, separators=(",", ":")).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/firmware-config/api/settings":
            return self._json(settings_payload())
        self._json({"error": "not found"}, 404)


def main():
    global SCENARIO, SPOOL_ID, RFID_MODE, SETTINGS_PAD
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=7125)
    ap.add_argument("--http-port", type=int, default=8080,
                    help="porta estilo nginx para /firmware-config (na U1 real: 80). 0 desliga.")
    ap.add_argument("--scenario", default="printing",
                    choices=["idle", "heating", "printing", "paused", "complete", "error"])
    ap.add_argument("--spool", type=int, default=40)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--rfid-mode", default="openrfid",
                    help="valor de Filament Detection: openrfid (real) ou external")
    ap.add_argument("--settings-pad", type=int, default=30066,
                    help="tamanho mínimo do JSON de settings (real U1: 30066)")
    args = ap.parse_args()
    SCENARIO, SPOOL_ID = args.scenario, args.spool
    RFID_MODE, SETTINGS_PAD = args.rfid_mode, args.settings_pad
    httpd = ThreadingHTTPServer((args.bind, args.port), Handler)
    print(f"Fake Moonraker em http://{args.bind}:{args.port}  cenário={SCENARIO}  spool={SPOOL_ID}")
    if args.http_port:
        http_srv = ThreadingHTTPServer((args.bind, args.http_port), NginxHandler)
        th = threading.Thread(target=http_srv.serve_forever, daemon=True)
        th.start()
        print(f"Fake nginx    em http://{args.bind}:{args.http_port}  "
              f"/firmware-config  rfid={RFID_MODE}  pad={SETTINGS_PAD}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()
