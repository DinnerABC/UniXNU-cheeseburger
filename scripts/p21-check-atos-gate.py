from pathlib import Path
import hashlib,json,struct,sys
r=Path.cwd(); o=r/'out/p21/atos-gate'; t=o/'source-tree/out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree'
h=lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
report=json.loads((o/'kernel-source-build.json').read_text()); assert report['result']=='OFFICIAL_KERNEL_SOURCE_BUILD_PASS'
built=t/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837'
assert h(built)==report['kernel_sha256']
# The prelink input is native/kernel.macho, not the build-tree copy. Bind both:
# the build report alone does not prove what was prelinked or booted.
for src in sorted((r/'package/BoardRuntime').iterdir()):
 if not src.is_file() or src.suffix not in ('.cpp','.h','.c','.inc'): continue
 dst=t/'kits/cheeseburger/BoardRuntime'/src.name
 assert dst.is_file() and h(dst)==h(src), f'BoardRuntime out of sync: {src.name}'
record=json.loads((t/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/pexpert/DEVELOPMENT/pe_init.o.json').read_text().strip().rstrip(',')); assert 'XNU_DT_PANIC_REGION=1' in record['arguments']
sys.path.insert(0,str(r/'.work/p21-official-framework/kits/XnuPrelink'));import native_prelink as np
b=(o/'native/kernel.macho').read_bytes(); m=np.macho(b); bb=built.read_bytes(); mb=np.macho(bb)
def seg(data,mach,name):
 for s in mach['segments']:
  if s['name']==name: return data[s['fileoff']:s['fileoff']+s['filesize']]
 raise AssertionError(f'missing segment {name}')
# __TEXT holds the Mach-O header/load commands (LINKEDIT offsets differ); the
# executable and data contents must be identical to the verified compilation.
for name in ('__TEXT_EXEC','__DATA_CONST','__DATA','__BOOTDATA'):
 assert seg(b,m,name)==seg(bb,mb,name), f'native differs from build output: {name}'
for s in m['segments']:
 if s['name']=='__TEXT_EXEC':
  for at in range(s['fileoff'],s['fileoff']+s['filesize']-3,4): assert struct.unpack_from('<I',b,at)[0] not in (0xd500419f,0xd500409f)
p=o/'prelink-final/kernel/kernel.macho'; assert h(p) in (o/'prelink.log').read_text()
assert 'panic screen' in (o/'panic-screen.log').read_text()
f=o/'kernel/kernel.macho'; np.macho(f.read_bytes())
gates=sorted(o.glob('gate-*.json'))
if gates:
 g=json.loads(gates[-1].read_text()); assert g['kernel_sha256']==h(f), 'gate pins a different kernel'
 image=Path(g['image'])
 if image.is_file(): assert h(image)==g['image_sha256'], 'gate pins a different image'
out={'compiled_kernel_sha256':report['kernel_sha256'],'native_sha256':h(o/'native/kernel.macho'),'prelink_sha256':h(p),'kernel_sha256':h(f),'checks':'BoardRuntime sync, compilation, native/build code equality, panic region, PAN instructions, prelink hash, entry patch, gate kernel/image pin, Mach-O','booted':False}
print(json.dumps(out,indent=2))
result={'result':'PASS','sha256':h(f),'bytes':f.stat().st_size,'native_sha256':out['native_sha256'],'prelink_sha256':out['prelink_sha256'],'scope':'BoardRuntime sync, compilation, native/build executable+data equality, panic region, PAN scan, prelink hash, entry patch, gate kernel/image pin, Mach-O; device unverified'}
f.with_name('validation.json').write_text(json.dumps(result,indent=2)+'\n')
