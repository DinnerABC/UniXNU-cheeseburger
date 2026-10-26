from pathlib import Path
import json,sys,tomllib,hashlib
r=Path(__file__).resolve().parents[2];o=r/'out/p21/upstream-a';sys.path.insert(0,str(r/'tools/ram_payload'));import bundle;bundle.MAX_BYTES=256*1024*1024
raw=(o/'payload/payload.bundle').read_bytes();files=bundle.verify(raw);cfg=tomllib.loads(files['\\EFI\\UniXNU\\unixnu.toml'].decode());args=cfg['entry'][0]['args'].split()
for key in ['offline-mobile-graphics-load=1','offline-legacy-register=1','cheeseburger-disable-watchdogd=1']:assert key in args
assert 'p21-iokit-trace=1' in args and 'kextlog=0xfff' in args
assert 'offline-legacy-cpu-present=1' in args
assert not any(a in args for a in ['sheng-fb-gpu=1','sheng-gpu-addon=1','sheng-gpu-metal=1'])
assert 'sheng-aks=1' in args and 'offline-key-events=1' in args
baseline=tomllib.loads((r/'out/p21/r17/config/unixnu.toml').read_text())['entry'][0]['args'].split()
assert 'cheeseburger-upstream-userland=1' in args
assert [a for a in args if a not in ['p21-iokit-trace=1','kextlog=0xfff','cheeseburger-upstream-userland=1']]==baseline
assert files['\\scratch-data.img']==(r/'out/p21/r17/data-seed/owner-data-1g.dbr').read_bytes()
assert hashlib.sha256(files['\\scratch-data.img']).hexdigest()=='f1a23b6ba8e1a5ed13e1b3e36c3666e5eb76c3a2d54ac1e1803ff2b1e52a06c4'
assert len(cfg['entry'][0]['args'])<1024
for name,p in [('\\kernel.macho','kernel/kernel.macho'),('\\kernel.macho.adt','adt/kernel.macho.adt')]:assert files[name]==(o/p).read_bytes()
v={'result':'DISPLAY_ENTRY_GATE_AND_PACKED_INPUTS_PASS','watchdog_policy_enabled':True,'payload_sha256':hashlib.sha256(raw).hexdigest(),'files':bundle.manifest(raw)};(o/'display-input-check.json').write_text(json.dumps(v,indent=2)+'\n');print(v['result'])
