.section .rodata.img,"a"
.align 4

.global img_bg_factory_map
.align 4
img_bg_factory_map:
    .incbin "/workspace/u1-screen/firmware/src/assets/bin/img_bg_factory.bin"

.global img_u1_toolhead_map
.align 4
img_u1_toolhead_map:
    .incbin "/workspace/u1-screen/firmware/src/assets/bin/img_u1_toolhead.bin"

.global img_u1_carriage_map
.align 4
img_u1_carriage_map:
    .incbin "/workspace/u1-screen/firmware/src/assets/bin/img_u1_carriage.bin"

.global img_u1_spool_map
.align 4
img_u1_spool_map:
    .incbin "/workspace/u1-screen/firmware/src/assets/bin/img_u1_spool.bin"

.global img_u1_part_map
.align 4
img_u1_part_map:
    .incbin "/workspace/u1-screen/firmware/src/assets/bin/img_u1_part.bin"
