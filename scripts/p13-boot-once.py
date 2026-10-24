#!/usr/bin/env python3
"""One RAM boot after successful userdata write. Transport OKAY is not an XNU proof."""
from pathlib import Path
import hashlib,json,re,runpy,struct,sys,zlib
r=Path(__file__).resolve().parents[1];sys.path.insert(0,str(r/'tools/p13'));from device import Device
out=r/'out/p13';f=json.loads((out/'flash-result.json').read_text());assert f['state']=='FASTBOOT_FLASH_SUCCEEDED' and f['attempts']==1
state=out/'boot-result.json';assert not state.exists(),'No automatic identical-image repeat'
g=json.loads((out/'preflight.json').read_text());image=r/g['boot_image'];b=image.read_bytes();assert hashlib.sha256(b).hexdigest()==g['boot_sha256']=='26d57d0db06d0d2830a9757e7ec55f382c1b89349045965f5b4b2480005c5f1f'
assert b[:8]==b'ANDROID!';ks,ka,rs,ra,ss,sa,ta,page,ver,_=struct.unpack_from('<10I',b,8);assert (ka,ra,ta,page,ver,ss)==(0x8000,0x1000000,0x100,4096,0,0)
dc=zlib.decompressobj(31);fd=dc.decompress(b[page:page+ks])+dc.flush();assert hashlib.sha256(fd).hexdigest()=='a53667e7053260d629a9e84477eb128a18e77e82bce5920229e07859265a5cea'
parse=runpy.run_path(str(r/'scripts/p3-dtb-inspect.py'))['parse'];actual=parse((r/'out/p3/private/recovery-device.dtb').read_bytes())['/'];candidate=parse(dc.unused_data)['/']
for key in ['model','compatible','qcom,msm-id','qcom,board-id']:assert candidate[key]==actual[key],key
d=Device('boot-once');assert re.search(r'product:\s*msm8998',d.run('fastboot',['getvar','product']));assert re.search(r'unlocked:\s*yes',d.run('fastboot',['getvar','unlocked']))
record={'state':'ATTEMPT_STARTED','attempts':1,'image':g['boot_image'],'image_sha256':g['boot_sha256'],'transport_success':False,'root_mount_proven':False,'launchd_proven':False,'run_dir':str(d.out.relative_to(r))};state.write_text(json.dumps(record,indent=2)+'\n')
try:
 text=d.run('fastboot',['boot',str(image)],timeout=180);assert re.search(r'Booting.*OKAY',text,re.S);record.update(state='FASTBOOT_BOOT_ACCEPTED_SCREEN_PENDING',transport_success=True)
except Exception as e:record.update(state='BOOT_FAILED_STOP_NO_RETRY',error=str(e));raise
finally:state.write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2))
