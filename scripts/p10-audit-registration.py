#!/usr/bin/env python3
"""Locate unchanged security registration call sites via real rebound import thunks."""
from pathlib import Path
import hashlib,json,re,struct,subprocess,sys,os
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'.work/p4-unixnu/kits/NativePrelink'));import native_prelink as np
p=r/'out/p10'/os.environ.get('P10_PRELINK_VARIANT','prelink-v4')/'kernel/kernel.macho';b=p.read_bytes();m=np.macho(b);plan=json.loads(p.with_suffix('.json').read_text());assert hashlib.sha256(b).hexdigest()==plan['candidate_sha256'];o=r/'out/p10/registration';o.mkdir(exist_ok=True)
exports=np.exports(b,m);slots={x['new_site']:exports.get(x['new_target'],[]) for x in plan['bindings'] if x['kind']=='kernel'}
def read(a,n):return b[np.backed_offset(m['segments'],a,n,len(b)):][:n]
def signed(x,n):return x-(1<<n) if x&(1<<(n-1)) else x
def adr(word,pc):
 imm=signed(((word>>5)&0x7ffff)<<2|((word>>29)&3),21)
 return (pc&~4095)+(imm<<12) if word&0x80000000 else pc+imm
result=[]
for module,name in [('com.apple.security.AppleImage4','_img4_interface_register'),('com.apple.driver.AppleMobileFileIntegrity','_amfi_interface_register')]:
 e=next(x for x in plan['extents'] if x['module']==module and x['segment']=='__TEXT_EXEC');blob=read(e['va'],e['size']);stubs={}
 for at in range(0,len(blob)-11,4):
  a,load,br=struct.unpack_from('<3I',blob,at);pc=e['va']+at
  if a&0x9f00001f==0x90000010 and load&0xffc003ff==0xf9400210 and br==0xd61f0200:
   slot=adr(a,pc)+((load>>10)&4095)*8
   if name in slots.get(slot,[]):stubs[pc]=slot
 assert stubs,(module,'no real registration thunk')
 calls=[]
 for at in range(0,len(blob)-3,4):
  word=struct.unpack_from('<I',blob,at)[0];pc=e['va']+at
  if word&0xfc000000!=0x94000000:continue
  target=pc+(signed(word&0x3ffffff,26)<<2)
  if target not in stubs:continue
  command=['/opt/homebrew/opt/llvm/bin/llvm-objdump','-D','--start-address='+hex(pc-96),'--stop-address='+hex(pc+40),str(p)]
  assembly=subprocess.run(command,capture_output=True,text=True,check=True).stdout
  (o/(name+'-'+hex(pc)+'.asm')).write_text(assembly)
  calls.append(dict(call=hex(pc),thunk=hex(target),slot=hex(stubs[target]),window=assembly))
 assert calls,(module,'no actual BL to registration')
 # Find the actual ADRP/ADD argument formation, using x0 for Image4 or
 # the preserved x8 table for AMFI's MOV x0,x8 immediately before BL.
 call=int(calls[0]['call'],16);reg=0 if name=='_img4_interface_register' else 8;pairs=[]
 for pc in range(call-256,call-7,4):
  a,add=struct.unpack('<2I',read(pc,8))
  if a&0x9f00001f==0x90000000|reg and add&0xffc003ff==0x91000000|(reg<<5)|reg:
   pairs.append((pc,adr(a,pc)+((add>>10)&4095)))
 assert pairs,(module,'no actual interface table argument')
 table=pairs[-1][1];layout=json.loads((r/'out/p10/security-abi/layout.json').read_text())
 if reg==0:
  version=struct.unpack('<I',read(table,4))[0];assert version==layout['img4_expected_version']
  runtime=struct.unpack('<Q',read(table+layout['img4_runtime_default'],8))[0];np.backed_offset(m['segments'],runtime,8,len(b));details=dict(interface_version=version,expected_interface_version=layout['img4_expected_version'],runtime_default=hex(runtime))
 else:
  tc=table+layout['tc_offset'];version=struct.unpack('<I',read(tc,4))[0];assert version>=2
  funcs={}
  for field in ['tc_loadModule','tc_load','tc_query']:
   function=struct.unpack('<Q',read(tc+layout[field],8))[0];assert e['va']<=function<e['va']+e['size'];funcs[field]=hex(function)
  details=dict(trust_cache_interface_version=version,required_minimum=2,trust_cache_functions=funcs)
 result.append(dict(module=module,registration=name,calls=calls,table=hex(table),compiled_layout=layout,**details))
(o/'registration-calls.json').write_text(json.dumps(result,indent=2)+'\n')
for row in result:
 print(row['module'],row['registration'])
 for c in row['calls']:print(c['window'])
