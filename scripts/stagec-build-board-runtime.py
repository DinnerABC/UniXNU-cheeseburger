#!/usr/bin/env python3
"""Rebuild the board source closure against the new official kernel records."""
from pathlib import Path
import json,os,shutil,subprocess,sys,hashlib
ROOT_ENTRY=Path(__file__).resolve().parents[1];R=ROOT_ENTRY if (ROOT_ENTRY/'board.toml').is_file() else (ROOT_ENTRY/'kits/cheeseburger').resolve().parent;U=R/'.work/p21-official-framework';T=R/'out/p23/ios-16.7.16-20H392/tree';B=T/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837';S=T/'build/stagec-board-src/BoardRuntime';O=R/'out/p23/ios-16.7.16-20H392/runtime-build';O.mkdir(parents=True,exist_ok=True)
shutil.copytree(R/'package/BoardRuntime',S,dirs_exist_ok=True)
for name in ['RootAuth.cpp','MediaInventory.cpp']:
 p=S/name; t=p.read_text().replace('E652776C-E75D-4712-850F-AB4D81C3D836','DADD93F8-DBBF-4B17-A1DF-0F11E64172F7').replace('6B06A0EF-7B90-4C3B-B25A-D30E696BAE3C','96096B28-B6DC-4BC7-9D3A-2632A0FE53F1').replace('D2420479-F58E-44C5-AB79-1815EC32C686','D8A691FB-D618-46C5-A5D2-84692FDBFE44');p.write_text(t)
ENV=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',PATH='/opt/homebrew/opt/python@3.13/libexec/bin:/opt/homebrew/opt/llvm/bin:'+os.environ['PATH'])
records={}
for lang,name in [('c','osfmk/DEVELOPMENT/startup.o.json'),('cpp','iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'),('bsd','bsd/DEVELOPMENT/kern_exec.o.json')]:records[lang]=json.loads((B/name).read_text().strip().rstrip(','))
# Qualified original ABI declarations are inputs, not reused executable objects.
abi=O/'abi/IOKit/storage';abi.mkdir(parents=True,exist_ok=True)
for p in (R/'out/p11/storage-abi/include/IOKit/storage').glob('*.h'):shutil.copy2(p,abi/p.name)
shutil.copy2(T/'build/storage-abi/include/TargetConditionals.h',O/'abi/TargetConditionals.h')
for p in [R/'.work/p20-r11/framework/kits/cheeseburger/VerifiedBlockPolicy.h']:
 if not (S/p.name).exists():shutil.copy2(p,S/p.name)
def run(cmd,label,cwd=None):
 result=subprocess.run([str(v) for v in cmd],cwd=cwd,env=ENV,capture_output=True,text=True);p=O/(label+'.log');
 if p.exists() and p.read_text()!=result.stdout+result.stderr:
  old=O/(label+'-previous-'+hashlib.sha256(p.read_bytes()).hexdigest()[:12]+'.log');shutil.copy2(p,old)
 p.write_text(result.stdout+result.stderr)
 if result.returncode:raise SystemExit(label+':\n'+result.stdout[-1800:]+result.stderr[-3500:])
compiled=[];commands=[]
def compile(src,name=None,flags=(),late=False):
 src=Path(src);name=name or src.stem;obj=O/(name+'.o');lang='cpp' if src.suffix=='.cpp' else 'c';rec=records['bsd' if src.stem in ['BootWatchdogPolicy','BootLog','verified_root','xnu_crypto'] else lang];args=rec['arguments'];cmd=[];i=0
 while i<len(args):
  a=args[i]
  if a==rec['file']:cmd.append(str(src))
  elif a=='-o':cmd+=['-o',str(obj)];i+=1
  elif a in ('-MF','-MJ','-MT'):i+=1
  elif a not in ('-MD','-MMD','-MP'):cmd.append(a)
  i+=1
 cmd+=['-I',str(S),'-I',str(O/'abi'),'-I',str(T/'.cache/xnu-work/osfmk'),'-I',str(U/'kits/VerifiedRoot/include'),'-Wno-error','-mgeneral-regs-only','-DUNIXNU_BOARD_ABI_20H392=1',*flags]
 run(cmd,name+'-compile',rec['directory']);commands.append({'source':str(src.relative_to(R)),'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'command':cmd,'cwd':rec['directory'],'object_sha256':hashlib.sha256(obj.read_bytes()).hexdigest()})
 if late:
  transformed=O/(name+'.late.o');run(['/opt/homebrew/opt/llvm/bin/llvm-objcopy','--remove-section=__DATA,__mod_init_func','--globalize-symbol=__GLOBAL__sub_I_'+src.name,obj,transformed],name+'-late');obj=transformed
 compiled.append(obj);return obj
cpp=['IOKitMatchTrace','NativeUfs','CheeseburgerPlatformPower','NativeRamBlock','RamLateRegistration','MediaInventory','DataActivation','RootAuth','CacheAuth','NativeLegacyDisplay','NativeGOPPresenter','LegacyDisplayProbe','HidInputProbe','NativeHidInput','CheeseburgerButtons','CheeseburgerTouch','NativeKeyStoreEvents','KeyStoreProbe','KeyStoreFixture']
late={'NativeUfs','CheeseburgerPlatformPower','NativeRamBlock','NativeLegacyDisplay','NativeGOPPresenter','NativeHidInput','NativeKeyStoreEvents'}
for n in cpp:compile(S/(n+'.cpp'),flags=['-DUNIXNU_HID_TOUCH_SOURCE=1'] if n=='NativeHidInput' else [],late=n in late)
for n in ['BootWatchdogPolicy','no-store-provider','ObserverAnchor','CompactObserver','DisplayObservation','BootProcessProbe','P20ExitProbe','BootFaultProbe','StartupWaitProbe']:compile(S/(n+'.c'))
subprocess.run([sys.executable,R/'scripts/p21-select-official-log-reports.py',*compiled],check=True)
compile(S/'BootLog.c')
for n in ['verified_root','xnu_crypto']:
 p=U/'kits/VerifiedRoot'/('src/verified_root.c' if n=='verified_root' else 'adapters/xnu_crypto.c');compile(p)
# These drivers share original superclass aliases. Emit each absolute alias once;
# reject conflicting addresses instead of weakening the linker duplicate check.
bindings={};binding_sources=[]
for n in ['display','hid','pmgr','ufs','ram']:
 src=S/((('ufs' if n=='ram' else n)+'-original-bindings-20H392.S'));binding_sources.append({'source':str(src.relative_to(R)),'sha256':hashlib.sha256(src.read_bytes()).hexdigest()})
 for line in src.read_text().splitlines():
  if line.startswith('.set '):
   symbol,address=line[5:].split(',',1);address=int(address.strip(),0)
   if symbol in bindings and bindings[symbol]!=address:raise SystemExit('Conflicting original binding: '+symbol)
   bindings[symbol]=address
src=O/'original-bindings.S';src.write_text(''.join('.globl '+s+'\n.set '+s+', '+hex(a)+'\n' for s,a in sorted(bindings.items())))
obj=O/'original-bindings.o';run(['xcrun','clang','-target','arm64-apple-ios16.0','-c',src,'-o',obj],'original-bindings');compiled.append(obj)
(O/'original-bindings.json').write_text(json.dumps({'sources':binding_sources,'unique_aliases':len(bindings),'conflicts':0,'aliases':{s:hex(a) for s,a in bindings.items()}},indent=2)+'\n')
# Recompile the unchanged signed policy inputs: never regenerate keys/manifests.
policy=R/'out/p23/ios-16.7.16-20H392/policies/inputs.S';obj=O/'verified-policy.o';run(['xcrun','clang','-target','arm64-apple-ios16.0','-c',policy,'-o',obj],'verified-policy');compiled.append(obj)
archive=O/'libCheeseburgerRuntime.a';archive.unlink(missing_ok=True);run(['xcrun','ar','rcs',archive,*compiled],'archive')
(O/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');(O/'result.json').write_text(json.dumps({'result':'BOARD_RUNTIME_SOURCE_BUILD_PASS','objects':len(compiled),'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'old_objects_reused':False,'touch_producer':'CheeseburgerTouch QUP5/RMI4 F12; opt-in, hardware unverified','hardware_tested':False},indent=2)+'\n');dest=T/'build/native-legacy-display';dest.mkdir(parents=True,exist_ok=True)
shutil.copy2(archive,dest/'libNativeLegacyDisplay.late.a')
report={'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'source_sha256':{str(p.relative_to(T)):hashlib.sha256(p.read_bytes()).hexdigest() for p in S.rglob('*') if p.is_file() and p.suffix in ('.h','.inc','.S','.c','.cpp')},'objects_rebuilt':len(compiled),'old_objects_reused':False,'abi':'20H392','candidate_slide':'-0xbc0000'}
(dest/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
print('Source-only board runtime built',len(compiled),'objects')
