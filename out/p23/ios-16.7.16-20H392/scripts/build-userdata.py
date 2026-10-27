#!/usr/bin/env python3
"""Build the Stage C userdata SHNGROOT image from the real iPhone X iOS 16.7.16 OS and SystemOS DMGs.

The two windows are copied byte for byte from the whole-disk view of each DMG (the same
contract as the official `unixnu build userdata`): 1 MiB header, 1 MiB aligned windows.
This only prepares a file under out/p23; it never touches the phone or a partition.
"""
from pathlib import Path
import hashlib
import json
import os
import plistlib
import re
import struct
import subprocess

R = Path(__file__).resolve().parents[4]
BASE = R / 'out/p23/ios-16.7.16-20H392'
OS_DMG = BASE / 'extracted/087-86698-044.dmg'
SYSTEM_DMG = BASE / 'extracted/087-86593-047.dmg'
OUT = BASE / 'userdata'
IMAGE = OUT / 'userdata-root.img'
REPORT = OUT / 'userdata-root.json'
MIB = 1 << 20
CHUNK = 8 << 20
OLD_UUID = '04769D26-2CA0-4825-B443-E2E5F88300F2'
LOG_WINDOW_OFFSET = 10 * (1 << 30)
LOG_WINDOW_BYTES = 32 << 20

if IMAGE.exists():
    raise SystemExit('refuse to overwrite ' + str(IMAGE))
OUT.mkdir(parents=True, exist_ok=True)


def image_bytes(dmg):
    info = plistlib.loads(subprocess.check_output(
        ['hdiutil', 'imageinfo', '-plist', str(dmg)]))
    size = info['Size Information']['Total Bytes']
    if size % 512:
        raise SystemExit(f'{dmg}: size not 512 aligned')
    return size


def attach(dmg):
    text = subprocess.check_output(
        ['hdiutil', 'attach', '-nomount', '-readonly', '-noverify', '-noautofsck', '-plist', str(dmg)])
    info = plistlib.loads(text)
    for entity in info['system-entities']:
        device = entity.get('dev-entry', '')
        if re.fullmatch(r'/dev/disk\d+', device):
            return device
    raise SystemExit(f'{dmg}: no whole-disk device')


def copy_window(target, offset, dmg, size):
    device = attach(dmg)
    raw = device.replace('/dev/disk', '/dev/rdisk')
    digest = hashlib.sha256()
    done = 0
    try:
        with open(raw, 'rb', buffering=0) as source:
            while done < size:
                want = min(1 << 20, size - done)
                chunk = b''
                while len(chunk) < want:
                    try:
                        part = source.read(want - len(chunk))
                    except OSError:
                        # Raw devices can reject a tail that is not a multiple of
                        # their logical block; retry in 512-byte units.
                        part = source.read(min(512, want - len(chunk)))
                    if not part:
                        raise SystemExit(f'{raw}: short read at {done + len(chunk)}')
                    chunk += part
                target.seek(offset + done)
                target.write(chunk)
                digest.update(chunk)
                done += len(chunk)
    finally:
        subprocess.run(['hdiutil', 'detach', device], check=True)
    if done != size:
        raise SystemExit(f'{dmg}: copied {done}, expected {size}')
    return digest.hexdigest()


os_size = image_bytes(OS_DMG)
system_size = image_bytes(SYSTEM_DMG)
os_offset = MIB
system_offset = os_offset + (os_size + MIB - 1) // MIB * MIB
total = system_offset + system_size
capacity = 47_938_797_568
if total > capacity:
    raise SystemExit(f'image {total} exceeds userdata {capacity}')
if total > LOG_WINDOW_OFFSET:
    raise SystemExit('new image would reach the persistent log window')

with IMAGE.open('wb') as target:
    target.truncate(total)
    print(f'OS window {os_size} bytes at {os_offset:#x}', flush=True)
    os_sha = copy_window(target, os_offset, OS_DMG, os_size)
    print(f'SystemOS window {system_size} bytes at {system_offset:#x}', flush=True)
    system_sha = copy_window(target, system_offset, SYSTEM_DMG, system_size)
    header = bytearray(4096)
    header[0:8] = b'SHNGROOT'
    struct.pack_into('<IIQ', header, 8, 1, 2, total)
    for index, (name, offset, size, digest) in enumerate([
            ('OS', os_offset, os_size, os_sha),
            ('SystemOS', system_offset, system_size, system_sha)]):
        struct.pack_into('<32sQQII32s40s', header, 24 + 128 * index,
                         name.encode(), offset, size, 512, 1, bytes.fromhex(digest), bytes(40))
    header[4064:4096] = hashlib.sha256(bytes(header[:4064])).digest()
    target.seek(0)
    target.write(header)
    target.flush()
    os.fsync(target.fileno())

# Independent readback of the header and both window boundaries.
with IMAGE.open('rb') as f:
    raw = f.read(MIB)
if raw[:8] != b'SHNGROOT' or struct.unpack_from('<I', raw, 8)[0] != 1 or struct.unpack_from('<I', raw, 12)[0] != 2:
    raise SystemExit('header readback failed')
if hashlib.sha256(raw[:4064]).digest() != raw[4064:4096]:
    raise SystemExit('header checksum readback failed')
for index, (name, offset, size, digest) in enumerate([
        ('OS', os_offset, os_size, os_sha), ('SystemOS', system_offset, system_size, system_sha)]):
    entry = struct.unpack_from('<32sQQII32s40s', raw, 24 + 128 * index)
    assert entry[0].split(b'\0')[0].decode() == name and entry[1] == offset and entry[2] == size
    assert entry[5].hex() == digest
    with IMAGE.open('rb') as f:
        f.seek(offset)
        h = hashlib.sha256()
        left = size
        while left:
            chunk = f.read(min(CHUNK, left))
            if not chunk:
                raise SystemExit(f'{name}: readback short')
            h.update(chunk)
            left -= len(chunk)
        if h.hexdigest() != digest:
            raise SystemExit(f'{name}: readback hash mismatch')

image_sha = hashlib.sha256()
with IMAGE.open('rb') as f:
    while chunk := f.read(CHUNK):
        image_sha.update(chunk)

report = {
    'result': 'IOS16_7_16_20H392_USERDATA_BUILT_AND_READBACK_VERIFIED',
    'os': 'iPhone OS 16.7.16 (20H392), iPhone10,3/iPhone10,6 (D22/D221)',
    'source_ipsw': 'out/p23/ios-16.7.16-20H392/download/iPhone10,3,iPhone10,6_16.7.16_20H392_Restore.ipsw',
    'image_bytes': total,
    'image_sha256': image_sha.hexdigest(),
    'manifest_sha256': hashlib.sha256(raw).hexdigest(),
    'windows': [
        {'name': 'OS', 'offset': os_offset, 'size': os_size, 'sha256': os_sha},
        {'name': 'SystemOS', 'offset': system_offset, 'size': system_size, 'sha256': system_sha},
    ],
    'userdata_bytes': capacity,
    'log_window': {'offset': LOG_WINDOW_OFFSET, 'bytes': LOG_WINDOW_BYTES,
                   'overlap': False, 'new_image_end': total},
    'old_root_uuid': OLD_UUID,
    'new_os_volume_uuid': 'DADD93F8-DBBF-4B17-A1DF-0F11E64172F7',
    'new_systemos_volume_uuid': '96096B28-B6DC-4BC7-9D3A-2632A0FE53F1',
    'root_uuid_matches_current_adt': False,
    'window_bytes_are_original_ipsw_disks': True,
    'phone_operations': [],
    'partition_writes': False,
}
REPORT.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
