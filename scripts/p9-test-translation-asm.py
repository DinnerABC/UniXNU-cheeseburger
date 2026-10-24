#!/usr/bin/env python3
"""Regression: the tested bad object must fail; fixed object AND final kernel pass."""
from pathlib import Path
import json,re,subprocess
r=Path(__file__).resolve().parents[1];o=r/'out/p9/translation-regression';o.mkdir(exist_ok=True)
def check(name,path,bounds=()):
 cmd=['/opt/homebrew/opt/llvm/bin/llvm-objdump','-d',*bounds,str(path)]
 s=subprocess.run(cmd,capture_output=True,text=True,check=True).stdout;(o/(name+'.asm')).write_text(s)
 ins=[x for x in s.splitlines() if re.match(r'^\s*[0-9a-f]+: [0-9a-f]{8}',x)]
 sites=[i for i,x in enumerate(ins) if re.search(r'\bat\s+s1e1r,',x)]
 valid=[i for i in sites if i+2<len(ins) and re.search(r'\bisb\b',ins[i+1]) and re.search(r'\bmrs\s+\w+, PAR_EL1',ins[i+2])]
 return dict(queries=len(sites),valid_sequences=len(valid),pass_all=bool(sites) and len(sites)==len(valid))
bad=check('actually-tested-v1',r/'out/p9/capture/P9Capture.o');good=check('fixed-object-v2',r/'out/p9/capture-v2/P9Capture.o')
k=r/'out/p9/kernel-v2/kernel.macho';table=(k.parent/'tool-validation/symbols.log').read_text();addresses=[int(x.split()[0],16) for x in table.splitlines() if re.match(r'^[0-9a-f]+ ',x) and '(__TEXT_EXEC,__text)' in x and re.search(r' _(p9_|capture$)',x)]
sections=subprocess.run(['/opt/homebrew/opt/llvm/bin/llvm-objdump','--section-headers',str(r/'out/p9/capture-v2/P9Capture.o')],capture_output=True,text=True,check=True).stdout
size=int(re.search(r'^\s*0 __text\s+([0-9a-f]+)',sections,re.M)[1],16)
linked=check('fixed-final-kernel-v2',k,['--start-address='+hex(min(addresses)),'--stop-address='+hex(min(addresses)+size)])
assert not bad['pass_all'] and bad['queries']>0,bad
assert good['pass_all'] and linked['pass_all'] and good['queries']==linked['queries'],(good,linked)
(o/'result.json').write_text(json.dumps(dict(result='PASS',tested_v1=bad,corrected_object=good,corrected_final_kernel=linked,body_capture_hardware_retested=False),indent=2)+'\n');print('Regression reproduced actual v1 missing ISB/PAR reads; every v2 object and final-kernel query is complete')
