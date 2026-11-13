#!/usr/bin/env python3
"""Process walk/state sheets + U1 overlays; rewrite assets.h / images.c / images.S."""
from __future__ import annotations
from pathlib import Path
import numpy as np
from PIL import Image, ImageFilter

HERE = Path(__file__).resolve().parent
src = (HERE / "convert_assets.py").read_text()
idx = src.find("# ---- background from official")
ns = {}
exec(compile(src[:idx], "convert_assets.py", "exec"), ns)
chroma = ns["chroma"]
corner_keys = ns["corner_keys"]
tight = ns["tight"]
split_equal = ns["split_equal"]
mark_recolor = ns["mark_recolor"]
flip_h = ns["flip_h"]
emit_truecolor_alpha = ns["emit_truecolor_alpha"]
emit_indexed8 = ns["emit_indexed8"]
PNG_OUT = ns["PNG_OUT"]
BIN_OUT = ns["BIN_OUT"]
SRC_OUT = ns["SRC_OUT"]
FW = ns["FW"]
REFS = FW / "assets_png" / "refs"
GREEN_KEYS = [(0, 255, 0), (1, 254, 1), (20, 255, 20), (0, 230, 0),
              (40, 255, 40), (20, 233, 25), (14, 233, 19), (22, 223, 26),
              (25, 219, 25), (12, 240, 16), (30, 250, 30), (8, 200, 12)]

def despill(im: Image.Image) -> Image.Image:
    """Remove green fringe on semi-transparent edges."""
    arr = np.array(im.convert("RGBA")).astype(np.int16)
    r, g, b, a = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2], arr[:, :, 3]
    fringe = (a > 0) & (a < 250) & (g > r + 15) & (g > b + 15)
    # pull green down to the average of r,b
    g2 = g.copy()
    g2[fringe] = ((r[fringe] + b[fringe]) // 2)
    arr[:, :, 1] = g2
    # fully transparent if almost green and low alpha
    almost = (a < 80) & (g > r + 20) & (g > b + 20)
    arr[:, :, 3] = np.where(almost, 0, arr[:, :, 3])
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")

def process_sheet(path, n, thresh=42, pad=4):
    im = Image.open(path)
    keys = corner_keys(im) + GREEN_KEYS
    print(f"{path.name}: size={im.size} key={keys[0]}")
    cut = despill(chroma(im, keys, thresh=thresh))
    raw = split_equal(cut, n)
    frames = [despill(tight(f, pad=pad)) for f in raw]
    for i, f in enumerate(frames):
        print(f"  raw tight {i}: {f.size}")
    return frames

def fit_group(frames, max_h, recolor=False):
    """Uniform scale so the tallest frame is max_h, then pad to common box (feet at bottom)."""
    mh = max(f.height for f in frames)
    scale = max_h / mh if mh > max_h else (max_h / mh if mh > 0 else 1)
    # Always scale so tallest == max_h (unify size with original 168-style characters)
    scale = max_h / mh
    scaled = []
    for f in frames:
        nw = max(1, int(round(f.width * scale)))
        nh = max(1, int(round(f.height * scale)))
        scaled.append(f.resize((nw, nh), Image.Resampling.LANCZOS))
    mw = max(s.width for s in scaled)
    mh2 = max(s.height for s in scaled)
    out = []
    for s in scaled:
        canvas = Image.new("RGBA", (mw, mh2), (0, 0, 0, 0))
        canvas.paste(s, ((mw - s.width) // 2, mh2 - s.height), s)
        if recolor:
            canvas = mark_recolor(canvas, hue_lo=270, hue_hi=20, sat_min=35, val_min=40)
            # also try magenta/pink more broadly: hue wraps
            canvas = mark_recolor(canvas, hue_lo=280, hue_hi=350, sat_min=35, val_min=40)
        out.append(canvas)
    return out

def extract_u1():
    backup = FW / "assets_png" / "img_bg_factory_orig.png"
    if not backup.exists():
        raise SystemExit("missing img_bg_factory_orig.png")
    bg = Image.open(backup).convert("RGB")
    a = np.array(bg)

    # --- toolhead: precise crop, mask dark chamber ---
    tx0, ty0, tx1, ty1 = 392, 148, 450, 204
    th = bg.crop((tx0, ty0, tx1, ty1)).convert("RGBA")
    ta = np.array(th)
    r, g, b = ta[:, :, 0].astype(int), ta[:, :, 1].astype(int), ta[:, :, 2].astype(int)
    lum = (r + g + b) / 3.0
    # chamber back is dark (lum typically < 50, low chroma)
    # keep: light carriage, yellow nozzle, black outline (low lum BUT near a kept pixel)
    yellow = (r > 160) & (g > 90) & (r > b + 30)
    light = lum >= 90  # carriage body
    outline = (lum < 50) & (lum > 8) & ((r < 50) | (np.abs(r - b) < 25))
    # start with yellow + light
    keep = yellow | light
    # dilate and include dark outline pixels adjacent to keep
    k = keep.copy()
    for _ in range(3):
        k2 = k.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                k2 |= np.roll(np.roll(k, dy, 0), dx, 1)
        k = k2
    keep = keep | (k & (lum < 80))  # outline
    ta[:, :, 3] = np.where(keep, 255, 0)
    th_im = tight(Image.fromarray(ta, "RGBA"), pad=1)
    th_info = emit_truecolor_alpha(th_im, "img_u1_toolhead")
    print("toolhead", th_im.size)
    th_im.save("/tmp/u1_toolhead.png")

    # --- benchy ---
    bx0, by0, bx1, by1 = 386, 186, 462, 250
    pt = bg.crop((bx0, by0, bx1, by1)).convert("RGBA")
    pa = np.array(pt)
    r, g, b = pa[:, :, 0].astype(int), pa[:, :, 1].astype(int), pa[:, :, 2].astype(int)
    boat = (r > 165) & (g > 110) & (b < 130) & (r > b + 45) & (g > b + 20)
    # include dark windows of the cabin (holes in yellow) only if enclosed — skip, they can be transparent
    # dilate slightly for outline
    k = boat.copy()
    for _ in range(2):
        k2 = k.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                k2 |= np.roll(np.roll(k, dy, 0), dx, 1)
        k = k2
    # black outline around boat
    keep = boat | (k & (r + g + b < 180))
    pa[:, :, 3] = np.where(keep, 255, 0)
    pt_im = tight(Image.fromarray(pa, "RGBA"), pad=1)
    pt_info = emit_truecolor_alpha(pt_im, "img_u1_part")
    print("part", pt_im.size)
    pt_im.save("/tmp/u1_part.png")

    # --- inpaint: clone chamber / bed ---
    work = a.copy()
    # toolhead hole: copy from a clean column just to the left, same y
    src_x0, src_x1 = 340, 385
    for y in range(ty0, ty1):
        for x in range(tx0, tx1):
            # only overwrite where we removed the toolhead (original was "kept")
            lx = x - tx0
            ly = y - ty0
            if ly < keep.shape[0] and lx < keep.shape[1]:
                pass
    # Use the toolhead keep mask in original crop coords
    th_keep = keep if False else None
    tha = np.array(th)
    # recount keep from tha alpha we set — tha is before tight, same as crop
    th_keep = tha[:, :, 3] > 0
    # dilate for inpaint
    m = th_keep.copy()
    for _ in range(3):
        m2 = m.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                m2 |= np.roll(np.roll(m, dy, 0), dx, 1)
        m = m2
    for y in range(m.shape[0]):
        for x in range(m.shape[1]):
            if not m[y, x]:
                continue
            # sample from clean chamber to the left, same local y
            sx = src_x0 + (x % max(1, src_x1 - src_x0))
            work[ty0 + y, tx0 + x] = a[ty0 + y, sx]

    # benchy hole: above bed (~y 222) use chamber; on bed use bed color
    pa_keep = pa[:, :, 3] > 0
    m = pa_keep.copy()
    for _ in range(3):
        m2 = m.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                m2 |= np.roll(np.roll(m, dy, 0), dx, 1)
        m = m2
    BED_Y = 222  # absolute
    # sample bed color
    bed_sample = a[230:248, 400:450]
    bed_color = np.median(bed_sample.reshape(-1, 3), axis=0).astype(np.uint8)
    for y in range(m.shape[0]):
        ay = by0 + y
        for x in range(m.shape[1]):
            if not m[y, x]:
                continue
            if ay >= BED_Y:
                work[ay, bx0 + x] = bed_color
            else:
                sx = src_x0 + (x % max(1, src_x1 - src_x0))
                work[ay, bx0 + x] = a[ay, sx]

    clean = Image.fromarray(work, "RGB")
    bg_info = emit_indexed8(clean, "img_bg_factory", colors=224)
    print("BG", bg_info)
    clean.crop((250, 80, 600, 300)).save("/tmp/u1_clean_crop.png")

    # After tight(), placement needs the offset of tight crop within the raw box.
    # We used pad=1 tight. Compute:
    def tight_off(raw_im, box):
        arr = np.array(raw_im)
        vis = arr[:, :, 3] > 16
        ys, xs = np.where(vis)
        if len(xs) == 0:
            return box[0], box[1]
        return box[0] + int(xs.min()), box[1] + int(ys.min())

    # re-make raw (before tight) for offset — we already tighted. Approximate:
    # toolhead tight of (tx0,ty0) crop with pad=1
    # We'll just use measured values from the tight images vs boxes.
    # For firmware, place toolhead so its visual matches original:
    # I'll inspect after and write coords.
    (FW / "src" / "u1_anim_coords.h").write_text(
        "/* Auto-gerado — coordenadas da cena U1 (800x480). */\n"
        "#pragma once\n"
        f"#define U1_CHAMBER_X0 322\n"
        f"#define U1_CHAMBER_Y0 150\n"
        f"#define U1_CHAMBER_X1 536\n"
        f"#define U1_CHAMBER_Y1 258\n"
        f"#define U1_TOOLHEAD_X {tx0}\n"
        f"#define U1_TOOLHEAD_Y {ty0}\n"
        f"#define U1_TOOLHEAD_W {th_im.size[0]}\n"
        f"#define U1_TOOLHEAD_H {th_im.size[1]}\n"
        f"#define U1_TOOLHEAD_TRAVEL 70\n"
        f"#define U1_PART_X {bx0}\n"
        f"#define U1_PART_Y {by0}\n"
        f"#define U1_PART_W {pt_im.size[0]}\n"
        f"#define U1_PART_H {pt_im.size[1]}\n"
    )
    return [bg_info, th_info, pt_info], (tx0, ty0, bx0, by0)

def emit_all(all_infos):
    asm = ['.section .rodata.img,"a"', ".align 4", ""]
    c = ['#include "assets.h"', ""]
    h = ["#pragma once", "#include <lvgl.h>", "#ifdef __cplusplus", 'extern "C" {', "#endif", ""]
    for info in all_infos:
        sym = info["name"] + "_map"
        asm += [f".global {sym}", ".align 4", f"{sym}:", f'    .incbin BIN_{info["name"].upper()}', ""]
        h.append(f"extern const lv_img_dsc_t {info['name']};")
        h.append(f"#define {info['name'].upper()}_W {info['w']}")
        h.append(f"#define {info['name'].upper()}_H {info['h']}")
        c += [
            f"extern const uint8_t {sym}[];",
            f"const lv_img_dsc_t {info['name']} = {{",
            f"    .header = {{ .cf = {info['cf']}, .always_zero = 0, .reserved = 0, .w = {info['w']}, .h = {info['h']} }},",
            f"    .data_size = {info['size']},",
            f"    .data = {sym},",
            "};",
            "",
        ]
    h += [
        "",
        "extern const lv_img_dsc_t *const img_operario_walk[4];",
        "extern const lv_img_dsc_t *const img_operario_walk_l[4];",
        "extern const lv_img_dsc_t *const img_operario_troca[4];",
        "extern const lv_img_dsc_t *const img_operario_senta[2];",
        "extern const lv_img_dsc_t *const img_operario_celebra[2];",
        "extern const lv_img_dsc_t *const img_operario_susto[2];",
        "extern const lv_img_dsc_t *const img_operadora_idle[2];",
        "extern const lv_img_dsc_t *const img_operadora_apresenta[2];",
        "extern const lv_img_dsc_t *const img_operadora_dorme[2];",
        "extern const lv_img_dsc_t *const img_operadora_celebra[2];",
        "extern const lv_img_dsc_t *const img_operadora_susto[2];",
        "",
        "#ifdef __cplusplus", "}", "#endif", "",
    ]
    c += [
        "const lv_img_dsc_t *const img_operario_walk[4] = { &img_operario_walk_0, &img_operario_walk_1, &img_operario_walk_2, &img_operario_walk_3 };",
        "const lv_img_dsc_t *const img_operario_walk_l[4] = { &img_operario_walk_l_0, &img_operario_walk_l_1, &img_operario_walk_l_2, &img_operario_walk_l_3 };",
        "const lv_img_dsc_t *const img_operario_troca[4] = { &img_operario_troca_0, &img_operario_troca_1, &img_operario_troca_2, &img_operario_troca_3 };",
        "const lv_img_dsc_t *const img_operario_senta[2] = { &img_operario_senta_0, &img_operario_senta_1 };",
        "const lv_img_dsc_t *const img_operario_celebra[2] = { &img_operario_celebra_0, &img_operario_celebra_1 };",
        "const lv_img_dsc_t *const img_operario_susto[2] = { &img_operario_susto_0, &img_operario_susto_1 };",
        "const lv_img_dsc_t *const img_operadora_idle[2] = { &img_operadora_0, &img_operadora_1 };",
        "const lv_img_dsc_t *const img_operadora_apresenta[2] = { &img_operadora_2, &img_operadora_3 };",
        "const lv_img_dsc_t *const img_operadora_dorme[2] = { &img_operadora_dorme_0, &img_operadora_dorme_1 };",
        "const lv_img_dsc_t *const img_operadora_celebra[2] = { &img_operadora_celebra_0, &img_operadora_celebra_1 };",
        "const lv_img_dsc_t *const img_operadora_susto[2] = { &img_operadora_susto_0, &img_operadora_susto_1 };",
        "",
    ]
    (SRC_OUT / "images.S").write_text("\n".join(asm) + "\n")
    (SRC_OUT / "images.c").write_text("\n".join(c) + "\n")
    (SRC_OUT / "assets.h").write_text("\n".join(h) + "\n")
    total = sum(i["size"] for i in all_infos)
    print(f"TOTAL {total} bytes ({total/1024:.1f} KB), {len(all_infos)} images")
    for i in all_infos:
        print(f"  {i['name']:28s} {i['w']:4d}x{i['h']:<4d} {i['size']/1024:6.1f} KB")

def emit_named(frames, names, recolor=False):
    infos = []
    if recolor:
        frames = [mark_recolor(f, hue_lo=280, hue_hi=350, sat_min=35, val_min=40) for f in frames]
    for f, n in zip(frames, names):
        inf = emit_truecolor_alpha(f, n)
        rec = int((np.array(f)[:, :, 3] == 254).sum())
        print(f"  {n} {f.size} recolor={rec}")
        infos.append(inf)
    return infos

def main():
    all_infos = []
    u1, _ = extract_u1()
    all_infos.extend(u1)

    walk = fit_group(process_sheet(REFS / "sheet-operario-walk-cycle.jpg", 4), 168, recolor=True)
    all_infos += emit_named(walk, [f"img_operario_walk_{i}" for i in range(4)])
    for i, f in enumerate(walk):
        all_infos.append(emit_truecolor_alpha(flip_h(f), f"img_operario_walk_l_{i}"))

    for i in range(4):
        all_infos.append(emit_truecolor_alpha(Image.open(PNG_OUT / f"img_operario_troca_{i}.png"), f"img_operario_troca_{i}"))

    wst = fit_group(process_sheet(REFS / "sheet-operario-states.jpg", 6), 196, recolor=True)
    all_infos += emit_named(wst, [
        "img_operario_senta_0", "img_operario_senta_1",
        "img_operario_celebra_0", "img_operario_celebra_1",
        "img_operario_susto_0", "img_operario_susto_1",
    ])

    for i in range(4):
        all_infos.append(emit_truecolor_alpha(Image.open(PNG_OUT / f"img_operadora_{i}.png"), f"img_operadora_{i}"))

    ost = fit_group(process_sheet(REFS / "sheet-operadora-states.jpg", 6), 196, recolor=False)
    all_infos += emit_named(ost, [
        "img_operadora_dorme_0", "img_operadora_dorme_1",
        "img_operadora_celebra_0", "img_operadora_celebra_1",
        "img_operadora_susto_0", "img_operadora_susto_1",
    ])

    emit_all(all_infos)
    # previews
    def sheet(names, out):
        ims = [Image.open(PNG_OUT / f"{n}.png").convert("RGBA") for n in names]
        w = sum(i.width for i in ims) + 8 * len(ims)
        h = max(i.height for i in ims) + 10
        c = Image.new("RGBA", (w, h), (8, 15, 35, 255))
        x = 4
        for i in ims:
            c.paste(i, (x, h - i.height - 4), i)
            x += i.width + 8
        c.save(out)
    sheet([f"img_operario_walk_{i}" for i in range(4)], "/tmp/pv_walk.png")
    sheet(["img_operario_senta_0","img_operario_senta_1","img_operario_celebra_0",
           "img_operario_celebra_1","img_operario_susto_0","img_operario_susto_1"], "/tmp/pv_op_st.png")
    sheet(["img_operadora_dorme_0","img_operadora_dorme_1","img_operadora_celebra_0",
           "img_operadora_celebra_1","img_operadora_susto_0","img_operadora_susto_1"], "/tmp/pv_chefe_st.png")

if __name__ == "__main__":
    main()
