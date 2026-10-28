#!/usr/bin/env python3
"""Call the fixed official prelink driver; never merge kexts locally."""
from pathlib import Path
import hashlib,json,os,sys,tomllib
r=Path(__file__).resolve().parents[4];tree=r/'.work/p21-official-framework';sys.path.insert(0,str(tree/'scripts'));sys.path.insert(0,str(tree/'kits/XnuPrelink'))
from unixnu import kernel
import native_prelink as np
native=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else r/'out/p23/ios-16.7.16-20H392/native/kernel.macho'
cache=r/'out/p23/ios-16.7.16-20H392/analysis/root-trust-data/kernelcache/kernelcache.thin.macho';source={'kernelcache_sha256':'2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221','early_security_init':'com.apple.driver.AppleMobileFileIntegrity=0xfffffff005703c54'}
assert hashlib.sha256(cache.read_bytes()).hexdigest()==source['kernelcache_sha256']
nm=np.macho(native.read_bytes());cm=np.macho(cache.read_bytes())
# Derive layout from complete coalesced ranges, including holes, not device addresses.
old_end=max(x['va']+x['size'] for x in cm['segments'] if x['name'] in ['__PRELINK_TEXT','__PLK_TEXT_EXEC','__PLK_DATA_CONST','__PRELINK_DATA'])
native_begin=min(x['va'] for x in nm['segments'] if x['size'])
slide=-0xbc0000
assert old_end+slide+4096<=native_begin, 'Retain existing P19 absolute storage bindings; fail on overlap'
out=r/'out/p23/ios-16.7.16-20H392'/os.environ.get('P14_PRELINK_VARIANT','prelink-final');out.mkdir(parents=True,exist_ok=True)
spec=dict(tool='out/p23/ios-16.7.16-20H392/tools/prelink/emit_prelink.py',kernel=str(native.relative_to(r)),kernelcache=str(cache.relative_to(r)),kernelcache_sha256=source['kernelcache_sha256'],slide=hex(slide),roots=['com.apple.security.AppleImage4','com.apple.driver.AppleMobileFileIntegrity','com.apple.kext.CoreTrust','com.apple.kec.pthread','com.apple.filesystems.apfs','com.apple.filesystems.hfs.kext','com.apple.AppleFSCompression.AppleFSCompressionTypeZlib','com.apple.security.sandbox','com.apple.kext.AppleMatch','com.apple.iokit.IOSurface','com.apple.iokit.IOMobileGraphicsFamily','com.apple.iokit.IOHIDFamily','com.apple.iokit.IOHIDEventDriver','com.apple.driver.AppleSEPKeyStore','com.apple.driver.AppleSEPCredentialManager'],early_security_init=source['early_security_init'])
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
