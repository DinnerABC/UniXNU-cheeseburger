#!/usr/bin/env python3
"""Bounded VM-stage diagnostics; replace the temporary FB identity map normally."""
from pathlib import Path
import json,difflib
r=Path(__file__).resolve().parents[1];s=r/'.work/p4-unixnu/.cache/xnu-work';baseline=r/'out/p7/milestones/early-pe-cpu-v2';p=s/'osfmk/arm64/arm_vm_init.c';before=p.read_text();assert before==(baseline/'arm_vm_init-v2.c').read_text()
macro=(r/'config/p7/c-checkpoints.inc').read_text()
needle='void\narm_vm_init(uint64_t memory_size, boot_args * args)';assert needle in before
t=before.replace(needle,macro+'\n'+needle,1)
t=t.replace('\tgVirtBase = args->virtBase;', '\tP7_C_POINT(17, 320, 0xffff00ffU); /* VM function entered */\n\tgVirtBase = args->virtBase;',1)
points=[('\tarm_vm_prot_init(args);',18,336,0xff0080ff,'before kernel region protections'),('\tarm_vm_prot_init(args);',19,352,0xff00ff00,'kernel region protections built'),('\tarm_vm_physmap_init(args);',20,368,0xffff8000,'before physical aperture setup'),('\tarm_vm_physmap_init(args);',21,384,0xff00ffff,'physical aperture setup returned'),('\tset_mmu_ttb_alternate(cpu_ttep & TTBR_BADDR_MASK);',22,400,0xffffffff,'new TTBR1 active; old TTBR0 still valid')]
for needle,stage,row,color,meaning in points:
 assert needle in t
 marker='\tP7_C_POINT('+str(stage)+', '+str(row)+', '+hex(color)+'U); /* '+meaning+' */'
 t=t.replace(needle,marker+'\n'+needle if stage in [18,20] else needle+'\n'+marker,1)
p.write_text(t)
q=s/'osfmk/arm/arm_init.c';old=q.read_text();assert old==(baseline/'arm_init-v2.c').read_text()
needle='\tarm_vm_init(xmaxmem, args);'
remap='''
#if defined(ARM64_BOARD_CONFIG_MSM8998)
	/* TTBR0 has now been invalidated by normal VM initialization.
	 * Obtain a real early IO VA; never retain the old physical FB alias. */
	extern volatile uint64_t p6_diag_record[];
	uint64_t p7_fb_phys = p6_diag_record[16];
	uint64_t p7_fb_bytes = p6_diag_record[17];
	p6_diag_record[16] = 0;
	if (p7_fb_phys && p7_fb_bytes && p7_fb_bytes <= 0x1000000ULL) {
		vm_offset_t p7_fb_va = ml_io_map((vm_offset_t)p7_fb_phys, (vm_size_t)p7_fb_bytes);
		p6_diag_record[29] = p7_fb_phys;
		p6_diag_record[16] = (uint64_t)p7_fb_va;
		P7_C_POINT(23, 416, 0xffff80ffU); /* VM returned and valid IO VA is usable */
	}
#endif
'''
new=old.replace(needle,needle+remap,1)
needle='\tPE_init_platform(TRUE, &BootCpuData);';assert needle in new
new=new.replace(needle,needle+'\n\tP7_C_POINT(24, 432, 0xff80ff80U); /* later PE returned, not IOKit completion */',1)
q.write_text(new)
patch=''.join(difflib.unified_diff(before.splitlines(True),t.splitlines(True),fromfile='a/osfmk/arm64/arm_vm_init.c',tofile='b/osfmk/arm64/arm_vm_init.c'))+''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/osfmk/arm/arm_init.c',tofile='b/osfmk/arm/arm_init.c'))
(r/'patches/p7/vm-checkpoints-v3.patch').write_text(patch)
(r/'out/p7/vm-checkpoints-v3.json').write_text(json.dumps({'points':[dict(stage=17,row=320,color='0xffff00ff',meaning='VM function entry')]+[dict(stage=a,row=b,color=hex(c),meaning=d) for _,a,b,c,d in points]+[dict(stage=23,row=416,color='0xffff80ff',meaning='arm_vm_init returned and ml_io_map produced usable IO VA'),dict(stage=24,row=432,color='0xff80ff80',meaning='PE_init_platform(TRUE) returned')],'normal_ttbr0_invalidation_preserved':True,'post_vm_output':'Only after ml_io_map creates valid IO VA; no old identity mapping assumption'},indent=2)+'\n')
print('P7-v3 VM checkpoints plus post-return IO remap installed; normal TTBR0 invalidation retained')
