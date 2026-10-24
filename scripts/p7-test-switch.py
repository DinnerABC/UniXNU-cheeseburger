#!/usr/bin/env python3
"""Attempt actual linked switch on synthetic memory; never a device boot proof."""
from pathlib import Path
import runpy,json,struct,collections,re
r=Path(__file__).resolve().parents[1];m=runpy.run_path(str(r/'scripts/p7-test-bootstrap.py'));g=m['setup'].__globals__;g['PA']=0x40004000;g['FB']=0x9d400000;u=m['setup'](g['FB']);base=m['base'];physical=m['physical'];symbols=m['symbols'];REG=m['UC_ARM64_REG_X0'];ARGS=m['ARGS']
from unicorn import UC_HOOK_CODE,UcError,UC_TLB_CPU
from unicorn.arm64_const import UC_ARM64_REG_PC
u.mem_write(ARGS+8,struct.pack('<3Q',base,g['PA'],0x20000000))
for reg,value in [(20,ARGS),(21,0),(22,base),(23,g['PA']),(24,0x20000000),(25,physical(symbols['_bootstrap_pagetables']))]:u.reg_write(REG+reg,value)
u.cpr_write(3,6,1,1,0,u.cpr_read(3,6,1,1,0)|(1<<10)|1) # SCR: non-secure AArch64 lower EL
u.cpr_write(3,4,1,1,0,u.cpr_read(3,4,1,1,0)|(1<<31)) # HCR.RW
u.ctl_set_tlb_mode(UC_TLB_CPU)
recent=collections.deque(maxlen=10)
def trace(emu,pc,size,data):recent.append(hex(pc))
u.hook_add(UC_HOOK_CODE,trace)
report={'scope':'Unicorn synthetic CPU/memory only; not real phone MMU evidence','kernel_sha256':m['hashlib'].sha256(m['b']).hexdigest()}
try:
 u.emu_start(physical(symbols['_start_first_cpu']+0xd8),symbols['_arm_init'],count=500000)
 pc=u.reg_read(UC_ARM64_REG_PC);assert pc==symbols['_arm_init'],hex(pc)
 for row,color in [(112,0xff0000ff),(136,0xffff8000),(160,0xffc0c0c0)]:
  data=bytes(u.mem_read(g['FB']+4320*row,8*4320));assert data==struct.pack('<I',color)*(len(data)//4),(row,data[:16].hex())
 code=(r/'out/p7/kernel/disassembly.log').read_text().splitlines()
 finish=next(int(line.split(':')[0],16)+8 for line in code if re.match(r'^[0-9a-f]+:',line) and symbols['_arm_init']<=int(line.split(':')[0],16)<symbols['_arm_init']+256 and 'msr' in line and 'VBAR_EL1' in line)
 u.emu_start(pc,finish,count=100000)
 data=bytes(u.mem_read(g['FB']+4320*184,8*4320));assert data==struct.pack('<I',0xffff80ff)*(len(data)//4)
 report['s09']='Actual emulated C prologue and first inline diagnostic statement executed; not real phone C proof'
 report.update(result='PASS',last_pc=hex(u.reg_read(UC_ARM64_REG_PC)),c_entry_pc=hex(pc),s06_s07_s08='actual emulated instruction markers verified',sctlr=hex(u.cpr_read(3,0,1,0,0)),bootargs_kva=hex(u.reg_read(REG)))
except (UcError,AssertionError) as e:report.update(result='FAIL',error=str(e),last_pc=hex(u.reg_read(UC_ARM64_REG_PC)))
report['registers']={n:hex(u.cpr_read(*q)) for n,q in {'SCTLR':(3,0,1,0,0),'TCR':(3,0,2,0,2),'TTBR0':(3,0,2,0,0),'TTBR1':(3,0,2,0,1),'MAIR':(3,0,10,2,0),'ESR':(3,0,5,2,0),'FAR':(3,0,6,0,0)}.items()};report['recent_pc']=list(recent);(r/'out/p7/switch-test.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(report['result']!='PASS')
