#!/bin/sh
# Gera as fontes LVGL da UI clara (Montserrat Latin-1 com acentos pt-BR + simbolos LVGL).
# Requer: cd scripts && npm install   (lv_font_conv)
# Sem compressão: LV_USE_FONT_COMPRESSED=0 no lv_conf e render mais rápido.
set -e
cd "$(dirname "$0")/.."
CONV=scripts/node_modules/.bin/lv_font_conv
F=tools/fonts
OUT=src/assets/fonts_ui
mkdir -p $OUT
SYMS=61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650
gen() { # nome tamanho ttf faixa [simbolos]
  if [ -n "$5" ]; then
    $CONV --bpp 4 --size $2 --font $F/$3 -r $4 --font $F/FontAwesome5.woff -r $5 --no-compress --format lvgl --lv-include lvgl.h --force-fast-kern-format -o $OUT/$1.c
  else
    $CONV --bpp 4 --size $2 --font $F/$3 -r $4 --no-compress --format lvgl --lv-include lvgl.h --force-fast-kern-format -o $OUT/$1.c
  fi
}
gen font_ui_14 14 Montserrat-Medium.ttf 0x20-0x7E,0xA0-0xFF,0x2022,0x2013,0x2014,0x2026,0x2264 $SYMS
gen font_ui_16 16 Montserrat-SemiBold.ttf 0x20-0x7E,0xA0-0xFF,0x2022,0x2013,0x2014,0x2026,0x2264 $SYMS
gen font_ui_20 20 Montserrat-SemiBold.ttf 0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2026 $SYMS
gen font_ui_28 28 Montserrat-SemiBold.ttf 0x20-0x7E,0xA0-0xFF,0x2014
gen font_ui_48 48 Montserrat-SemiBold.ttf 0x20-0x7E,0xB0,0x2014
# icones (FontAwesome 5 free): casa, controles, cubo, antena, engrenagem, termometro,
# fogo, vento, sincronia, relogio, camadas, caixa, wifi, livro, info, impressora, etiqueta
ICONS=0xf015,0xf1de,0xf1b2,0xf519,0xf013,0xf2c9,0xf06d,0xf72e,0xf021,0xf017,0xf5fd,0xf466,0xf1eb,0xf02d,0xf05a,0xf02f,0xf02b,0xf769,0xf053
$CONV --bpp 4 --size 26 --font $F/FontAwesome5.woff -r $ICONS --no-compress --format lvgl --lv-include lvgl.h -o $OUT/font_icon_26.c
$CONV --bpp 4 --size 18 --font $F/FontAwesome5.woff -r $ICONS --no-compress --format lvgl --lv-include lvgl.h -o $OUT/font_icon_18.c
