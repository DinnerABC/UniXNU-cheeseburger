#!/usr/bin/env python3
"""Execute linked P7 ARM64 probe instructions in Unicorn; synthetic memory only."""
from pathlib import Path
import sys,struct,re,json,hashlib
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'.work/p6/pythondeps'))
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_INSN,UcError
from unicorn.arm64_const import *
k=r/'out/p7/kernel/kernel.macho';b=k.read_bytes();v=json.loads(k.with_name('validation.json').read_text());base=min(s['vmaddr'] for s in v['segments'] if s['vmsize']);PA=0x40004000;ARGS=0x48000000;FB=0x60000000
symbols={}
for line in (k.parent/'symbols.log').read_text().splitlines():
 p=line.split()
 if len(p)>=3 and re.fullmatch('[0-9a-fA-F]+',p[0]):symbols[p[-1]]=int(p[0],16)
def physical(a):return PA+a-base
lines=(k.parent/'disassembly.log').read_text().splitlines();starts=[]
for line in lines:
 m=re.match(r'^([0-9a-f]+): [0-9a-f]+\s+mrs\s+x9, SCTLR_EL1',line)
 if m:
  va=int(m[1],16);seg=next(s for s in v['segments'] if s['vmaddr']<=va+4<s['vmaddr']+s['filesize']);off=seg['fileoff']+va+4-seg['vmaddr'];word=struct.unpack_from('<I',b,off)[0]
  if word&0x7f000000==0x37000000:starts.append(va)
# Only the three checkpoint macros before the appended fault handler.
starts=[a for a in starts if symbols['_start_first_cpu']<a<symbols['_p6_bad_bootargs']][:3];assert len(starts)==3,starts
FB_BYTES=4320*1920

def setup(framebuffer=FB):
 u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);span=(max(s['vmaddr']+s['vmsize'] for s in v['segments'] if s['vmsize'])-base+4095)&~4095
 u.mem_map(PA,span)
 for s in v['segments']:
  if s['filesize']:u.mem_write(physical(s['vmaddr']),b[s['fileoff']:s['fileoff']+s['filesize']])
 u.mem_map(ARGS,4096);u.mem_map(FB,(FB_BYTES+4095)&~4095)
 args=bytearray(4096);struct.pack_into('<HH',args,0,2,2);struct.pack_into('<6Q',args,0x28,framebuffer,1,4320,1080,1920,32);u.mem_write(ARGS,bytes(args))
 u.reg_write(UC_ARM64_REG_PSTATE,0x3c5);u.cpr_write(3,0,1,0,0,0)
 for i in range(29):u.reg_write(UC_ARM64_REG_X0+i,0x1000+i)
 u.reg_write(UC_ARM64_REG_X20,ARGS)
 return u
results=[]
for at,row,color,stage in zip(starts,[24,48,72],[0xffffff00,0xff00ffff,0xffffffff],[1,3,5]):
 u=setup();before=[u.reg_read(UC_ARM64_REG_X0+i) for i in range(29)]
 # The first conditional TBNZ points exactly to macro's end.
 pc=physical(at);word=struct.unpack('<I',bytes(u.mem_read(pc+4,4)))[0];imm=(word>>5)&0x3fff;imm=imm-(0x4000 if imm&0x2000 else 0);end=pc+4+imm*4
 u.emu_start(pc,end,count=100000)
 actual=bytes(u.mem_read(FB+row*4320,8*4320));assert actual==struct.pack('<I',color)*(len(actual)//4)
 assert bytes(u.mem_read(FB, row*4320))==bytes(row*4320)
 after=[u.reg_read(UC_ARM64_REG_X0+i) for i in range(29)]
 assert all(before[i]==after[i] for i in range(29) if i not in [9,10,11,12])
 rec=physical(symbols['_p6_diag_record']);assert struct.unpack('<Q',bytes(u.mem_read(rec+8,8)))[0]==stage
 results.append(dict(test='checkpoint',stage=stage,result='PASS',start_va=hex(at),registers_preserved_except=[9,10,11,12]))
# Inject only the MRS result to test the guard branch, not MMU translation.
# The emulator stays MMU-off; framebuffer backing is deliberately absent.
u=setup();u.mem_unmap(FB,(FB_BYTES+4095)&~4095)
def inject_sctlr(emu,reg,cp,data):
 if (cp.op0,cp.op1,cp.crn,cp.crm,cp.op2)==(3,0,1,0,0):
  emu.reg_write(reg,1);return True
 return False
u.hook_add(UC_HOOK_INSN,inject_sctlr,None,1,0,UC_ARM64_INS_MRS)
pc=physical(starts[0]);word=struct.unpack('<I',bytes(u.mem_read(pc+4,4)))[0];imm=(word>>5)&0x3fff;imm=imm-(0x4000 if imm&0x2000 else 0)
u.emu_start(pc,pc+4+imm*4,count=1000)
results.append(dict(test='SCTLR.M guard branch with no framebuffer backing',result='PASS',scope='Injected MRS input; no translation or device MMU proof'))

# Exercise actual fault assembly with seeded exception registers; not a real phone fault.
for fb in [FB,0]:
 u=setup(fb);esr=0x96000004;far=0x123456789abcdef0;elr=0x40012340
 u.cpr_write(3,0,5,2,0,esr);u.cpr_write(3,0,6,0,0,far);u.cpr_write(3,0,4,0,1,elr)
 u.reg_write(UC_ARM64_REG_SP_EL0,0x11110000);u.reg_write(UC_ARM64_REG_SP_EL1,0x22220000);u.reg_write(UC_ARM64_REG_SP,0x22220000)
 def stop(emu,address,size,data):
  if bytes(emu.mem_read(address,4))==b'\0\0\0\x14':emu.emu_stop()
 u.hook_add(UC_HOOK_CODE,stop)
 u.emu_start(physical(symbols['_p6_early_fault']),0,count=2000000)
 rec=physical(symbols['_p6_diag_record']);fields=struct.unpack('<16Q',bytes(u.mem_read(rec,128)))
 assert (fields[2],fields[3],fields[4])==(esr,far,elr)
 assert fields[6]==0x11110000 and fields[7]==0x22220000, tuple(hex(x) for x in fields[6:8])
 results.append(dict(test='fault recorder',framebuffer_valid=bool(fb),result='PASS',esr=hex(fields[2]),far=hex(fields[3]),elr=hex(fields[4]),stack_memory_accessed=False))
(r/'out/p7/probe-tests.json').write_text(json.dumps(dict(result='PASS',kernel_sha256=hashlib.sha256(b).hexdigest(),emulator='Unicorn2.1.4',scope='Host execution with synthetic BootArgs, registers, framebuffer; not device MMU or fault evidence',tests=results),indent=2)+'\n');print(json.dumps(results,indent=2))
