#!/usr/bin/env python3
"""Apply narrow P7 diagnostics to the frozen, actually tested P6 source."""
from pathlib import Path
import json,hashlib,difflib,shutil
r=Path(__file__).resolve().parents[1];s=r/'.work/p4-unixnu/.cache/xnu-work';p=s/'osfmk/arm64/start.s';before=p.read_text()
expected=json.loads((r/'out/p7/baseline/manifest.json').read_text())['evidence/p6/start-linked-v1.s']['sha256']
assert hashlib.sha256(p.read_bytes()).hexdigest()==expected,'Require unchanged P6 source before applying'
t=before.replace('.macro MSR_VBAR_EL1_X0',(r/'config/p7/post-mmu.inc').read_text()+'\n.macro MSR_VBAR_EL1_X0',1)
t=t.replace('Lbootstrap_tables_ready:\n','Lbootstrap_tables_ready:\n#if defined(ARM64_BOARD_CONFIG_MSM8998)\n'+(r/'config/p7/fb-map.inc').read_text().rstrip()+'\n#endif\n',1)
t=t.replace('adrp\tlr, EXT(arm_init)@page','adrp\tlr, EXT(p7_c_entry)@page',1).replace('add\t\tlr, lr, EXT(arm_init)@pageoff','add\t\tlr, lr, EXT(p7_c_entry)@pageoff',1)
assert 'adrp\tlr, EXT(p7_c_entry)@page' in t
# Keep a V=P early vector across SCTLR on cold boot; normal warm path unchanged.
needle='1:\n\tMSR_VBAR_EL1_X0\n\n#if HAS_APPLE_PAC'
assert needle in t
t=t.replace(needle,'''1:
#if defined(ARM64_BOARD_CONFIG_MSM8998)
	cbnz x21, 86f
	adrp x0, EXT(LowExceptionVectorBase)@page
	add x0, x0, EXT(LowExceptionVectorBase)@pageoff
86:
#endif
	MSR_VBAR_EL1_X0

#if HAS_APPLE_PAC''',1)
needle='\tP6_BAR 72,0xffff,0xffff,5 // S05 white: before SCTLR enables MMU'
snapshot='''
	adrp x12, EXT(p6_diag_record)@page
	add x12, x12, EXT(p6_diag_record)@pageoff
	mrs x9, TTBR0_EL1
	str x9, [x12, #144]
	and x9, x9, #(TTBR_BADDR_MASK)
	cmp x9, x25
	b.ne EXT(p6_bad_bootargs)
	mrs x9, TTBR1_EL1
	str x9, [x12, #152]
	and x9, x9, #(TTBR_BADDR_MASK)
	add x10, x25, PGBYTES
	cmp x9, x10
	b.ne EXT(p6_bad_bootargs)
	mrs x9, TCR_EL1
	str x9, [x12, #160]
	MOV64 x10, TCR_EL1_BOOT
	cmp x9, x10
	b.ne EXT(p6_bad_bootargs)
	mrs x9, MAIR_EL1
	str x9, [x12, #168]
	ubfx x9, x9, #(CACHE_ATTRINDX_DISABLE * 8), #8
	cbnz x9, EXT(p6_bad_bootargs)
	mrs x9, SCTLR_EL1
	str x9, [x12, #176]
	adr x9, .
	str x9, [x12, #184]
	mrs x9, VBAR_EL1
	str x9, [x12, #192]
'''
t=t.replace(needle,snapshot+needle,1)
needle='\tMSR_SCTLR_EL1_X0\n\tisb\t\tsy\n'
assert needle in t
t=t.replace(needle,needle+'''
#if defined(ARM64_BOARD_CONFIG_MSM8998)
	mrs x9, SCTLR_EL1
	tbz x9, #0, EXT(p6_early_fault)
	P7_BAR 112,0x00ff,0xff00,6 // S06 blue: M readback and post-ISB execution
#endif
''',1)
t=t.replace('\t.space 128,0','\t.space 256,0',1)
# MMU-on early fault rendering is permitted only after bounded map publication.
t=t.replace('\ttbnz x9, #0, Lp6_fault_stop','\ttbz x9, #0, 85f\n\tldr x10, [x16, #128]\n\tcbz x10, Lp6_fault_stop\n85:',1)
t=t.replace('\tP6_BAR 96,0x0000,0xffff,0xff // Red fault stripe','\tP7_BAR 96,0x0000,0xffff,0xff // bounded mapped FB; may be unavailable on pre-map fault',1)
t+='''
#if defined(ARM64_BOARD_CONFIG_MSM8998)
	.text
	.align 2
	.globl EXT(p7_c_entry)
LEXT(p7_c_entry)
	P7_BAR 136,0x8000,0xffff,7 // S07 orange: LR reached KVA wrapper
	stp x0, lr, [sp, #-16]!
	ldp x0, lr, [sp], #16
	P7_BAR 160,0xc0c0,0xffc0,8 // S08 gray: real stack store/load
	b EXT(arm_init)
#endif
'''
p.write_text(t)
q=s/'osfmk/arm/arm_init.c';old=q.read_text();(r/'out/p7/baseline/arm_init-p6.c').write_text(old)
# Compiler prologue executes before this first C statement; preserve BootArgs via clobbers.
asm='''
#if defined(ARM64_BOARD_CONFIG_MSM8998)
	__asm__ volatile(
	    "adrp x12, _p6_diag_record@PAGE\\n"
	    "add x12, x12, _p6_diag_record@PAGEOFF\\n"
	    "ldr x9, [x12, #128]\\n"
	    "cbz x9, 99f\\n"
	    "mov x10, #9\\n"
	    "str x10, [x12, #8]\\n"
	    "ldr x10, [x12, #208]\\n"
	    "mov x11, #184\\n"
	    "madd x9, x10, x11, x9\\n"
	    "lsl x10, x10, #3\\n"
	    "mov w11, #0x80ff\\n"
	    "movk w11, #0xffff, lsl #16\\n"
	    "98: stp w11, w11, [x9], #8\\n"
	    "subs x10, x10, #8\\n"
	    "b.hi 98b\\n"
	    "dsb sy\\n"
	    "99:\\n"
	    "adrp x9, _ExceptionVectorsBase@PAGE\\n"
	    "add x9, x9, _ExceptionVectorsBase@PAGEOFF\\n"
	    "msr VBAR_EL1, x9\\n"
	    "isb\\n" ::: "x9", "x10", "x11", "x12", "cc", "memory");
#endif
'''
needle='\tboot_args       *args)\n{\n';assert needle in old
new=old.replace(needle,needle+asm,1);q.write_text(new)
patch=''.join(difflib.unified_diff(before.splitlines(True),t.splitlines(True),fromfile='a/osfmk/arm64/start.s',tofile='b/osfmk/arm64/start.s'))+''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/osfmk/arm/arm_init.c',tofile='b/osfmk/arm/arm_init.c'))
(r/'patches/p7/mmu-diag-v1.patch').write_text(patch)
print('P7 bounded Device-nGnRnE framebuffer mapping, early vectors, S06/S07/S08 and first-C S09 installed')
