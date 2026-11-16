#include "asset_bins.h"

.section .rodata.img,"a"
.align 4

.global img_bg_factory_map
.align 4
img_bg_factory_map:
    .incbin BIN_IMG_BG_FACTORY

.global img_u1_toolhead_map
.align 4
img_u1_toolhead_map:
    .incbin BIN_IMG_U1_TOOLHEAD

.global img_u1_carriage_map
.align 4
img_u1_carriage_map:
    .incbin BIN_IMG_U1_CARRIAGE

.global img_u1_spool_map
.align 4
img_u1_spool_map:
    .incbin BIN_IMG_U1_SPOOL

.global img_u1_part_map
.align 4
img_u1_part_map:
    .incbin BIN_IMG_U1_PART
