#!/usr/bin/env python3
"""Read-only batch qualification of the last frozen userland diagnostic candidate.

Checks inputs and compiled interfaces, not live Persona, filesystem semantics,
software rasterization or a completed desktop. Never accesses a phone.
"""
from pathlib import Path
import hashlib,json,re,subprocess,tomllib
R=Path(__file__).resolve().parents[1];O=R/'out/p21/userland-audit';C=R/'out/p21/r17-diag2'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(v,s):
 if not v:raise SystemExit('FAIL: '+s)
checks=[]
def check(v,s):require(v,s);checks.append(s)
boot=json.loads((C/'boot-result.json').read_text());image=R/boot['image']
check(sha(image)==boot['sha256'],'frozen last-boot image SHA256')
k=json.loads((C/'kernel/validation.json').read_text());check(sha(C/'kernel/kernel.macho')==k['sha256'],'final prelinked kernel SHA256')
args=tomllib.loads((C/'config/unixnu.toml').read_text())['entry'][0]['args'].split()
check(all(x in args for x in ['cpus=1','offline-mobile-graphics-load=1','offline-legacy-cpu-present=1','offline-key-events=1','sheng-aks=1']),'existing display/CPU/KeyStore entry gates')
check(not any(x in args for x in ['sheng-fb-gpu=1','sheng-gpu-addon=1','sheng-gpu-metal=1']),'no GPU/Metal enable switch')
s=json.loads((C/'data-seed/validation.json').read_text())
check(sha(C/'data-seed/owner-data-1g.dbr')==s['output']['encoded_sha256'],'unchanged qualified RAM Data')
check(s['build']=='21H461' and s['skip_setup'] and s['no_auto_dim'],'matching-build first-boot preferences qualified')
check(all(v['uid']==501 and v['gid']==501 and v['mode']=='0600' for v in s['verified_preferences'].values()),'mobile preferences uid/gid/mode validation')
nm=subprocess.check_output(['/opt/homebrew/opt/llvm/bin/llvm-nm','--defined-only',str(C/'native/kernel.macho')],text=True)
syms=set(re.findall(r'^\S+\s+\S\s+(\S+)$',nm,re.M));required=['_persona','_persona_alloc','_persona_lookup','_renameatx_np','_fsync','_native_keyevents_allocate','_native_keystore_probe']
check(set(required)<=syms,'Persona/filesystem/KeyStore compiled interfaces present')
local=R/'package/BoardRuntime/NativeKeyStoreEvents.cpp';official=R/'.work/p21-official-framework/drivers/AppleKeyStoreState/NativeKeyStoreEvents.cpp'
check(local.read_bytes()==official.read_bytes(),'KeyStore core matches pinned official implementation')
pin=json.loads((C/'prelink-final/spec.json').read_text())['spec'];roots=pin['roots']
check(all(x in roots for x in ['com.apple.filesystems.apfs','com.apple.kec.pthread','com.apple.security.sandbox','com.apple.iokit.IOSurface','com.apple.iokit.IOMobileGraphicsFamily']),'original filesystem/security/pthread/graphics roots retained')
check(sha(R/pin['kernelcache'])==pin['kernelcache_sha256'],'fixed original kernelcache')
latest=R/'out/p21/persistent-logs/20261026T074907Z';ret=json.loads((latest/'retrieval.json').read_text());check(ret['write_errors']==0 and ret['rounds']==96,'last diagnostic retrieval valid')
report={'result':'FROZEN_USERLAND_INPUT_AND_INTERFACE_CHECK_PASS','checks':checks,'image':boot['image'],'image_sha256':boot['sha256'],'kernel_sha256':k['sha256'],'source_core_sha256':sha(local),'compiled_interfaces':required,'original_roots':roots,'phone_operations':[],'runtime_fix_proven':False,'limits':['Symbol presence does not prove all syscall branches or permissions','Data preference qualification does not prove runtime APFS operation coverage','KeyStore is ephemeral state/event compatibility, not secure storage','No software frame or U4 desktop acceptance claim']}
O.mkdir(exist_ok=True);(O/'baseline-check.json').write_text(json.dumps(report,indent=2)+'\n');print(report['result'],len(checks),'checks')
