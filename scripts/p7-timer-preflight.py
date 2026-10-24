#!/usr/bin/env python3
from pathlib import Path
import json,subprocess,hashlib,re
r=Path(__file__).resolve().parents[1];o=r/'out/p7/preflight';o.mkdir(parents=True,exist_ok=True)
obj=r/'.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/osfmk/DEVELOPMENT/start.o';recipe=obj.with_suffix('.o.json');v,_=json.JSONDecoder().raw_decode(recipe.read_text());a=v['arguments'][:];a[a.index('-o')+1]=str(o/'macros.txt');a[a.index('-c')]='-E';a.append('-dM');p=subprocess.run(a,cwd=v['directory'],capture_output=True,text=True);(o/'preprocess.log').write_text(p.stdout+p.stderr);assert p.returncode==0
macros=(o/'macros.txt').read_text();sequence=bytes.fromhex('af0038d5ef1d4092000088d200bc70d30000019100080f8b011e8052010000b9df3f03d5');kernel=r/'out/p6/kernel/kernel.macho'
checks={}
for n,q in [('start_object',obj),('kernel',kernel)]:
 b=q.read_bytes();checks[n]=dict(file=str(q.relative_to(r)),sha256=hashlib.sha256(b).hexdigest(),legacy_sequence_count=b.count(sequence));assert checks[n]['legacy_sequence_count']==0
for n in ['BCM2837','ARM64_BOARD_CONFIG_BCM2837','ARM64_BOARD_CONFIG_MSM8998','XNU_NATIVE_ARCH_TIMER','ARM_ARCH_TIMER']:
 m=re.search(r'^#define '+n+r'(?: (.*))?$',macros,re.M);checks[n]=m.group(1) if m else 'UNDEFINED'
assert checks['BCM2837']=='UNDEFINED' and checks['ARM64_BOARD_CONFIG_BCM2837']=='UNDEFINED'
p=subprocess.run(['/opt/homebrew/opt/llvm/bin/llvm-objdump','--disassemble',str(obj)],capture_output=True,text=True);assert p.returncode==0;(o/'start-object-disassembly.log').write_text(p.stdout)
checks.update(result='EXCLUDED',official_patch_applied='No: source guard is still #if defined(BCM2837), but this target does not define BCM2837',reason='Legacy sequence absent from actual object and final P6 image; S05 is downstream of the source legacy block',compile_recipe=v,preprocess_command=a)
(o/'timer.json').write_text(json.dumps(checks,indent=2)+'\n');print(json.dumps({k:v for k,v in checks.items() if k not in ['compile_recipe','preprocess_command']},indent=2))
