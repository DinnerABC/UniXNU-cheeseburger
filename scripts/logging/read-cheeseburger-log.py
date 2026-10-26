#!/usr/bin/env python3
"""Read the pinned log window from root Recovery. Never writes phone storage."""
from pathlib import Path
import sys,json,subprocess,hashlib,datetime,struct,runpy
R=Path(__file__).resolve().parents[2];plan=json.loads((R/'out/p21/r14/log-window.json').read_text());sys.path.insert(0,str(R/'out/p21/r14/scripts'));from device import Device
d=Device('persistent-log-read');d.require_root_recovery();assert d.shell('getprop ro.product.device')=='cheeseburger'
assert d.shell('readlink -f /dev/block/bootdevice/by-name/userdata')==plan['partition'];assert int(d.shell('blockdev --getsize64 /dev/block/sda14'))==47938797568
assert int(d.shell('cat /sys/class/block/sda14/start'))*512==plan['userdata_first_lba']*4096
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ');out=R/'out/p21/persistent-logs'/stamp;out.mkdir(parents=True,exist_ok=False,mode=0o700)
cmd=[str(R/'.work/p3-tools-35/platform-tools/adb'),'-s',d.serial,'exec-out',f"dd if={plan['partition']} bs=4096 skip={plan['offset_bytes']//4096} count={plan['blocks']} 2>/tmp/p21-read-log-stderr"]
v=subprocess.run(cmd,env=d.env,capture_output=True,timeout=120);raw=v.stdout;(out/'window.bin').write_bytes(raw);assert v.returncode==0 and len(raw)==plan['bytes'],(v.returncode,len(raw))
report={'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'partition_reads_only':True,'receipt_dir':str(d.out.relative_to(R))}
if raw[:8]!=b'SHLOG001':
 report['result']='NO_VALID_COMMITTED_LOG_HEADER';(out/'retrieval.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(2)
H=struct.Struct('<8sIIQQ8I6I32s');h=H.unpack_from(raw);assert h[1]==3 and 140<=h[2]<=4096 and h[4]==plan['absolute_first_lba'] and h[5]==plan['blocks'];assert (h[6],h[7],h[9],h[10]) in [(1,32,33,8),(1,256,257,32)]
assert h[8]<=h[7]*4096 and 0<h[11]<=(plan['blocks']-h[9])//h[10] and h[9]+h[10]*h[11]<=plan['blocks'];assert h[19].split(b'\0')[0]==b'cheeseburger-P21';assert struct.unpack_from('<5I',raw,H.size)==(0,0,0,0,0)
report.update(msg_blocks=h[7],slot_blocks=h[10],slot_count=h[11],slots_written=h[12],ring_overwritten=max(0,h[12]-h[11]),result='BOUNDED_LOG_WINDOW_READ_PASS',boot_id=hex(h[3]),rounds=h[14],write_errors=h[15],state=h[18],tag='cheeseburger-P21')
decoder=R.parent/'upstream/UniXNU-sheng/scripts/read-sheng-log.py';subprocess.run([sys.executable,str(decoder),'--decode',str(out/'window.bin'),'--out',str(out/'decoded')],check=True)
(out/'retrieval.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));print('Saved:',out)
subprocess.run([sys.executable,str(R/'scripts/logging/analyze-cheeseburger-run.py'),str(out)],check=True)
