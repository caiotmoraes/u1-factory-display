# Gera asset_bins.h com caminhos absolutos desta máquina (Linux ou Windows).
# O GNU as não honra -I para .incbin; o pré-processador C resolve o macro.
from hashlib import sha256
from pathlib import Path
from PIL import Image

Import("env")

project = Path(env["PROJECT_DIR"])
bindir = project / "src" / "assets" / "bin"
bindir.mkdir(parents=True, exist_ok=True)

assets = {
    "img_bg_factory": (800, 480),
    "img_u1_toolhead": (41, 37),
    "img_u1_carriage": (43, 40),
    "img_u1_spool": (24, 26),
    "img_u1_part": (80, 56),
}


def encode_image(image, indexed):
    if indexed:
        image = image.convert("RGB").quantize(
            colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE
        )
        palette = bytes(image.getpalette()).ljust(256 * 3, b"\0")
        data = bytearray()
        for r, g, b in zip(palette[::3], palette[1::3], palette[2::3]):
            data.extend((b, g, r, 255))
        data.extend(image.tobytes())
        return data

    data = bytearray()
    pixels = image.convert("RGBA").tobytes()
    for r, g, b, a in zip(pixels[::4], pixels[1::4], pixels[2::4], pixels[3::4]):
        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        data.extend((rgb565 & 0xFF, rgb565 >> 8, a))
    return data


hdr = bindir.parent / "asset_bins.h"
lines = [
    "/* GERADO por extra_script.py a cada build. Não edite. */",
    "#pragma once",
    "",
]
for name, size in assets.items():
    path = bindir / f"{name}.bin"
    with Image.open(project / "assets_png" / f"{name}.png") as image:
        if image.size != size:
            raise ValueError(f"{name}: expected {size}, got {image.size}")
        data = encode_image(image, indexed=name == "img_bg_factory")
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)
    lines.append(f'#define BIN_{name.upper()} "{path.as_posix()}" /* {sha256(data).hexdigest()} */')
    lines.append("")
text = "\n".join(lines)
if not hdr.exists() or hdr.read_text() != text:
    hdr.write_text(text)
    print("extra_script: escreveu", hdr)
