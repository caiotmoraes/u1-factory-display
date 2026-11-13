#include "assets.h"

extern const uint8_t img_bg_factory_map[];
const lv_img_dsc_t img_bg_factory = {
    .header = { .cf = LV_IMG_CF_INDEXED_8BIT, .always_zero = 0, .reserved = 0, .w = 800, .h = 480 },
    .data_size = 385024,
    .data = img_bg_factory_map,
};

extern const uint8_t img_u1_toolhead_map[];
const lv_img_dsc_t img_u1_toolhead = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 41, .h = 37 },
    .data_size = 4551,
    .data = img_u1_toolhead_map,
};

extern const uint8_t img_u1_carriage_map[];
const lv_img_dsc_t img_u1_carriage = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 43, .h = 40 },
    .data_size = 5160,
    .data = img_u1_carriage_map,
};

extern const uint8_t img_u1_spool_map[];
const lv_img_dsc_t img_u1_spool = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 24, .h = 26 },
    .data_size = 1872,
    .data = img_u1_spool_map,
};

extern const uint8_t img_u1_part_map[];
const lv_img_dsc_t img_u1_part = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 80, .h = 56 },
    .data_size = 13440,
    .data = img_u1_part_map,
};

extern const uint8_t img_operadora_0_map[];
const lv_img_dsc_t img_operadora_0 = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 94, .h = 176 },
    .data_size = 49632,
    .data = img_operadora_0_map,
};

extern const uint8_t img_operadora_1_map[];
const lv_img_dsc_t img_operadora_1 = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 94, .h = 176 },
    .data_size = 49632,
    .data = img_operadora_1_map,
};

extern const uint8_t img_operadora_2_map[];
const lv_img_dsc_t img_operadora_2 = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 94, .h = 176 },
    .data_size = 49632,
    .data = img_operadora_2_map,
};

extern const uint8_t img_operadora_3_map[];
const lv_img_dsc_t img_operadora_3 = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 94, .h = 176 },
    .data_size = 49632,
    .data = img_operadora_3_map,
};

extern const uint8_t img_operadora_dorme_0_map[];
const lv_img_dsc_t img_operadora_dorme_0 = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 91, .h = 196 },
    .data_size = 53508,
    .data = img_operadora_dorme_0_map,
};

extern const uint8_t img_operadora_dorme_1_map[];
const lv_img_dsc_t img_operadora_dorme_1 = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 91, .h = 196 },
    .data_size = 53508,
    .data = img_operadora_dorme_1_map,
};

extern const uint8_t img_operadora_celebra_0_map[];
const lv_img_dsc_t img_operadora_celebra_0 = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 91, .h = 196 },
    .data_size = 53508,
    .data = img_operadora_celebra_0_map,
};

extern const uint8_t img_operadora_celebra_1_map[];
const lv_img_dsc_t img_operadora_celebra_1 = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 91, .h = 196 },
    .data_size = 53508,
    .data = img_operadora_celebra_1_map,
};

extern const uint8_t img_operadora_susto_0_map[];
const lv_img_dsc_t img_operadora_susto_0 = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 91, .h = 196 },
    .data_size = 53508,
    .data = img_operadora_susto_0_map,
};

extern const uint8_t img_operadora_susto_1_map[];
const lv_img_dsc_t img_operadora_susto_1 = {
    .header = { .cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 91, .h = 196 },
    .data_size = 53508,
    .data = img_operadora_susto_1_map,
};

const lv_img_dsc_t *const img_operadora_idle[2] = { &img_operadora_0, &img_operadora_1 };
const lv_img_dsc_t *const img_operadora_apresenta[2] = { &img_operadora_2, &img_operadora_3 };
const lv_img_dsc_t *const img_operadora_dorme[2] = { &img_operadora_dorme_0, &img_operadora_dorme_1 };
const lv_img_dsc_t *const img_operadora_celebra[2] = { &img_operadora_celebra_0, &img_operadora_celebra_1 };
const lv_img_dsc_t *const img_operadora_susto[2] = { &img_operadora_susto_0, &img_operadora_susto_1 };

