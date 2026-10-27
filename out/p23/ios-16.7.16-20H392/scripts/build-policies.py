#!/usr/bin/env python3
"""Sign the Stage C iOS 16.7.16 (20H392, D22) userdata windows with the project owner key
and verify every block.

The output has the same owner-policy object shape the frozen kernel currently carries in
libVerifiedRootPolicy.a.  Producing it is not the same as linking it into a boot image:
the current kernel's embedded manifests still describe the 17.7.11 windows.  The volume
UUIDs are the APFS UUIDs of the two restored disks (087-86698-044 OS,
087-86593-047 SystemCryptex) and must match the ADT root UUID of the boot image.
"""
from pathlib import Path
import ctypes
import hashlib
import importlib.util
import json
import os
import subprocess
import sys

R = Path(__file__).resolve().parents[4]
BASE = R / 'out/p23/ios-16.7.16-20H392'
IMAGE = BASE / 'userdata/userdata-root.img'
REPORT = BASE / 'userdata/userdata-root.json'
OUT = BASE / 'policies'
KEY = R / 'out/p12/policies/private/owner.pem'
PUBLIC = R / 'out/p12/policies/owner-public.pem'
VERIFIER = R / 'out/p12/verified-root-host/verified-root.dylib'
MANIFEST_TOOL = R / '.work/p4-unixnu/kits/VerifiedRoot/tools/manifest.py'
UUID = {
    'OS': 'DADD93F8-DBBF-4B17-A1DF-0F11E64172F7',
    'SystemOS': '96096B28-B6DC-4BC7-9D3A-2632A0FE53F1',
}

if UUID['SystemOS'] in ('6B06A0EF-7B90-4C3B-B25A-D30E696BAE3C', '', 'TBD'):
    raise SystemExit('refusing to sign with an unconverted SystemOS volume UUID')
if not IMAGE.is_file() or not REPORT.is_file():
    raise SystemExit('the new userdata image/report is not ready')
if not KEY.is_file() or not PUBLIC.is_file():
    raise SystemExit('the project owner key pair is missing')
spec = importlib.util.spec_from_file_location('ios16_manifest', MANIFEST_TOOL)
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)
report = json.loads(REPORT.read_text())
OUT.mkdir(parents=True, exist_ok=True)


class Window:
    def __init__(self, offset, size):
        self.file = IMAGE.open('rb', buffering=8 << 20)
        self.file.seek(offset)
        self.left = size
        self.size = size

    def read(self, count=-1):
        if count < 0 or count > self.left:
            count = self.left
        data = self.file.read(count)
        self.left -= len(data)
        return data

    def close(self):
        self.file.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


class View(ctypes.Structure):
    _fields_ = [('hashes', ctypes.c_void_p), ('image_bytes', ctypes.c_uint64),
                ('block_count', ctypes.c_uint64), ('generation', ctypes.c_uint64),
                ('volume_uuid', ctypes.c_uint8 * 16), ('image_sha256', ctypes.c_uint8 * 32)]


lib = ctypes.CDLL(str(VERIFIER))
lib.vr_host_open.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                             ctypes.c_uint64, ctypes.POINTER(View)]
lib.vr_host_block.argtypes = [ctypes.POINTER(View), ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t]

policies = []
assembly = ['.section __DATA_CONST,__const\n.balign 8\n']
for window in report['windows']:
    name = window['name']
    size = window['size']
    dest = OUT / name
    if dest.exists():
        raise SystemExit('refuse to overwrite ' + str(dest))
    with Window(window['offset'], size) as stream:
        record = manifest.make(stream, size, KEY, PUBLIC, UUID[name], 1, dest)
    if record['image_sha256'] != window['sha256']:
        raise SystemExit(f'{name}: signed image hash differs from userdata report')
    envelope = (dest / 'manifest.bin').read_bytes()
    anchor = (dest / 'trusted-key.bin').read_bytes()
    view = View()
    if lib.vr_host_open(envelope, len(envelope), anchor, 1, ctypes.byref(view)) != 0:
        raise SystemExit(f'{name}: host verifier rejected the manifest')
    digest = hashlib.sha256()
    count = 0
    with Window(window['offset'], size) as stream:
        while data := stream.read(4096):
            if lib.vr_host_block(ctypes.byref(view), count, data, len(data)) != 0:
                raise SystemExit(f'{name}: block {count} rejected')
            digest.update(data)
            count += 1
    if count != view.block_count or digest.hexdigest() != window['sha256']:
        raise SystemExit(f'{name}: block verification did not cover the window')
    record.update(real_c_signature_and_all_blocks_verified=True,
                  actual_blocks_verified=count,
                  root_manifest_window_hash_match=True,
                  volume_uuid=UUID[name],
                  apple_signature_verified=False)
    (dest / 'verification.json').write_text(json.dumps(record, indent=2) + '\n')
    policies.append({'name': name, 'manifest': str((dest / 'manifest.bin').relative_to(R)),
                     'bytes': len(envelope), 'blocks': count, 'volume_uuid': UUID[name],
                     'image_sha256': digest.hexdigest()})
    symbol = '_verified_root_' + name.lower() + '_manifest'
    assembly += ['\n.balign 8\n.globl ' + symbol + '\n' + symbol + ':\n'
                 '.incbin "' + str(dest / 'manifest.bin') + '"\n']
    print(f'FULLY VERIFIED {name} blocks={count} uuid={UUID[name]}', flush=True)

assembly += ['\n.balign 8\n.globl _verified_root_key\n_verified_root_key:\n'
             '.incbin "' + str(OUT / 'OS/trusted-key.bin') + '"\n'
             '.balign 8\n.globl _verified_root_policies,_verified_root_policy_count\n'
             '_verified_root_policies:\n']
for policy in policies:
    assembly += ['.quad _verified_root_' + policy['name'].lower() + '_manifest, ' + str(policy['bytes']) + '\n']
assembly += ['_verified_root_policy_count:\n.quad ' + str(len(policies)) + '\n']
(OUT / 'inputs.S').write_text(''.join(assembly))
subprocess.run(['xcrun', 'clang', '-target', 'arm64-apple-ios17.0', '-c',
                str(OUT / 'inputs.S'), '-o', str(OUT / 'inputs.o')], check=True)
subprocess.run(['xcrun', 'ar', 'rcs', str(OUT / 'libVerifiedRootPolicy.a'), str(OUT / 'inputs.o')], check=True)

result = {
    'result': 'IOS16_7_16_20H392_OWNER_POLICIES_SIGNED_ALL_BLOCKS_VERIFIED',
    'private_key': str(KEY.relative_to(R)),
    'public_key': str(PUBLIC.relative_to(R)),
    'policies': policies,
    'library': str((OUT / 'libVerifiedRootPolicy.a').relative_to(R)),
    'library_sha256': hashlib.sha256((OUT / 'libVerifiedRootPolicy.a').read_bytes()).hexdigest(),
    'signature_scope': 'owner-signed ordered block hashes; not Apple secure-boot or root-seal verification',
    'linked_into_current_kernel': False,
    'current_kernel_manifests': 'out/p22/r7/kernel/kernel.macho still embeds the 17.7.11 windows (1,483,785 / 806,400 blocks)',
    'phone_operations': [],
}
(OUT / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
