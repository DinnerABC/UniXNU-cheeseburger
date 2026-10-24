#!/usr/bin/env python3
"""Continue real linked C code with the real ADT but synthetic RAM/CPU.
Stops at panic or an unmapped peripheral; never evidence of phone execution.
"""
from pathlib import Path
import json,struct,collections
r=Path(__file__).resolve().parents[1]
code=(r/'scripts/p7-test-switch.py').read_text().replace("raise SystemExit(report['result']!='PASS')", "assert report['result']=='PASS'")
n={'__file__':str(r/'scripts/p7-test-switch.py'),'__name__':'p7_host_switch'};exec(compile(code,n['__file__'],'exec'),n)
u=n['u'];m=n['m'];symbols=n['symbols'];REG=n['REG'];ARGS=n['ARGS'];base=n['base'];pa=0x40004000
from unicorn import UC_HOOK_CODE,UcError
from unicorn.arm64_const import UC_ARM64_REG_PC
# Back the complete synthetic owned window so VM allocation can be exercised.
cursor=pa;limit=pa+0x20000000
for lo,hi,prot in sorted(u.mem_regions()):
 if hi<pa or lo>=limit:continue
 if cursor<lo:u.mem_map(cursor,lo-cursor)
 cursor=max(cursor,hi+1)
if cursor<limit:u.mem_map(cursor,limit-cursor)
tree=(r/'out/p7/adt/kernel.macho.adt').read_bytes();dt=0x49000000;u.mem_write(dt,tree)
u.mem_write(ARGS+0x60,struct.pack('<QI',base+dt-pa,len(tree)));u.mem_write(ARGS+0x20,struct.pack('<Q',dt+0x10000));u.mem_write(ARGS+0x478,struct.pack('<Q',0x20000000));u.mem_write(ARGS+0x6c,b'-v keepsyms=1 cpus=1\0')
recent=collections.deque(maxlen=12);stop={};panics={symbols[name]:name for name in ['_panic','_panic_with_options','_panic_context'] if name in symbols}
def trace(emu,pc,size,data):
 recent.append(hex(pc))
 if pc in panics:
  stop.update(reason=panics[pc],pc=hex(pc),x0=hex(emu.reg_read(REG)));emu.emu_stop()
u.hook_add(UC_HOOK_CODE,trace)
try:u.emu_start(u.reg_read(UC_ARM64_REG_PC),0,count=6000000)
except UcError as e:stop.update(reason=str(e),pc=hex(u.reg_read(UC_ARM64_REG_PC)))
if not stop:stop.update(reason='Bounded instruction limit or natural stop',pc=hex(u.reg_read(UC_ARM64_REG_PC)))
def string_at(a):
 if base<=a<base+0x20000000:a=pa+a-base
 try:return bytes(u.mem_read(a,256)).split(b'\0')[0].decode('utf8','replace')
 except UcError:return None
stop['x0_string']=string_at(u.reg_read(REG));stop['stage']=struct.unpack('<Q',bytes(u.mem_read(n['physical'](symbols['_p6_diag_record'])+8,8)))[0];stop['recent_pc']=list(recent)
stop['scope']='Host synthetic CPU/RAM plus real ADT; not a phone panic or platform execution proof';stop['kernel_sha256']=n['report']['kernel_sha256']
(r/'out/p7/host-early-c.json').write_text(json.dumps(stop,indent=2)+'\n');print(json.dumps(stop,indent=2))
