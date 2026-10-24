#!/usr/bin/env python3
"""Reuse pinned original-system-trust bundler with cached range-extracted inputs."""
from pathlib import Path
import hashlib,json,sys,difflib
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'.work/p4-unixnu/scripts'));from unixnu.ipsw import im4p_payload
p=r/'out/p10/inputs/A';meta=json.loads((p/'candidate.json').read_text());assert meta['kernelcache_sha256']=='f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7'
static=im4p_payload((r/meta['components']['StaticTrustCache']['file']).read_bytes());o=r/'out/p10/trust';o.mkdir(exist_ok=True);(o/'static-trustcache.bin').write_bytes(static)
original=r/'.work/p4-unixnu/scripts/build-original-system-trust.py';s=original.read_text();a=s.index('with zipfile.ZipFile(');b=s.index('tag, sequence, end = der(original, 0)',a)
body=s[:a]+"original = (ROOT / "+repr(meta['components']['Cryptex1,SystemTrustCache']['file'])+").read_bytes()\n"+s[b:]
body=body.replace('ROOT = Path(__file__).resolve().parents[1]','ROOT = Path('+repr(str(r))+')').replace("ROOT / '.cache/firmware/target-volumes/static-trustcache.bin'","ROOT / 'out/p10/trust/static-trustcache.bin'").replace("ROOT / 'build/original-static-system-trust'","ROOT / 'out/p10/trust'")
(o/'adapted-original-bundler.py').write_text(body);(r/'patches/p10/trust-cache-input-adapter.patch').write_text(''.join(difflib.unified_diff(s.splitlines(True),body.splitlines(True),fromfile='a/build-original-system-trust.py',tofile='b/cached-range-inputs.py')))
exec(compile(body,str(o/'adapted-original-bundler.py'),'exec'),{'__name__':'__main__','__file__':str(original)})
(o/'provenance.json').write_text(json.dumps(dict(official_source=str(original.relative_to(r)),official_script_sha256=hashlib.sha256(s.encode()).hexdigest(),transport_only_adaptation=True,same_donor_build=meta['build'],original_static_im4p=meta['components']['StaticTrustCache'],original_system_im4p=meta['components']['Cryptex1,SystemTrustCache'],no_own_entries_added=True,iphone_root_compatible_claim=False),indent=2)+'\n')
