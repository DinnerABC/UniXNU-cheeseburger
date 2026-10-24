#!/usr/bin/env python3
"""Check real final machine-code ordering and original ABI imports before boot."""
from pathlib import Path
import hashlib,json,struct,sys,subprocess
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'.work/p4-unixnu/kits/NativePrelink'));import native_prelink as np
p=r/'out/p12/kernel/kernel.macho';b=p.read_bytes();m=np.macho(b);plan=json.loads((p.parent/'prelink-report.json').read_text());symbols={n:a for a,ns in np.exports(b,m).items() for n in ns}
# The bootstrap function is local, so inspect the real full symbol table too.
for line in subprocess.check_output(['/opt/homebrew/opt/llvm/bin/llvm-nm',str(p)],text=True).splitlines():
 parts=line.split()
 if len(parts)==3:
  try:symbols[parts[2]]=int(parts[0],16)
  except ValueError:pass
def read(va,n):
 at=np.backed_offset(m['segments'],va,n,len(b));return b[at:at+n]
assert hashlib.sha256(b).hexdigest()==plan['candidate_sha256']
assert plan['slide']==-0xc2d000,'Original absolute-import layout changed: rebuild bindings'
for name in ['_kernel_bootstrap_thread','_cheeseburger_ufs_register','_cheeseburger_pmgr_startup','_mac_policy_initmach','_PE_init_iokit','__ZN9NativeUfs10gMetaClassE','__ZN12NativeUfsLun10gMetaClassE','__ZN25CheeseburgerPlatformPower10gMetaClassE']:
 assert name in symbols,name
calls=[];start=symbols['_kernel_bootstrap_thread']
for at in range(start,start+0x4000,4):
 word=struct.unpack('<I',read(at,4))[0]
 if word&0xfc000000==0x94000000:
  imm=word&0x3ffffff
  if imm&0x2000000:imm-=1<<26
  calls.append((at,at+imm*4))
order=[]
for name in ['_PE_init_iokit','_cheeseburger_ufs_register','_cheeseburger_pmgr_startup','_mac_policy_initmach']:
 pcs=[pc for pc,target in calls if target==symbols[name]]
 assert len(pcs)==1,(name,pcs);order.append(dict(function=name,call=hex(pcs[0]),target=hex(symbols[name])))
assert [int(x['call'],16) for x in order]==sorted(int(x['call'],16) for x in order)
for module,report in [('pmgr','build-report.json'),('native-ufs','bindings.json')]:
 j=json.loads((r/'out/p12'/module/report).read_text())
 for name,address in j['bindings'].items():
  assert name in symbols and symbols[name]==int(address,16),(name,address,symbols.get(name))
# These are the exact actual original provider implementations, not software
# values invented to look like a working Qualcomm security/power register.
vptr=0xfffffff0060b56d8
read_func,write_func=struct.unpack('<2Q',read(vptr+320*8,16))
assert struct.unpack('<2I',read(read_func,8))==(0x12800000,0xd65f03c0)
assert struct.unpack('<I',read(write_func,4))[0]==0xd65f03c0
for module in ['com.apple.driver.AppleMobileFileIntegrity','com.apple.security.AppleImage4','com.apple.kec.corecrypto','com.apple.kec.pthread','com.apple.iokit.IOStorageFamily','com.apple.driver.ApplePMGR']:
 e=next(x for x in plan['extents'] if x['module']==module and x['segment']=='__TEXT_EXEC')
 assert hashlib.sha256(read(e['va'],e['size'])).hexdigest()==plan['original_executable_sha256'][module]
# Neither custom class may be constructed before its original parent is loaded.
for obj in [r/'out/p12/pmgr/CheeseburgerPlatformPower.late.o',r/'out/p12/native-ufs/NativeUfs.late.o']:
 data=obj.read_bytes();info=np.macho(data)
 assert not any(s['name']=='__mod_init_func' for seg in info['segments'] for s in seg['sections'])
tables=(r/'out/p12/kernel-source/KernelConfigTables.cpp').read_text()
assert "'CFBundleIdentifier'='org.unixnu.cheeseburger.NativeUfs'" not in tables
assert "'CFBundleIdentifier'='org.unixnu.cheeseburger.PlatformPower'" not in tables
assert "'IOClass'='NativeUfs'" in tables and "'IOClass'='CheeseburgerPlatformPower'" in tables
pmgr=json.loads((r/'out/p12/pmgr/build-report.json').read_text())
source=(r/'drivers/CheeseburgerPlatformPower/CheeseburgerPlatformPower.cpp').read_bytes()
assert hashlib.sha256(source).hexdigest()==pmgr['source_sha256']
assert b'P11 PMGR: platform DT relationships changed' in b
assert b'nub=sheng_original_pmgr_nub_create(provider,this)' not in source
result=dict(result='P12_INTEGRATION_GATE_PASS',kernel_sha256=plan['candidate_sha256'],actual_bl_order=order,original_aon_read=hex(read_func),original_aon_write=hex(write_func),aon_contract='Original unsupported sentinel and no-hardware write, not verified Qualcomm state',hardware_power_implemented=False,native_ufs_linked=True,runtime_storage_abi_gate=True,original_code_preserved=True,hardware_tested=False)
o=r/'out/p12/integration';o.mkdir(exist_ok=True);(o/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
