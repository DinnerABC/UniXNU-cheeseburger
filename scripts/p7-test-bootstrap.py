#!/usr/bin/env python3
"""Execute final linked table-building instructions; no emulated MMU enable."""
from pathlib import Path
import runpy,struct,json,hashlib
r=Path(__file__).resolve().parents[1];m=runpy.run_path(str(r/'scripts/p7-test-probes.py'))
Uc=m['Uc'];PA=m['PA'];ARGS=m['ARGS'];base=m['base'];symbols=m['symbols'];b=m['b'];v=m['v'];physical=m['physical'];setup=m['setup'];UC_ARM64_REG_X0=m['UC_ARM64_REG_X0'];UC_ARM64_REG_X20=m['UC_ARM64_REG_X20']
setup.__globals__['FB']=0x9d400000
results=[]
for aligned in [True,False]:
 # Relocate the fixture by the exact VA low-21-bit residue for valid alignment.
 newpa=0x40000000+(base&0x1fffff) if aligned else 0x40104000
 m['PA']=newpa
 # Imported function global dictionary belongs to runpy's module, not m.
 physical.__globals__['PA']=newpa;setup.__globals__['PA']=newpa
 u=setup(0x9d400000);u.mem_write(ARGS+8,struct.pack('<3Q',base,newpa,0x20000000))
 for reg,value in [(20,ARGS),(21,0),(22,base),(23,newpa),(24,0x20000000),(25,physical(symbols['_bootstrap_pagetables']))]:u.reg_write(UC_ARM64_REG_X0+reg,value)
 start=physical(symbols['_start_first_cpu']+0xd8)
 u.emu_start(start,physical(symbols['common_start']),count=200000)
 root=physical(symbols['_bootstrap_pagetables']);read=lambda a:struct.unpack('<Q',bytes(u.mem_read(a,8)))[0]
 def walk(table,va):
  desc=read(table+((va>>30)&255)*8);assert desc&3==3
  block=read((desc&0x0000fffffffff000)+((va>>21)&511)*8);assert block&3==1
  return (block&0x0000ffffffe00000)|(va&0x1fffff)
 targets=['__start','_arm_init','_LowExceptionVectorBase','_p6_diag_record']
 for name in targets:
  va=symbols[name];expected=physical(va);actual=walk(root+4096,va)
  assert (actual==expected)==aligned,(name,hex(actual),hex(expected))
  assert walk(root,expected)==expected
 # Walk the exact L3 framebuffer mapping and prove adjacent pages absent.
 fb=0x9d400000;size=(m['FB_BYTES']+4095)&~4095
 l2=read(root+((fb>>30)&255)*8)&0x0000fffffffff000
 def page_desc(va):
  entry=read(l2+((va>>21)&511)*8)
  return read((entry&0x0000fffffffff000)+((va>>12)&511)*8) if entry&3==3 else 0
 for va in range(fb,fb+size,4096):
  desc=page_desc(va);assert desc&0x0000fffffffff000==va
  assert desc&0x7ff==0x60f and desc&(3<<53)==3<<53
 assert page_desc(fb-4096)==0 and page_desc(fb+size)==0
 results.append(dict(test='actual linked bootstrap mapping',aligned=aligned,result='PASS',scope='Synthetic BootArgs and memory; MMU is never enabled',physical_base=hex(newpa),virtual_base=hex(base),mapped_targets=targets,misalignment_detected=not aligned))
(r/'out/p7/bootstrap-tests.json').write_text(json.dumps(dict(kernel_sha256=hashlib.sha256(b).hexdigest(),result='PASS',tests=results),indent=2)+'\n');print(json.dumps(results,indent=2))
