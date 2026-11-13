#!/usr/bin/env python3
"""Pré-visualizações 800×480 espelhando layout.h e ui.cpp (sem LVGL/SDL)."""
from __future__ import annotations
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageChops
import numpy as np

ROOT = Path(__file__).resolve().parents[2]  # /workspace/u1-screen
FW = ROOT / "firmware"
PNG = FW / "assets_png"
FONT_PATH = ROOT / "spool-factory" / "assets" / "pixel.ttf"
OUT = ROOT / "preview"
OUT_DOCS = FW / "docs" / "preview"

SCREEN_W, SCREEN_H = 800, 480
FLOOR_Y = 452
PANEL_W = 400
SCENE_X = 400
SCENE_W = 400
SCENE_H = 480
SCENE_CROP_X = 206
HUD_X, HUD_Y, HUD_W, HUD_H = 0, 0, 400, 480
PAD = 16
OPERADORA_X = 304
TAB_Y = 8
FTAB_Y = 10
U1_TOOLHEAD_X, U1_TOOLHEAD_Y = 205, 151
U1_TOOLHEAD_X_MIN, U1_TOOLHEAD_X_MAX = 154, 264
U1_CARRIAGE_X, U1_CARRIAGE_Y = 205, 47
U1_PART_X, U1_PART_Y = 179, 186
U1_SLOTS = ((42, 80), (126, 70), (202, 72), (277, 82))
U1_SLOT_HL_Y, U1_SLOT_HL_H = 318, 80
U1_SLOT_NUM_Y, U1_SLOT_PLATE_Y, U1_SLOT_SPOOL_Y = 330, 348, 358

NAVY, CARD, MINT = 0x080F23, 0x111D37, 0x59ECC5
CREAM, GOLD, MUTED = 0xF9F2DE, 0xE5B142, 0xAAB6CE
ALERT, NIGHT, INK = 0xEF9C73, 0x050818, 0x081725
BAR_BG = 0x0C1930

def rgb(c, a=255):
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255, a)

def font(sz):
    return ImageFont.truetype(str(FONT_PATH), sz)

F12, F16 = font(12), font(16)

def load(name):
    im = Image.open(PNG / name).convert("RGBA")
    return im

SPRITES = {
    "bg": Image.open(PNG / "img_bg_factory.png").convert("RGBA"),
    "op_idle": [load("img_operadora_0.png"), load("img_operadora_1.png")],
    "op_apresenta": [load("img_operadora_2.png"), load("img_operadora_3.png")],
    "chefe_dorme": [load("img_operadora_dorme_0.png"), load("img_operadora_dorme_1.png")],
    "chefe_celebra": [load("img_operadora_celebra_0.png"), load("img_operadora_celebra_1.png")],
    "chefe_susto": [load("img_operadora_susto_0.png"), load("img_operadora_susto_1.png")],
    "toolhead": load("img_u1_toolhead.png"),
    "carriage": load("img_u1_carriage.png"),
    "spool": load("img_u1_spool.png"),
    "part": load("img_u1_part.png"),
}

def paste(dst, src, x, y):
    dst.alpha_composite(src, (int(x), int(y)))

def feet(dst, src, x):
    paste(dst, src, x, FLOOR_Y - src.size[1])

def recolor_sprite(im, rgb_int):
    """Mesmo pipeline do firmware: a=254 vira a cor, pulando marrom/escuro."""
    arr = np.array(im.convert("RGBA"))
    Rt, Gt, Bt = (rgb_int >> 16) & 255, (rgb_int >> 8) & 255, rgb_int & 255
    Rt, Gt, Bt = Rt & 0xF8, Gt & 0xFC, Bt & 0xF8
    a = arr[:, :, 3]
    mask = a == 254
    if not mask.any():
        return im
    r = arr[:, :, 0].astype(int)
    g = arr[:, :, 1].astype(int)
    b = arr[:, :, 2].astype(int)
    brown = (r > 40) & ((b * 5) < r) & (g <= r)
    skip = mask & brown
    paint = mask & ~skip
    out = arr.copy()
    out[:, :, 3] = np.where(skip, 255, out[:, :, 3])
    out[:, :, 0] = np.where(paint, Rt, out[:, :, 0])
    out[:, :, 1] = np.where(paint, Gt, out[:, :, 1])
    out[:, :, 2] = np.where(paint, Bt, out[:, :, 2])
    out[:, :, 3] = np.where(paint, 255, out[:, :, 3])
    return Image.fromarray(out, "RGBA")

def active_rgb(s):
    ae = s.get("active_extruder")
    feeders = s.get("feeders") or []
    if ae is not None and 0 <= ae < len(feeders):
        c = feeders[ae].get("color")
        if c:
            return int(str(c).lstrip("#"), 16)
    for key in ("spool", "filament"):
        sp = s.get(key)
        if sp and sp.get("color"):
            return int(str(sp["color"]).lstrip("#"), 16)
    return 0x59ECC5

def draw_bays(cropped, s):
    spool = SPRITES["spool"]
    feeders = s.get("feeders") or []
    ae = s.get("active_extruder", -1)
    d = ImageDraw.Draw(cropped)
    for i, (x, w) in enumerate(U1_SLOTS):
        on = (i == ae)
        fd = feeders[i] if i < len(feeders) else None
        has = bool(fd) and bool(
            fd.get("id") or fd.get("color") or fd.get("material") or fd.get("filament_detected")
        )
        if on:
            d.rectangle((x - 2, U1_SLOT_HL_Y, x + w + 1, U1_SLOT_HL_Y + U1_SLOT_HL_H - 1),
                        outline=rgb(MINT), width=2)
        if has:
            col = int(str(fd.get("color") or "CCCCCC").lstrip("#"), 16)
            tinted = recolor_sprite(spool, col)
            sx = x + (w - tinted.size[0]) // 2
            paste(cropped, tinted, sx, U1_SLOT_SPOOL_Y)
            mat = (fd.get("material") or "--")[:4]
        else:
            mat = "--"
        # número e placa nos compartimentos existentes (arte do fundo intacta)
        nx = x + (w - 20) // 2
        d.rectangle((nx, U1_SLOT_NUM_Y, nx + 19, U1_SLOT_NUM_Y + 17),
                    fill=rgb(0x080F23), outline=rgb(MINT if on else 0x2A3C5C))
        txt(d, (nx + 2, U1_SLOT_NUM_Y + 1), str(i + 1), F16, MINT if on else CREAM)
        pw = 48
        px = x + (w - pw) // 2
        py = U1_SLOT_PLATE_Y
        d.rectangle((px, py, px + pw - 1, py + 13),
                    fill=rgb(0x0C1930, 255),
                    outline=rgb(MINT if on else 0x2A3C5C))
        tw = 12 * len(mat)
        txt(d, (px + (pw - tw) // 2, py + 1), mat, F12, MINT if on else (CREAM if has else MUTED))

def txt(draw, xy, s, fnt, color, max_w=None):
    fill = rgb(color) if isinstance(color, int) else color
    if max_w is not None:
        cell = fnt.size
        n = max(1, max_w // cell)
        s = s[:n]
    draw.text(xy, s, font=fnt, fill=fill)

def panel(base, x, y, w, h, fill=CARD, opa=230, border=MINT, bw=2):
    overlay = Image.new("RGBA", (w, h), rgb(fill, opa))
    d = ImageDraw.Draw(overlay)
    d.rectangle((0, 0, w - 1, h - 1), outline=rgb(border), width=bw)
    base.alpha_composite(overlay, (x, y))

def tabs(draw_img, active):
    # ecrãs cheios: WIFI, FAB, ARM, DIA no canto direito
    items = ((392, 68, "WIFI", 3), (464, 108, "FABRICA", 0),
             (576, 112, "ARMAZEM", 1), (692, 100, "DIARIO", 2))
    for x, w, name, aid in items:
        y = TAB_Y
        on = aid == active
        panel(draw_img, x, y, w, 28, fill=MINT if on else CARD, opa=255)
        d = ImageDraw.Draw(draw_img)
        tw = 12 * len(name)
        tx = x + (w - tw) // 2
        ty = y + (28 - 12) // 2
        txt(d, (tx, ty), name, F12, INK if on else MINT)

def factory_tabs(draw_img):
    items = ((10, 88, "WIFI", False), (102, 94, "FABRICA", True),
             (200, 96, "ARMAZEM", False), (300, 90, "DIARIO", False))
    for x, w, name, on in items:
        panel(draw_img, x, FTAB_Y, w, 28, fill=MINT if on else CARD, opa=255)
        d = ImageDraw.Draw(draw_img)
        tw = 12 * len(name)
        tx = x + (w - tw) // 2
        ty = FTAB_Y + (28 - 12) // 2
        txt(d, (tx, ty), name, F12, INK if on else MINT)

def fmt_time(sec):
    if sec is None or sec < 0:
        return "--"
    s = int(sec + 0.5)
    h, m = s // 3600, (s % 3600) // 60
    if h > 0:
        return f"{h}h {m:02d}min"
    return f"{m}min"

def hud(base, s):
    # painel opaco à esquerda, 400x480
    overlay = Image.new("RGBA", (HUD_W, HUD_H), rgb(NAVY, 255))
    base.alpha_composite(overlay, (HUD_X, HUD_Y))
    d = ImageDraw.Draw(base)
    d.rectangle((HUD_W - 3, 0, HUD_W - 1, HUD_H - 1), fill=rgb(MINT))
    factory_tabs(base)

    ox = 16
    txt(d, (ox, 48), s["state"], F16, MINT)
    sub = s.get("sub") or ""
    txt(d, (ox, 72), sub, F12, GOLD, max_w=HUD_W - 36)

    pct = int(s.get("progress", 0) * 100 + 0.5)
    pct = max(0, min(100, pct))
    txt(d, (ox, 98), f"{pct}%", F16, CREAM)
    bx, by, bw, bh = ox, 124, HUD_W - 36, 18
    d.rectangle((bx, by, bx + bw - 1, by + bh - 1), fill=rgb(BAR_BG))
    iw = int(round(bw * pct / 100))
    if iw > 0:
        d.rectangle((bx, by, bx + iw - 1, by + bh - 1), fill=rgb(MINT))

    if s.get("state") in ("IMPRIMINDO", "AQUECENDO"):
        rest = "RESTAM  " + fmt_time(s.get("remaining_s"))
    else:
        rest = "RESTAM  --"
    txt(d, (ox, 152), rest, F16, CREAM)

    if s.get("layer_total"):
        cam = f"CAMADA  {s['layer']} / {s['layer_total']}"
    else:
        cam = "CAMADA  --"
    txt(d, (ox, 180), cam, F12, MUTED)

    ae = s.get("active_extruder", 0)
    nozzle = s.get("nozzle", 24)
    ntg = s.get("nozzle_target", nozzle)
    bed = s.get("bed", 24)
    btg = s.get("bed_target", bed)
    panel(base, 16, 206, 178, 64, fill=CARD, opa=255, border=0x2A3C5C)
    panel(base, 206, 206, 178, 64, fill=CARD, opa=255, border=0x2A3C5C)
    txt(d, (24, 214), f"BICO {ae + 1}\n{nozzle:.0f} / {ntg:.0f} C", F12, CREAM)
    txt(d, (214, 214), f"MESA\n{bed:.0f} / {btg:.0f} C", F12, CREAM)

    txt(d, (ox, 284), f"ROLO ATIVO   BICO {ae + 1}", F12, GOLD)
    spool = s.get("spool")
    filament = s.get("filament")  # físico, sem spool_id
    if spool:
        col = int(spool["color"], 16)
        d.rectangle((16, 308, 16 + 27, 308 + 27), fill=rgb(col), outline=rgb(CREAM), width=2)
        line1 = f"#{spool['id']}  {spool['material']}"
        line2 = f"{spool['vendor']}  {spool.get('name') or ''}"
        line3 = f"{spool['g']:.0f} g" if spool.get("g") is not None else ""
        txt(d, (52, 306), line1 + "\n" + line2 + (("\n" + line3) if line3 else ""), F12, CREAM)
        if spool.get("low"):
            txt(d, (ox, 358), "FILAMENTO BAIXO", F12, ALERT)
    elif filament:
        col = int(filament.get("color", "FFFFFF"), 16)
        d.rectangle((16, 308, 16 + 27, 308 + 27), fill=rgb(col), outline=rgb(CREAM), width=2)
        v = filament.get("vendor") or "Generic"
        m = filament.get("material") or ""
        txt(d, (52, 306), f"{v}  {m}\nSEM ROLO ASSOCIADO", F12, CREAM)
    else:
        txt(d, (52, 306), "SEM ROLO ATIVO", F12, CREAM)

    feeders = s.get("feeders") or []
    for i in range(4):
        y = 378 + i * 18
        on = (i == ae)
        if i < len(feeders):
            f = feeders[i]
            col = int(f.get("color") or "1A2438", 16)
            if f.get("id"):
                g = f.get("g")
                gtxt = f"  {g:.0f}g" if g is not None else f"  #{f['id']}"
                line = f"F{i+1}  {f.get('vendor','')} {f.get('material','')}{gtxt}"
            elif f.get("unassigned") or f.get("material"):
                line = f"F{i+1}  {f.get('vendor','Generic')} {f.get('material','')}  --"
                col = int(f.get("color") or "FFFFFF", 16)
            else:
                line = f"F{i+1}  vazio"
        else:
            line = f"F{i+1}  --"
            col = 0x1A2438
        bw = 2 if on else 1
        d.rectangle((16, y + 2, 27, y + 13), fill=rgb(col), outline=rgb(MINT if on else CREAM), width=bw)
        txt(d, (34, y), line, F12, MINT if on else CREAM, max_w=HUD_W - 52)

    if s.get("ip"):
        ip = f"U1  {s['ip']}"
    else:
        ip = "U1  --"
    txt(d, (ox, 454), ip, F12, MUTED)

def scene(s, active_tab=0):
    img = Image.new("RGBA", (SCREEN_W, SCREEN_H), rgb(NAVY))
    # cena direita: crop do fundo 800
    bg = SPRITES["bg"]
    if bg.mode != "RGBA":
        bg = bg.convert("RGBA")
    cropped = Image.new("RGBA", (SCENE_W, SCENE_H), rgb(NAVY))
    cropped.alpha_composite(bg, (-SCENE_CROP_X, 0))

    if s.get("night"):
        veil = Image.new("RGBA", (SCENE_W, SCENE_H), rgb(NIGHT, 128))
        cropped.alpha_composite(veil)
    if s.get("error"):
        veil = Image.new("RGBA", (SCENE_W, SCENE_H), (80, 0, 0, 76))
        cropped.alpha_composite(veil)

    draw_bays(cropped, s)

    th = SPRITES["toolhead"]
    car = SPRITES["carriage"]
    part = SPRITES["part"]
    th_x = s.get("th_x", U1_TOOLHEAD_X)
    th_y = s.get("th_y", U1_TOOLHEAD_Y)
    dx = th_x - U1_TOOLHEAD_X
    paste(cropped, th, th_x, th_y)
    paste(cropped, car, U1_CARRIAGE_X + dx, U1_CARRIAGE_Y)
    prog = s.get("progress", 0) or 0
    if s.get("state") in ("OCIOSA", "OFFLINE", "ERRO"):
        vis = 0
    elif s.get("state") in ("CONCLUÍDA", "CONCLUIDA"):
        vis = part.size[1]
    else:
        vis = int(prog * part.size[1] + 0.5)
    if vis > 2:
        py, pw, ph = U1_PART_Y, part.size[0], part.size[1]
        crop = part.crop((0, ph - vis, pw, ph))
        paste(cropped, crop, U1_PART_X, py + (ph - vis))

    ch_kind, ch_fr = s.get("chefe", ("dorme", 0))
    ch_map = {"apresenta": "op_apresenta", "idle": "op_idle",
              "dorme": "chefe_dorme", "celebra": "chefe_celebra", "susto": "chefe_susto"}
    ch = SPRITES[ch_map.get(ch_kind, "chefe_dorme")][ch_fr]
    feet(cropped, ch, OPERADORA_X)

    if s.get("party"):
        colors = [MINT, GOLD, CREAM, 0xFF6B8A]
        d = ImageDraw.Draw(cropped)
        for i in range(16):
            x = (i * 47) % (SCENE_W - 8)
            y = (i * 19) % 200
            y = (y + 8 * (6 + i % 3)) % SCENE_H
            d.rectangle((x, y, x + 4, y + 7), fill=rgb(colors[i % 4]))

    img.alpha_composite(cropped, (SCENE_X, 0))
    hud(img, s)
    # factory tabs already drawn inside hud; other screens use tabs()
    return img.convert("RGB")

# --- dados ---
PLA = dict(id=40, material="PETG", vendor="Masterprint", name="Black", g=697, color="000000", low=False)
SUNLU_BROWN = dict(id=38, material="PETG", vendor="SUNLU", name="Coffee Brown", g=107, color="362111", low=False)
ORANGE = dict(id=17, material="PETG", vendor="Masterprint", name="Orange", g=121, color="FF4000", low=False)
PETG_LOW = dict(id=17, material="PETG", vendor="Masterprint", name="Orange", g=72, color="FF4000", low=True)
WHITE_FIL = dict(vendor="Generic", material="PLA", color="FFFFFF")

FEEDERS_LIVE = [
    dict(id=40, vendor="Masterprint", material="PETG", g=697, color="000000"),
    dict(id=38, vendor="SUNLU", material="PETG", g=107, color="362111"),
    dict(id=0, vendor="Generic", material="PLA", color="FFFFFF", unassigned=True),
    dict(id=17, vendor="Masterprint", material="PETG", g=121, color="FF4000"),
]
FEEDERS_OK = [
    dict(id=40, vendor="Masterprint", material="PETG", g=697, color="000000"),
    dict(id=38, vendor="SUNLU", material="PETG", g=107, color="362111"),
    dict(id=24, vendor="SUNLU", material="PLA", g=658, color="FFFFFF"),
    dict(id=17, vendor="Masterprint", material="PETG", g=121, color="FF4000"),
]

SCENES = {
    "estado-offline.png": dict(
        state="OFFLINE", sub="SEM CONEXAO", night=True, progress=0,
        nozzle=24, bed=24, spool=None, ip=None, feeders=[],
        chefe=("dorme", 0),
    ),
    "estado-ociosa.png": dict(
        state="OCIOSA", sub="FABRICA DORMINDO", night=True, progress=0,
        nozzle=24, bed=24, spool=PLA, ip="192.168.0.4",
        active_extruder=0, feeders=FEEDERS_OK,
        chefe=("dorme", 1),
    ),
    "estado-aquecendo.png": dict(
        state="AQUECENDO", sub="AQUECENDO O BICO", progress=0,
        layer=0, layer_total=755, remaining_s=6660,
        nozzle=180, nozzle_target=210, bed=50, bed_target=55,
        spool=PLA, ip="192.168.0.4", active_extruder=0, feeders=FEEDERS_OK,
        chefe=("apresenta", 0), th_x=224,
    ),
    "estado-imprimindo.png": dict(
        state="IMPRIMINDO", sub="interial_PLA_1h51m.gcode", progress=0.21,
        layer=154, layer_total=755, remaining_s=6388,
        nozzle=210, nozzle_target=210, bed=55, bed_target=55,
        spool=None, filament=WHITE_FIL, ip="192.168.0.4",
        active_extruder=2, feeders=FEEDERS_LIVE,
        chefe=("apresenta", 1), th_x=234,
    ),
    "estado-pausada.png": dict(
        state="PAUSADA", sub="interial_PLA_1h51m.gcode", progress=0.21,
        layer=154, layer_total=755, remaining_s=6388,
        nozzle=210, nozzle_target=210, bed=55, bed_target=55,
        spool=None, filament=WHITE_FIL, ip="192.168.0.4",
        active_extruder=2, feeders=FEEDERS_LIVE,
        chefe=("apresenta", 0), th_x=234,
    ),
    "estado-concluida.png": dict(
        state="CONCLUÍDA", sub="", progress=1.0,
        layer=755, layer_total=755, remaining_s=0,
        nozzle=80, bed=40, spool=dict(id=24, material="PLA", vendor="SUNLU",
                                      name="White", g=658, color="FFFFFF"),
        ip="192.168.0.4", party=True, active_extruder=2, feeders=FEEDERS_OK,
        chefe=("celebra", 1),
    ),
    "estado-erro.png": dict(
        state="ERRO", sub="", progress=0.12,
        layer=18, layer_total=755,
        nozzle=215, bed=60, spool=PLA, ip="192.168.0.4", error=True,
        active_extruder=0, feeders=FEEDERS_OK,
        chefe=("susto", 1),
    ),
    "estado-pouco-filamento.png": dict(
        state="IMPRIMINDO", sub="interial_PLA_1h51m.gcode", progress=0.91,
        layer=700, layer_total=755, remaining_s=540,
        nozzle=210, nozzle_target=210, bed=55, bed_target=55,
        spool=PETG_LOW, ip="192.168.0.4",
        active_extruder=3, feeders=[
            FEEDERS_OK[0], FEEDERS_OK[1], FEEDERS_OK[2],
            dict(id=17, vendor="Masterprint", material="PETG", g=72, color="FF4000"),
        ],
        chefe=("apresenta", 0), th_x=194,
    ),
    "estado-troca-rolo.png": dict(
        state="OCIOSA", sub="FABRICA DORMINDO", night=True, progress=0,
        nozzle=24, bed=24, spool=PETG_LOW, ip="192.168.0.4",
        active_extruder=3, feeders=FEEDERS_OK,
        chefe=("dorme", 0),
    ),
}


def details():
    img = Image.new("RGBA", (SCREEN_W, SCREEN_H), rgb(NAVY))
    d = ImageDraw.Draw(img)
    txt(d, (20, 20), "DETALHES DA IMPRESSAO", F16, MINT)
    body = (
        "Arquivo:\n"
        "benchy.gcode\n\n"
        "Bico  215 C  (alvo 215 C)\n"
        "Mesa  60 C  (alvo 60 C)\n\n"
        "Progresso  67%\n"
        "Camada  142 / 210\n"
        "Estado  IMPRIMINDO\n"
        "Moonraker  conectado\n"
        "IP  192.168.0.4:7125\n"
        "\n"
        "Rolo #40  Generic PLA\n"
        "Volt  540 g"
    )
    txt(d, (20, 56), body, F12, CREAM)
    txt(d, (20, 440), "toque para voltar a fabrica", F12, MUTED)
    tabs(img, 0)
    return img.convert("RGB")

def warehouse():
    img = Image.new("RGBA", (SCREEN_W, SCREEN_H), rgb(NAVY))
    d = ImageDraw.Draw(img)
    tabs(img, 1)
    txt(d, (12, 48), "ARMAZEM DE ROLOS", F16, MINT)
    panel(img, 420, 46, 70, 28, fill=MINT, opa=255)
    txt(ImageDraw.Draw(img), (434, 54), "NOVO", F12, INK)
    panel(img, 12, 92, 280, 34, fill=CARD, opa=255)
    txt(ImageDraw.Draw(img), (20, 100), "Buscar...", F12, MUTED)
    x = 12
    for lab, on in (("ARQUIVADOS", False), ("<=200 g", False), ("COMPACTA", False), ("LIMPAR", False)):
        w = chip(img, x, 132, lab, on)
        x += w + 8
    txt(ImageDraw.Draw(img), (510, 136), "3 ativos  |  1412 g  |  1 <=200 g", F12, MUTED)
    cards = [
        dict(color=0xD5A62A, id=40, mat="PLA", vend="Generic", name="Volt", g=540, loc="Prateleira A", use=True),
        dict(color=0xFF4000, id=41, mat="PETG", vend="Masterprint", name="Masterprint", g=72, loc="Caixa 2", low=True),
        dict(color=0x59ECC5, id=18, mat="PLA", vend="Generic", name="Mint Silk", g=800, loc="Carrinho"),
    ]
    for i, c in enumerate(cards):
        col, row = i % 2, i // 2
        x, y = 16 + col * 388, 176 + row * 104
        border = GOLD if c.get("use") else (ALERT if c.get("low") else MINT)
        panel(img, x, y, 376, 96, fill=CARD, opa=230, border=border, bw=3 if c.get("use") else 2)
        dd = ImageDraw.Draw(img)
        dd.rectangle((x + 10, y + 14, x + 37, y + 41), fill=rgb(c["color"]), outline=rgb(CREAM))
        txt(dd, (x + 48, y + 8), f"{c['mat']}  {c['vend']}", F12, MINT)
        txt(dd, (x + 48, y + 30), c["name"], F12, CREAM)
        gcol = ALERT if c.get("low") else MUTED
        txt(dd, (x + 48, y + 62), f"#{c['id']}   {c['g']} g", F12, gcol)
        if c.get("use"):
            txt(dd, (x + 280, y + 8), "EM USO", F12, GOLD)
        if c.get("loc"):
            txt(dd, (x + 220, y + 62), c["loc"], F12, MUTED)
    return img.convert("RGB")

def wifi_screen():
    img = Image.new("RGBA", (SCREEN_W, SCREEN_H), rgb(NAVY))
    d = ImageDraw.Draw(img)
    tabs(img, 3)
    txt(d, (12, 48), "WI-FI", F16, MINT)
    txt(d, (12, 88), "Agora: MinhaCasa    IP 192.168.0.50", F12, CREAM)
    txt(d, (12, 118), "Esta placa so fala 2.4 GHz. Redes 5 GHz nao aparecem.", F12, GOLD)
    panel(img, 12, 150, 140, 36, fill=MINT, opa=255)
    txt(ImageDraw.Draw(img), (36, 160), "VARRER", F12, INK)
    nets = [("MinhaCasa", -42, True, True), ("Vizinho_2G", -67, True, False),
            ("CafeLivre", -80, False, False)]
    for i, (ssid, rssi, lock, cur) in enumerate(nets):
        y = 200 + i * 52
        panel(img, 12, y, 760, 44, fill=CARD, opa=230, border=GOLD if cur else MINT)
        dd = ImageDraw.Draw(img)
        bars = "****" if rssi > -55 else "***" if rssi > -70 else "**"
        lab = f"{ssid}   {'lock' if lock else 'aberta'}   {rssi} dBm  {bars}"
        txt(dd, (24, y + 14), lab, F12, CREAM)
    return img.convert("RGB")

def diario():
    img = Image.new("RGBA", (SCREEN_W, SCREEN_H), rgb(NAVY))
    d = ImageDraw.Draw(img)
    tabs(img, 2)
    txt(d, (12, 48), "DIARIO", F16, MINT)
    panel(img, 12, 88, 400, 34, fill=CARD, opa=255)
    txt(ImageDraw.Draw(img), (20, 96), "Buscar no diario...", F12, MUTED)
    txt(d, (12, 128), "Historico local desta tela (60 linhas).", F12, MUTED)
    ev = [
        (GOLD, "13/11 15:40:02 · Rolo #40 associado ao feeder 1"),
        (MINT, "13/11 15:32:11 · NFC AABBCCDD associado ao rolo #40"),
        (CREAM, "13/11 15:20:00 · Estoque: 3 rolos"),
        (ALERT, "13/11 14:58:44 · Sem conexao com a U1"),
    ]
    for i, (col, line) in enumerate(ev):
        y = 168 + i * 40
        panel(img, 12, y, 776, 34, fill=CARD, opa=200, border=CARD, bw=0)
        txt(ImageDraw.Draw(img), (20, y + 10), line, F12, col)
    return img.convert("RGB")

def proposed():
    """Proposta de navegação — NÃO é o firmware atual."""
    img = Image.new("RGBA", (SCREEN_W, SCREEN_H), rgb(NAVY))
    # faint factory floor stripe at the bottom
    d = ImageDraw.Draw(img)
    d.rectangle((0, 452, 799, 479), fill=rgb(0x111D37))
    d.rectangle((0, 448, 799, 451), fill=rgb(GOLD))

    tabs(img, 1)
    txt(d, (16, 12), "ARMAZEM DE ROLOS", F16, MINT)
    # search + NFC
    panel(img, 16, 44, 520, 28, fill=CARD, opa=255)
    txt(ImageDraw.Draw(img), (24, 52), "buscar  PLA  local  marca", F12, MUTED)
    panel(img, 548, 44, 100, 28, fill=CARD, opa=255)  # behind tab area? move left
    # NFC button under tabs is crowded — put it left of filters
    panel(img, 544, 44, 96, 28, fill=MINT, opa=255)
    # wait tabs are at 560 — overlap. Put NFC at left of search row only if we
    # move search. Search 16-500, NFC 520-548? Let me put chips below.

    # redo: title already, search full width below title, chips, list
    # I already drew a messy search. Let me restart this function more carefully.
    return None  # placeholder, rewritten below

def chip(img, x, y, label, on=False, w=None):
    w = w or (len(label) * 12 + 16)
    panel(img, x, y, w, 24, fill=MINT if on else CARD, opa=255)
    d = ImageDraw.Draw(img)
    txt(d, (x + 8, y + 6), label, F12, INK if on else MINT)
    return w

def proposed():
    img = Image.new("RGBA", (SCREEN_W, SCREEN_H), rgb(NAVY))
    d = ImageDraw.Draw(img)
    tabs(img, 1)
    txt(d, (16, 12), "ARMAZEM", F16, MINT)

    # search bar
    panel(img, 16, 44, 620, 28, fill=CARD, opa=255)
    txt(ImageDraw.Draw(img), (24, 52), "PETG  branco", F12, CREAM)
    panel(img, 644, 44, 140, 28, fill=GOLD, opa=255)
    tw = 12 * 7
    txt(ImageDraw.Draw(img), (644 + (140 - tw) // 2, 44 + 8), "LER NFC", F12, INK)

    # filter chips
    x = 16
    for lab, on in (("PLA", False), ("PETG", True), ("<=200g", True),
                    ("ARQUIVADOS", False), ("POR PESO", True)):
        x += chip(img, x, 80, lab, on) + 8

    # two-column cards
    cards = [
        dict(color=0xFF4000, id=41, mat="PETG", name="Masterprint", g=72, loc="Prateleira B", low=True),
        dict(color=0xD5A62A, id=40, mat="PLA", name="Volt Generic", g=540, loc="Prateleira A", low=False),
        dict(color=0x59ECC5, id=18, mat="PLA", name="Mint Silk", g=188, loc="Carrinho", low=False),
        dict(color=0x3A6BFF, id=7, mat="PETG", name="Azul Navy", g=95, loc="Gaveta 2", low=True),
        dict(color=0xF9F2DE, id=22, mat="PLA", name="Natural", g=120, loc="Prateleira A", low=False),
        dict(color=0xC0392B, id=55, mat="ABS", name="Vermelho", g=40, loc="Arquivo", low=True, arch=True),
    ]
    # 3 rows × 2 cols, card 376×96
    for i, c in enumerate(cards):
        col, row = i % 2, i // 2
        x, y = 16 + col * 388, 116 + row * 104
        border = ALERT if c.get("low") else MINT
        panel(img, x, y, 376, 96, fill=CARD, opa=255, border=border)
        dd = ImageDraw.Draw(img)
        dd.rectangle((x + 10, y + 14, x + 41, y + 45), fill=rgb(c["color"]), outline=rgb(CREAM))
        txt(dd, (x + 52, y + 12), f"#{c['id']}  {c['mat']}", F16, MINT)
        extra = "  ARQUIVADO" if c.get("arch") else ""
        txt(dd, (x + 52, y + 36), f"{c['name']}{extra}", F12, CREAM)
        gcol = ALERT if c.get("low") else CREAM
        txt(dd, (x + 52, y + 56), f"{c['g']} g   {c['loc']}", F12, gcol)
        if c.get("low"):
            txt(dd, (x + 52, y + 76), "POUCO FILAMENTO", F12, ALERT)

    # watermark
    banner = Image.new("RGBA", (SCREEN_W, 22), rgb(GOLD, 230))
    img.alpha_composite(banner, (0, SCREEN_H - 22))
    txt(ImageDraw.Draw(img), (16, SCREEN_H - 18),
        "PROPOSTA 800x480  -  nao implementado nesta versao", F12, INK)
    return img.convert("RGB")

def proposed_ficha():
    img = Image.new("RGBA", (SCREEN_W, SCREEN_H), rgb(NAVY))
    tabs(img, 1)
    d = ImageDraw.Draw(img)
    txt(d, (16, 12), "FICHA DO ROLO  #41", F16, MINT)
    panel(img, 16, 48, 280, 280, fill=CARD, opa=255, border=ALERT)
    d = ImageDraw.Draw(img)
    d.rectangle((40, 72, 272, 200), fill=rgb(0xFF4000), outline=rgb(CREAM), width=2)
    txt(d, (40, 216), "PETG", F16, MINT)
    txt(d, (40, 244), "Masterprint", F12, CREAM)
    txt(d, (40, 264), "72 g  de  1000 g", F12, ALERT)
    txt(d, (40, 284), "POUCO FILAMENTO", F12, ALERT)

    panel(img, 312, 48, 472, 280, fill=CARD, opa=255)
    d = ImageDraw.Draw(img)
    txt(d, (328, 64), "Local     Prateleira B", F12, CREAM)
    txt(d, (328, 88), "Comentario   (vazio)", F12, MUTED)
    txt(d, (328, 112), "NFC   AA:BB:CC:DD", F12, CREAM)
    txt(d, (328, 136), "Feeder   nenhum", F12, MUTED)
    # action buttons (disabled / future)
    y = 180
    for i, (lab, on) in enumerate((
        ("EDITAR PESO", True),
        ("ASSOCIAR FEEDER", True),
        ("VINCULAR NFC", True),
        ("EXCLUIR", False),
    )):
        col, row = i % 2, i // 2
        x = 328 + col * 220
        yy = y + row * 48
        colr = ALERT if lab == "EXCLUIR" else MINT
        panel(img, x, yy, 208, 36, fill=CARD, opa=255, border=colr)
        tw = 12 * len(lab)
        txt(ImageDraw.Draw(img), (x + (208 - tw) // 2, yy + 12), lab, F12, colr)

    txt(ImageDraw.Draw(img), (16, 348),
        "Escritas (PATCH/POST no proxy Spoolman e spoollink da U1)\n"
        "ficam para a proxima versao. Toque fora para voltar a lista.",
        F12, MUTED)
    banner = Image.new("RGBA", (SCREEN_W, 22), rgb(GOLD, 230))
    img.alpha_composite(banner, (0, SCREEN_H - 22))
    txt(ImageDraw.Draw(img), (16, SCREEN_H - 18),
        "PROPOSTA 800x480  -  nao implementado nesta versao", F12, INK)
    return img.convert("RGB")

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    written = []
    for name, s in SCENES.items():
        im = scene(s)
        im.save(OUT / name, optimize=True)
        written.append(name)
    for name, fn in (
        ("tela-detalhes.png", details),
        ("tela-armazem.png", warehouse),
        ("tela-wifi.png", wifi_screen),
        ("tela-diario.png", diario),
        ("proposta-armazem.png", proposed),
        ("proposta-ficha.png", proposed_ficha),
    ):
        fn().save(OUT / name, optimize=True)
        written.append(name)
    # GIF: carro + bico juntos, easing suave (espelha o timer de 50 ms)
    frames = []
    base = dict(SCENES["estado-imprimindo.png"])
    n = 40
    span = U1_TOOLHEAD_X_MAX - U1_TOOLHEAD_X_MIN
    for i in range(n):
        half = n / 2.0
        t = (i / half) if i < half else (2.0 - i / half)
        u = t * t * (3.0 - 2.0 * t)
        th = U1_TOOLHEAD_X_MIN + int(u * span + 0.5)
        s = dict(base)
        s["th_x"] = th
        s["chefe"] = ("apresenta", (i // 8) % 2)
        frames.append(scene(s))
    gif = OUT / "anim-imprimindo.gif"
    frames[0].save(gif, save_all=True, append_images=frames[1:],
                   duration=90, loop=0, optimize=False)
    written.append(gif.name)

    # índice 3x3 dos estados
    names = [n for n in SCENES]
    cols, rows = 3, 3
    thumb_w, thumb_h = 800 // 2, 480 // 2
    pad = 12
    label_h = 22
    iw = cols * thumb_w + (cols + 1) * pad
    ih = rows * (thumb_h + label_h) + (rows + 1) * pad + 20
    idx = Image.new("RGB", (iw, ih), (8, 15, 35))
    d = ImageDraw.Draw(idx)
    d.text((pad, 6), "U1 Factory Display  v0.4.2  —  estados", font=F12, fill=rgb(MINT)[:3])
    for i, name in enumerate(names):
        r, c = divmod(i, cols)
        x = pad + c * (thumb_w + pad)
        y = 24 + pad + r * (thumb_h + label_h + pad)
        im = Image.open(OUT / name).resize((thumb_w, thumb_h), Image.Resampling.LANCZOS)
        idx.paste(im, (x, y))
        d.text((x, y + thumb_h + 4), name.replace(".png", ""), font=F12, fill=rgb(CREAM)[:3])
    idx.save(OUT / "indice.png", optimize=True)
    written.append("indice.png")

    import shutil
    OUT_DOCS.mkdir(parents=True, exist_ok=True)
    print("previews em", OUT)
    for n in written:
        fp = OUT / n
        print(f"  {n:32} {fp.stat().st_size:7d}  {Image.open(fp).size}")
        dest = OUT_DOCS / n
        if dest.resolve() != fp.resolve():
            shutil.copy2(fp, dest)
    print("cópia em", OUT_DOCS)

if __name__ == "__main__":
    main()
