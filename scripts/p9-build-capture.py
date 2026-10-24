#!/usr/bin/env python3
from pathlib import Path
import json,subprocess,hashlib,os,re
r=Path(__file__).resolve().parents[1];variant=os.environ.get('P9_CAPTURE_VARIANT','capture');assert re.fullmatch(r'capture(?:-v[0-9]+)?',variant);o=r/'out/p9'/variant;o.mkdir(parents=True,exist_ok=True);base=r/'.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/osfmk/DEVELOPMENT';recipe=base/'startup.o.json';v=json.loads(recipe.read_text().strip().rstrip(','));src=r/'drivers/p9/P9Capture.c';obj=o/'P9Capture.o';cmd=[];args=v['arguments'];i=0
while i<len(args):
 a=args[i]
 if a==v['file']:cmd.append(str(src))
 elif a=='-o':cmd+=['-o',str(obj)];i+=1
 elif a in ['-MF','-MT','-MJ']:i+=1
 elif a in ['-MMD','-MD','-MP']:pass
 else:cmd.append(a)
 i+=1
cmd+=['-Wno-error','-I',str(r/'drivers/p9')]
p=subprocess.run(cmd,cwd=v['directory'],capture_output=True,text=True);(o/'compile.log').write_text(p.stdout+p.stderr);(o/'build.json').write_text(json.dumps(dict(command=cmd,exit_code=p.returncode,source_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),hardware_tested=False),indent=2)+'\n')
if p.returncode:raise SystemExit(p.returncode)
dis=subprocess.run(['/opt/homebrew/opt/llvm/bin/llvm-objdump','-d',str(obj)],capture_output=True,text=True,check=True).stdout
(o/'disassembly.asm').write_text(dis)
# Check every emitted query, not merely a source string or one lucky match.
instructions=[line for line in dis.splitlines() if re.match(r'^\s*[0-9a-f]+: [0-9a-f]{8}',line)]
queries=[i for i,line in enumerate(instructions) if re.search(r'\bat\s+s1e1r,',line)]
assert queries,'No actual translation query emitted'
for i in queries:
 assert re.search(r'\bisb\b',instructions[i+1]),instructions[i:i+3]
 assert re.search(r'\bmrs\s+\w+, PAR_EL1',instructions[i+2]),instructions[i:i+3]
(o/'instruction-check.json').write_text(json.dumps(dict(result='PASS',translation_queries=len(queries),all_followed_by_isb_and_par_read=True,hardware_tested=False),indent=2)+'\n')
subprocess.run(['xcrun','ar','rcs',str(o/'libP9Capture.a'),str(obj)],check=True);print('Actual ARM64 first-fatal capture archive compiled')
