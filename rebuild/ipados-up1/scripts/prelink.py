#!/usr/bin/env python3
"""Call the fixed official prelink driver; never merge kexts locally."""
from pathlib import Path
import hashlib,json,os,sys,tomllib
r=Path(__file__).resolve().parents[4];tree=r/'.work/p21-official-framework';sys.path.insert(0,str(tree/'scripts'));sys.path.insert(0,str(tree/'kits/XnuPrelink'))
from unixnu import kernel
import native_prelink as np
native=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else r/'out/p21/upstream-a/native/kernel.macho'
cache=r/'out/p12/inputs/A/kernelcache.macho';source=tomllib.loads((r.parent/'upstream/UniXNU-sheng/board.toml').read_text())['kernel']['prelink']
assert hashlib.sha256(cache.read_bytes()).hexdigest()==source['kernelcache_sha256']
nm=np.macho(native.read_bytes());cm=np.macho(cache.read_bytes())
# Derive layout from complete coalesced ranges, including holes, not device addresses.
old_end=max(x['va']+x['size'] for x in cm['segments'] if x['name'] in ['__PRELINK_TEXT','__PLK_TEXT_EXEC','__PLK_DATA_CONST','__PRELINK_DATA'])
native_begin=min(x['va'] for x in nm['segments'] if x['size'])
slide=-0xc2d000
assert old_end+slide+4096<=native_begin, 'Retain existing P19 absolute storage bindings; fail on overlap'
out=r/'out/p21/upstream-a'/os.environ.get('P14_PRELINK_VARIANT','prelink-final');out.mkdir(parents=True,exist_ok=True)
spec=dict(tool='.work/p21-official-framework/kits/XnuPrelink/emit_prelink.py',kernel=str(native.relative_to(r)),kernelcache=str(cache.relative_to(r)),kernelcache_sha256=source['kernelcache_sha256'],slide=hex(slide),roots=['com.apple.security.AppleImage4','com.apple.driver.AppleMobileFileIntegrity','com.apple.kext.CoreTrust','com.apple.kec.pthread','com.apple.filesystems.apfs','com.apple.filesystems.hfs.kext','com.apple.AppleFSCompression.AppleFSCompressionTypeZlib','com.apple.security.sandbox','com.apple.kext.AppleMatch','com.apple.iokit.IOSurface','com.apple.iokit.IOMobileGraphicsFamily','com.apple.iokit.IOHIDFamily','com.apple.iokit.IOHIDEventDriver'],early_security_init=source['early_security_init'])
assert spec['early_security_init'].startswith('com.apple.driver.AppleMobileFileIntegrity=')
(out/'spec.json').write_text(json.dumps(dict(spec=spec,layout_source='Actual native and donor Mach-O coalesced segment boundaries',initializer_source='Pinned upstream declaration, accepted only after exact donor hash equality',original_coalesced_end=hex(old_end),native_first_segment=hex(native_begin)),indent=2)+'\n')
class Context:
 tree=r
 work=out
 logs=out/'logs'
 board={'kernel':{'prelink':spec}}
 def option(self,*args):return None
Context.logs.mkdir(parents=True,exist_ok=True)
result=kernel.prelink(Context());print(result)
