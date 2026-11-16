#!/usr/bin/env python3
"""Sobe o FakeMoonraker, exercita os endpoints que o firmware usa e valida o JSON."""
import json, subprocess, sys, time, urllib.request, urllib.error
from pathlib import Path

HERE = Path(__file__).resolve().parent
PORT = 17125
HTTP_PORT = 17180

def get(path, port=PORT):
    with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=2) as r:
        return json.load(r)

def get_raw(path, port=PORT):
    with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=2) as r:
        raw = r.read()
        return raw, json.loads(raw)

def post(path, body):
    data = json.dumps(body).encode()
    req = urllib.request.Request(f"http://127.0.0.1:{PORT}{path}", data=data,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=2) as r:
        return json.load(r)

def main():
    proc = subprocess.Popen(
        [sys.executable, str(HERE / "fake_moonraker.py"), "--port", str(PORT),
         "--http-port", str(HTTP_PORT), "--rfid-mode", "openrfid",
         "--scenario", "printing", "--bind", "127.0.0.1"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        time.sleep(0.4)
        info = get("/printer/info")
        assert info["result"]["state"] == "ready", info
        objs = get("/printer/objects/query?print_stats&virtual_sdcard&extruder&heater_bed&idle_timeout&webhooks")
        st = objs["result"]["status"]
        assert st["print_stats"]["state"] == "printing"
        assert 0.6 < st["virtual_sdcard"]["progress"] < 0.7
        assert st["extruder"]["temperature"] == 215
        assert st["print_stats"]["info"]["current_layer"] == 142
        sm = get("/server/spoolman/status")
        assert sm["result"]["spoolman_connected"] is True
        assert sm["result"]["spool_id"] == 40
        proxied = post("/server/spoolman/proxy", {
            "request_method": "GET", "path": "/v1/spool/40", "use_v2_response": True,
        })
        spool = proxied["result"]["response"]
        assert spool["id"] == 40
        assert spool["filament"]["color_hex"] == "D5A62A"
        assert spool["filament"]["material"] == "PLA"
        assert spool["remaining_weight"] == 540.0
        # spool baixo
        proc.kill(); proc.wait()
        proc = subprocess.Popen(
            [sys.executable, str(HERE / "fake_moonraker.py"), "--port", str(PORT),
         "--http-port", str(HTTP_PORT), "--rfid-mode", "openrfid",
             "--scenario", "printing", "--spool", "41", "--bind", "127.0.0.1"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        time.sleep(0.4)
        sm = get("/server/spoolman/status")
        assert sm["result"]["spool_id"] == 41
        proxied = post("/server/spoolman/proxy", {
            "request_method": "GET", "path": "/v1/spool/41", "use_v2_response": True,
        })
        spool = proxied["result"]["response"]
        assert spool["remaining_weight"] == 72.0
        assert spool["remaining_weight"] < 100
        # lista, patch, feeders, firmware-config, spoollink, gcode, create/delete
        lst = post("/server/spoolman/proxy", {
            "request_method": "GET", "path": "/v1/spool?allow_archived=true",
            "use_v2_response": True,
        })
        arr = lst["result"]["response"]
        assert isinstance(arr, list) and len(arr) >= 2, lst
        ids = {s["id"] for s in arr}
        assert 40 in ids and 41 in ids and 42 in ids, ids

        patched = post("/server/spoolman/proxy", {
            "request_method": "PATCH", "path": "/v1/spool/40",
            "use_v2_response": True,
            "body": "{\"location\":\"Bancada\",\"comment\":\"teste\"}",
        })
        assert patched["result"]["response"]["location"] == "Bancada", patched

        sid = get("/server/spoolman/spool_id")
        assert sid["result"]["spool_id"] == 41

        # Moonraker :7125 não serve firmware-config (404, como a U1 real)
        try:
            get("/firmware-config/api/settings")
            raise AssertionError("esperado 404 no Moonraker para /firmware-config")
        except urllib.error.HTTPError as e:
            assert e.code == 404, e

        raw, fc = get_raw("/firmware-config/api/settings", port=HTTP_PORT)
        # item rfid pode estar em qualquer grupo; valor real é openrfid
        cur = None
        for g in fc.values():
            if isinstance(g, dict):
                for it in g.get("items") or []:
                    if it.get("id") == "rfid":
                        cur = it.get("current")
        assert cur == "openrfid", (cur, fc)
        assert len(raw) >= 30000, f"settings too small: {len(raw)}"

        objs2 = get("/printer/objects/query?filament_feed%20left&print_task_config")
        st2 = objs2["result"]["status"]
        assert st2["filament_feed left"]["extruder0"]["filament_detected"] is True
        assert st2["print_task_config"]["filament_spool_id"][0] == 40

        sl = post("/printer/spoollink/set", {
            "channel": 0,
            "info": {"VENDOR": "Generic", "MAIN_TYPE": "PLA", "SPOOL_ID": 40,
                     "OFFICIAL": True, "ALPHA": 255, "CARD_UID": [],
                     "COLOR_NUMS": 1, "RGB_1": 13985322, "ARGB_COLOR": 4278190080},
            "message": "teste",
        })
        assert sl.get("result") == "ok", sl

        gc = post("/printer/gcode/script", {"script": "AUTO_FEEDING EXTRUDER=0 LOAD=1"})
        assert gc.get("result") == "ok", gc

        created = post("/server/spoolman/proxy", {
            "request_method": "POST", "path": "/v1/spool", "use_v2_response": True,
            "body": "{\"filament_id\":1,\"initial_weight\":1000,\"used_weight\":0}",
        })
        nid = created["result"]["response"]["id"]
        assert nid >= 43, created

        deleted = post("/server/spoolman/proxy", {
            "request_method": "DELETE", "path": "/v1/spool/%d" % nid,
            "use_v2_response": True,
        })
        assert "result" in deleted, deleted

        assert post("/printer/print/pause", {})["result"] == "ok"
        assert get("/printer/objects/query")["result"]["status"]["print_stats"]["state"] == "paused"
        assert post("/printer/print/resume", {})["result"] == "ok"
        assert post("/printer/print/cancel", {})["result"] == "ok"
        filename = get("/server/files/list?root=gcodes")["result"][0]["path"]
        assert post("/printer/print/start", {"filename": filename})["result"] == "ok"
        assert get("/printer/objects/query")["result"]["status"]["print_stats"]["filename"] == filename
        try:
            post("/printer/print/start", {"filename": filename})
        except urllib.error.HTTPError as exc:
            assert exc.code == 409
        else:
            raise AssertionError("O simulador aceitou duas impressões simultâneas.")
        assert post("/printer/emergency_stop", {})["result"] == "ok"
        assert get("/printer/objects/query")["result"]["status"]["print_stats"]["state"] == "error"
        print("test_protocol: todos os endpoints ok")
        return 0
    finally:
        proc.kill()
        proc.wait()

if __name__ == "__main__":
    sys.exit(main())
