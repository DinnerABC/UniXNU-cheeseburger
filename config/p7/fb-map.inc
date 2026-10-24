// P7: map only page-rounded BootArgs framebuffer; no stack or calls.
// x2 is last allocated bootstrap page. All pool pages were zeroed above.
	adrp x8, EXT(p6_diag_record)@page
	add x8, x8, EXT(p6_diag_record)@pageoff
	ldr x9, [x20, #0x28]
	cbz x9, EXT(p6_bad_bootargs)
	tst x9, #4095
	b.ne EXT(p6_bad_bootargs)
	ldr x10, [x20, #0x50]
	cmp x10, #32
	b.ne EXT(p6_bad_bootargs)
	ldr x10, [x20, #0x38]
	cbz x10, EXT(p6_bad_bootargs)
	cmp x10, #16, lsl #12
	b.hi EXT(p6_bad_bootargs)
	str x10, [x8, #208]
	ldr x11, [x20, #0x48]
	cmp x11, #300
	b.lo EXT(p6_bad_bootargs)
	cmp x11, #4096
	b.hi EXT(p6_bad_bootargs)
	mul x10, x10, x11
	add x10, x10, #4095
	and x10, x10, #0xfffffffffffff000
	MOV64 x12, 0x1000000
	cmp x10, x12
	b.hi EXT(p6_bad_bootargs)
	add x11, x9, x10
	cmp x11, x9
	b.ls EXT(p6_bad_bootargs)
	lsr x12, x11, #38
	cbnz x12, EXT(p6_bad_bootargs)
	sub x12, x11, #1
	eor x12, x12, x9
	lsr x12, x12, #30
	cbnz x12, EXT(p6_bad_bootargs) // one L1 slot only
	add x12, x23, x24
	cmp x9, x12
	b.hs 81f
	cmp x11, x23
	b.hi EXT(p6_bad_bootargs) // refuse overlap with owned RAM window
81:
	lsr x12, x9, #30
	and x12, x12, #255
	add x12, x25, x12, lsl #3
	ldr x13, [x12]
	cbnz x13, EXT(p6_bad_bootargs) // do not overwrite an existing RAM mapping
	add x17, x25, #(BOOTSTRAP_TABLE_BYTES >> 12), lsl #12
	add x16, x2, #4096
	cmp x16, x17
	b.hs EXT(p6_bad_bootargs)
	mov x13, x16
	orr x14, x13, #3
	str x14, [x12]
	str x10, [x8, #136]
	mov x7, x9
82:
	lsr x12, x9, #21
	and x12, x12, #511
	add x12, x13, x12, lsl #3
	ldr x14, [x12]
	cbnz x14, 83f
	add x16, x16, #4096
	cmp x16, x17
	b.hs EXT(p6_bad_bootargs)
	orr x14, x16, #3
	str x14, [x12]
83:
	and x14, x14, #0x0000fffffffff000
	lsr x15, x9, #12
	and x15, x15, #511
	add x15, x14, x15, lsl #3
	// Target CACHE_ATTRINDX_DISABLE (Device-nGnRnE), AF, OuterShareable, PXN/UXN.
	MOV64 x18, (0x0060000000000603 | (CACHE_ATTRINDX_DISABLE << 2))
	orr x18, x18, x9
	str x18, [x15]
	add x9, x9, #4096
	cmp x9, x11
	b.lo 82b
	str x7, [x8, #128] // publish only after complete mapping
	dsb sy
