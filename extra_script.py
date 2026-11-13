# Gera asset_bins.h com caminhos absolutos desta máquina (Linux ou Windows).
# O GNU as não honra -I para .incbin; o pré-processador C resolve o macro.
from pathlib import Path
Import("env")

bindir = Path(env["PROJECT_DIR"]) / "src" / "assets" / "bin"
hdr = Path(env["PROJECT_DIR"]) / "src" / "assets" / "asset_bins.h"
lines = [
    "/* GERADO por extra_script.py a cada build. Não edite. */",
    "#pragma once",
    "",
]
for p in sorted(bindir.glob("*.bin")):
    lines.append(f'#define BIN_{p.stem.upper()} "{p.as_posix()}"')
    lines.append("")
text = "\n".join(lines)
if not hdr.exists() or hdr.read_text() != text:
    hdr.write_text(text)
    print("extra_script: escreveu", hdr)
