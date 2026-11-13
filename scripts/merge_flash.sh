#!/bin/sh
# Junta bootloader + partições + app numa imagem para gravar em 0x0.
#
# IMPORTANTE (ESP32-S3): o header da imagem que o ROM lê TEM que ser DIO.
# O framework já entrega o bootloader em DIO; --flash-mode qio no merge
# reescreve esse byte, o ROM tenta QIO (mode:QIO, clock div:1) e cai em
# ets_loader.c:78 + TG0WDT_SYS_RST. O QIO de verdade fica no memory_type
# qio_opi e é ligado pelo 2nd stage, depois do ROM.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/.pio/build/waveshare_s3_lcd_7"
OUT="$ROOT/build"
if [ -z "${ESPTOOL:-}" ]; then
  if [ -x "$ROOT/../.venv/bin/python3" ]; then
    ESPTOOL="$ROOT/../.venv/bin/python3 -m esptool"
  else
    ESPTOOL="python3 -m esptool"
  fi
fi

mkdir -p "$OUT"
# Prefer esptool 5.x syntax (hyphens); fall back to 4.x underscores.
if $ESPTOOL --help 2>&1 | grep -q 'image-info'; then
  MERGE="merge-bin"
  FMODE="--flash-mode"
  FFREQ="--flash-freq"
  FSIZE="--flash-size"
else
  MERGE="merge_bin"
  FMODE="--flash_mode"
  FFREQ="--flash_freq"
  FSIZE="--flash_size"
fi

$ESPTOOL --chip esp32s3 $MERGE \
  -o "$OUT/u1_factory_merged_fix.bin" \
  $FMODE dio $FFREQ 80m $FSIZE 16MB \
  0x0     "$BUILD/bootloader.bin" \
  0x8000  "$BUILD/partitions.bin" \
  0x10000 "$BUILD/firmware.bin"

cp -a "$BUILD/bootloader.bin"  "$OUT/bootloader_0x0.bin"
cp -a "$BUILD/partitions.bin"  "$OUT/partitions_0x8000.bin"
cp -a "$BUILD/firmware.bin"    "$OUT/u1_factory_0x10000.bin"
# keep friendly names too
cp -a "$BUILD/bootloader.bin"  "$OUT/bootloader.bin"
cp -a "$BUILD/partitions.bin"  "$OUT/partitions.bin"
cp -a "$BUILD/firmware.bin"    "$OUT/u1_factory.bin"


cp -a "$OUT/u1_factory_merged_fix.bin" "$OUT/u1_factory_merged_v2.bin"

python3 -c "
import sys
from pathlib import Path
p = Path(sys.argv[1])
b = p.read_bytes()[:16]
mode, sz = b[2], (b[3] >> 4) & 0xF
print('header:', b.hex(), 'mode=%d (2=DIO) size_nibble=%d (4=16MB)' % (mode, sz))
if mode != 2:
    raise SystemExit('ERRO: flash_mode %d, esperado DIO (2)' % mode)
if sz != 4:
    raise SystemExit('ERRO: flash_size nibble %d, esperado 4 (16MB)' % sz)
print('header OK: DIO, 16MB')
" "$OUT/u1_factory_merged_v2.bin"

echo "OK: $OUT/u1_factory_merged_v2.bin  (copia em u1_factory_merged_fix.bin)"
