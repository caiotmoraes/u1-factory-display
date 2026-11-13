#pragma once
/* Coordenadas da cena 800x480. O preview Python deve espelhar estes valores.
 * Fábrica v0.3: painel de dados à esquerda (400px) e cena animada à direita (400px). */
#define SCREEN_W          800
#define SCREEN_H          480
#define FLOOR_Y           452          /* pés dos personagens (coords da cena) */

#define PANEL_W           400
#define SCENE_X           400
#define SCENE_W           400
#define SCENE_H           480

#define HUD_X             0
#define HUD_Y             0
#define HUD_W             400
#define HUD_H             480

/* Operadora (estática em X, só troca de quadro). 304 deixa a sprite
 * de 94 px inteira na cena de 400 (crop 206; a posição-mundo antiga
 * 538 cairia em 332 e sairia 26 px pela direita). */
#define OPERADORA_X       304

/* Quatro abas no canto direito: WIFI + FABRICA + ARMAZEM + DIARIO
 * (ecrãs cheios: armazém, diário, wifi, detalhes). */
#define TAB_Y             8
#define TAB_H             28
#define TAB_WIFI_X        392
#define TAB_WIFI_W        68
#define TAB_FAB_X         464
#define TAB_FAB_W         108
#define TAB_ARM_X         576
#define TAB_ARM_W         112
#define TAB_DIA_X         692
#define TAB_DIA_W         100

/* Abas no topo do painel esquerdo da fábrica */
#define FTAB_Y            10
#define FTAB_H            28
#define FTAB_WIFI_X       10
#define FTAB_WIFI_W       88
#define FTAB_FAB_X        102
#define FTAB_FAB_W        94
#define FTAB_ARM_X        200
#define FTAB_ARM_W        96
#define FTAB_DIA_X        300
#define FTAB_DIA_W        90

#define COLOR_NAVY        0x080F23
#define COLOR_CARD        0x111D37
#define COLOR_PANEL       0x10192F
#define COLOR_MINT        0x59ECC5
#define COLOR_CREAM       0xF9F2DE
#define COLOR_GOLD        0xE5B142
#define COLOR_GOLD2       0xFFB52B
#define COLOR_BLUE        0x2562B8
#define COLOR_MUTED       0xAAB6CE
#define COLOR_ALERT       0xEF9C73
#define COLOR_NIGHT       0x050818
