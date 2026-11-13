#pragma once
#include <lvgl.h>
#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t img_bg_factory;
#define IMG_BG_FACTORY_W 800
#define IMG_BG_FACTORY_H 480
extern const lv_img_dsc_t img_u1_toolhead;
#define IMG_U1_TOOLHEAD_W 41
#define IMG_U1_TOOLHEAD_H 37
extern const lv_img_dsc_t img_u1_carriage;
#define IMG_U1_CARRIAGE_W 43
#define IMG_U1_CARRIAGE_H 40
extern const lv_img_dsc_t img_u1_spool;
#define IMG_U1_SPOOL_W 24
#define IMG_U1_SPOOL_H 26
extern const lv_img_dsc_t img_u1_part;
#define IMG_U1_PART_W 80
#define IMG_U1_PART_H 56
extern const lv_img_dsc_t img_operadora_0;
#define IMG_OPERADORA_0_W 94
#define IMG_OPERADORA_0_H 176
extern const lv_img_dsc_t img_operadora_1;
#define IMG_OPERADORA_1_W 94
#define IMG_OPERADORA_1_H 176
extern const lv_img_dsc_t img_operadora_2;
#define IMG_OPERADORA_2_W 94
#define IMG_OPERADORA_2_H 176
extern const lv_img_dsc_t img_operadora_3;
#define IMG_OPERADORA_3_W 94
#define IMG_OPERADORA_3_H 176
extern const lv_img_dsc_t img_operadora_dorme_0;
#define IMG_OPERADORA_DORME_0_W 91
#define IMG_OPERADORA_DORME_0_H 196
extern const lv_img_dsc_t img_operadora_dorme_1;
#define IMG_OPERADORA_DORME_1_W 91
#define IMG_OPERADORA_DORME_1_H 196
extern const lv_img_dsc_t img_operadora_celebra_0;
#define IMG_OPERADORA_CELEBRA_0_W 91
#define IMG_OPERADORA_CELEBRA_0_H 196
extern const lv_img_dsc_t img_operadora_celebra_1;
#define IMG_OPERADORA_CELEBRA_1_W 91
#define IMG_OPERADORA_CELEBRA_1_H 196
extern const lv_img_dsc_t img_operadora_susto_0;
#define IMG_OPERADORA_SUSTO_0_W 91
#define IMG_OPERADORA_SUSTO_0_H 196
extern const lv_img_dsc_t img_operadora_susto_1;
#define IMG_OPERADORA_SUSTO_1_W 91
#define IMG_OPERADORA_SUSTO_1_H 196

extern const lv_img_dsc_t *const img_operadora_idle[2];
extern const lv_img_dsc_t *const img_operadora_apresenta[2];
extern const lv_img_dsc_t *const img_operadora_dorme[2];
extern const lv_img_dsc_t *const img_operadora_celebra[2];
extern const lv_img_dsc_t *const img_operadora_susto[2];

#ifdef __cplusplus
}
#endif

