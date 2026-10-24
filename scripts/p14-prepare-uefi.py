from pathlib import Path
import difflib,hashlib,json,shutil
r=Path(__file__).resolve().parents[1];s=Path('/home/dinnerabc.guest/unixnu-p9/source');assert (s/'.p14-overlay-marker').exists();out=r/'out/p14/uefi-source';out.mkdir(exist_ok=True);changes=[]
for rel,source in [('P5Integration/RamPayload/CompressedPayload.h','CompressedPayload.h'),('P9Integration/Probe/P9Expected.h','Expected.h'),('DarwinBootPkg/Driver/P9Expected.h','Expected.h')]:
 p=s/rel;old=p.read_text();new=(r/'config/p14'/source).read_text();p.write_text(new);changes.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+rel,tofile='b/'+rel));d=out/rel;d.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,d)
(r/'patches/p14/uefi-pins.patch').write_text(''.join(changes));(out/'manifest.json').write_text(json.dumps({str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file() and p.name!='manifest.json'},indent=2)+'\n');print('P14 UEFI identity pins only; compression loader code unchanged')
