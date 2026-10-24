#!/usr/bin/env python3
from pathlib import Path
import difflib,hashlib,json
r=Path(__file__).resolve().parents[1];p=r/'.work/p4-unixnu/.cache/xnu-work/osfmk/arm/arm_init.c';before=p.read_text()
assert before==(r/'evidence/p7/arm_init-linked-v1.c').read_text(),'Require v1 exact C source'
t=before.replace('__startup_func\nvoid\narm_init(', (r/'config/p7/c-checkpoints.inc').read_text()+'\n__startup_func\nvoid\narm_init(',1)
points=[('arm_setup_pre_sign();',10,208,0xff00ff80,'pre-sign done'),('arm_slide_rebase_and_sign_image();',11,224,0xff8080ff,'slide/rebase done'),('cpu_data_init(&BootCpuData);',12,240,0xffffc000,'BootArgs copied and cpu data initialized'),('PE_init_platform(FALSE, args); /* Get platform expert set up */',13,256,0xff80ffff,'early PE returned; not IOKit'),('ml_parse_cpu_topology();',14,272,0xffff8080,'CPU topology parsed'),('cpu_bootstrap();',15,288,0xff80ff80,'cpu_bootstrap returned')]
for needle,stage,row,color,meaning in points:
 assert needle in t,needle
 t=t.replace(needle,needle+'\n\tP7_C_POINT('+str(stage)+', '+str(row)+', '+hex(color)+'U); /* '+meaning+' */',1)
needle='\tarm_vm_init(xmaxmem, args);';assert needle in t
t=t.replace(needle,'\tP7_C_POINT(16, 304, 0xffffffffU); /* about to enter arm_vm_init; no post-VM FB assumption */\n'+needle,1)
p.write_text(t)
(r/'patches/p7/c-checkpoints-v2.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),t.splitlines(True),fromfile='a/osfmk/arm/arm_init.c',tofile='b/osfmk/arm/arm_init.c')))
(r/'out/p7/c-checkpoints-v2.json').write_text(json.dumps({'points':[dict(stage=s,row=row,color=hex(c),meaning=m) for _,s,row,c,m in points]+[dict(stage=16,row=304,color='0xffffffff',meaning='before arm_vm_init; no completion claim')],'scope':'Only before VM rebuild; no raw framebuffer accesses after arm_vm_init'},indent=2)+'\n')
print('P7-v2 seven C checkpoints installed without changing initialization calls')
