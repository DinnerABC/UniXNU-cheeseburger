#!/usr/bin/env python3
"""User-directed R3 metadata repack; no new kernel compile or diagnostic sweep."""
from pathlib import Path
import json,shutil,struct,sys,subprocess,os
R=Path(__file__).resolve().parents[1];B=R/'out/p22/r2';O=R/'out/p22/r3';O.mkdir(exist_ok=False)
for n in ['scripts','adt','config','data-seed','source','native','kernel','tests']:
 shutil.copytree(B/n,O/n)
for n in ['uefi','logs']:(O/n).mkdir()
for p in (O/'scripts').glob('*'):
 if p.is_file():
  s=p.read_text().replace('out/p22/r2','out/p22/r3').replace('boot-p22-r2-8cpu-4g.img','boot-p22-r3-8cpu-2g.img')
  if p.name in ['guest-overlay.sh','prepare-uefi.py']:
   s=s.replace('unixnu-p22r2','unixnu-p22r3').replace('.p22r2-overlay-marker','.p22r3-overlay-marker')
   if p.name=='guest-overlay.sh':s=s.replace('LOWER=/home/dinnerabc.guest/unixnu-p22/upper','LOWER=/home/dinnerabc.guest/unixnu-p22r2/upper:/home/dinnerabc.guest/unixnu-p22/upper')
  p.write_text(s)
sys.path.insert(0,str(R/'tools/adt'));from adt import decode,encode
p=O/'adt/kernel.macho.adt';tree=decode(p.read_bytes())
next(c for c in tree['children'] if c['name']=='chosen')['properties']['darwinboot-memory-size']=struct.pack('<Q',2*1024**3)
next(c for c in tree['children'] if c['name']=='defaults')['properties']['kern.maxvnodes']=struct.pack('<I',33792)
def emit(n):return encode(n['name'],{k:v for k,v in n['properties'].items() if k!='name'},[emit(c) for c in n['children']])
raw=emit(tree);p.write_bytes(raw)
import hashlib
v=json.loads((O/'adt/validation.json').read_text());v.update(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw),owned_memory_bytes=2*1024**3,maxvnodes=33792,scope='metadata repack; extra checks omitted at user request');(O/'adt/validation.json').write_text(json.dumps(v,indent=2)+'\n')
p=O/'config/unixnu.toml';p.write_text(p.read_text().replace('P22 R2 8CPU 4GiB TOUCH','P22 R3 8CPU 2GiB TOUCH'))
env=dict(os.environ,PATH='/opt/homebrew/opt/python@3.13/libexec/bin:/opt/homebrew/opt/llvm/bin:'+os.environ['PATH'])
def run(cmd,log):
 print(log,flush=True)
 with (O/log).open('w') as f:subprocess.run([str(x) for x in cmd],cwd=R,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
run([sys.executable,O/'scripts/build-payload.py'],'build-payload.log')
run(['bash','scripts/p2-vm.sh','shell','--workdir','/project/UniXNU-cheeseburger','p2','bash','out/p22/r3/scripts/guest-overlay.sh'],'guest-overlay.log')
run(['bash','scripts/p2-vm.sh','shell','--workdir','/project/UniXNU-cheeseburger','p2','python3','out/p22/r3/scripts/prepare-uefi.py'],'prepare-uefi.log')
run(['bash','scripts/p2-vm.sh','shell','--workdir','/project/UniXNU-cheeseburger','p2','bash','scripts/p2-linux-build.sh','/home/dinnerabc.guest/unixnu-p9/source','/project/UniXNU-cheeseburger/out/p22/r3/uefi'],'firmware-build.log')
(O/'logs/build-uefi-01.json').write_text(json.dumps({'exit_code':0,'scope':'user-directed repack only'})+'\n')
code='from pathlib import Path;import shutil;s=Path("/home/dinnerabc.guest/unixnu-p9/source");o=Path("/project/UniXNU-cheeseburger/out/p22/r3/uefi");shutil.copy2(s/"workspace/Build/cheeseburger/RELEASE_GCC5/FV/MSM8998PKG_UEFI.fd",o/"MSM8998PKG_UEFI.fd");shutil.copy2(s/"Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/DeviceTreeBlob/Android/Android-cheeseburger.dtb",o/"Android-cheeseburger.dtb")'
run(['bash','scripts/p2-vm.sh','shell','--workdir','/project/UniXNU-cheeseburger','p2','python3','-c',code],'export-uefi.log')
run([sys.executable,O/'scripts/pack.py'],'pack.log')
m=json.loads((O/'uefi/packaging.json').read_text());g=json.loads((B/'offline-gate.json').read_text());g.update(result='P22_R3_USER_REQUESTED_REPACK_READY',image=m['image'],image_bytes=m['bytes'],image_sha256=m['sha256'],adt_sha256=hashlib.sha256(raw).hexdigest(),owned_memory_bytes=2*1024**3,maxvnodes=33792,full_audit=False,extra_checks_omitted_at_user_request=True,kernel_reused_from='out/p22/r2/kernel/kernel.macho',scope='2GiB ADT/vnode metadata repack only; no new tests or independent audit');(O/'offline-gate.json').write_text(json.dumps(g,indent=2)+'\n');shutil.copy2(R/'out/p22/log-window.json',O/'log-window.json')
p=O/'scripts/boot-once.py';s=p.read_text().replace('P22_R2_8CPU_4G_OFFLINE_INPUT_PASS','P22_R3_USER_REQUESTED_REPACK_READY').replace('p22-r2-resource-once','p22-r3-2g-once');p.write_text(s)
run([sys.executable,p],'boot-once.log');print((O/'boot-result.json').read_text(),flush=True)
