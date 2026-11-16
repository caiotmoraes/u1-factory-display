#!/usr/bin/env python3
"""Gera src/strings_pt.h a partir de spec/strings.json (U1 Spool Spec v1).

Cada chave vira S_<CHAVE> (pontos -> _ , maiúsculas). Placeholders {nome} viram
especificadores printf na ordem em que aparecem: id, ch, n, s, p, a, b, t -> %d;
os demais -> %s. A tela é só pt-BR (decisão 5), então só o texto "pt" é usado.
Uso: python3 tools/gen_strings.py
"""
import json, os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INTS = {"id", "ch", "n", "s", "p", "a", "b", "t"}
src = json.load(open(os.path.join(ROOT, "spec", "strings.json"), encoding="utf-8"))
out = ["/* GERADO por tools/gen_strings.py a partir de spec/strings.json — não editar. */",
       "#pragma once", ""]
for key, val in src.items():
    text = val["pt"]
    if "{" in text:
        text = text.replace("%", "%%")
        text = re.sub(r"\{([a-z]+)\}", lambda m: "%d" if m.group(1) in INTS else "%s", text)
    name = "S_" + re.sub(r"[^A-Z0-9]", "_", key.upper())
    lit = text.replace("\\", "\\\\").replace('"', '\\"')
    out.append(f'#define {name} "{lit}"')
path = os.path.join(ROOT, "src", "strings_pt.h")
open(path, "w", encoding="utf-8").write("\n".join(out) + "\n")
print(f"{path}: {len(src)} textos")
