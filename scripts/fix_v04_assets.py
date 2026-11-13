#!/usr/bin/env python3
"""v0.4 asset pass: clean a=254 on hair, split toolhead, emit bay spool."""
from __future__ import annotations

import colorsys
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
PNG = ROOT / "assets_png"
BIN = ROOT / "src" / "assets" / "bin"
ASSETS_C = ROOT / "src" / "assets" / "images.c"
ASSETS_S = ROOT / "src" / "assets" / "images.S"
ASSETS_H = ROOT / "src" / "assets" / "assets.h"
COORDS = ROOT / "src" / "u1_anim_coords.h"
QA = Path("/tmp/u1view/v04")
QA.mkdir(parents=True, exist_ok=True)

SCENE_CROP_X = 240
COMBINED_ORIG_X, COMBINED_ORIG_Y = 411, 47


def to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def emit_truecolor_alpha(im: Image.Image, name: str, save_png=True):
    im = im.convert("RGBA")
    w, h = im.size
    arr = np.array(im)
    data = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = arr[y, x]
            c = to_rgb565(int(r), int(g), int(b))
            data.append(c & 0xFF)
            data.append((c >> 8) & 0xFF)
            data.append(int(a))
    (BIN / f"{name}.bin").write_bytes(data)
    if save_png:
        im.save(PNG / f"{name}.png")
    return dict(name=name, w=w, h=h, size=len(data))


def is_filament_color(r, g, b) -> bool:
    """Pink/magenta winding only — not brown hair (hue ~10-20)."""
    h, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
    hue, sat, val = h * 360.0, s * 100.0, v * 255.0
    if 280.0 <= hue <= 355.0 and sat >= 35.0 and val >= 20.0:
        return True
    return False


def clean_a254(im: Image.Image):
    arr = np.array(im.convert("RGBA"))
    a = arr[:, :, 3]
    was = a == 254
    n_was = int(was.sum())
    keep = np.zeros(a.shape, dtype=bool)
    kept = dropped = 0
    ys, xs = np.where(was)
    for y, x in zip(ys, xs):
        r, g, b = int(arr[y, x, 0]), int(arr[y, x, 1]), int(arr[y, x, 2])
        if is_filament_color(r, g, b):
            keep[y, x] = True
            kept += 1
        else:
            dropped += 1
    a = np.where(was & ~keep, 255, a)
    arr[:, :, 3] = a
    return Image.fromarray(arr, "RGBA"), n_was, kept, dropped


def clean_all_characters():
    rows = []
    for p in sorted(PNG.glob("img_operario_*.png")):
        im = Image.open(p)
        w0, h0 = im.size
        out, n_was, kept, dropped = clean_a254(im)
        assert out.size == (w0, h0), p
        meta = emit_truecolor_alpha(out, p.stem)
        rows.append((p.name, n_was, kept, dropped, meta["size"]))
        print(f"  clean {p.name}: a254 {n_was} -> keep {kept}, drop {dropped}")
    return rows


def tight_bbox(arr, a_min=10, pad=1):
    ys, xs = np.where(arr[:, :, 3] > a_min)
    if len(xs) == 0:
        return 0, 0, arr.shape[1], arr.shape[0]
    x0, x1 = int(xs.min()), int(xs.max()) + 1
    y0, y1 = int(ys.min()), int(ys.max()) + 1
    x0 = max(0, x0 - pad)
    y0 = max(0, y0 - pad)
    x1 = min(arr.shape[1], x1 + pad)
    y1 = min(arr.shape[0], y1 + pad)
    return x0, y0, x1, y1


def split_toolhead():
    combined = Image.open("/tmp/u1view/png_backup_v03/img_u1_toolhead.png").convert("RGBA")
    assert combined.size == (55, 141), combined.size
    arr = np.array(combined)
    top = arr[:50]
    cx0, cy0, cx1, cy1 = tight_bbox(top, pad=1)
    carriage = Image.fromarray(arr[cy0:cy1, cx0:cx1], "RGBA")
    # inner: prefer the already-extracted 41x37 (local 0,104 in combined)
    inner_path = PNG / "img_u1_toolhead_inner.png"
    if inner_path.exists():
        inner = Image.open(inner_path).convert("RGBA")
        ix, iy = 0, 104
    else:
        bot = arr[90:]
        bx0, by0, bx1, by1 = tight_bbox(bot, pad=1)
        inner = Image.fromarray(arr[90 + by0 : 90 + by1, bx0:bx1], "RGBA")
        ix, iy = bx0, 90 + by0

    car_meta = emit_truecolor_alpha(carriage, "img_u1_carriage")
    th_meta = emit_truecolor_alpha(inner, "img_u1_toolhead")

    car_ox = COMBINED_ORIG_X + cx0
    car_oy = COMBINED_ORIG_Y + cy0
    th_ox = COMBINED_ORIG_X + ix
    th_oy = COMBINED_ORIG_Y + iy
    info = {
        "carriage": dict(
            w=car_meta["w"], h=car_meta["h"], size=car_meta["size"],
            orig=(car_ox, car_oy),
            scene=(car_ox - SCENE_CROP_X, car_oy),
            local=(cx0, cy0),
        ),
        "toolhead": dict(
            w=th_meta["w"], h=th_meta["h"], size=th_meta["size"],
            orig=(th_ox, th_oy),
            scene=(th_ox - SCENE_CROP_X, th_oy),
            local=(ix, iy),
        ),
    }
    print("  carriage", info["carriage"])
    print("  toolhead", info["toolhead"])
    carriage.save(QA / "carriage.png")
    inner.save(QA / "toolhead_inner.png")
    return info


def make_spool(w=24, h=26) -> Image.Image:
    """Front-facing pixel spool. Flanges/hub stay dark (a=255); winding is pink a=254."""
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    cx, cy = w // 2, h // 2 - 1
    R = 11
    hole = 3
    flange = 2
    for y in range(h):
        for x in range(w):
            dx, dy = x - cx + 0.5, y - cy + 0.5
            d = (dx * dx + dy * dy) ** 0.5
            if d > R + 0.6:
                continue
            # outer flange (never recolor)
            if d > R - flange:
                # slightly lighter on NW for form
                nw = (dx + dy) < -2
                if nw:
                    col = (72, 76, 88, 255)
                elif d > R - 0.4:
                    col = (18, 20, 28, 255)  # rim
                else:
                    col = (32, 34, 44, 255)
                px[x, y] = col
            elif d <= hole:
                # hub / hole
                if d > hole - 0.8:
                    px[x, y] = (28, 30, 38, 255)
                else:
                    px[x, y] = (10, 12, 18, 255)
            else:
                # filament winding — pink, a=254
                # shade by vertical position so the pre-recolor art looks round;
                # firmware replaces RGB with solid filament color.
                t = (dy / R + 1) * 0.5  # 0 top .. 1 bottom
                # bands for a wound look
                band = int((d * 3 + y)) % 3
                if t < 0.35:
                    r, g, b = 230, 90, 200
                elif t < 0.7:
                    r, g, b = 210, 50, 170
                else:
                    r, g, b = 160, 30, 130
                if band == 0:
                    r = min(255, r + 18)
                    b = min(255, b + 12)
                # NW highlight
                if dx < -2 and dy < -1 and d < R - flange - 1:
                    r, g, b = min(255, r + 30), min(255, g + 40), min(255, b + 20)
                px[x, y] = (r, g, b, 254)
    # tiny stand (dark, a=255) so it sits in the bay
    for x in range(cx - 3, cx + 4):
        if 0 <= x < w and h - 2 >= 0:
            if px[x, h - 2][3] == 0:
                px[x, h - 2] = (24, 26, 34, 255)
    for x in range(cx - 2, cx + 3):
        if 0 <= x < w:
            px[x, h - 1] = (16, 18, 24, 255)
    return im


def descriptor_block(name, w, h, size):
    return (
        f"extern const uint8_t {name}_map[];\n"
        f"const lv_img_dsc_t {name} = {{\n"
        f"    .header = {{ .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = {w}, .h = {h} }},\n"
        f"    .data_size = {size},\n"
        f"    .data = {name}_map,\n"
        f"}};\n"
    )


def asm_block(name):
    path = f"/workspace/u1-screen/firmware/src/assets/bin/{name}.bin"
    return (
        f".global {name}_map\n"
        f".align 4\n"
        f"{name}_map:\n"
        f'    .incbin "{path}"\n'
    )


def patch_registration(car, th, spool):
    # images.c — replace toolhead descriptor, insert carriage + spool after it
    c = ASSETS_C.read_text()
    old = (
        "extern const uint8_t img_u1_toolhead_map[];\n"
        "const lv_img_dsc_t img_u1_toolhead = {\n"
        "    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 55, .h = 141 },\n"
        "    .data_size = 23265,\n"
        "    .data = img_u1_toolhead_map,\n"
        "};\n"
    )
    new = (
        descriptor_block("img_u1_toolhead", th["w"], th["h"], th["size"])
        + "\n"
        + descriptor_block("img_u1_carriage", car["w"], car["h"], car["size"])
        + "\n"
        + descriptor_block("img_u1_spool", spool["w"], spool["h"], spool["size"])
    )
    if old not in c:
        # maybe already patched? try regex-ish replace of current toolhead block
        import re
        pat = re.compile(
            r"extern const uint8_t img_u1_toolhead_map\[\];\n"
            r"const lv_img_dsc_t img_u1_toolhead = \{[^}]+\};\n",
            re.S,
        )
        if not pat.search(c):
            raise SystemExit("images.c toolhead block not found")
        if "img_u1_carriage" in c:
            print("  images.c already has carriage, updating toolhead size only")
        c = pat.sub(new, c, count=1)
    else:
        c = c.replace(old, new, 1)
    ASSETS_C.write_text(c)
    print("  patched images.c")

    s = ASSETS_S.read_text()
    olds = (
        ".global img_u1_toolhead_map\n"
        ".align 4\n"
        "img_u1_toolhead_map:\n"
        '    .incbin "/workspace/u1-screen/firmware/src/assets/bin/img_u1_toolhead.bin"\n'
    )
    news = olds + "\n" + asm_block("img_u1_carriage") + "\n" + asm_block("img_u1_spool")
    if ".global img_u1_carriage_map" in s:
        print("  images.S already has carriage")
    else:
        if olds not in s:
            raise SystemExit("images.S toolhead block not found")
        s = s.replace(olds, news, 1)
        ASSETS_S.write_text(s)
        print("  patched images.S")

    h = ASSETS_H.read_text()
    oldh = (
        "extern const lv_img_dsc_t img_u1_toolhead;\n"
        "#define IMG_U1_TOOLHEAD_W 55\n"
        "#define IMG_U1_TOOLHEAD_H 141\n"
    )
    newh = (
        f"extern const lv_img_dsc_t img_u1_toolhead;\n"
        f"#define IMG_U1_TOOLHEAD_W {th['w']}\n"
        f"#define IMG_U1_TOOLHEAD_H {th['h']}\n"
        f"extern const lv_img_dsc_t img_u1_carriage;\n"
        f"#define IMG_U1_CARRIAGE_W {car['w']}\n"
        f"#define IMG_U1_CARRIAGE_H {car['h']}\n"
        f"extern const lv_img_dsc_t img_u1_spool;\n"
        f"#define IMG_U1_SPOOL_W {spool['w']}\n"
        f"#define IMG_U1_SPOOL_H {spool['h']}\n"
    )
    if "img_u1_carriage" in h:
        import re
        h = re.sub(r"#define IMG_U1_TOOLHEAD_W \d+", f"#define IMG_U1_TOOLHEAD_W {th['w']}", h)
        h = re.sub(r"#define IMG_U1_TOOLHEAD_H \d+", f"#define IMG_U1_TOOLHEAD_H {th['h']}", h)
        ASSETS_H.write_text(h)
        print("  assets.h already had carriage, updated sizes")
    else:
        if oldh not in h:
            raise SystemExit("assets.h toolhead block not found:\n" + h[:500])
        h = h.replace(oldh, newh, 1)
        ASSETS_H.write_text(h)
        print("  patched assets.h")


def write_coords(car, th):
    text = f"""/* Auto-gerado — coordenadas da cena U1 no painel direito 400x480.
 * Origem (0,0) = canto do clip (tela x=400, y=0).
 * O fundo 800x480 é desenhado com deslocamento -SCENE_CROP_X.
 *
 * v0.4: o carro do teto (img_u1_carriage) e o bico da câmara
 * (img_u1_toolhead) são duas sprites curtas, movidas com o mesmo dx.
 * Isso evita invalidar uma faixa alta e transparente no painel RGB.
 */
#pragma once
#define SCENE_CROP_X 240
#define SCENE_OX(orig_x) ((orig_x) - SCENE_CROP_X)

#define U1_CHAMBER_X0 86
#define U1_CHAMBER_Y0 152
#define U1_CHAMBER_X1 292
#define U1_CHAMBER_Y1 256

/* bico (câmara), scene-local — rest X alinhado à esquerda do conjunto antigo */
#define U1_TOOLHEAD_X {th['scene'][0]}
#define U1_TOOLHEAD_Y {th['scene'][1]}
#define U1_TOOLHEAD_W {th['w']}
#define U1_TOOLHEAD_H {th['h']}
#define U1_TOOLHEAD_X_MIN 120
#define U1_TOOLHEAD_X_MAX 230
#define U1_TOOLHEAD_TRAVEL (U1_TOOLHEAD_X_MAX - U1_TOOLHEAD_X_MIN)

/* gantry do teto — mesmo dx = (toolhead_x - U1_TOOLHEAD_X) */
#define U1_CARRIAGE_X {car['scene'][0]}
#define U1_CARRIAGE_Y {car['scene'][1]}
#define U1_CARRIAGE_W {car['w']}
#define U1_CARRIAGE_H {car['h']}

#define U1_PART_X 145
#define U1_PART_Y 186
#define U1_PART_W 80
#define U1_PART_H 56

/* 4 baias do topo (scene-local). Espaçamento 50 px, y da face ~12–60. */
#define U1_BAY_COUNT 4
#define U1_BAY_Y     12
#define U1_BAY_H     48
#define U1_BAY_X0    108
#define U1_BAY0_X    108
#define U1_BAY0_W    44
#define U1_BAY1_X    158
#define U1_BAY1_W    46
#define U1_BAY2_X    208
#define U1_BAY2_W    46
#define U1_BAY3_X    258
#define U1_BAY3_W    46

#define U1_BED_Y 222

/* janela do teto (clip do carro superior, se precisar) */
#define U1_ROOF_X0 88
#define U1_ROOF_Y0 52
#define U1_ROOF_X1 308
#define U1_ROOF_Y1 86
"""
    COORDS.write_text(text)
    print("  wrote", COORDS)


def qa_images(car, th, spool_im):
    # walk_0 cleaned + recolor-to-white simulation (exact RGB565 pipeline)
    walk = Image.open(PNG / "img_operario_walk_0.png").convert("RGBA")
    walk.save(QA / "walk0_clean.png")
    arr = np.array(walk)
    white = to_rgb565(255, 255, 255)
    vis = arr.copy()
    a254 = 0
    dark254 = 0
    hair254 = 0
    for y in range(arr.shape[0]):
        for x in range(arr.shape[1]):
            r, g, b, a = arr[y, x]
            if a == 254:
                a254 += 1
                luma = 0.299 * r + 0.587 * g + 0.114 * b
                if luma < 90:
                    dark254 += 1
                if y < 60 and x < 70:
                    hair254 += 1
                vis[y, x] = (255, 255, 255, 255)
    Image.fromarray(vis).save(QA / "walk0_recolor_white.png")
    print(f"  QA walk0: remaining a254={a254} dark254={dark254} hair-region a254={hair254}")

    # composite on dark bg for visibility
    bg = Image.new("RGBA", (walk.size[0] + 20, walk.size[1] + 20), (16, 24, 40, 255))
    bg.alpha_composite(Image.fromarray(vis), (10, 10))
    bg.convert("RGB").save(QA / "walk0_recolor_white_bg.png")
    bg2 = Image.new("RGBA", bg.size, (16, 24, 40, 255))
    bg2.alpha_composite(walk, (10, 10))
    bg2.convert("RGB").save(QA / "walk0_clean_bg.png")

    # spool tinted black / white / orange
    def tint(im, rgb):
        a = np.array(im)
        c = to_rgb565(*rgb)
        lo, hi = c & 0xFF, (c >> 8) & 0xFF
        # simulate firmware: replace a=254, skip brown
        out = a.copy()
        for y in range(a.shape[0]):
            for x in range(a.shape[1]):
                if a[y, x, 3] != 254:
                    continue
                r, g, b = int(a[y, x, 0]), int(a[y, x, 1]), int(a[y, x, 2])
                luma = (r * 3 + g * 6 + b) / 10
                if luma < 22 or (r > 30 and b * 5 < r * 2 and g <= r):
                    out[y, x, 3] = 255
                    continue
                # reconstruct display RGB565
                R = (c >> 8) & 0xF8
                G = (c >> 3) & 0xFC
                B = (c << 3) & 0xF8
                out[y, x] = (R, G, B, 255)
        return Image.fromarray(out)

    sheet = Image.new("RGBA", (140, 50), (16, 24, 40, 255))
    for i, col in enumerate([(255, 255, 255), (0, 0, 0), (255, 64, 0), (0x36, 0x21, 0x11)]):
        t = tint(spool_im, col)
        sheet.alpha_composite(t, (8 + i * 32, 10))
    sheet.convert("RGB").resize((140 * 4, 50 * 4), Image.NEAREST).save(QA / "spool_tints_x4.png")
    spool_im.resize((spool_im.size[0] * 6, spool_im.size[1] * 6), Image.NEAREST).save(QA / "spool_raw_x6.png")

    # bays on real bg
    bgf = Image.open(PNG / "img_bg_factory.png").convert("RGBA")
    crop = Image.new("RGBA", (400, 120), (8, 15, 35, 255))
    crop.alpha_composite(bgf, (-240, 0))
    bays = [(108, 44), (158, 46), (208, 46), (258, 46)]
    colors = [(0, 0, 0), (0x36, 0x21, 0x11), (255, 255, 255), (255, 64, 0)]
    mats = ["PETG", "PETG", "PLA", "PETG"]
    d = ImageDraw.Draw(crop)
    for i, ((x, w), col, m) in enumerate(zip(bays, colors, mats)):
        if i == 2:
            d.rectangle((x - 1, 11, x + w, 12 + 48), outline=(0x59, 0xEC, 0xC5, 255), width=2)
        t = tint(spool_im, col)
        sx = x + (w - t.size[0]) // 2
        crop.alpha_composite(t, (sx, 24))
        d.rectangle((x + 1, 48, x + w - 2, 59), fill=(12, 25, 48, 230), outline=(42, 60, 92, 255))
    crop.convert("RGB").resize((400 * 2, 120 * 2), Image.NEAREST).save(QA / "bays_mock_x2.png")


def main():
    print("== clean character a=254 ==")
    clean_all_characters()
    print("== split toolhead ==")
    info = split_toolhead()
    print("== spool ==")
    spool_im = make_spool()
    spool = emit_truecolor_alpha(spool_im, "img_u1_spool")
    print("  spool", spool)
    print("== register ==")
    patch_registration(info["carriage"], info["toolhead"], spool)
    write_coords(info["carriage"], info["toolhead"])
    print("== QA ==")
    qa_images(info["carriage"], info["toolhead"], spool_im)
    print("done")


if __name__ == "__main__":
    main()
