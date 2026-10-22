#!/usr/bin/env python3
from pathlib import Path
import difflib,json,hashlib
r=Path(__file__).resolve().parents[1];p=r/'.work/p4-unixnu/.cache/xnu-work/osfmk/arm64/start.s';before=p.read_text()
if '// P6 EARLY DIAGNOSTICS V1' in before:raise SystemExit('P6 patch already applied; reuse current source')
macro='''
// P6 EARLY DIAGNOSTICS V1: x20=BootArgs PA; clobbers x9-x12 only.
// No stack/calls. Never access the physical framebuffer with MMU enabled.
.macro P6_BAR
	mrs x9, SCTLR_EL1
	tbnz x9, #0, 99f
	cbz x20, 99f
	adrp x12, EXT(p6_diag_record)@page
	add x12, x12, EXT(p6_diag_record)@pageoff
	mov x9, #$3
	str x9, [x12, #8]
	ldr x9, [x20, #0x50]
	cmp x9, #32
	b.ne 99f
	ldr x9, [x20, #0x48]
	cmp x9, #($0+8)
	b.lo 99f
	ldr x9, [x20, #0x28]
	cbz x9, 99f
	ldr x10, [x20, #0x38]
	cbz x10, 99f
	cmp x10, #16, lsl #12
	b.hi 99f
	mov x11, #$0
	madd x9, x10, x11, x9
	lsl x10, x10, #3
	bic x10, x10, #7
	mov w11, #$1
	movk w11, #$2, lsl #16
98:
	stp w11, w11, [x9], #8
	subs x10, x10, #8
	b.hi 98b
	dsb sy
99:
.endmacro
'''
after=before.replace('.macro MSR_VBAR_EL1_X0',macro+'\n.macro MSR_VBAR_EL1_X0',1)
# Establish known BootArgs and the existing kernel early vector before OSLAR.
marker='LEXT(start_first_cpu)\n'
after=after.replace(marker,marker+'''
#if defined(ARM64_BOARD_CONFIG_MSM8998)
	mov x20, x0
	adrp x12, EXT(LowExceptionVectorBase)@page
	add x12, x12, EXT(LowExceptionVectorBase)@pageoff
	msr VBAR_EL1, x12
	isb
#endif
''',1)
needle='\tldr\t\tx26, [x20, BA_BOOT_FLAGS]\t\t\t// Get the kernel boot flags'
# Only cold-boot instance: start_cpu has same load earlier.
at=after.index(marker);pos=after.index(needle,at)+len(needle)
check='''
#if defined(ARM64_BOARD_CONFIG_MSM8998)
	tst x20, #15
	b.ne EXT(p6_bad_bootargs)
	ldr w9, [x20]
	mov w10, #2
	movk w10, #2, lsl #16
	cmp w9, w10
	b.ne EXT(p6_bad_bootargs)
	cbz x24, EXT(p6_bad_bootargs)
	P6_BAR 24,0xff00,0xffff,1 // S01 yellow: actual BootArgs loaded/checked
#endif
'''
after=after[:pos]+check+after[pos:]
after=after.replace('Lbootstrap_tables_ready:\n','Lbootstrap_tables_ready:\n#if defined(ARM64_BOARD_CONFIG_MSM8998)\n\tP6_BAR 48,0xffff,0xff00,3 // S03 cyan: tables built\n#endif\n',1)
after=after.replace('\t// Enable caches, MMU, ROP and JOP\n','\t// Enable caches, MMU, ROP and JOP\n#if defined(ARM64_BOARD_CONFIG_MSM8998)\n\tP6_BAR 72,0xffff,0xffff,5 // S05 white: before SCTLR enables MMU\n#endif\n',1)
# Existing early vectors stay aligned and branch to a no-stack fault recorder.
start=after.index('LEXT(LowExceptionVectorBase)');end=after.index('#if defined(KERNEL_INTEGRITY_KTRR)',start)
span=after[start:end];assert span.count('\tb\t\t.')==16
span=span.replace('\tb\t\t.','\tb\t\tEXT(p6_early_fault)');after=after[:start]+span+after[end:]
font=['11111/10001/10001/10001/10001/10001/11111','00100/01100/00100/00100/00100/00100/01110','11111/00001/00001/11111/10000/10000/11111','11111/00001/00001/11111/00001/00001/11111','10001/10001/10001/11111/00001/00001/00001','11111/10000/10000/11111/00001/00001/11111','11111/10000/10000/11111/10001/10001/11111','11111/00001/00010/00100/01000/01000/01000','11111/10001/10001/11111/10001/10001/11111','11111/10001/10001/11111/00001/00001/11111','01110/10001/10001/11111/10001/10001/10001','11110/10001/10001/11110/10001/10001/11110','11111/10000/10000/10000/10000/10000/11111','11110/10001/10001/10001/10001/10001/11110','11111/10000/10000/11111/10000/10000/11111','11111/10000/10000/11111/10000/10000/10000']
glyphs=[sum((c=='1')<<i for i,c in enumerate(s.replace('/',''))) for s in font]
handler='''
#if defined(ARM64_BOARD_CONFIG_MSM8998)
	.align 3
	.globl EXT(p6_bad_bootargs)
LEXT(p6_bad_bootargs)
	mov x9, #0xbad
	b EXT(p6_record_fault)
	.globl EXT(p6_early_fault)
LEXT(p6_early_fault)
	mov x9, #0x6001
LEXT(p6_record_fault)
	// Own kernel data only; no stack/calls, no cross-reboot retention claim.
	adrp x16, EXT(p6_diag_record)@page
	add x16, x16, EXT(p6_diag_record)@pageoff
	str x9, [x16]
	mrs x9, ESR_EL1
	mrs x10, FAR_EL1
	mrs x11, ELR_EL1
	stp x9, x10, [x16, #16]
	str x11, [x16, #32]
	mrs x9, SPSR_EL1
	mrs x12, SPSel
	msr SPSel, #0
	mov x10, sp
	msr SPSel, #1
	mov x11, sp
	tbnz x12, #0, 97f
	msr SPSel, #0
97:
	stp x9, x10, [x16, #40]
	str x11, [x16, #56]
	str lr, [x16, #64]
	mrs x9, SCTLR_EL1
	mrs x10, TCR_EL1
	mrs x11, TTBR0_EL1
	stp x9, x10, [x16, #72]
	str x11, [x16, #88]
	mrs x10, TTBR1_EL1
	mrs x11, MAIR_EL1
	stp x10, x11, [x16, #96]
	str x20, [x16, #112]
	ldr x10, [x16, #8]
	str x10, [x16, #120]
	dsb sy
	// MMU-on framebuffer isn't guaranteed mapped: retain record and stop.
	tbnz x9, #0, Lp6_fault_stop
	cbz x20, Lp6_fault_stop
	P6_BAR 96,0x0000,0xffff,0xff // Red fault stripe
	ldr x19, [x20, #0x28]
	cbz x19, Lp6_fault_stop
	tst x19, #3
	b.ne Lp6_fault_stop
	ldr x21, [x20, #0x38]
	cbz x21, Lp6_fault_stop
	cmp x21, #16, lsl #12
	b.hi Lp6_fault_stop
	ldr x9, [x20, #0x40]
	cmp x9, #640
	b.lo Lp6_fault_stop
	ldr x9, [x20, #0x48]
	cmp x9, #300
	b.lo Lp6_fault_stop
	ldr x9, [x20, #0x50]
	cmp x9, #32
	b.ne Lp6_fault_stop
	// Display ESR, ELR, FAR as three 16-hex-digit rows. Only when MMU off.
	mov x22, #0
Lp6_hex_line:
	adrp x16, EXT(p6_diag_record)@page
	add x16, x16, EXT(p6_diag_record)@pageoff
	cmp x22, #0
	b.eq Lp6_esr
	cmp x22, #1
	b.eq Lp6_elr
	ldr x24, [x16, #24]
	b Lp6_hex_value
Lp6_esr:
	ldr x24, [x16, #16]
	b Lp6_hex_value
Lp6_elr:
	ldr x24, [x16, #32]
Lp6_hex_value:
	mov x23, #0
Lp6_digit:
	mov x9, #60
	lsl x10, x23, #2
	sub x9, x9, x10
	lsrv x9, x24, x9
	and x9, x9, #15
	adr x10, Lp6_hex_font
	ldr x25, [x10, x9, lsl #3]
	mov x26, #0
Lp6_glyph_row:
	mov x27, #0
Lp6_glyph_col:
	mov x9, #5
	madd x9, x26, x9, x27
	lsrv x9, x25, x9
	and x9, x9, #1
	mov w15, #-1
	cmp x9, #0
	csel w15, w15, wzr, ne
	mov x28, #0
Lp6_pixel_row:
	mov x9, #48
	madd x9, x22, x9, xzr
	add x9, x9, #128
	mov x10, #6
	madd x9, x26, x10, x9
	add x9, x9, x28
	madd x14, x9, x21, x19
	mov x9, #36
	mul x9, x23, x9
	mov x10, #6
	madd x9, x27, x10, x9
	add x9, x9, #24
	add x14, x14, x9, lsl #2
	mov x29, #6
Lp6_pixel_col:
	str w15, [x14], #4
	subs x29, x29, #1
	b.ne Lp6_pixel_col
	add x28, x28, #1
	cmp x28, #6
	b.lo Lp6_pixel_row
	add x27, x27, #1
	cmp x27, #5
	b.lo Lp6_glyph_col
	add x26, x26, #1
	cmp x26, #7
	b.lo Lp6_glyph_row
	add x23, x23, #1
	cmp x23, #16
	b.lo Lp6_digit
	add x22, x22, #1
	cmp x22, #3
	b.lo Lp6_hex_line
	dsb sy
Lp6_fault_stop:
	b Lp6_fault_stop
	.align 3
Lp6_hex_font:
'''+''.join('\t.quad '+hex(g)+'\n' for g in glyphs)+'''
	.section __BOOTDATA,__data
	.align 3
	.globl EXT(p6_diag_record)
LEXT(p6_diag_record)
	.space 128,0
	.text
#endif
'''
after+='\n'+handler;p.write_text(after)
(r/'patches/p6/early-diag-v1.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/osfmk/arm64/start.s',tofile='b/osfmk/arm64/start.s')))
(r/'out/p6/diagnostic-spec.json').write_text(json.dumps(dict(version='P6-v1',markers={'S00':'P5 magenta 16rows at0, actual __start','S01':'yellow8rows at24, BootArgs loaded/check passed','S03':'cyan8rows at48, bootstrap tables built','S05':'white8rows at72, before SCTLR MMU enable','FAULT':'red8rows at96; if MMU off hex ESR/ELR/FAR rows128/176/224'},fault_record_bytes=128,mmu_on_framebuffer_access=False,source_sha256=hashlib.sha256(after.encode()).hexdigest(),implemented_but_not_hardware_validated=True),indent=2)+'\n')
print('P6-v1 three pre-MMU checkpoints and no-stack early fault recorder installed')
