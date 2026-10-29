#!/usr/bin/env python3
"""Build the owner-signed verified-root policy for the grouped OS+Data window.

The new OS window is the 12 GiB container built by the APFS group work: the OS
volume keeps its UUID, and a role=Data volume now lives in the same container.
SystemOS keeps its existing signed manifest. The owner key and anchor are the
ones already trusted by the running kernel.
"""
import hashlib
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

R = Path(__file__).resolve().parents[1]
BASE = R / 'out/p23/ios-16.7.16-20H392'
GROUP = BASE / 'apfs-group'
IMAGE = GROUP / 'os-writable.dmg'
WINDOW_BYTES = 12 * 1024 * 1024 * 1024
OS_UUID = 'DADD93F8-DBBF-4B17-A1DF-0F11E64172F7'
KEY = R / 'out/p12/policies/private/owner.pem'
PUBLIC = R / 'out/p12/policies/owner-public.pem'
SYSTEMOS_MANIFEST = BASE / 'policies/SystemOS/manifest.bin'
OUT = BASE / 'policies-group'
TOOL = R / '.work/p4-unixnu/kits/VerifiedRoot/tools/manifest.py'


def load_tool():
    spec = importlib.util.spec_from_file_location('p23_group_manifest', TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    if not IMAGE.is_file():
        raise SystemExit(f'missing grouped image: {IMAGE}')
    size = IMAGE.stat().st_size
    window = min(size, WINDOW_BYTES)
    if window != WINDOW_BYTES:
        raise SystemExit('image smaller than the configured window')

    class Bounded:
        def __init__(self, path, limit):
            self._file = path.open('rb', buffering=8 << 20)
            self._left = limit

        def read(self, n=-1):
            if self._left <= 0:
                return b''
            take = self._left if n < 0 else min(n, self._left)
            data = self._file.read(take)
            self._left -= len(data)
            return data

        def close(self):
            self._file.close()

    out_os = OUT / 'OS'
    if (out_os / 'manifest.bin').exists():
        print('reuse existing grouped OS manifest')
    else:
        module = load_tool()
        stream = Bounded(IMAGE, window)
        try:
            record = module.make(stream, window, KEY, PUBLIC, OS_UUID, 1, out_os)
        finally:
            stream.close()
        (OUT / 'OS-record.json').write_text(json.dumps(record, indent=2) + '\n')
        print('signed grouped OS manifest', record['image_size'] if 'image_size' in record else window)

    header = (out_os / 'header.bin').read_bytes()
    import struct
    image_bytes, blocks = struct.unpack_from('<QQ', header, 24)
    if image_bytes != window:
        raise SystemExit(f'manifest window {image_bytes} != {window}')
    image_sha = header[56:88].hex()

    anchor = (out_os / 'trusted-key.bin').read_bytes()
    system_os = SYSTEMOS_MANIFEST.read_bytes()
    assembly = ['.section __PLK_DATA_CONST,__const\n.balign 8\n']
    assembly.append('.balign 8\n.globl _verified_root_os_manifest\n_verified_root_os_manifest:\n.incbin "%s"\n' % (out_os / 'manifest.bin'))
    assembly.append('.balign 8\n.globl _verified_root_systemos_manifest\n_verified_root_systemos_manifest:\n.incbin "%s"\n' % SYSTEMOS_MANIFEST)
    key_bin = OUT / 'trusted-key.bin'
    key_bin.write_bytes(anchor)
    assembly.append('.balign 8\n.globl _verified_root_key\n_verified_root_key:\n.incbin "%s"\n' % key_bin)
    assembly.append('.balign 8\n.globl _verified_root_policies,_verified_root_policy_count\n_verified_root_policies:\n')
    assembly.append('.quad _verified_root_os_manifest, %d\n' % (out_os / 'manifest.bin').stat().st_size)
    assembly.append('.quad _verified_root_systemos_manifest, %d\n' % len(system_os))
    assembly.append('_verified_root_policy_count:\n.quad 2\n')
    inputs = OUT / 'inputs.S'
    inputs.write_text(''.join(assembly))
    obj = OUT / 'inputs.o'
    subprocess.run(['xcrun', 'clang', '-target', 'arm64-apple-ios16.0', '-c', str(inputs), '-o', str(obj)], check=True)
    lib = OUT / 'libVerifiedRootPolicy.a'
    subprocess.run(['xcrun', 'ar', 'rcs', str(lib), str(obj)], check=True)
    result = {
        'result': 'P23_GROUPED_OS_POLICY_SIGNED',
        'window': {'offset': 0x400000000, 'bytes': window, 'sha256': image_sha, 'volume_uuid': OS_UUID},
        'os_manifest_bytes': (out_os / 'manifest.bin').stat().st_size,
        'os_manifest_blocks': blocks,
        'systemos_manifest_bytes': len(system_os),
        'library': str(lib.relative_to(R)),
        'library_sha256': hashlib.sha256(lib.read_bytes()).hexdigest(),
        'key': str(KEY.relative_to(R)),
        'device_written': False,
    }
    (OUT / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
