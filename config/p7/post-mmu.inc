// P7 draw: mapping was bounded/validated before SCTLR; x9-x12 scratch only.
.macro P7_BAR
	adrp x12, EXT(p6_diag_record)@page
	add x12, x12, EXT(p6_diag_record)@pageoff
	ldr x9, [x12, #128]
	cbz x9, 89f
	mov x10, #$3
	str x10, [x12, #8]
	ldr x10, [x12, #208]
	mov x11, #$0
	madd x9, x10, x11, x9
	lsl x10, x10, #3
	mov w11, #$1
	movk w11, #$2, lsl #16
88:
	stp w11, w11, [x9], #8
	subs x10, x10, #8
	b.hi 88b
	dsb sy
89:
.endmacro
