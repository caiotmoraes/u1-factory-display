/* Auto-gerado — coordenadas da cena U1 no painel direito 400x480.
 * Origem (0,0) = canto do clip (tela x=400, y=0).
 * O fundo 800x480 é desenhado com deslocamento -SCENE_CROP_X.
 *
 * v0.4: o carro do teto (img_u1_carriage) e o bico da câmara
 * (img_u1_toolhead) são duas sprites curtas, movidas com o mesmo dx.
 * Isso evita invalidar uma faixa alta e transparente no painel RGB.
 */
#pragma once
#define SCENE_CROP_X 240
#define SCENE_OX(orig_x) ((orig_x) - SCENE_CROP_X)

#define U1_CHAMBER_X0 86
#define U1_CHAMBER_Y0 152
#define U1_CHAMBER_X1 292
#define U1_CHAMBER_Y1 256

/* bico (câmara), scene-local — rest X alinhado à esquerda do conjunto antigo */
#define U1_TOOLHEAD_X 171
#define U1_TOOLHEAD_Y 151
#define U1_TOOLHEAD_W 41
#define U1_TOOLHEAD_H 37
#define U1_TOOLHEAD_X_MIN 120
#define U1_TOOLHEAD_X_MAX 230
#define U1_TOOLHEAD_TRAVEL (U1_TOOLHEAD_X_MAX - U1_TOOLHEAD_X_MIN)

/* gantry do teto — mesmo dx = (toolhead_x - U1_TOOLHEAD_X) */
#define U1_CARRIAGE_X 183
#define U1_CARRIAGE_Y 47
#define U1_CARRIAGE_W 43
#define U1_CARRIAGE_H 40

#define U1_PART_X 145
#define U1_PART_Y 186
#define U1_PART_W 80
#define U1_PART_H 56

/* 4 compartimentos da gaveta inferior — já estão no fundo, NÃO redesenhar.
 * Coords scene-local (SCENE_CROP_X=240), interiores medidos no art:
 * orig x 248-327, 332-401, 408-479, 483-564; luzes em y≈319-326;
 * berço em y≈360-390. */
#define U1_SLOT_COUNT  4
#define U1_SLOT0_X     8
#define U1_SLOT0_W     80
#define U1_SLOT1_X     92
#define U1_SLOT1_W     70
#define U1_SLOT2_X     168
#define U1_SLOT2_W     72
#define U1_SLOT3_X     243
#define U1_SLOT3_W     82
#define U1_SLOT_Y      328   /* abaixo das luzes, topo do interior */
#define U1_SLOT_H      68    /* até ~396, acima da moldura */
#define U1_SLOT_HL_Y   318
#define U1_SLOT_HL_H   80
#define U1_SLOT_NUM_Y  330
#define U1_SLOT_PLATE_Y 348
#define U1_SLOT_SPOOL_Y 358  /* assenta no berço em U */

#define U1_BED_Y 222

/* janela do teto (clip do carro superior, se precisar) */
#define U1_ROOF_X0 88
#define U1_ROOF_Y0 52
#define U1_ROOF_X1 308
#define U1_ROOF_Y1 86
