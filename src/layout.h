#pragma once
/* Tema claro v0.6 ("widgets claros"): tela 800x480, barra lateral à esquerda
 * e área de conteúdo à direita. Os nomes antigos das cores continuam valendo
 * como papéis semânticos (NAVY = fundo, CARD = cartão, MINT = destaque...). */
#define SCREEN_W          800
#define SCREEN_H          480

/* barra lateral (cartão branco com 5 ícones) */
#define SIDEBAR_X         12
#define SIDEBAR_Y         12
#define SIDEBAR_W         64
#define SIDEBAR_H         456
/* área de conteúdo de todas as telas */
#define CONTENT_X         88
#define CONTENT_Y         12
#define CONTENT_W         700
#define CONTENT_H         456

#define CARD_RADIUS       16
#define BTN_RADIUS        12

/* paleta */
#define COLOR_BG          0xF2F0EC   /* fundo off-white */
#define COLOR_SURFACE     0xFFFFFF   /* cartões */
#define COLOR_SURFACE2    0xF7F6F3   /* campos, sub-cartões */
#define COLOR_LINE        0xE4E1DB   /* bordas suaves */
#define COLOR_TEXT        0x2E3440
#define COLOR_TEXT2       0x5E6675
#define COLOR_ACCENT      0x5B8DEF   /* azul suave */
#define COLOR_ACCENT_BG   0xE7EEFC
#define COLOR_GREEN       0x6E9A72
#define COLOR_AMBER       0xE3A447
#define COLOR_RED         0xE0695A
#define COLOR_RED_BG      0xFCEAE7
#define COLOR_TEMP        0x4E6C93   /* números de temperatura */

/* papéis antigos -> tema claro */
#define COLOR_NAVY        COLOR_BG
#define COLOR_CARD        COLOR_SURFACE
#define COLOR_PANEL       COLOR_SURFACE2
#define COLOR_MINT        COLOR_ACCENT
#define COLOR_CREAM       COLOR_TEXT
#define COLOR_GOLD        COLOR_AMBER
#define COLOR_GOLD2       COLOR_AMBER
#define COLOR_BLUE        COLOR_ACCENT
#define COLOR_MUTED       0x8A919E
#define COLOR_ALERT       COLOR_RED
#define COLOR_NIGHT       COLOR_TEXT
/* texto "escuro sobre botão colorido" das telas antigas: ui_make_btn troca
 * por branco quando o fundo é escuro o bastante. */
#define COLOR_ON_ACCENT   0x081725

/* ícones FontAwesome 5 (font_icon_18 / font_icon_26) */
#define ICON_HOME     "\xEF\x80\x95"   /* f015 */
#define ICON_SLIDERS  "\xEF\x87\x9E"   /* f1de */
#define ICON_CUBE     "\xEF\x86\xB2"   /* f1b2 */
#define ICON_NFC      "\xEF\x94\x99"   /* f519 */
#define ICON_COG      "\xEF\x80\x93"   /* f013 */
#define ICON_THERMO   "\xEF\x8B\x89"   /* f2c9 */
#define ICON_FIRE     "\xEF\x81\xAD"   /* f06d */
#define ICON_WIND     "\xEF\x9C\xAE"   /* f72e */
#define ICON_SYNC     "\xEF\x80\xA1"   /* f021 */
#define ICON_CLOCK    "\xEF\x80\x97"   /* f017 */
#define ICON_LAYERS   "\xEF\x97\xBD"   /* f5fd */
#define ICON_BOX      "\xEF\x91\xA6"   /* f466 */
#define ICON_WIFI     "\xEF\x87\xAB"   /* f1eb */
#define ICON_BOOK     "\xEF\x80\xAD"   /* f02d */
#define ICON_INFO     "\xEF\x81\x9A"   /* f05a */
#define ICON_PRINT    "\xEF\x80\xAF"   /* f02f */
#define ICON_TAG      "\xEF\x80\xAB"   /* f02b */
#define ICON_TEMP_HI  "\xEF\x9D\xA9"   /* f769 */
#define ICON_BACK     "\xEF\x81\x93"   /* f053 */
