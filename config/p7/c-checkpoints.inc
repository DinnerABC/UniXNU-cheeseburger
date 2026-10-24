/* P7-v2: pre-arm_vm_init only. Uses the already validated device mapping.
 * No helper calls; no change to assertions or platform initialization. */
#if defined(ARM64_BOARD_CONFIG_MSM8998)
#define P7_C_POINT(stage, row, color) do { \
    __asm__ volatile( \
        "adrp x12, _p6_diag_record@PAGE\n" \
        "add x12, x12, _p6_diag_record@PAGEOFF\n" \
        "ldr x9, [x12, #128]\n" \
        "cbz x9, 99f\n" \
        "ldr x10, [x12, #208]\n" \
        "mov x11, #%c1\n" \
        "add x11, x11, #8\n" \
        "mul x11, x11, x10\n" \
        "ldr x9, [x12, #136]\n" \
        "cmp x11, x9\n" \
        "b.hi 99f\n" \
        "mov x11, #%c0\n" \
        "str x11, [x12, #8]\n" \
        "ldr x9, [x12, #128]\n" \
        "mov x11, #%c1\n" \
        "madd x9, x10, x11, x9\n" \
        "lsl x10, x10, #3\n" \
        "mov w11, #%c2\n" \
        "movk w11, #%c3, lsl #16\n" \
        "98: stp w11, w11, [x9], #8\n" \
        "subs x10, x10, #8\n" \
        "b.hi 98b\n" \
        "dsb sy\n" \
        "99:\n" \
        :: "i"(stage), "i"(row), "i"((color) & 65535), "i"((color) >> 16) \
        : "x9", "x10", "x11", "x12", "cc", "memory"); \
} while (0)
#else
#define P7_C_POINT(stage, row, color) do {} while (0)
#endif
