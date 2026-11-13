#!/usr/bin/env python3
"""Extrai o carro do gantry (teto) + o bico da câmara num único sprite móvel,
   limpa os dois buracos no fundo, atualiza bins/headers/coords."""
from __future__ import annotations
from pathlib import Path
import re
import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
src = (HERE / "convert_assets.py").read_text()
idx = src.find("# ---- background from official")
ns = {}
exec(compile(src[:idx], "convert_assets.py", "exec"), ns)
tight = ns["tight"]
emit_truecolor_alpha = ns["emit_truecolor_alpha"]
emit_indexed8 = ns["emit_indexed8"]
PNG_OUT = ns["PNG_OUT"]
BIN_OUT = ns["BIN_OUT"]
SRC_OUT = ns["SRC_OUT"]
FW = ns["FW"]

ORIG = PNG_OUT / "img_bg_factory_orig.png"
CLEAN_PREV = PNG_OUT / "img_bg_factory.png"
DEBUG = Path("/tmp/u1view")
DEBUG.mkdir(exist_ok=True)

SCENE_CROP_X = 240  # coluna do BG 800 visível em x=0 do painel direito


def dilate(m, n=2):
    k = m.copy()
    for _ in range(n):
        k2 = k.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                k2 |= np.roll(np.roll(k, dy, 0), dx, 1)
        k = k2
    return k


def extract_roof_carriage(bg: Image.Image):
    a = np.array(bg.convert("RGB"))
    H, W = a.shape[:2]
    r, g, b = a[:, :, 0].astype(int), a[:, :, 1].astype(int), a[:, :, 2].astype(int)
    lum = (r + g + b) // 3
    # yellow circle seed
    y0, y1, x0, x1 = 66, 86, 410, 475
    ys, xs = np.where((r > 180) & (g > 120) & (b < 90) & (r > b + 50)
                      & (np.arange(H)[:, None] >= y0) & (np.arange(H)[:, None] < y1)
                      & (np.arange(W)[None, :] >= x0) & (np.arange(W)[None, :] < x1))
    cy, cx = int(ys.mean()), int(xs.mean())
    WX0, WX1, WY0, WY1 = 325, 555, 50, 86  # stop before fascia (y>=86)
    yellow = (r > 180) & (g > 110) & (b < 100) & (r > b + 40)
    light = lum >= 100
    mid = (lum >= 70) & (lum < 100)
    keep = np.zeros((H, W), bool)
    stack = [(cy, cx)]
    seen = np.zeros((H, W), bool)
    while stack:
        y, x = stack.pop()
        if y < WY0 or y >= WY1 or x < WX0 or x >= WX1:
            continue
        if seen[y, x]:
            continue
        seen[y, x] = True
        if not (yellow[y, x] or light[y, x] or mid[y, x]):
            continue
        # drop warning-triangle orange at the TOP corners (not the lower circle)
        if y < 64 and (r[y, x] > 180) and (g[y, x] > 90) and (b[y, x] < 80) and (x < cx - 16 or x > cx + 16):
            continue
        keep[y, x] = True
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy or dx:
                    stack.append((y + dy, x + dx))
    # dark outline adjacent
    k = dilate(keep, 2)
    keep = keep | (k & (lum < 90) & (np.arange(H)[:, None] < WY1))
    # trim fascia just in case
    keep[86:, :] = False
    ys, xs = np.where(keep)
    bbox = int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1
    print("roof carriage bbox", bbox, "n", int(keep.sum()), "center", cx, cy)
    rgba = np.dstack([a, np.where(keep, 255, 0).astype(np.uint8)])
    raw = Image.fromarray(rgba, "RGBA")
    spr = tight(raw, pad=1)
    # tight offset
    vis = rgba[:, :, 3] > 16
    ys, xs = np.where(vis)
    px, py = int(xs.min()), int(ys.min())
    # pad=1: tight goes 1 px out if possible
    px = max(0, px - 1)
    py = max(0, py - 1)
    return spr, keep, (px, py)


def extract_toolhead_from_existing():
    """Reusa o sprite já aparado (bico da câmara) nas coords conhecidas."""
    p = PNG_OUT / "img_u1_toolhead.png"
    im = Image.open(p).convert("RGBA")
    # coords atuais (u1_anim_coords.h v0.2)
    return im, (411, 151)


def extract_part_from_existing():
    im = Image.open(PNG_OUT / "img_u1_part.png").convert("RGBA")
    return im, (385, 186)


def inpaint_roof(work: np.ndarray, keep: np.ndarray, orig: np.ndarray):
    """Reconstrói trilho + baias escuras no lugar do carro."""
    m = dilate(keep, 3)
    m[86:, :] = False  # nunca tocar na fascia
    # trilho: y 60-63 é prata contínua; copiar de uma coluna limpa à esquerda
    rail_src_x = 390
    # baia-fonte à esquerda do carro (completa, com triângulo)
    src_x0, src_x1 = 372, 420  # 48 px
    H, W, _ = work.shape
    for y in range(50, 86):
        for x in range(320, 560):
            if not m[y, x]:
                continue
            if 60 <= y <= 63:
                work[y, x] = orig[y, rail_src_x]
            else:
                sx = src_x0 + ((x - 424) % max(1, src_x1 - src_x0))
                # se a fonte cair no próprio mask, empurra mais pra esquerda
                if 0 <= sx < W and not m[y, sx]:
                    work[y, x] = orig[y, sx]
                else:
                    sx2 = 390 + (x % 7)
                    work[y, x] = orig[y, sx2]
    return work


def inpaint_chamber(work, orig):
    """Reaplica a limpeza da câmara (bico + benchy) a partir do original."""
    # Use the already-cleaned previous BG for the chamber region — it was
    # carefully clone-stamped. Copy chamber+bed from CLEAN_PREV if present.
    if CLEAN_PREV.exists():
        prev = np.array(Image.open(CLEAN_PREV).convert("RGB"))
        # chamber opening and bed: x 326-532, y 148-258
        work[148:258, 326:532] = prev[148:258, 326:532]
        return work
    return work


def combine(carriage, cpos, toolhead, tpos):
    cx, cy = cpos
    tx, ty = tpos
    cw, ch = carriage.size
    tw, th = toolhead.size
    min_x = min(cx, tx)
    min_y = min(cy, ty)
    max_x = max(cx + cw, tx + tw)
    max_y = max(cy + ch, ty + th)
    canvas = Image.new("RGBA", (max_x - min_x, max_y - min_y), (0, 0, 0, 0))
    canvas.paste(carriage, (cx - min_x, cy - min_y), carriage)
    canvas.paste(toolhead, (tx - min_x, ty - min_y), toolhead)
    return canvas, (min_x, min_y)


def patch_images_c(th_w, th_h, th_size):
    p = SRC_OUT / "images.c"
    t = p.read_text()
    t2, n = re.subn(
        r"(const lv_img_dsc_t img_u1_toolhead = \{.*?\.w = )\d+(, \.h = )\d+( \},\n    \.data_size = )\d+",
        rf"\g<1>{th_w}\g<2>{th_h}\g<3>{th_size}",
        t,
        count=1,
        flags=re.S,
    )
    if n != 1:
        raise SystemExit(f"failed to patch images.c (n={n})")
    p.write_text(t2)
    h = SRC_OUT / "assets.h"
    ht = h.read_text()
    ht = re.sub(r"#define IMG_U1_TOOLHEAD_W \d+", f"#define IMG_U1_TOOLHEAD_W {th_w}", ht)
    ht = re.sub(r"#define IMG_U1_TOOLHEAD_H \d+", f"#define IMG_U1_TOOLHEAD_H {th_h}", ht)
    h.write_text(ht)
    print("patched images.c / assets.h")


def write_coords(cpos, csize, tpos, tsize, part_pos, part_size, combined_pos, combined_size):
    # scene-local = original - SCENE_CROP_X
    def sx(x):
        return x - SCENE_CROP_X

    cx, cy = combined_pos
    cw, ch = combined_size
    # travel in original coords, then convert
    th_xmin, th_xmax = 360, 470  # original, conservative
    text = f"""/* Auto-gerado — coordenadas da cena U1 no painel direito 400x480.
 * Origem (0,0) = canto do clip (tela x={400}, y=0).
 * O fundo 800x480 é desenhado com deslocamento -SCENE_CROP_X.
 * img_u1_toolhead é o carro COMPLETO (gantry do teto + bico da câmara),
 * uma única camada que se move junta.
 */
#pragma once
#define SCENE_CROP_X {SCENE_CROP_X}
#define SCENE_OX(orig_x) ((orig_x) - SCENE_CROP_X)

#define U1_CHAMBER_X0 {sx(326)}
#define U1_CHAMBER_Y0 152
#define U1_CHAMBER_X1 {sx(532)}
#define U1_CHAMBER_Y1 256

#define U1_TOOLHEAD_X {sx(cx)}
#define U1_TOOLHEAD_Y {cy}
#define U1_TOOLHEAD_W {cw}
#define U1_TOOLHEAD_H {ch}
#define U1_TOOLHEAD_X_MIN {sx(th_xmin)}
#define U1_TOOLHEAD_X_MAX {sx(th_xmax)}
#define U1_TOOLHEAD_TRAVEL (U1_TOOLHEAD_X_MAX - U1_TOOLHEAD_X_MIN)

#define U1_PART_X {sx(part_pos[0])}
#define U1_PART_Y {part_pos[1]}
#define U1_PART_W {part_size[0]}
#define U1_PART_H {part_size[1]}

/* LEDs / lamps (centro dos pontos, para lv_obj retângulos) — coords da cena */
#define U1_LED_PANEL_X {sx(548)}
#define U1_LED_PANEL_Y 104
#define U1_LED_PANEL_W 16
#define U1_LED_PANEL_H 26
#define U1_LAMP_L_X {sx(348)}
#define U1_LAMP_R_X {sx(516)}
#define U1_LAMP_Y 154
#define U1_FRONT_LED_X {sx(506)}
#define U1_FRONT_LED_Y 386
#define U1_FRONT_LED_W 34
#define U1_FRONT_LED_H 8
#define U1_BED_Y 222

/* janela do teto (clip do carro superior, se precisar) */
#define U1_ROOF_X0 {sx(328)}
#define U1_ROOF_Y0 52
#define U1_ROOF_X1 {sx(548)}
#define U1_ROOF_Y1 86
"""
    (FW / "src" / "u1_anim_coords.h").write_text(text)
    print("wrote u1_anim_coords.h")
    print(text)


def debug_vis(clean, combined, cpos, orig):
    # rest composition in original 800 space
    vis = clean.convert("RGBA")
    vis.paste(combined, cpos, combined)
    vis.crop((280, 50, 620, 280)).resize((680, 460), Image.Resampling.NEAREST).save(DEBUG / "combined_rest.png")
    # moved +40
    vis2 = clean.convert("RGBA")
    vis2.paste(combined, (cpos[0] + 40, cpos[1]), combined)
    vis2.crop((280, 50, 620, 280)).resize((680, 460), Image.Resampling.NEAREST).save(DEBUG / "combined_moved.png")
    # sprite isolated
    combined.resize((combined.width * 3, combined.height * 3), Image.Resampling.NEAREST).save(DEBUG / "combined_sprite.png")
    # roof inpaint closeup
    clean.crop((320, 48, 560, 100)).resize((720, 156), Image.Resampling.NEAREST).save(DEBUG / "roof_inpaint.png")
    # 400-wide scene crop mock
    scene = Image.new("RGBA", (400, 480), (8, 15, 35, 255))
    bg = clean.convert("RGBA")
    scene.alpha_composite(bg, (-SCENE_CROP_X, 0))
    scene.alpha_composite(combined, (cpos[0] - SCENE_CROP_X, cpos[1]))
    part = Image.open(PNG_OUT / "img_u1_part.png").convert("RGBA")
    scene.alpha_composite(part, (385 - SCENE_CROP_X, 186))
    scene.save(DEBUG / "scene_400.png")
    print("debug vis saved")


def main():
    orig_im = Image.open(ORIG).convert("RGB")
    orig = np.array(orig_im)
    carriage, keep, cpos = extract_roof_carriage(orig_im)
    print("carriage sprite", carriage.size, "pos", cpos)
    carriage.resize((carriage.width * 6, carriage.height * 6), Image.Resampling.NEAREST).save(DEBUG / "carriage_final.png")

    toolhead, tpos = extract_toolhead_from_existing()
    part, ppos = extract_part_from_existing()
    combined, comb_pos = combine(carriage, cpos, toolhead, tpos)
    print("COMBINED", combined.size, "pos", comb_pos)

    work = orig.copy()
    work = inpaint_chamber(work, orig)
    work = inpaint_roof(work, keep, orig)
    clean = Image.fromarray(work, "RGB")

    debug_vis(clean, combined, comb_pos, orig_im)

    th_info = emit_truecolor_alpha(combined, "img_u1_toolhead")
    pt_info = emit_truecolor_alpha(part, "img_u1_part")
    bg_info = emit_indexed8(clean, "img_bg_factory", colors=224)
    print("emit", th_info, pt_info, bg_info)
    patch_images_c(th_info["w"], th_info["h"], th_info["size"])
    write_coords(cpos, carriage.size, tpos, toolhead.size, ppos, part.size, comb_pos, combined.size)
    print("OK")


if __name__ == "__main__":
    main()
