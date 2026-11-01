from pathlib import Path
import hashlib,json,struct,sys
r=Path.cwd(); o=r/'out/p21/atos-gate'; t=o/'source-tree/out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree'
h=lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
report=json.loads((o/'kernel-source-build.json').read_text()); assert report['result']=='OFFICIAL_KERNEL_SOURCE_BUILD_PASS'
assert h(t/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837')==report['kernel_sha256']
assert h(t/'kits/cheeseburger/BoardRuntime/CheeseburgerGpuCp.cpp')==h(r/'package/BoardRuntime/CheeseburgerGpuCp.cpp')
assert h(t/'kits/cheeseburger/BoardRuntime/CheeseburgerAtosGate.h')==h(r/'package/BoardRuntime/CheeseburgerAtosGate.h')
record=json.loads((t/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/pexpert/DEVELOPMENT/pe_init.o.json').read_text().strip().rstrip(',')); assert 'XNU_DT_PANIC_REGION=1' in record['arguments']
sys.path.insert(0,str(r/'.work/p21-official-framework/kits/XnuPrelink'));import native_prelink as np
b=(o/'native/kernel.macho').read_bytes(); m=np.macho(b)
for s in m['segments']:
 if s['name']=='__TEXT_EXEC':
  for at in range(s['fileoff'],s['fileoff']+s['filesize']-3,4): assert struct.unpack_from('<I',b,at)[0] not in (0xd500419f,0xd500409f)
p=o/'prelink-final/kernel/kernel.macho'; assert h(p) in (o/'prelink.log').read_text()
assert 'panic screen' in (o/'panic-screen.log').read_text()
f=o/'kernel/kernel.macho'; np.macho(f.read_bytes())
print(json.dumps({'compiled_kernel_sha256':report['kernel_sha256'],'prelink_sha256':h(p),'kernel_sha256':h(f),'checks':'source hashes, compilation, panic region, PAN instructions, prelink hash, entry patch, Mach-O','booted':False},indent=2))
result={'result':'PASS','sha256':h(f),'bytes':f.stat().st_size,'scope':'source hashes, compilation, panic region, PAN scan, prelink hash, entry patch, Mach-O; device unverified'}
f.with_name('validation.json').write_text(json.dumps(result,indent=2)+'\n')
