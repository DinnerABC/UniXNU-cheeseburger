#!/usr/bin/env python3
"""Validate official output and selected ABI evidence before RAM payload packing."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys,os
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'.work/p4-unixnu/kits/NativePrelink'));import native_prelink as np
p=r/'out/p11'/os.environ.get('P11_PRELINK_VARIANT','prelink-v2')/'kernel/kernel.macho';b=p.read_bytes();plan=json.loads(p.with_suffix('.json').read_text());m=np.macho(b);assert hashlib.sha256(b).hexdigest()==plan['candidate_sha256'];assert plan['original_sha256']=='f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7'
def read(a,n):return b[np.backed_offset(m['segments'],a,n,len(b)):][:n]
for binding in plan['bindings']:assert struct.unpack('<Q',read(binding['new_site'],8))[0]==binding['new_target']
info=next(x for x in m['segments'] if x['name']=='__PRELINK_INFO');entries=np.unserialize_xml(read(info['va'],info['filesize']))['_PrelinkInfoDictionary'];by_name={x['CFBundleIdentifier']:x for x in entries};assert len(by_name)==len(entries)
abi=json.loads((r/'out/p11/security-abi/layout.json').read_text());modules=[]
for name in plan['components']:
 e=by_name[name]
 for dep in e.get('OSBundleLibraries',{}):assert dep in by_name,(name,dep)
 module=np.macho(b,np.backed_offset(m['segments'],e['_PrelinkExecutableLoadAddr'],32,len(b)));assert module['kind']==11
 start=struct.unpack('<Q',read(e['_PrelinkKmodInfo']+abi['kmod_start'],8))[0]
 assert any(x['name']=='__TEXT_EXEC' and x['va']<=start<x['va']+x['filesize'] for x in module['segments']),(name,hex(start))
 extent=next(x for x in plan['extents'] if x['module']==name and x['segment']=='__TEXT_EXEC');assert hashlib.sha256(read(extent['va'],extent['size'])).hexdigest()==plan['original_executable_sha256'][name]
 modules.append(dict(bundle=name,start=hex(start),original_start=hex(start-plan['slide']),text_sha256=plan['original_executable_sha256'][name]))
regs=json.loads((r/'out/p11/registration/registration-calls.json').read_text());assert regs[0]['interface_version']==20 and regs[1]['trust_cache_interface_version']>=2
symbols=(r/'out/p11/native/tool-validation/symbols.log').read_text();assert '_source_corecrypto14_bootstrap' not in symbols
# A checked N_ABS alias imports the real original class; it is not a second class.
nb=(r/'out/p11/native/kernel.macho').read_bytes();nm=np.macho(nb);sc=next(at for cmd,at,_ in nm['commands'] if cmd==2);so,nc,st,_=struct.unpack_from('<4I',nb,sc+8);found=[]
for ix in range(nc):
 si,kind,sect,desc,value=struct.unpack_from('<IBBHQ',nb,so+ix*16);name=nb[st+si:nb.index(b'\0',st+si)].decode()
 if name=='__ZN20IOBlockStorageDevice10gMetaClassE':found.append((kind&0xe,value))
assert found==[(2,0xfffffff006eb01d0)],('Original storage metaclass must be a single audited absolute import',found)
for name in ['com.apple.security.AppleImage4','com.apple.driver.AppleMobileFileIntegrity','com.apple.kext.CoreTrust','com.apple.kec.corecrypto']:assert name in by_name
assert by_name['com.apple.driver.AppleMobileFileIntegrity']['_NativePrelinkEarlySecurityInit']==plan['early_security_initializers']['com.apple.driver.AppleMobileFileIntegrity']
assert len(plan['bindings'])>0 and all(x['size'] for x in plan['coalesced_segments'])
o=r/'out/p11/qualification';o.mkdir(exist_ok=True);result=dict(result='STATIC_PRELINK_AND_SELECTED_ABI_GATE_PASS',candidate_sha256=plan['candidate_sha256'],bindings_verified=len(plan['bindings']),dependencies_complete=True,modules=modules,source_xnu='10063.141.1',donor_xnu='10063.142.1.703.8',darwin_abi='23.6 ordinary ARM64',exact_xnu_patch_version_equal=False,selected_img4_interface=20,selected_amfi_trustcache_interface=3,original_code_preserved=True,duplicate_source_crypto_and_storage_omitted=True,native_ufs_original_storage_binding_restored=True,runtime_abi_qualified=False,hardware_tested=False,apple_personalization_claimed=False)
(o/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
out=r/'out/p11/kernel';out.mkdir(exist_ok=True)
subprocess.run([sys.executable,str(r/'scripts/p4-export-kernel.py'),'--source',str(p),'--output',str(out)],check=True)
(out/'prelink-report.json').write_text(json.dumps(plan,indent=2)+'\n')
