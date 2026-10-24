from pathlib import Path
import json,shutil,subprocess
r=Path(__file__).resolve().parents[1];o=r/'out/p11/native-ufs';o.mkdir(parents=True,exist_ok=True);d=r/'drivers/p11/NativeUfs';d.mkdir(parents=True,exist_ok=True)
# P11 source is canonical: never overwrite fixes with the frozen P8 baseline.
p=d/'NativeUfs.cpp'
assert p.is_file() and 'cheeseburger_ufs_register' in p.read_text()
record=json.loads((r/'.work/p4-unixnu/build/xnu/ios-obj/DEVELOPMENT_ARM64_MSM8998/iokit/DEVELOPMENT/IOCPU.cpo.json').read_text().strip().rstrip(','));cmd=[];a=record['arguments'];i=0
while i<len(a):
 x=a[i]
 if x==record['file']:cmd.append(str(p))
 elif x=='-o':cmd.extend(['-o',str(o/'NativeUfs.o')]);i+=1
 elif x in ['-MF','-MT','-MJ']:i+=1
 elif x in ['-MMD','-MD','-MP']:pass
 else:cmd.append(x)
 i+=1
cmd.extend(['-I',str(r/'out/p11/storage-abi/include'),'-Wno-error','-Wno-deprecated-declarations'])
(o/'compile-command.json').write_text(json.dumps(cmd,indent=2)+'\n')
subprocess.run(cmd,cwd=record['directory'],check=True)
s=(r/'.work/p4-unixnu/scripts/build-native-virtio-bindings.py').read_text().replace("ROOT / 'kits/NativePrelink'","ROOT / '.work/p4-unixnu/kits/NativePrelink'").replace("ROOT / 'build/storage-abi/interface-audit.json'","ROOT / 'out/p11/storage-abi/interface-audit.json'")
s=s.replace("choices=['NativeVirtioBlock', 'NativeRamBlock'], default='NativeVirtioBlock'","choices=['NativeUfsLun'], default='NativeUfsLun'")
s=s.replace("ROOT / 'build/native-virtio-block'","ROOT / 'out/p11/native-ufs'").replace("out / (component + '.o')","out / 'NativeUfs.o'")
s=s.replace("not audit['live_native_4k']", "not audit['static_dispatch_qualified']")
s=s.replace("First qualify original live storage metadata and its real base functions","First qualify original static dispatch and enforce runtime metaclass gates")
s=s.replace("ROOT / 'build/native-prelink/native-apfs-load-positive.macho'","ROOT / 'out/p11/baseline/out/p10/kernel/kernel.macho'")
s=s.replace("out / ('lib' + component + '.a')", "out / 'libNativeUfs.a'")
# The audit has no live size/translation observation. Runtime refusal gates in
# this source are mandatory and checked before invoking the binder.
assert 'original storage runtime ABI mismatch' in p.read_text()
binder=r/'scripts/p11-bind-ufs.py';binder.write_text(s)
subprocess.run(['python3',str(binder)],check=True)

# Late initialization is required because original storage metadata is loaded
# after the native kernel's initial C++ constructor pass.
raw=o/'NativeUfs.o';late=o/'NativeUfs.late.o'
subprocess.run(['/opt/homebrew/opt/llvm/bin/llvm-objcopy','--remove-section=__DATA,__mod_init_func','--globalize-symbol=__GLOBAL__sub_I_NativeUfs.cpp',str(raw),str(late)],check=True)
archive=o/'libNativeUfs.a';archive.unlink()
subprocess.run(['xcrun','ar','rcs',str(archive),str(late),str(o/'original-bindings.o')],check=True)
