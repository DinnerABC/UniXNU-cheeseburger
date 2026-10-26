from pathlib import Path
import os,sys,time,json,hashlib,shutil,subprocess,struct
R=Path(__file__).resolve().parents[2];O=R/'out/p21/upstream-a';T=R/'out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree';env=dict(os.environ,PATH='/opt/homebrew/opt/python@3.13/libexec/bin:/opt/homebrew/opt/llvm/bin:'+os.environ['PATH'],DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
def run(cmd,log=None):
 print('RUN',str(cmd[0]),str(cmd[1]) if len(cmd)>1 else '',flush=True)
 if log:
  with (O/log).open('w') as f:subprocess.run([str(x) for x in cmd],cwd=R,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
 else:subprocess.run([str(x) for x in cmd],cwd=R,env=env,check=True)
for i in range(600):
 if (O/'kernel-source-build.json').exists():break
 text=(O/'kernel-build-03.log').read_text()
 if 'Traceback (most recent call last)' in text:raise RuntimeError('Kernel build failed')
 time.sleep(2)
else:raise RuntimeError('Kernel build still incomplete; stop packaging')
record=json.loads((T/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/pexpert/DEVELOPMENT/pe_init.o.json').read_text().strip().rstrip(','));assert 'XNU_DT_PANIC_REGION=1' in record['arguments']
(O/'panic-region-fix.json').write_text(json.dumps({'result':'COMPILED_DT_PANIC_REGION_ENABLED','template_fixed_address_disabled':True,'device_panic_region_declared':False,'full_audit':False},indent=2)+'\n')
source=T/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf';(O/'native').mkdir(exist_ok=True);shutil.copy2(source,O/'native/kernel.macho')
sys.path.insert(0,str(R/'.work/p21-official-framework/kits/XnuPrelink'));import native_prelink as np
b=source.read_bytes();m=np.macho(b);pan=[]
for seg in m['segments']:
 if seg['name']!='__TEXT_EXEC':continue
 start=seg['fileoff'];size=seg['filesize']
 for at in range(start,start+size-3,4):
  w=struct.unpack_from('<I',b,at)[0]
  if w in (0xd500419f,0xd500409f):pan.append(hex(seg['va']+at-start))
assert not pan,('PAN instructions still present',pan)
(O/'pan-fix.json').write_text(json.dumps({'result':'PAN_IMMEDIATE_INSTRUCTIONS_ABSENT_FROM_NATIVE_TEXT','remaining':pan,'compile_config':'CONFIG_XNU_NATIVE_PAN=n','full_audit':False},indent=2)+'\n')
run([sys.executable,R/'scripts/p21-up1/prelink.py'],'prelink.log')
(O/'kernel').mkdir(exist_ok=True);shutil.copy2(O/'prelink-final/kernel/kernel.macho',O/'kernel/kernel.macho')
env['UNIXNU_UEFI']=str(R.parent/'upstream/uefi-sheng')
run([sys.executable,R/'.work/p21-official-framework/scripts/patch-xnu-entry-probe.py',O/'kernel/kernel.macho','--panic-screen','--no-reset'],'panic-screen.log')
p=O/'kernel/kernel.macho';blob=p.read_bytes();(p.parent/'validation.json').write_text(json.dumps({'result':'PASS','sha256':hashlib.sha256(blob).hexdigest(),'bytes':len(blob),'scope':'compile/link/prelink/patch inputs plus targeted absence of PAN; full audit omitted'},indent=2)+'\n')
run([sys.executable,R/'scripts/p21-up1/build-payload.py'],'build-payload.log')
for attempt in range(90):
 if 'READY.' in (O/'vm-start.log').read_text():break
 time.sleep(2)
else:raise RuntimeError('Build VM not ready')
run(['bash','scripts/p2-vm.sh','shell','--workdir','/project/UniXNU-cheeseburger','p2','bash','scripts/p21-up1/guest-overlay.sh'],'guest-overlay.log')
run(['bash','scripts/p2-vm.sh','shell','--workdir','/project/UniXNU-cheeseburger','p2','python3','scripts/p21-up1/prepare-uefi.py'],'prepare-uefi.log')
run(['bash','scripts/p2-vm.sh','shell','--workdir','/project/UniXNU-cheeseburger','p2','bash','scripts/p2-linux-build.sh','/home/dinnerabc.guest/unixnu-p9/source','/project/UniXNU-cheeseburger/out/p21/upstream-a/uefi'],'firmware-build.log')
(O/'logs/build-uefi-01.json').write_text(json.dumps({'exit_code':0,'log':'out/p21/upstream-a/firmware-build.log','full_audit':False})+'\n')
code='from pathlib import Path;import shutil;s=Path("/home/dinnerabc.guest/unixnu-p9/source");o=Path("/project/UniXNU-cheeseburger/out/p21/upstream-a/uefi");shutil.copy2(s/"workspace/Build/cheeseburger/RELEASE_GCC5/FV/MSM8998PKG_UEFI.fd",o/"MSM8998PKG_UEFI.fd");shutil.copy2(s/"Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/DeviceTreeBlob/Android/Android-cheeseburger.dtb",o/"Android-cheeseburger.dtb")'
run(['bash','scripts/p2-vm.sh','shell','--workdir','/project/UniXNU-cheeseburger','p2','python3','-c',code],'export-uefi.log')
run([sys.executable,R/'scripts/p21-up1/pack.py'],'pack.log')
print((O/'uefi/packaging.json').read_text(),flush=True)
