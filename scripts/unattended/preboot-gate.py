#!/usr/bin/env python3
"""Pre-boot gate: refuse to flash unless the automatic return to Recovery is intact.

Checks the packed image (not just the toml): the reset arguments must be inside the
~512-byte window the device actually receives, the kernel must export the park and
countdown entry points, the countdown must not depend on the log writer, and the
firmware must carry this payload's pins.
"""
import hashlib
import json
import lzma
import re
import struct
import sys
from pathlib import Path

R = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(R / 'tools/ram_payload'))
import bundle  # noqa: E402

bundle.MAX_BYTES = 256 * 1024 * 1024
WINDOW = 500  # bytes actually delivered before truncation


def fail(message):
    print(json.dumps({'result': 'PREBOOT_GATE_FAIL', 'reason': message}))
    sys.exit(2)


def main(candidate):
    C = R / candidate
    image = C / 'uefi/boot-p23-stagec-20h392-nosync.img'
    if not image.is_file():
        fail(f'missing image {image}')
    raw = image.read_bytes()
    ksize, _, rsize, _, _, _, _, page, _, _ = struct.unpack_from('<10I', raw, 8)
    payload_at = page + ((ksize + page - 1) // page) * page
    payload = raw[payload_at:payload_at + rsize]
    if payload[:8] != b'P13LZ001':
        fail('payload header')
    expanded = lzma.decompress(payload[56:], format=lzma.FORMAT_ALONE)
    files = bundle.verify(expanded)
    kernel = files['\\kernel.macho']
    toml = files['\\EFI\\UniXNU\\unixnu.toml'].decode()
    args = re.search(r'^args = "(.*)"$', toml, re.M).group(1)

    if len(args) >= WINDOW:
        fail(f'boot args {len(args)} bytes exceed the delivered window')
    allow_no_reset = '--allow-no-reset' in sys.argv
    for needle in ('cheeseburger-panic-hold=', 'sheng-log=1'):
        at = args.find(needle)
        if at < 0 or at >= WINDOW:
            fail(f'{needle} missing or beyond byte {WINDOW}')
    reset = re.search(r'cheeseburger-unattended-reset=(\d+)', args)
    if not allow_no_reset:
        at = args.find('cheeseburger-unattended-reset=')
        if at < 0 or at >= WINDOW:
            fail(f'cheeseburger-unattended-reset= missing or beyond byte {WINDOW}')
        if not reset or int(reset.group(1)) <= 0:
            fail('unattended reset countdown not positive')
    else:
        print('operator override: countdown reset disabled for this boot', file=sys.stderr)

    for symbol in (b'_board_panic_reset', b'_cheeseburger_unattended_reset_start'):
        if kernel.find(symbol) < 0:
            fail(f'kernel lacks {symbol.decode()}')
    if b'reset skipped; log or Recovery BCB unavailable' in kernel:
        fail('kernel still skips the countdown when the log/BCB is missing')

    source = (R / 'package/BoardRuntime/CheeseburgerPlatformPower.cpp').read_text()
    tail = source[source.index('unattended_reset_thread'):source.index('cheeseburger_unattended_reset_start')]
    if 'no log/BCB; countdown reset' not in tail:
        fail('countdown source no longer resets without log/BCB')
    halt_at = source.index('static int cb_halt_restart')
    halt_body = source[halt_at:source.index('static void unattended_console_line', halt_at)]
    if 'cb_reset_note' in halt_body:
        fail('reset path still writes a blocking UFS note')
    panic_body = source[source.index('void board_panic_reset(void)'):source.index('static int cb_halt_restart')]
    if '#if 0' in panic_body or 'cb_psci' not in panic_body:
        fail('panic path does not reset through PSCI (parked CPU with cpus=1)')

    validation = json.loads((C / 'kernel/validation.json').read_text())
    if hashlib.sha256(kernel).hexdigest() != validation['sha256']:
        fail('packed kernel differs from kernel/validation.json')
    manifest = json.loads((C / 'payload/manifest.json').read_text())
    if hashlib.sha256(expanded).hexdigest() != manifest['sha256']:
        fail('packed payload differs from payload/manifest.json')

    firmware = (C / 'uefi/MSM8998PKG_UEFI.fd').read_bytes()
    pins_log = C / 'logs/check-firmware-pins.log'
    if not pins_log.is_file():
        fail('no check-firmware-pins.log for this candidate')
    pins_text = pins_log.read_text()
    for key in ('"kernel"', '"adt"', '"payload"'):
        if key not in pins_text:
            fail(f'firmware pin scan has no {key} hit (rebuild UEFI)')
    packaging = json.loads((C / 'uefi/packaging.json').read_text())
    if hashlib.sha256(firmware).hexdigest() != packaging['firmware_sha256']:
        fail('firmware changed after packaging')

    print(json.dumps({
        'result': 'PREBOOT_GATE_PASS',
        'candidate': candidate,
        'image_sha256': hashlib.sha256(raw).hexdigest(),
        'kernel_sha256': validation['sha256'],
        'args_bytes': len(args),
        'countdown_s': int(reset.group(1)) if reset else 0,
        'firmware_sha256': hashlib.sha256(firmware).hexdigest(),
    }))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'out/p23/ios-16.7.16-20H392-logfix38')
