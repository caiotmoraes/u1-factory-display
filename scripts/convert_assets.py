#!/usr/bin/env python3
"""Crop sprite sheets, quantize the factory background, emit LVGL images (incbin)."""
from __future__ import annotations
from pathlib import Path
from PIL import Image, ImageFilter, ImageChops
import numpy as np
import shutil

ROOT = Path("/workspace/u1-screen")
FW = ROOT / "firmware"
SRC_OUT = FW / "src" / "assets"
PNG_OUT = FW / "assets_png"
BIN_OUT = FW / "src" / "assets" / "bin"
PNG_OUT.mkdir(parents=True, exist_ok=True)
BIN_OUT.mkdir(parents=True, exist_ok=True)
SRC_OUT.mkdir(parents=True, exist_ok=True)

def chroma(im: Image.Image, key_samples, thresh=55, also_keys=None) -> Image.Image:
    """Make near-key-color pixels transparent. key_samples: list of RGB tuples."""
    im = im.convert("RGBA")
    arr = np.array(im).astype(np.int16)
    rgb = arr[:, :, :3]
    keys = list(key_samples)
    if also_keys:
        keys.extend(also_keys)
    keys = np.array(keys, dtype=np.int16)
    dist = np.min(np.sqrt(((rgb[:, :, None, :] - keys[None, None, :, :]) ** 2).sum(-1)), axis=2)
    alpha = arr[:, :, 3].copy()
    # hard + soft edge for JPEG fringing
    fade_w = 12
    alpha[dist < thresh] = 0
    fade = (dist >= thresh) & (dist < thresh + fade_w)
    alpha[fade] = np.clip((dist[fade] - thresh) * (255 / fade_w), 0, 255).astype(np.int16)
    arr[:, :, 3] = alpha
    return Image.fromarray(arr.astype(np.uint8), "RGBA")

def corner_keys(im, n=12):
    a = np.array(im.convert("RGB"))
    h, w = a.shape[:2]
    pts = [a[0, 0], a[0, w-1], a[h-1, 0], a[h-1, w-1],
           a[2, 2], a[2, w-3], a[h-3, 2], a[h-3, w-3],
           a[0, w//2], a[h//2, 0], a[h-1, w//2], a[h//2, w-1]]
    # median of corners
    med = np.median(np.stack(pts[:8]), axis=0).astype(int)
    return [tuple(med.tolist())] + [tuple(p.tolist()) for p in pts[:4]]

def tight(im: Image.Image, pad=4) -> Image.Image:
    a = np.array(im)
    vis = a[:, :, 3] > 16
    if not vis.any():
        return im
    ys, xs = np.where(vis)
    y0, y1 = max(0, ys.min() - pad), min(im.height, ys.max() + 1 + pad)
    x0, x1 = max(0, xs.min() - pad), min(im.width, xs.max() + 1 + pad)
    return im.crop((x0, y0, x1, y1))

def split_equal(im: Image.Image, n: int) -> list[Image.Image]:
    w, h = im.size
    sw = w // n
    frames = []
    for i in range(n):
        inset = 8
        x0 = i * sw + (inset if i else 0)
        x1 = ((i + 1) * sw if i < n - 1 else w) - (inset if i < n - 1 else 0)
        frames.append(im.crop((x0, 0, x1, h)))
    return frames

def align_bottom(frames: list[Image.Image], height: int) -> list[Image.Image]:
    """Scale uniformly so max height == height, then pad to same box, feet at bottom."""
    # first scale each so its height is `height` (keep aspect), then pad width to max
    scaled = []
    for f in frames:
        if f.height == 0:
            scaled.append(f)
            continue
        ratio = height / f.height
        nw = max(1, int(round(f.width * ratio)))
        scaled.append(f.resize((nw, height), Image.Resampling.LANCZOS))
    mw = max(s.width for s in scaled)
    out = []
    for s in scaled:
        canvas = Image.new("RGBA", (mw, height), (0, 0, 0, 0))
        canvas.paste(s, ((mw - s.width) // 2, 0), s)
        out.append(canvas)
    return out

def mark_recolor(im: Image.Image, hue_lo=300, hue_hi=350, sat_min=80, val_min=80) -> Image.Image:
    """Set alpha=254 on hot-pink filament pixels so firmware can recolor them."""
    arr = np.array(im.convert("RGBA"))
    rgb = arr[:, :, :3].astype(np.uint8)
    hsv = np.array(Image.fromarray(rgb, "RGB").convert("HSV"))
    h, s, v = hsv[:, :, 0].astype(int), hsv[:, :, 1], hsv[:, :, 2]
    # PIL HSV hue is 0-255, 300-350 deg -> 300/360*255=212, 350/360*255=248
    h_lo = int(hue_lo / 360 * 255)
    h_hi = int(hue_hi / 360 * 255)
    if h_lo <= h_hi:
        hue_ok = (h >= h_lo) & (h <= h_hi)
    else:
        hue_ok = (h >= h_lo) | (h <= h_hi)
    mask = hue_ok & (s >= sat_min) & (v >= val_min) & (arr[:, :, 3] > 128)
    arr[:, :, 3] = np.where(mask, 254, arr[:, :, 3])
    return Image.fromarray(arr, "RGBA")

def flip_h(im: Image.Image) -> Image.Image:
    return im.transpose(Image.Transpose.FLIP_LEFT_RIGHT)

def to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def emit_truecolor_alpha(im: Image.Image, name: str):
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
    path = BIN_OUT / f"{name}.bin"
    path.write_bytes(data)
    im.save(PNG_OUT / f"{name}.png")
    return dict(name=name, w=w, h=h, cf="LV_IMG_CF_TRUE_COLOR_ALPHA",
                size=len(data), bin=path.name)

def emit_indexed8(im: Image.Image, name: str, colors=256):
    im = im.convert("RGB")
    pal = im.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    # pal is P mode
    palette = pal.getpalette()[: colors * 3]
    # pad palette to 256
    while len(palette) < 256 * 3:
        palette.extend([0, 0, 0])
    pix = np.array(pal)
    data = bytearray()
    # LVGL 8 INDEXED_8BIT: 256 x lv_color32_t (blue, green, red, alpha)
    for i in range(256):
        r, g, b = palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]
        data.extend([b, g, r, 0xFF])
    data.extend(pix.tobytes())
    path = BIN_OUT / f"{name}.bin"
    path.write_bytes(data)
    pal.convert("RGB").save(PNG_OUT / f"{name}.png")
    return dict(name=name, w=im.width, h=im.height, cf="LV_IMG_CF_INDEXED_8BIT",
                size=len(data), bin=path.name)

def process_sheet(path, n, name_prefix, height, recolor=False, thresh=58, extra_keys=None):
    im = Image.open(path)
    keys = corner_keys(im)
    if extra_keys:
        keys.extend(extra_keys)
    print(f"{path}: key={keys[0]} size={im.size}")
    cut = chroma(im, keys, thresh=thresh)
    raw_frames = split_equal(cut, n)
    frames = [tight(f, pad=6) for f in raw_frames]
    frames = align_bottom(frames, height)
    if recolor:
        frames = [mark_recolor(f, hue_lo=300, hue_hi=350, sat_min=55, val_min=70) for f in frames]  # only pink winding, never brown hair
    infos = []
    for i, f in enumerate(frames):
        infos.append(emit_truecolor_alpha(f, f"{name_prefix}_{i}"))
        n_recolor = int((np.array(f)[:, :, 3] == 254).sum())
        print(f"  {name_prefix}_{i} {f.size} recolor_px={n_recolor}")
    return infos, frames

# ---- background from official Spool Factory art ----
factory = Image.open(ROOT / "spool-factory" / "assets" / "factory.png").convert("RGB")
# scale to width 800, then crop height to 480, keep the U1 body
w, h = factory.size
scaled = factory.resize((800, int(round(h * 800 / w))), Image.Resampling.LANCZOS)
# 800 x ~600; crop equally, slight bias downward so hazard stripe stays
sh = scaled.height
if sh > 480:
    extra = sh - 480
    top = extra  # drop upper sky/lamps as needed, keep the factory floor
    top = max(0, min(top, extra))
    scaled = scaled.crop((0, top, 800, top + 480))
elif sh < 480:
    canvas = Image.new("RGB", (800, 480), (16, 25, 47))
    canvas.paste(scaled, (0, (480 - sh) // 2))
    scaled = canvas
scaled = scaled.resize((800, 480), Image.Resampling.LANCZOS)
bg_info = emit_indexed8(scaled, "img_bg_factory", colors=224)
print("BG", bg_info)

# ---- sheets ----
SHEETS = [
    ("sheet-operario-walk", 4, "img_operario_walk", 168, True, 62, None),
    ("sheet-operario-troca", 4, "img_operario_troca", 168, True, 36, [(9, 137, 139), (15, 150, 148), (8, 120, 122)]),
    ("sheet-operadora", 4, "img_operadora", 176, False, 62, None),
    ("sheet-bobina", 6, "img_bobina", 172, False, 62, None),
]
# map generated jpg paths
GEN = {
    "sheet-operario-walk": "/cursor/stores/user/grok-bot/agents/491e7a23-4347-490e-b33c-d1a99fb5b32d/assets/bfff439b33431e8009e9817b29c29b742312b199d37f77bdae0b8a328e149d4e.jpg",
    "sheet-operario-troca": "/cursor/stores/user/grok-bot/agents/491e7a23-4347-490e-b33c-d1a99fb5b32d/assets/9fa0e266a75e50e1fd29c820dc4bf61e14d7b0468121df1bc877b4855cfce3c0.jpg",
    "sheet-operadora": "/cursor/stores/user/grok-bot/agents/491e7a23-4347-490e-b33c-d1a99fb5b32d/assets/021038caeafd17b3bc841319e050d13285b1a33367b6032fa2b551d66cb53a79.jpg",
    "sheet-bobina": "/cursor/stores/user/grok-bot/agents/491e7a23-4347-490e-b33c-d1a99fb5b32d/assets/b1f4e9bce2dbf5f37b2376edf3a5df6bcfc95cd630421b2b14aa9ae6869f9cdb.jpg",
}

all_infos = [bg_info]
walk_frames = None
for key, n, prefix, height, recolor, thresh, extra in SHEETS:
    infos, frames = process_sheet(GEN[key], n, prefix, height, recolor, thresh, extra)
    all_infos.extend(infos)
    if prefix == "img_operario_walk":
        walk_frames = frames

# flipped walk for left direction
if walk_frames:
    for i, f in enumerate(walk_frames):
        ff = flip_h(f)
        all_infos.append(emit_truecolor_alpha(ff, f"img_operario_walk_l_{i}"))
        print(f"  flipped walk {i} {ff.size}")

# Rename bobina logically via aliases in the header (sleep 0-1, celebra 2-3, susto 4-5)

# ---- emit C descriptors + assembler incbin ----
asm = ['.section .rodata.img,"a"', ".align 4", ""]
c = [
    '#include "assets.h"',
    "",
]
h = [
    "#pragma once",
    "#include <lvgl.h>",
    "#ifdef __cplusplus",
    'extern "C" {',
    "#endif",
    "",
]
for info in all_infos:
    sym = info["name"] + "_map"
    asm.append(f".global {sym}")
    asm.append(f".align 4")
    asm.append(f"{sym}:")
    asm.append(f'    .incbin "bin/{info["bin"]}"')
    asm.append("")
    h.append(f"extern const lv_img_dsc_t {info['name']};")
    h.append(f"#define {info['name'].upper()}_W {info['w']}")
    h.append(f"#define {info['name'].upper()}_H {info['h']}")
    c.append(f"extern const uint8_t {sym}[];")
    c.append(f"const lv_img_dsc_t {info['name']} = {{")
    c.append(f"    .header = {{ .cf = {info['cf']}, .always_zero = 0, .reserved = 0, .w = {info['w']}, .h = {info['h']} }},")
    c.append(f"    .data_size = {info['size']},")
    c.append(f"    .data = {sym},")
    c.append("};")
    c.append("")

# convenience arrays
h.append("")
h.append("extern const lv_img_dsc_t *const img_operario_walk[4];")
h.append("extern const lv_img_dsc_t *const img_operario_walk_l[4];")
h.append("extern const lv_img_dsc_t *const img_operario_troca[4];")
h.append("extern const lv_img_dsc_t *const img_operadora_idle[2];")
h.append("extern const lv_img_dsc_t *const img_operadora_apresenta[2];")
h.append("extern const lv_img_dsc_t *const img_bobina_dorme[2];")
h.append("extern const lv_img_dsc_t *const img_bobina_celebra[2];")
h.append("extern const lv_img_dsc_t *const img_bobina_susto[2];")
h.append("")
h.append("#ifdef __cplusplus")
h.append("}")
h.append("#endif")

c.append("const lv_img_dsc_t *const img_operario_walk[4] = { &img_operario_walk_0, &img_operario_walk_1, &img_operario_walk_2, &img_operario_walk_3 };")
c.append("const lv_img_dsc_t *const img_operario_walk_l[4] = { &img_operario_walk_l_0, &img_operario_walk_l_1, &img_operario_walk_l_2, &img_operario_walk_l_3 };")
c.append("const lv_img_dsc_t *const img_operario_troca[4] = { &img_operario_troca_0, &img_operario_troca_1, &img_operario_troca_2, &img_operario_troca_3 };")
c.append("const lv_img_dsc_t *const img_operadora_idle[2] = { &img_operadora_0, &img_operadora_1 };")
c.append("const lv_img_dsc_t *const img_operadora_apresenta[2] = { &img_operadora_2, &img_operadora_3 };")
c.append("const lv_img_dsc_t *const img_bobina_dorme[2] = { &img_bobina_0, &img_bobina_1 };")
c.append("const lv_img_dsc_t *const img_bobina_celebra[2] = { &img_bobina_2, &img_bobina_3 };")
c.append("const lv_img_dsc_t *const img_bobina_susto[2] = { &img_bobina_4, &img_bobina_5 };")

(SRC_OUT / "images.S").write_text("\n".join(asm) + "\n")
(SRC_OUT / "images.c").write_text("\n".join(c) + "\n")
(SRC_OUT / "assets.h").write_text("\n".join(h) + "\n")

total = sum(i["size"] for i in all_infos)
print(f"TOTAL image data: {total} bytes ({total/1024:.1f} KB), {len(all_infos)} images")
for i in all_infos:
    print(f"  {i['name']:28s} {i['w']:4d}x{i['h']:<4d} {i['size']/1024:7.1f} KB  {i['cf']}")
