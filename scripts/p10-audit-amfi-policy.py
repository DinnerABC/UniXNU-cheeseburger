#!/usr/bin/env python3
"""Check the actual AMFI failure instructions and the one-property ADT fix."""
from pathlib import Path
import hashlib
import json
import struct
import sys

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / '.work/p4-unixnu/kits/NativePrelink'))
sys.path.insert(0, str(root / 'tools/adt'))
import native_prelink as np
from adt import decode

kernel = (root / 'out/p10/milestones/v4-security-pass-pthread-panic/out/p10/kernel/kernel.macho').read_bytes()
assert hashlib.sha256(kernel).hexdigest() == '569c9cf25dd7e2f1ec83b06263dc5d900dee4b45f222d691ccd6a9eb5a0ebf03'
m = np.macho(kernel)
plan = json.loads((root / 'out/p10/milestones/v4-security-pass-pthread-panic/out/p10/kernel/prelink-report.json').read_text())

def read(va, size):
    at = np.backed_offset(m['segments'], va, size, len(kernel))
    return kernel[at:at + size]

assert read(0xfffffff0046b2f96, 24).split(b'\0')[0] == b'amfi-only-platform-code'
message = read(0xfffffff0046b2fae, 80).split(b'\0')[0].decode()
assert 'AMFI: no amfi-only-platform-code device tree node!' in message
# Real failure call at caller-4, not a guessed symbol from the OCR.
assert struct.unpack('<I', read(0xfffffff004e9c880, 4))[0] == 0x940002b8
# Real code requires four bytes, then reads the policy as uint32.
assert struct.unpack('<I', read(0xfffffff004e87c54, 4))[0] == 0x7100101f
assert struct.unpack('<I', read(0xfffffff004e87c6c, 4))[0] == 0xb9400008
thunk = 0xfffffff004e9d360
a, load, branch = struct.unpack('<III', read(thunk, 12))
assert branch == 0xd61f0200
imm = ((a >> 5) & 0x7ffff) << 2 | ((a >> 29) & 3)
if imm & (1 << 20):
    imm -= 1 << 21
slot = (thunk & ~4095) + (imm << 12) + ((load >> 10) & 4095) * 8
binding = next(x for x in plan['bindings'] if x['new_site'] == slot)
assert '_panic' in np.exports(kernel, m).get(binding['new_target'], [])

old_path = root / 'out/p10/milestones/v3-amfi-policy-panic/out/p10/adt/kernel.macho.adt'
new_path = root / 'out/p10/adt/kernel.macho.adt'
old, new = decode(old_path.read_bytes()), decode(new_path.read_bytes())
chosen = next(x for x in new['children'] if x['name'] == 'chosen')
value = chosen['properties'].pop('amfi-only-platform-code')
assert value == struct.pack('<I', 1)
assert old == new, 'Unexpected hardware, identity, or other ADT change'
report = dict(result='PASS', module='com.apple.driver.AppleMobileFileIntegrity',
              panic_format=message, caller='0xfffffff004e9c884',
              caller_source='OCR corroborated by third LR and real panic call',
              old_property='absent', new_property_hex=value.hex(),
              property_size=4, value=1, policy='platform-only retained',
              only_change='/chosen/amfi-only-platform-code',
              kernel_unchanged=True, new_adt_sha256=hashlib.sha256(new_path.read_bytes()).hexdigest(),
              runtime_fix_verified=False, binding=binding)
(root / 'out/p10/fatal-analysis/amfi-policy-root-cause.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS: original AMFI panic binding and uint32 requirement; only platform-policy property added')
