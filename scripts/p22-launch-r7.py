#!/usr/bin/env python3
"""Boot the user-authorized Normal NC candidate once after packaging succeeds."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'out/p22/r7'
for _ in range(1200):
    if (OUT / 'uefi/packaging.json').exists():
        break
    log = OUT / 'finish-build.log'
    if log.exists() and 'Traceback (most recent call last)' in log.read_text():
        raise SystemExit('Packaging failed; no boot')
    time.sleep(2)
else:
    raise SystemExit('Packaging incomplete; no boot')

manifest = json.loads((OUT / 'uefi/packaging.json').read_text())
gate = json.loads((ROOT / 'out/p22/r6/offline-gate.json').read_text())
gate.update(
    result='P22_R7_NORMAL_NC_READY', image=manifest['image'],
    image_bytes=manifest['bytes'], image_sha256=manifest['sha256'],
    kernel_sha256=hashlib.sha256((OUT / 'kernel/kernel.macho').read_bytes()).hexdigest(),
    native_kernel_sha256=hashlib.sha256((OUT / 'native/kernel.macho').read_bytes()).hexdigest(),
    previous_runtime_log_preserved='out/p21/persistent-logs/20261026T180059Z/retrieval.json',
    scope='R6 eight-core PSCI ADT retained; Normal NC framebuffer aliases',
    smp_hardware_verified=True, framebuffer_normal_nc_hardware_verified=False,
    full_audit=False)
(OUT / 'offline-gate.json').write_text(json.dumps(gate, indent=2) + '\n')
(OUT / 'log-window.json').write_bytes((ROOT / 'out/p22/r6/log-window.json').read_bytes())
with (OUT / 'boot-once.log').open('w') as log:
    subprocess.run([sys.executable, str(OUT / 'scripts/boot-once.py')],
                   cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
print((OUT / 'boot-result.json').read_text())
