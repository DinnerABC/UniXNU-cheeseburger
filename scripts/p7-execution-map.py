#!/usr/bin/env python3
from pathlib import Path
import json,re,struct,subprocess,hashlib
r=Path(__file__).resolve().parents[1];o=r/'out/p7';k=o/'kernel/kernel.macho';b=k.read_bytes();v=json.loads(k.with_name('validation.json').read_text());sy={}
for line in k.with_name('symbols.log').read_text().splitlines():
 p=line.split()
 if len(p)>=3 and re.fullmatch('[0-9a-fA-F]+',p[0]):sy[p[-1]]=int(p[0],16)
def readva(a,n):
 for s in v['segments']:
  if s['vmaddr']<=a and a+n<=s['vmaddr']+s['filesize']:return b[s['fileoff']+a-s['vmaddr']:s['fileoff']+a-s['vmaddr']+n]
 raise ValueError(hex(a))
record=r/'.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/osfmk/DEVELOPMENT/start.o.json'
c,_=json.JSONDecoder().raw_decode(record.read_text());src=Path(c['file']);d=o/'execution-map';d.mkdir(exist_ok=True)
source=d/'constants.s';source.write_text(src.read_text().split('// P6 EARLY DIAGNOSTICS V1')[0]+ '\n.section __DATA,__const\n.quad TCR_EL1_BOOT,SCTLR_EL1_DEFAULT,ARM_PGSHIFT,ARM_TT_L1_SHIFT,ARM_TT_L2_SHIFT,BOOTSTRAP_TABLE_BYTES,SR_RESTORE_TCR_EL1\n')
a=c['arguments'][:];a[a.index(c['file'])]=str(source);a[a.index('-o')+1]=str(d/'constants.o');p=subprocess.run(a,cwd=c['directory'],capture_output=True,text=True);(d/'compile.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
z=(d/'constants.o').read_bytes();at=32;values=None
for i in range(struct.unpack_from('<I',z,16)[0]):
 cmd,size=struct.unpack_from('<II',z,at)
 if cmd==0x19:
  for j in range(struct.unpack_from('<I',z,at+64)[0]):
   q=at+72+j*80
   if z[q:q+16].rstrip(b'\0')==b'__const':
    off=struct.unpack_from('<I',z,q+48)[0];values=struct.unpack_from('<7Q',z,off)
 at+=size
assert values is not None
keys=['TCR_EL1_BOOT','SCTLR_EL1_DEFAULT','ARM_PGSHIFT','ARM_TT_L1_SHIFT','ARM_TT_L2_SHIFT','BOOTSTRAP_TABLE_BYTES','SR_RESTORE_TCR_EL1'];constants=dict(zip(keys,values));constants['linked_sysreg_restore_TCR_EL1']=struct.unpack('<Q',readva(sy['_sysreg_restore']+values[-1],8))[0]
lines=k.with_name('disassembly.log').read_text().splitlines();start=next(i for i,l in enumerate(lines) if '<__start>:' in l);end=next(i for i,l in enumerate(lines) if '<_p6_bad_bootargs>:' in l)
(d/'entry-assembly.log').write_text('\n'.join(lines[start:end+1])+'\n')
summary=dict(kernel_sha256=hashlib.sha256(b).hexdigest(),constants={n:hex(x) for n,x in constants.items()},symbols={n:hex(sy[n]) for n in ['__start','_start_first_cpu','common_start','_set_mmu_ttb','_arm_init','_LowExceptionVectorBase','_bootstrap_pagetables','_p6_diag_record']},compile_command=a,compile_exit=p.returncode,scope='Linked binary and exact target preprocessor constants, not device execution evidence')
(d/'map.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({n:hex(x) for n,x in constants.items()},indent=2))
