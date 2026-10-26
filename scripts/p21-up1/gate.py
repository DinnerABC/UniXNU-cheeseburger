from pathlib import Path
import json,hashlib,tomllib,subprocess
r=Path(__file__).resolve().parents[2];o=r/'out/p21/upstream-a'
obj=r/'out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/kern_exec.o'
assert '_cheeseburger_boot_watchdog_policy' in subprocess.check_output(['/opt/homebrew/opt/llvm/bin/llvm-nm','--undefined-only',str(obj)],text=True)
assert b'[ui-thread-stacks-live-bounded-24x16]' in (o/'native/kernel.macho').read_bytes()
assert json.loads((o/'tests/chrono-check.json').read_text())['result']=='CHRONO_STACK_PARSE_PASS'
assert b'[iokit-service-matching]' in (o/'native/kernel.macho').read_bytes()
assert b'[official-component-reports]' in (o/'native/kernel.macho').read_bytes()
assert b'[display-clients-live]' in (o/'native/kernel.macho').read_bytes()
assert json.loads((o/'tests/layout-check.json').read_text())['result']=='OFFICIAL_V3_DECODER_LARGE_LAYOUT_AND_RING_WRAP_PASS'
rec=json.loads((r/'out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree/build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/subr_log.o.json').read_text().strip().rstrip(','))
assert 'CONFIG_MSG_BSIZE_DEV=1048576' in rec['arguments']
assert json.loads((o/'tests/policy-check.json').read_text())['result']=='UP1_POLICY_SELECTION_AND_PLIST_PASS'
assert b'com.apple.identityservicesd' in (o/'native/kernel.macho').read_bytes() and b'cheeseburger-upstream-userland' in (o/'native/kernel.macho').read_bytes()
m=json.loads((o/'uefi/packaging.json').read_text());assert m['result']=='OFFLINE_PACKAGING_PASS'
image=r/m['image'];assert hashlib.sha256(image.read_bytes()).hexdigest()==m['sha256']
for name,result in [('kernel-source-build.json','OFFICIAL_KERNEL_SOURCE_BUILD_PASS'),('display-input-check.json','DISPLAY_ENTRY_GATE_AND_PACKED_INPUTS_PASS')]:assert json.loads((o/name).read_text())['result']==result
args=tomllib.loads((o/'config/unixnu.toml').read_text())['entry'][0]['args'].split();assert 'offline-mobile-graphics-load=1' in args and 'offline-legacy-register=1' in args
assert (o/'adt/kernel.macho.adt').read_bytes()==(r/'out/p21/r14/adt/kernel.macho.adt').read_bytes()
prior=r/'out/p21/persistent-logs/20261026T074907Z/retrieval.json';v=json.loads(prior.read_text());assert v['result']=='BOUNDED_LOG_WINDOW_READ_PASS' and v['write_errors']==0
assert (r/'out/p21/r14/private/window-backup/window-before.bin').stat().st_size==33554432
base=json.loads((r/'out/p21/r14/offline-gate.json').read_text())
g=dict(result='P21_UP1_BOUNDED_PERSIST_LOG_BOOT_INPUT_PASS',image_bytes=m['bytes'],image_sha256=m['sha256'],expected_owner_sha384=base['expected_owner_sha384'],full_audit=False,scope='R17 identical behavior inputs + IOKit trace + UI stacks + upstream diagnostics + packaging',runtime_log_window_writes_enabled=True,log_window_plan='out/p21/r14/log-window.json',system_partitions_write_enabled=False,GPT_write_enabled=False,previous_runtime_log_preserved=str(prior.relative_to(r)),hardware_fix_verified=False)
(o/'offline-gate.json').write_text(json.dumps(g,indent=2)+'\n');print(json.dumps(g,indent=2))
