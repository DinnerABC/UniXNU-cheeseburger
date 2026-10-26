#!/usr/bin/env python3
"""Check this preparation phase; deliberately does not grant a boot gate."""
from pathlib import Path
import hashlib,json,sys,tomllib,stat
R=Path(__file__).resolve().parents[1];O=R/'out/p21/official-adaptation';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
board=tomllib.loads((R/'board.toml').read_text());assert board['schema_version']==2 and board['board']['example'] is True
assert board['board']['soc']=='MSM8998' and board['board']['os_profile']=='ipados-17.7.11-21H461'
a=json.loads((O/'adt/validation.json').read_text());assert sha(O/'adt/kernel.macho.adt')==a['sha256'];assert a['hardware_nodes_byte_semantically_unchanged'] and not a['boot_ready']
d=json.loads((O/'data-seed/validation.json').read_text());assert d['result']=='OFFICIAL_SKIP_SETUP_READONLY_REMOUNT_PASS' and d['fsck_apfs_exit']==0
assert sha(O/'data-seed/owner-data-1g.dbr')==d['output']['encoded_sha256']
assert d['skip_setup'] and d['no_auto_dim'] and d['phone_operations']==[]
for v in d['verified_preferences'].values():assert (v['uid'],v['gid'],v['mode'])==(501,501,'0600')
assert sha(R/'out/p20/r8/data-seed/owner-data-1g.dbr')=='0451a8ca7c3914112f3379549939512b9c735312202aa1ef59b82815554772e0'
p=json.loads((O/'pmgr/build-report.json').read_text());assert p['virtual_entries']==340 and sha(R/'drivers/CheeseburgerPlatformPower/CheeseburgerPlatformPower.cpp')==p['source_sha256']
assert sha(O/'pmgr/libCheeseburgerPlatformPower.a')==p['archive_sha256']
s=json.loads((O/'official-source-preparation.json').read_text());assert s['patch_count']==56 and s['board_guard'] and not s['compiled']
assert sha(R/'.work/p21-official-framework/patches/xnu/zzz-boot-progress.patch')==s['framework_local_adaptation']['adapted_sha256']
assert (R/'unixnu.lock').is_file() and '13 structured areas checked' in (O/'porting-record-check.log').read_text()
assert sha(R/'out/p21/r7/uefi/boot-p21-r7.img')=='b135b5f8b39f44f44d464a0a3b3afd69567d752ba454c87627a0fa9038511029'
assert stat.S_IMODE((O/'private/platform-uuid-seed.hex').stat().st_mode)==0o600
result={'result':'OFFICIAL_PREPARATION_PHASE_PASS','official_framework':'16e592abee8a550dec8154ed1f38b5ebc4f16213','explicit_framework_patch':s['framework_local_adaptation']['patch'],'phase':'configuration, source stack, OS identity, local Data and PMGR preparation','full_migration_complete':False,'kernel_compiled':False,'boot_ready':False,'phone_operations':[],'R7_image_preserved':True,'remaining':['board archive recipes and source overlays','central DT binding/handoff adaptation','firmware/payload/bootimg entry adaptation','final candidate ABI and original-code checks']}
(O/'preparation-check.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
