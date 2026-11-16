#!/bin/bash
# Compila o simulador de host e gera os previews PNG 800x480 da UI.
# Uso: bash tools/sim/build.sh [pasta_saida]
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/docs/preview}"
LV="$ROOT/.pio/libdeps/waveshare_s3_lcd_7/lvgl"
AJ="$ROOT/.pio/libdeps/waveshare_s3_lcd_7/ArduinoJson/src"
B=/tmp/u1sim
mkdir -p $B/obj "$OUT"
CC="${CC:-gcc}"; CXX="${CXX:-g++-14}"
INC="-I$ROOT/tools/sim/stubs -I$ROOT/src -I$ROOT/src/assets -I$LV -I$AJ -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DU1_SIM=1"
if [ ! -f $B/liblvgl.a ] || [ "$ROOT/src/lv_conf.h" -nt $B/liblvgl.a ]; then
  rm -f $B/obj/*.o
  for f in $(find "$LV/src" -name '*.c'); do
    o=$B/obj/$(echo "$f" | md5sum | cut -c1-12).o
    $CC -O1 -w $INC -c "$f" -o "$o" &
    while [ "$(jobs -rp | wc -l)" -ge 8 ]; do sleep 0.1; done
  done
  wait
  ar rcs $B/liblvgl.a $B/obj/*.o
fi
for f in "$ROOT"/src/assets/fonts_ui/*.c "$ROOT/src/lv_mem_psram.c"; do $CC -O1 -w $INC -c "$f" -o $B/$(basename "$f" .c).o; done
$CXX -std=gnu++17 -O1 -w $INC -include Arduino.h \
  "$ROOT/tools/sim/sim_main.cpp" "$ROOT/tools/sim/fake.cpp" \
  "$ROOT/src/ui.cpp" "$ROOT/src/ui_stock.cpp" "$ROOT/src/control.cpp" \
  "$ROOT/src/spool_parse.cpp" "$ROOT/src/inventory_logic.cpp" "$ROOT/src/history.cpp" \
  "$ROOT/src/printer_state.cpp" "$ROOT/src/snapshot.cpp" \
  $B/font_*.o $B/lv_mem_psram.o $B/liblvgl.a -lm -o $B/u1sim
python3 "$ROOT/tools/sim/thumb_demo.py" $B/thumb.rgb565
THUMB=$B/thumb.rgb565 $B/u1sim $B
python3 - "$B" "$OUT" <<'PY'
import sys, glob, os
from PIL import Image
b, out = sys.argv[1], sys.argv[2]
for p in sorted(glob.glob(b + '/v060_*.ppm')):
    d = os.path.join(out, os.path.basename(p)[:-4] + '.png')
    Image.open(p).save(d)
    print(d)
PY
