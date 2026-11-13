#!/bin/sh
# Testes de host (não falam com a U1 real).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
echo "== máquina de estados =="
# printer_state.cpp é C portátil; gcc -x c evita depender de g++.
gcc -x c -std=c11 -Wall -Wextra -I "$ROOT/src" \
    "$ROOT/tests/test_printer_state.c" "$ROOT/src/printer_state.cpp" \
    -o /tmp/test_printer_state
/tmp/test_printer_state
echo
echo "== parser Spoolman / filtros / historico (host C++) =="
GXX="${GXX:-g++-14}"
AJ="$ROOT/.pio/libdeps/waveshare_s3_lcd_7/ArduinoJson/src"
if [ ! -d "$AJ" ]; then
  AJ="$(ls -d "$ROOT"/.pio/libdeps/*/ArduinoJson/src 2>/dev/null | head -1 || true)"
fi
if ! command -v "$GXX" >/dev/null 2>&1; then
  echo "AVISO: $GXX nao encontrado; pulando testes C++ do parser."
else
  "$GXX" -std=c++17 -Wall -Wextra -Werror -I "$ROOT/src" -I "$AJ" \
      "$ROOT/tests/test_spool_parse.cpp" \
      "$ROOT/src/spool_parse.cpp" \
      "$ROOT/src/inventory_logic.cpp" \
      "$ROOT/src/history.cpp" \
      -o /tmp/test_spool_parse
  /tmp/test_spool_parse
fi
echo
echo "== protocolo Moonraker / Spoolman (FakeMoonraker) =="
python3 "$ROOT/scripts/test_protocol.py"
echo
echo "== pipeline de pixels (TRUE_COLOR_ALPHA / recolor) =="
python3 "$ROOT/tests/test_pixels.py"
echo
echo "Todos os testes de host passaram."
