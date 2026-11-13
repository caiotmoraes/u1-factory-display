/* Auto-gerado — coordenadas da cena U1 no painel direito 400x480.
 * Origem (0,0) = canto do clip (tela x=400, y=0).
 * O fundo 800x480 é desenhado com deslocamento -SCENE_CROP_X.
 *
 * v0.4.2: SCENE_CROP_X=206 centra a impressora (corpo orig ≈190–623,
 * centro 406.5) na cena de 400 px. A máquina tem 434 px de largura, então
 * ~16–17 px das faces laterais do pedestal ficam fora; o corpo superior
 * (251–610) e os 4 compartimentos (248–564) cabem inteiros.
 * Deslocamento relativamente a v0.4.1 (crop 240): +34 px em todo X local.
 *
 * O carro do teto (img_u1_carriage) e o bico da câmara (img_u1_toolhead)
 * são duas sprites curtas, movidas com o mesmo dx. Os eixos visuais
 * (círculo amarelo e bico) coincidem: U1_CARRIAGE_X = U1_TOOLHEAD_X
 * (em v0.4.1 o carro ficava 12 px à direita do bico).
 */
#pragma once
#define SCENE_CROP_X 206
#define SCENE_OX(orig_x) ((orig_x) - SCENE_CROP_X)

#define U1_CHAMBER_X0 120
#define U1_CHAMBER_Y0 152
#define U1_CHAMBER_X1 326
#define U1_CHAMBER_Y1 256

/* bico (câmara), scene-local */
#define U1_TOOLHEAD_X 205
#define U1_TOOLHEAD_Y 151
#define U1_TOOLHEAD_W 41
#define U1_TOOLHEAD_H 37
#define U1_TOOLHEAD_X_MIN 154
#define U1_TOOLHEAD_X_MAX 264
#define U1_TOOLHEAD_TRAVEL (U1_TOOLHEAD_X_MAX - U1_TOOLHEAD_X_MIN)

/* gantry do teto — mesmo dx = (toolhead_x - U1_TOOLHEAD_X);
 * X de repouso igual ao do bico para alinhar os eixos visuais. */
#define U1_CARRIAGE_X 205
#define U1_CARRIAGE_Y 47
#define U1_CARRIAGE_W 43
#define U1_CARRIAGE_H 40

#define U1_PART_X 179
#define U1_PART_Y 186
#define U1_PART_W 80
#define U1_PART_H 56

/* 4 compartimentos da gaveta inferior — já estão no fundo, NÃO redesenhar.
 * Coords scene-local (SCENE_CROP_X=206), interiores medidos no art:
 * orig x 248-327, 332-401, 408-479, 483-564; luzes em y≈319-326;
 * berço em y≈360-390. */
#define U1_SLOT_COUNT  4
#define U1_SLOT0_X     42
#define U1_SLOT0_W     80
#define U1_SLOT1_X     126
#define U1_SLOT1_W     70
#define U1_SLOT2_X     202
#define U1_SLOT2_W     72
#define U1_SLOT3_X     277
#define U1_SLOT3_W     82
#define U1_SLOT_Y      328   /* abaixo das luzes, topo do interior */
#define U1_SLOT_H      68    /* até ~396, acima da moldura */
#define U1_SLOT_HL_Y   318
#define U1_SLOT_HL_H   80
#define U1_SLOT_NUM_Y  330
#define U1_SLOT_PLATE_Y 348
#define U1_SLOT_SPOOL_Y 358  /* assenta no berço em U */

#define U1_BED_Y 222
