#!/usr/bin/env python3
from pathlib import Path
import difflib
r=Path(__file__).resolve().parents[1];p=r/'.work/p4-unixnu/.cache/xnu-work/osfmk/arm64/start.s';before=p.read_text()
marker='LEXT(_start)\n'
code='''#if defined(ARM64_BOARD_CONFIG_MSM8998)
	// P5: actual XNU __start breadcrumb, independent of DarwinEnter green bar.
	// Only framebuffer supplied by verified DarwinBoot BootArgs; preserve x0.
	ldr x9, [x0, #0x50]  // Video depth, Darwin23 boot_args ABI
	cmp x9, #32
	b.ne 95f
	ldr x9, [x0, #0x28]  // Video physical base
	cbz x9, 95f
	ldr x10, [x0, #0x48] // Video height
	cmp x10, #16
	b.lo 95f
	ldr x10, [x0, #0x38] // rowbytes
	cbz x10, 95f
	cmp x10, #16, lsl #12
	b.hi 95f
	lsl x10, x10, #4
	bic x10, x10, #7
	mov w11, #0x00ff
	movk w11, #0xffff, lsl #16 // BGRA magenta: actual kernel entry only
94:
	stp w11, w11, [x9], #8
	subs x10, x10, #8
	b.hi 94b
	dsb sy
95:
#endif
'''
if '// P5: actual XNU __start' not in before:
 after=before.replace(marker,marker+code,1);assert after!=before;p.write_text(after)
 (r/'patches/p5/xnu-entry-marker.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/osfmk/arm64/start.s',tofile='b/osfmk/arm64/start.s')))
print('P5 actual __start magenta breadcrumb added; no loader-only entry claim')
