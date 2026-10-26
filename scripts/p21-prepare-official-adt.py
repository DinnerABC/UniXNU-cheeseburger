#!/usr/bin/env python3
"""Apply official typed OS properties to the qualified board hardware ADT.

Format-1 transition supported by docs/porting/device-tree.md. This does not
invent a new ADT generator or change hardware resources. Full format-2 DTS
conversion is a separate step and is not declared complete by this command.
"""
from pathlib import Path
import hashlib, json, os, runpy, secrets, struct, sys, tomllib
ROOT=Path(__file__).resolve().parents[1]
UP=ROOT/'.work/p21-official-framework'
OUT=ROOT/'out/p21/official-adaptation'
REV='16e592abee8a550dec8154ed1f38b5ebc4f16213'
sys.path.insert(0,str(UP/'scripts'))
from unixnu.devicetree import node, typed_properties
from unixnu.profiles import merge
sys.path.insert(0,str(ROOT/'tools/adt'))
from adt import decode
import subprocess
assert subprocess.check_output(['git','-C',str(UP),'rev-parse','HEAD'],text=True).strip()==REV
BASE=ROOT/'out/p21/r7/adt/kernel.macho.adt'
source=BASE.read_bytes()
assert hashlib.sha256(source).hexdigest()=='888bcccd18b642e80510390e0e381f899468b20d0c0ce8511aac2fb17d006183'
tree=decode(source)
profile=tomllib.loads((UP/'profiles/os/ipados-17.7.11-21H461/profile.toml').read_text())
board=tomllib.loads((ROOT/'config/p21-official/board.toml').read_text())
config=merge(profile,board)
# Recorded hardware platform ID, not Apple's donor chip ID and not a guessed ID.
fdt=ROOT/'out/p3/private/recovery-device.dtb'
assert hashlib.sha256(fdt.read_bytes()).hexdigest()=='743b9d0fee2a7aedb9db32dc57ddfb8197378b78ea003d8c653f492b7e5a2448'
platform=runpy.run_path(str(ROOT/'scripts/p3-dtb-inspect.py'))['parse'](fdt.read_bytes())['/']
chip,revision=struct.unpack('>II',platform['qcom,msm-id'])
assert chip==int(config['payload']['adt']['chip_id'],16)==0x124
private=OUT/'private';private.mkdir(parents=True,exist_ok=True);os.chmod(private,0o700)
seedfile=private/'platform-uuid-seed.hex'
if not seedfile.exists():
 with seedfile.open('x') as f:f.write(f'{secrets.randbelow((1<<64)-1)+1:016x}\n')
 os.chmod(seedfile,0o600)
seed=int(seedfile.read_text().strip(),16);assert 0<seed<1<<64
old=decode(source)
for target,section in [('device-tree','root'),('chosen','chosen'),('defaults','defaults'),('options','options'),('product','product'),('buttons','buttons')]:
 n=tree if target=='device-tree' else next((c for c in tree['children'] if c['name']==target),None)
 if n is None:
  n={'name':target,'properties':{'name':target.encode()+b'\0'},'children':[]};tree['children'].append(n)
 n['properties'].update(dict(typed_properties(config.get('adt',{}).get(section,{}))))
chosen=next(c for c in tree['children'] if c['name']=='chosen')
chosen['properties'].update({'random-seed':os.urandom(64),'unique-chip-id':struct.pack('<Q',seed),'chip-id':struct.pack('<I',chip)})
# Signed-text fixes stay out until the matching kernel consumer and original
# userland bytes are qualified. A separate report records this incomplete step.
assert 'text-fixups' not in chosen['properties']
def emit(n):return node(n['name'],[(k,v) for k,v in n['properties'].items() if k!='name'],[emit(c) for c in n['children']])
raw=emit(tree);assert decode(raw)==tree
for name in ['cpus','psci','memory','reserved-memory','arm-io']:
 assert next(c for c in old['children'] if c['name']==name)==next(c for c in tree['children'] if c['name']==name),name
oldchosen=next(c for c in old['children'] if c['name']=='chosen')
assert oldchosen['children']==chosen['children']
for k in ['root-matching','dram-base','dram-size','darwinboot-memory-size','ephemeral-storage','nvram-proxy-data']:
 assert oldchosen['properties'][k]==chosen['properties'][k],k
out=OUT/'adt';out.mkdir(parents=True,exist_ok=True);(out/'kernel.macho.adt').write_bytes(raw)
report={'result':'OFFICIAL_OS_PROPERTIES_BOARD_HARDWARE_PRESERVED','framework_revision':REV,'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'hardware_nodes_byte_semantically_unchanged':True,'root_uuid_and_memory_map_unchanged':True,'UUID_seed':'private stable software identity, not hardware ECID','chip_id_source':'captured qcom,msm-id first cell 0x124','model':'iPad7,1','target_type':'J120','board_marker':'oneplus,cheeseburger','home_behavior':'legacy Home event/chord, no touchscreen gestures claimed','signed_text_fixups':'pending kernel and original-byte qualification','kernel_power_matching_rebuild_required':True,'boot_ready':False}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
