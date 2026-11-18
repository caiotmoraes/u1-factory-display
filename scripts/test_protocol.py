#!/usr/bin/env python3
"""Sobe o FakeMoonraker, exercita os endpoints que o firmware usa e valida o JSON."""
import json, os, subprocess, sys, time, urllib.parse, urllib.request, urllib.error
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

def post_raw(path, data=b""):
    """POST como o firmware no SET_SPOOL_ID: script na query, corpo vazio.
    Retorna (status, json)."""
    req = urllib.request.Request(f"http://127.0.0.1:{PORT}{path}", data=data, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=2) as r:
            return r.status, json.load(r)
    except urllib.error.HTTPError as e:
        return e.code, json.load(e)

def link_requests(ch, sid):
    """Pedidos montados pelas funções do firmware (tests/link_request.cpp)."""
    exe = os.environ.get("LINK_REQUEST")
    if exe and os.path.exists(exe):
        out = subprocess.check_output([exe, str(ch), str(sid)], text=True).splitlines()
        return tuple(out[:5])
    script = f"SET_SPOOL_ID CHANNEL={ch} SPOOL_ID={sid}"
    loc = f"Snapmaker U1 - Extrusor {ch + 1}"
    return ("/printer/gcode/script?script=" + urllib.parse.quote(script, safe=""),
            f"/v1/spool/{sid}", json.dumps({"location": loc}, separators=(",", ":")),
            "/v1/spool?location=" + urllib.parse.quote(loc, safe=""),
            json.dumps({"location": "Armazém"}, separators=(",", ":"), ensure_ascii=False))

def check_set_spool_link():
    """Bug 2026-11-18: ler NFC > inserir no T2 não gravava filament_spool_id nem o local."""
    cfg = lambda **kw: post("/_fake/config", kw)
    cfg(spool_ids=[40, 0, 0, 0], detected=[None, True, None, None], gcode_log=[],
        detect_log=[], patch_log=[], fail_set_spool="", fail_spoolman_patch="")
    # 1) filament_detect/set sozinho NÃO vincula (reproduz a U1 real)
    r = post("/printer/filament_detect/set", {"channel": 1,
             "info": {"CARD_TYPE": "NTAG", "CARD_UID": [0x53, 0x25, 0x28, 0xB5, 0x14, 0x00, 0x01]}})
    assert r["result"] == "ok", r
    ids = get("/printer/objects/query?print_task_config")["result"]["status"]["print_task_config"]["filament_spool_id"]
    assert ids == [40, 0, 0, 0], ids
    # 2) SET_SPOOL_ID exato, canal 0-based, na query string
    gpath, ppath, pbody, _, _ = link_requests(1, 41)
    assert gpath == "/printer/gcode/script?script=SET_SPOOL_ID%20CHANNEL%3D1%20SPOOL_ID%3D41", gpath
    code, r = post_raw(gpath)
    assert code == 200 and r.get("result") == "ok", (code, r)
    log = get("/_fake/state")["gcode_log"][-1]
    assert log["script"] == "SET_SPOOL_ID CHANNEL=1 SPOOL_ID=41" and log["source"] == "query", log
    ids = get("/printer/objects/query?print_task_config")["result"]["status"]["print_task_config"]["filament_spool_id"]
    assert ids == [40, 41, 0, 0], ids
    # T4 = CHANNEL=3
    g4, _, b4, _, _ = link_requests(3, 40)
    assert g4.endswith("SET_SPOOL_ID%20CHANNEL%3D3%20SPOOL_ID%3D40"), g4
    assert json.loads(b4) == {"location": "Snapmaker U1 - Extrusor 4"}, b4
    # 3) PATCH do local pelo proxy, corpo exato
    assert ppath == "/v1/spool/41" and pbody == '{"location":"Snapmaker U1 - Extrusor 2"}', (ppath, pbody)
    pr = post("/server/spoolman/proxy", {"request_method": "PATCH", "path": ppath,
                                         "use_v2_response": True, "body": json.loads(pbody)})
    assert pr["result"]["response"]["location"] == "Snapmaker U1 - Extrusor 2", pr
    pl = get("/_fake/state")["patch_log"][-1]
    assert pl["path"] == "/v1/spool/41" and pl["body"] == {"location": "Snapmaker U1 - Extrusor 2"}, pl
    # 4) caminhos de erro: canal inválido, SpoolLink recusa, Spoolman recusa
    code, r = post_raw("/printer/gcode/script?script=" + urllib.parse.quote("SET_SPOOL_ID CHANNEL=4 SPOOL_ID=41", safe=""))
    assert code == 400 and "CHANNEL" in r["error"]["message"], (code, r)
    cfg(fail_set_spool="SpoolLink: spool #99 not found")
    code, r = post_raw(link_requests(2, 99)[0])
    assert code == 400 and r["error"]["message"] == "SpoolLink: spool #99 not found", (code, r)
    ids = get("/printer/objects/query?print_task_config")["result"]["status"]["print_task_config"]["filament_spool_id"]
    assert ids == [40, 41, 0, 0], ids
    cfg(fail_set_spool="", fail_spoolman_patch="Spoolman offline")
    pr = post("/server/spoolman/proxy", {"request_method": "PATCH", "path": "/v1/spool/41",
                                         "use_v2_response": True, "body": pbody})
    assert pr["result"]["response"] is None and pr["result"]["error"]["message"] == "Spoolman offline", pr
    cfg(fail_spoolman_patch="", detected=[None, None, None, None], spool_ids=[40, 0, 0, 0])

def proxy(method, path, body=None):
    req = {"request_method": method, "path": path, "use_v2_response": True}
    if body is not None:
        req["body"] = body
    return post("/server/spoolman/proxy", req)["result"]

def check_release_previous():
    """Troca no T2: o rolo que marcava "Snapmaker U1 - Extrusor 2" vai para o Armazém.
    Mesma sequência do link_channel() (firmware) e do App 2.0.3."""
    _, ppath, pbody, qpath, stored = link_requests(1, 41)
    assert qpath == "/v1/spool?location=Snapmaker%20U1%20-%20Extrusor%202", qpath
    assert json.loads(stored) == {"location": "Armazém"}, stored
    # antes: #40 no T2 (anterior), #42 com local personalizado parecido
    proxy("PATCH", "/v1/spool/40", {"location": "Snapmaker U1 - Extrusor 2"})
    proxy("PATCH", "/v1/spool/42", {"location": "Snapmaker U1 - Extrusor 2 (seco)"})
    proxy("PATCH", ppath, json.loads(pbody))           # novo #41 no T2
    rows = proxy("GET", qpath)["response"]
    loc = "Snapmaker U1 - Extrusor 2"
    # filtro do Spoolman é parcial: o exato fica no cliente
    assert {r["id"] for r in rows} >= {40, 41, 42}, [r["id"] for r in rows]
    release = [r["id"] for r in rows if r["id"] != 41 and r.get("location") == loc]
    assert release == [40], release
    for sid in release:
        r = proxy("PATCH", f"/v1/spool/{sid}", json.loads(stored))
        assert r["response"]["location"] == "Armazém", r
    after = {r["id"]: r["location"] for r in proxy("GET", "/v1/spool")["response"]}
    assert after[40] == "Armazém" and after[41] == loc and after[42] == loc + " (seco)", after

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

        check_set_spool_link()
        check_release_previous()

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
