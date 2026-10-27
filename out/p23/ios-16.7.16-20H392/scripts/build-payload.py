#!/usr/bin/env python3
"""Assemble the P23 payload from the new kernel/ADT, iOS16 trust bundle and owned Data seed."""
from pathlib import Path
import hashlib
import json
import lzma
import struct
import sys

R = Path(__file__).resolve().parents[4]
B = R / 'out/p23/ios-16.7.16-20H392'
O = B / 'payload'
O.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(R / 'tools/ram_payload'))
import bundle  # noqa: E402
bundle.MAX_BYTES = 256 * 1024 * 1024

files = {}
definitions = []
inputs = [
    ('\\kernel.macho', B / 'kernel/kernel.macho', 'PASS'),
    ('\\kernel.macho.adt', B / 'adt/kernel.macho.adt', 'STAGE_C_D22AP_ADT_IDENTITY_PASS'),
]
for index, (name, path, result) in enumerate(inputs):
    data = path.read_bytes()
    validation = json.loads(path.with_name('validation.json').read_text())
    assert validation['result'] == result, (path, validation['result'])
    assert hashlib.sha256(data).hexdigest() == validation['sha256'], path
    files[name] = data
    tag = 'Kernel' if index == 0 else 'Adt'
    definitions.append('STATIC CONST UINT8 P9' + tag + 'Hash[32] = {' +
                       ','.join('0x' + hashlib.sha256(data).hexdigest()[j:j + 2]
                                for j in range(0, 64, 2)) + '};')
    definitions.append('#define P9' + tag + 'Bytes ' + str(len(data)))

config = (B / 'config/unixnu.toml').read_bytes()
assert b'P12 OFFLINE REFERENCE' not in config
files['\\EFI\\UniXNU\\unixnu.toml'] = config

seed = B / 'data-seed/skip-setup-1g-owned.dbr'
seed_report = json.loads((B / 'data-seed/skip-setup-1g-owned.json').read_text())
assert seed_report['result'] == 'IOS16_SEED_OWNERSHIP_COMPLETE'
assert seed_report['apply_errors'] == [] and seed_report['metadata_mismatch'] == []
assert seed_report['volume_uuid'] == 'D8A691FB-D618-46C5-A5D2-84692FDBFE44'
assert hashlib.sha256(seed.read_bytes()).hexdigest() == seed_report['output']['sha256']
files['\\scratch-data.img'] = seed.read_bytes()
files['\\EFI\\UniXNU\\unixnu.toml'] = files['\\EFI\\UniXNU\\unixnu.toml'].replace(
    b'path = "/kernel.macho"', b'path = "/kernel.macho"\ninitrd = "/scratch-data.img"')

trust = B / 'trust/ios-16.7.16-20H392-static-system.bundle'
trust_report = json.loads((B / 'trust/ios-16.7.16-20H392-static-system.json').read_text())
assert trust_report['result'] == 'IOS16_STATIC_SYSTEM_TRUST_BUNDLE_BUILT'
assert hashlib.sha256(trust.read_bytes()).hexdigest() == trust_report['sha256']
files['\\kernel.macho.trustcache'] = trust.read_bytes()

raw = bundle.pack(files)
sha = hashlib.sha256(raw).digest()
(O / 'payload.bundle').write_bytes(raw)
packed = lzma.compress(raw, format=lzma.FORMAT_ALONE, preset=6)
packed = packed[:5] + len(raw).to_bytes(8, 'little') + packed[13:]
assert lzma.decompress(packed, format=lzma.FORMAT_ALONE) == raw
container = struct.pack('<8sQQ32s', b'P13LZ001', len(packed), len(raw), sha) + packed
(O / 'payload.container').write_bytes(container)
(O / 'payload.lzma').write_bytes(packed)

(B / 'config/Expected.h').write_text(
    '/* Generated P23 kernel/ADT hashes; loader unchanged. */\n' + '\n'.join(definitions) + '\n')

header = (R / 'config/p13/CompressedPayload.h').read_text()
old_hash = ('0x21,0xaf,0x3f,0x6e,0xb4,0x18,0xc3,0x10,0x3a,0xb4,0xee,0xd4,0x73,0xcf,0x43,'
            '0xe1,0x48,0x98,0x85,0x68,0xe5,0x17,0x2f,0xd6,0x9c,0x64,0xb3,0xa3,0xdf,0x04,0x91,0x94')
assert old_hash in header
header = header.replace(old_hash, ','.join('0x%02x' % x for x in sha))
header = header.replace('124993536', str(len(raw)))
header = header.replace('fixed P12 payload', 'fixed P23 payload').replace(
    'expanded P12 payload', 'expanded P23 payload')
(B / 'config/CompressedPayload.h').write_text(header)
harness = (R / 'out/p16/source/decode-lzma.c').read_text().replace('125050880', str(len(raw)))
(B / 'source').mkdir(exist_ok=True)
(B / 'source/decode-lzma.c').write_text(harness)

result = dict(result='PAYLOAD_COMPRESSED_HOST_ROUNDTRIP_PASS', bytes=len(raw), sha256=sha.hex(),
              container_bytes=len(container),
              container_sha256=hashlib.sha256(container).hexdigest(),
              files=bundle.manifest(raw), seed=seed_report['output']['sha256'],
              trust=trust_report['sha256'], compression_protocol_unchanged=True)
(O / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({k: result[k] for k in ('result', 'bytes', 'sha256', 'container_bytes')}, indent=2))
