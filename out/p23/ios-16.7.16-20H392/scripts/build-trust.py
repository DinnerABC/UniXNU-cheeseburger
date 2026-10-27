#!/usr/bin/env python3
"""Build and validate the iOS 16.7.11 original static+System trust bundle.

The IPSW trust caches are IM4P-wrapped; the payload is the v2 module the kernel/AMFI
and DarwinBoot consume.  This creates the same two-module bundle shape as the
current 17.7.11 payload input, without adding an owner module or changing Apple data.
"""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys

R = Path(__file__).resolve().parents[4]
BASE = R / 'out/p23/ios-16.7.16-20H392'
OUT = BASE / 'trust'
sys.path.insert(0, str(R / '.work/p21-official-framework/scripts'))
from unixnu.ipsw import im4p_payload

MODULES = [
    ('StaticTrustCache', 'Firmware/087-86698-044.dmg.trustcache'),
    ('Cryptex1,SystemTrustCache', 'Firmware/087-86593-047.dmg.trustcache'),
]


def module(path):
    wrapped = (BASE / 'extracted' / path).read_bytes()
    return im4p_payload(wrapped)


def validate_module(data):
    if len(data) < 24 or struct.unpack_from('<I', data)[0] != 2:
        raise SystemExit('not a v2 trust module')
    entries = struct.unpack_from('<I', data, 20)[0]
    if len(data) != 24 + entries * 24:
        raise SystemExit(f'module length {len(data)} != 24 + {entries}*24')
    previous = None
    for index in range(entries):
        at = 24 + index * 24
        cdhash = data[at:at + 20]
        hash_type = data[at + 20]
        if not 1 <= hash_type <= 4:
            raise SystemExit(f'entry {index}: bad hash type {hash_type}')
        if previous is not None and previous >= cdhash:
            raise SystemExit(f'entry {index}: not strictly ascending')
        previous = cdhash
    return entries


modules = [module(path) for _, path in MODULES]
counts = [validate_module(m) for m in modules]
count = len(modules)
starts, cursor = [], 4 + 4 * count
for m in modules:
    starts.append(cursor)
    cursor += len(m)
bundle = struct.pack('<I', count) + b''.join(struct.pack('<I', s) for s in starts) + b''.join(modules)

# Loader-equivalent structure check (DarwinBoot ValidTrustCaches / kernel parser).
if struct.unpack_from('<I', bundle)[0] != count:
    raise SystemExit('bundle count')
for index in range(count):
    start = struct.unpack_from('<I', bundle, 4 + 4 * index)[0]
    end = struct.unpack_from('<I', bundle, 8 + 4 * index)[0] if index + 1 < count else len(bundle)
    if start < 4 + 4 * count or start & 3 or end > len(bundle) or end - start < 24:
        raise SystemExit('bundle offset/length')
    validate_module(bundle[start:end])

# The original binaries must be covered by the static module.
coverage = {}

OUT.mkdir(parents=True, exist_ok=True)
target = OUT / 'ios-16.7.16-20H392-static-system.bundle'
if target.exists():
    raise SystemExit('refuse to overwrite ' + str(target))
target.write_bytes(bundle)
report = {
    'result': 'IOS16_STATIC_SYSTEM_TRUST_BUNDLE_BUILT',
    'bundle': str(target.relative_to(R)),
    'bytes': len(bundle),
    'sha256': hashlib.sha256(bundle).hexdigest(),
    'modules': [
        {'component': name, 'source': path,
         'module_bytes': len(m), 'entries': n,
         'im4p_sha256': hashlib.sha256((BASE / 'extracted' / path).read_bytes()).hexdigest()}
        for (name, path), m, n in zip(MODULES, modules, counts)],
    'static_coverage_checked': False,
    'apple_personalization_verified': False,
    'phone_operations': [],
}
(OUT / 'ios-16.7.16-20H392-static-system.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
