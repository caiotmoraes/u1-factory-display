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

