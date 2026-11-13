#!/usr/bin/env python3
"""Host verification of the LVGL TRUE_COLOR_ALPHA pipeline (v0.4).

Decodes generated .bin files the same way the board does
(lo, hi, alpha; RGB565; SWAP=0) and simulates firmware recolor,
including the dark/brown-hair skip.
"""
from __future__ import annotations
import sys
from pathlib import Path
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "src" / "assets" / "bin"
PNG = ROOT / "assets_png"
QA = Path("/tmp/u1view/v04")
QA.mkdir(parents=True, exist_ok=True)
failed = 0


def fail(msg):
    global failed
    failed += 1
    print("  FAIL:", msg)


def ok(msg):
    print("  OK  :", msg)


def decode_tca(name, w, h):
    data = (BIN / f"{name}.bin").read_bytes()
    expect = w * h * 3
    if len(data) != expect:
        fail(f"{name}: bin size {len(data)} != {expect} ({w}x{h}*3)")
    arr = np.zeros((h, w, 4), dtype=np.uint8)
    i = 0
    for y in range(h):
        for x in range(w):
            lo, hi, a = data[i], data[i + 1], data[i + 2]
            i += 3
            c = lo | (hi << 8)
            r = (c >> 8) & 0xF8
            g = (c >> 3) & 0xFC
            b = (c << 3) & 0xF8
            arr[y, x] = (r, g, b, a)
    return arr


def recolor(arr, rgb, skip_brown=True):
    """Firmware recolor_into."""
    Rt, Gt, Bt = (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255
    c565 = ((Rt & 0xF8) << 8) | ((Gt & 0xFC) << 3) | (Bt >> 3)
    out = arr.copy()
    h, w = arr.shape[:2]
    painted = skipped = 0
    for y in range(h):
        for x in range(w):
            if arr[y, x, 3] != 254:
                continue
            r, g, b = int(arr[y, x, 0]), int(arr[y, x, 1]), int(arr[y, x, 2])
            brown = (r > 40 and (b * 5) < r and g <= r)
            if skip_brown and brown:
                out[y, x, 3] = 255
                skipped += 1
                continue
            R = (c565 >> 8) & 0xF8
            G = (c565 >> 3) & 0xFC
            B = (c565 << 3) & 0xF8
            out[y, x] = (R, G, B, 255)
            painted += 1
    return out, painted, skipped


def test_walk():
    print("== walk_0 hair / filament ==")
    arr = decode_tca("img_operario_walk_0", 89, 168)
    Image.fromarray(arr).save(QA / "dec_walk0.png")
    a254 = int((arr[:, :, 3] == 254).sum())
    # hair region (helmet / hair): top-left of sprite
    hair = arr[10:60, 20:70]
    hair254 = int((hair[:, :, 3] == 254).sum())
    # sample known hair pixel from diagnosis: (45, 29) was (86,35,15,254)
    hr, hg, hb, ha = [int(v) for v in arr[29, 45]]
    print(f"    a254={a254} hair-region a254={hair254} pixel(45,29)=({hr},{hg},{hb},{ha})")
    if hair254 != 0:
        fail(f"hair region still has {hair254} a=254 pixels")
    else:
        ok("hair region has no a=254")
    if ha == 254:
        fail("known hair pixel (45,29) still marked 254")
    elif hr < 40 and hg < 40 and hb < 40:
        fail(f"hair pixel (45,29) too dark/washed ({hr},{hg},{hb})")
    else:
        ok(f"hair pixel (45,29) stays dark-brown ({hr},{hg},{hb},a={ha})")

    white, painted, skipped = recolor(arr, 0xFFFFFF)
    Image.fromarray(white).save(QA / "dec_walk0_white.png")
    # hair must stay dark after white recolor
    wr, wg, wb, wa = [int(v) for v in white[29, 45]]
    luma = 0.299 * wr + 0.587 * wg + 0.114 * wb
    if luma > 80:
        fail(f"after white recolor, hair (45,29) is bright luma={luma:.0f} ({wr},{wg},{wb})")
    else:
        ok(f"after white recolor, hair stays dark luma={luma:.0f}")
    if painted < 80:
        fail(f"too few filament pixels painted ({painted}); winding may have been stripped")
    else:
        ok(f"filament painted={painted} brown-skipped={skipped}")
    # winding should have some near-white pixels
    # sample right side of sprite (spool)
    spool = white[60:110, 50:89]
    near_white = int(((spool[:, :, 0] > 200) & (spool[:, :, 3] > 200)).sum())
    dark_flange = int(((spool[:, :, 0] < 80) & (spool[:, :, 3] > 200)).sum())
    if near_white < 30:
        fail(f"white winding almost absent ({near_white} near-white px)")
    else:
        ok(f"white winding visible ({near_white} px), dark flanges {dark_flange} px")
    if dark_flange < 10:
        fail("spool flanges disappeared (should stay dark)")
    else:
        ok("spool flanges stay dark")


def test_all_operario():
    print("== all operario sprites: no hair a=254, dims unchanged ==")
    # expected dims from assets (walk 89x168 etc.) — compare to backup PNGs
    backup = Path("/tmp/u1view/png_backup_v03")
    for png in sorted((ROOT / "assets_png").glob("img_operario_*.png")):
        im = Image.open(png)
        w, h = im.size
        arr = decode_tca(png.stem, w, h)
        # head-ish region: upper 35% , left 70% (or right for walk_l)
        y1 = max(8, int(h * 0.38))
        x0, x1 = 0, int(w * 0.75)
        if "_l_" in png.name or png.name.endswith("_l.png"):
            x0, x1 = int(w * 0.25), w
        if backup.exists():
            bim = Image.open(backup / png.name)
            if bim.size != im.size:
                fail(f"{png.name}: dimension changed {bim.size} -> {im.size}")
        hair_mark = 0
        ys, xs = np.where(arr[: y1, :, 3] == 254)
        for yy, xx in zip(ys, xs):
            r, g, b = [int(v) for v in arr[yy, xx, :3]]
            if r > 40 and (b * 5) < r and g <= r:
                luma = (r * 3 + g * 6 + b) // 10
                if luma >= 40:
                    hair_mark += 1
        if hair_mark:
            fail(f"{png.name}: {hair_mark} mid-tone brown a=254 (possible hair)")
    ok("operario dimensions preserved; no mid-tone brown a=254 leftover")


def test_toolhead():
    print("== split toolhead / carriage (no tall transparent strip) ==")
    th = decode_tca("img_u1_toolhead", 41, 37)
    car = decode_tca("img_u1_carriage", 43, 40)
    Image.fromarray(th).save(QA / "dec_toolhead.png")
    Image.fromarray(car).save(QA / "dec_carriage.png")
    if th.shape[0] > 50:
        fail(f"toolhead still tall: h={th.shape[0]}")
    else:
        ok(f"toolhead tight {th.shape[1]}x{th.shape[0]}")
    if car.shape[0] > 50:
        fail(f"carriage still tall: h={car.shape[0]}")
    else:
        ok(f"carriage tight {car.shape[1]}x{car.shape[0]}")
    # gap check: neither should be majority-transparent
    for name, arr in (("toolhead", th), ("carriage", car)):
        trans = int((arr[:, :, 3] < 2).sum())
        frac = trans / arr.shape[0] / arr.shape[1]
        if frac > 0.55:
            fail(f"{name} is {frac:.0%} transparent (dirty-rect waste)")
        else:
            ok(f"{name} transparent fraction {frac:.0%}")
        # some opaque pixels exist
        if int((arr[:, :, 3] > 200).sum()) < 80:
            fail(f"{name} has almost no opaque pixels")


def test_spool():
    print("== bay spool: black stays black, flanges never 254 ==")
    arr = decode_tca("img_u1_spool", 24, 26)
    Image.fromarray(arr).save(QA / "dec_spool.png")
    a254 = arr[:, :, 3] == 254
    n254 = int(a254.sum())
    if n254 < 40:
        fail(f"spool has too few recolor pixels ({n254})")
    else:
        ok(f"spool winding a254={n254}")
    # flanges: dark opaque, a!=254
    dark_opaque = (arr[:, :, 3] == 255) & (arr[:, :, 0] < 90) & (arr[:, :, 1] < 90)
    if int(dark_opaque.sum()) < 20:
        fail("spool missing dark flanges (a=255)")
    else:
        ok(f"dark flanges (a=255) = {int(dark_opaque.sum())} px")
    # recolor to black
    black, painted, skipped = recolor(arr, 0x000000)
    Image.fromarray(black).save(QA / "dec_spool_black.png")
    # winding now near-black, flanges still slightly lighter grey
    wind = black[a254]
    if wind.size and wind[:, 0].mean() > 40:
        fail(f"black filament winding not black (mean R={wind[:,0].mean():.0f})")
    else:
        ok("black filament winding is black")
    # flanges still visible (not all-black image)
    flange = black[dark_opaque]
    if flange.size and flange[:, 0].mean() < 8:
        fail("flanges also became pure black — spool would vanish on a dark bay")
    else:
        ok(f"flanges remain visible (mean R={flange[:,0].mean():.0f})")
    # white recolor: winding white, flanges dark
    white, _, _ = recolor(arr, 0xFFFFFF)
    Image.fromarray(white).save(QA / "dec_spool_white.png")
    wind_w = white[a254]
    if wind_w.size and wind_w[:, 0].mean() < 200:
        fail(f"white winding not white (mean R={wind_w[:,0].mean():.0f})")
    else:
        ok("white filament winding is white")
    fl_w = white[dark_opaque]
    if fl_w.size and fl_w[:, 0].mean() > 120:
        fail("flanges became light after white recolor")
    else:
        ok("flanges stay dark after white recolor")


def main():
    test_walk()
    test_all_operario()
    test_toolhead()
    test_spool()
    if failed:
        print(f"\n{failed} pixel test(s) FAILED")
        sys.exit(1)
    print("\nTodos os testes de pixels passaram.")


if __name__ == "__main__":
    main()
