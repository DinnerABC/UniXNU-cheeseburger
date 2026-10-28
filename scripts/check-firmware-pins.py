"""VM-side: verify the rebuilt UEFI modules contain this payload's pins, then export fd/dtb."""
from pathlib import Path
import json, shutil, sys

R = Path('/project/UniXNU-cheeseburger')
S = Path('/home/dinnerabc.guest/unixnu-p9/source')
O = R / (sys.argv[1] if len(sys.argv) > 1 else 'out/p23/ios-16.7.16-20H392-logfix3b') / 'uefi'
B = R / 'out/p23/ios-16.7.16-20H392'

payload = json.loads((B / 'payload/manifest.json').read_text())['sha256']
kernel = json.loads((B / 'kernel/validation.json').read_text())['sha256']
adt = json.loads((B / 'adt/validation.json').read_text())['sha256']
pins = {'payload': bytes.fromhex(payload), 'kernel': bytes.fromhex(kernel), 'adt': bytes.fromhex(adt)}
print('pins', {k: v.hex()[:16] for k, v in pins.items()})

hits = {}
build = S / 'workspace/Build/cheeseburger/RELEASE_GCC5'
for p in build.rglob('*'):
    if not p.is_file() or p.suffix not in ('.efi', '.fd', '.bin', '.dll'):
        continue
    data = p.read_bytes()
    for name, pin in pins.items():
        if pin in data:
            hits.setdefault(name, []).append(str(p.relative_to(S)))

fd = build / 'FV/MSM8998PKG_UEFI.fd'
dtb = S / 'Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/DeviceTreeBlob/Android/Android-cheeseburger.dtb'
assert fd.is_file() and dtb.is_file(), (fd, dtb)
O.mkdir(parents=True, exist_ok=True)
shutil.copy2(fd, O / 'MSM8998PKG_UEFI.fd')
shutil.copy2(dtb, O / 'Android-cheeseburger.dtb')
print(json.dumps({'hits': {k: v[:3] for k, v in hits.items()}, 'fd_bytes': fd.stat().st_size}, indent=2))
missing = [k for k in pins if k not in hits]
if missing:
    print('MISSING PINS:', missing)
    sys.exit(2)
