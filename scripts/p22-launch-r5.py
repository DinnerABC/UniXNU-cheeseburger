#!/usr/bin/env python3
"""Finish the bounded SMP candidate and perform the user-authorized single boot."""
from pathlib import Path
import json,hashlib,sys,time,subprocess
R=Path(__file__).resolve().parents[1];O=R/'out/p22/r5'
for _ in range(600):
 try:m=json.loads((O/'uefi/packaging.json').read_text());break
 except (FileNotFoundError,json.JSONDecodeError):time.sleep(2)
else:raise SystemExit('Packaging incomplete; no device action')
g=json.loads((R/'out/p22/r4/offline-gate.json').read_text());g.update(result='P22_R5_GENERIC_SMP_REPACK_READY',image=m['image'],image_bytes=m['bytes'],image_sha256=m['sha256'],kernel_sha256=hashlib.sha256((O/'kernel/kernel.macho').read_bytes()).hexdigest(),native_kernel_sha256=hashlib.sha256((O/'native/kernel.macho').read_bytes()).hexdigest(),previous_runtime_log_preserved='out/p21/persistent-logs/20261026T164846Z/retrieval.json',scope='generic SMP domain + native16bit reset affinity, eight cores and1GiB; bounded source review/compile/pack only',full_audit=False,smp_scheduler_domains=1,smp_hardware_verified=False)
(O/'offline-gate.json').write_text(json.dumps(g,indent=2)+'\n')
p=O/'scripts/boot-once.py';s=p.read_text().replace('P22_R4_USER_REQUESTED_MEMORY_ONLY_REPACK_READY','P22_R5_GENERIC_SMP_REPACK_READY').replace('p22-r4-1g-once','p22-r5-native-smp-once');p.write_text(s)
with (O/'boot-once.log').open('w') as f:subprocess.run([sys.executable,str(p)],cwd=R,stdout=f,stderr=subprocess.STDOUT,check=True)
print((O/'boot-result.json').read_text())
