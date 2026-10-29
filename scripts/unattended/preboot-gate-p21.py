#!/usr/bin/env python3
"""Offline gate for an iPadOS-17.7.11 (P21) candidate.

Verifies that the packed image, payload container, firmware and recorded kernel
agree with each other before the image is booted, and that the boot arguments
that make the run diagnosable are present. It never touches the device.
"""
import hashlib
import json
import lzma
import re
import struct
import sys
import zlib
from pathlib import Path

R = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(R / 'tools/ram_payload'))
import bundle  # noqa: E402

bundle.MAX_BYTES = 256 * 1024 * 1024


def fail(reason):
    print(json.dumps({'result': 'PREBOOT_GATE_P21_FAIL', 'reason': reason}))
    sys.exit(2)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main(candidate):
    C = R / candidate
    packaging_path = C / 'uefi/packaging.json'
    if not packaging_path.is_file():
        fail(f'missing {packaging_path}')
    packaging = json.loads(packaging_path.read_text())
    if packaging.get('result') != 'OFFLINE_PACKAGING_PASS':
        fail(f"packaging result {packaging.get('result')}")
    image = R / packaging['image']
    if not image.is_file():
        fail(f'missing image {image}')
    if sha(image) != packaging['sha256']:
        fail('image hash differs from packaging.json')

    raw = image.read_bytes()
    ksize, _, rsize, _, _, _, _, page, _, _ = struct.unpack_from('<10I', raw, 8)
    if page != 4096:
        fail(f'unexpected page size {page}')
    payload_at = page + ((ksize + page - 1) // page) * page
    payload = raw[payload_at:payload_at + rsize]
    if payload[:8] != b'P13LZ001':
        fail('payload container header')
    expanded = lzma.decompress(payload[56:], format=lzma.FORMAT_ALONE)

    manifest_path = C / 'payload/manifest.json'
    if not manifest_path.is_file():
        fail(f'missing {manifest_path}')
    manifest = json.loads(manifest_path.read_text())
    if hashlib.sha256(expanded).hexdigest() != manifest['sha256']:
        fail('expanded payload differs from payload/manifest.json')
    if len(expanded) != manifest['bytes']:
        fail('expanded payload length differs from manifest')

    kernel_validation = json.loads((C / 'kernel/validation.json').read_text())
    files = bundle.verify(expanded)
    kernel_in_payload = hashlib.sha256(files['\\kernel.macho']).hexdigest()
    if kernel_in_payload != kernel_validation.get('sha256'):
        fail('payload kernel differs from kernel/validation.json')

    firmware = (C / 'uefi/MSM8998PKG_UEFI.fd').read_bytes()
    if hashlib.sha256(firmware).hexdigest() != packaging['firmware_sha256']:
        fail('firmware changed after packaging')
    gz = zlib.decompressobj(31)
    kernel_image = gz.decompress(raw[page:page + ksize]) + gz.flush()
    dtb = (C / 'uefi/Android-cheeseburger.dtb').read_bytes()
    if kernel_image != firmware or gz.unused_data != dtb:
        fail('image kernel.gz-dtb does not carry this firmware and DTB')

    pins_log = C / 'logs/check-firmware-pins.log'
    pins_checked = False
    if pins_log.is_file():
        text = pins_log.read_text()
        adt_sha = json.loads((C / 'adt/validation.json').read_text()).get('sha256') \
            if (C / 'adt/validation.json').is_file() else None
        missing = [name for name, value in
                   (('payload', manifest['sha256']), ('kernel', kernel_in_payload), ('adt', adt_sha))
                   if value and value[:16] not in text]
        if missing:
            fail(f'firmware pin scan has no hit for {missing}')
        pins_checked = True

    toml = files['\\EFI\\UniXNU\\unixnu.toml'].decode()
    match = re.search(r'^args = "(.*)"$', toml, re.M)
    if not match:
        fail('boot arguments missing from unixnu.toml')
    args = match.group(1)
    for needle in ('sheng-log=1', 'keepsyms=1'):
        if needle not in args:
            fail(f'{needle} missing from boot arguments')

    print(json.dumps({
        'result': 'PREBOOT_GATE_P21_PASS',
        'candidate': candidate,
        'image_sha256': packaging['sha256'],
        'kernel_sha256': kernel_in_payload,
        'firmware_sha256': packaging['firmware_sha256'],
        'expanded_payload_sha256': manifest['sha256'],
        'args_bytes': len(args),
        'pins_log_checked': pins_checked,
    }))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'out/p21/r17-diag2')