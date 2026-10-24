#!/usr/bin/env python3
"""Exercise the pinned real prelink parser without creating an unsafe image."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys
r=Path(__file__).resolve().parents[1];kit=r/'.work/p4-unixnu/kits/NativePrelink';sys.path.insert(0,str(kit));import native_prelink as np
commit=subprocess.run(['git','-C',str(kit),'rev-parse','HEAD'],capture_output=True,text=True,check=True).stdout.strip();assert commit=='2b6ffb50117eee820b98aca9bd51b5c7dfb2970e'
native=r/'out/p9/kernel/kernel.macho';original=r/'out/p8/ios/iphone-kernelcache.macho';n=native.read_bytes();b=original.read_bytes();ni=np.macho(n)
assert hashlib.sha256(b).hexdigest()==json.loads((r/'out/p8/ios/iphone-abi.json').read_text())['kernel_sha256']
try:np.macho(b);refusal=None
except ValueError as e:refusal=str(e)
assert refusal=='Only ordinary little-endian ARM64 is supported'
prelink=[dict(name=x['name'],bytes=x['filesize']) for x in ni['segments'] if x['name'].startswith('__PRELINK') or x['name'].startswith('__PLK_')]
assert all(x['bytes']==0 for x in prelink)
o=r/'out/p9/security-inputs';o.mkdir(exist_ok=True)
v=dict(result='BLOCKED_COMPATIBLE_SECURITY_PROVIDER_REQUIRED',prelink_commit=commit,native_sha256=hashlib.sha256(n).hexdigest(),original_sha256=hashlib.sha256(b).hexdigest(),native_cpu_subtype=struct.unpack_from('<I',n,8)[0],iphone_cpu_subtype=struct.unpack_from('<I',b,8)[0],actual_pinned_parser_refusal=refusal,native_prelink_segments=prelink,missing_services=['Image4 interface registration','AMFI TrustCache interface registration'],check_removed=False,fake_provider_created=False,kernelcache_mixed=False,boot_image_created=False)
(o/'result.json').write_text(json.dumps(v,indent=2)+'\n');print(json.dumps(v,indent=2))
