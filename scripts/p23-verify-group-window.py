#!/usr/bin/env python3
"""Read-only verification of the grouped OS container written at userdata+16 GiB."""
import hashlib
import subprocess
import sys

ADB = '/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/.work/p3-tools-35/platform-tools/adb'
SERIAL = '3b25cc7c'
PARTITION = '/dev/block/sda14'
OFFSET_BYTES = 16 * 1024 * 1024 * 1024
WINDOW_BYTES = 12 * 1024 * 1024 * 1024
EXPECTED_SHA = '66372f415b1cb1183ae2fc6d06698aea8f9cc7cd1de8fd0d12b6369060707df1'


def adb(*args, **kwargs):
    return subprocess.run([ADB, '-s', SERIAL, *args], check=True, capture_output=True, **kwargs)


def main(full_hash):
    block = adb('exec-out', f'dd if={PARTITION} bs=4096 skip={OFFSET_BYTES // 4096} count=2 2>/dev/null').stdout
    if len(block) < 8192:
        sys.exit('short read')
    if block[510:512] != b'\x55\xaa':
        sys.exit('no GPT protective MBR signature')
    if block[512:520] != b'EFI PART':
        sys.exit('no GPT header')
    container = block[0x4400 + 32:0x4400 + 36]
    if container != b'NXSB':
        sys.exit('no APFS container superblock at the GPT partition start')
    print('GPT and APFS container magic verified at userdata+16 GiB')
    if not full_hash:
        return
    proc = subprocess.Popen([ADB, '-s', SERIAL, 'exec-out',
                             f'dd if={PARTITION} bs=1M skip={OFFSET_BYTES // (1 << 20)} count={WINDOW_BYTES // (1 << 20)} 2>/dev/null'],
                            stdout=subprocess.PIPE)
    digest = hashlib.sha256()
    read = 0
    for chunk in iter(lambda: proc.stdout.read(8 << 20), b''):
        digest.update(chunk)
        read += len(chunk)
    proc.wait()
    print('read', read, 'sha256', digest.hexdigest())
    if read != WINDOW_BYTES or digest.hexdigest() != EXPECTED_SHA:
        sys.exit('readback differs from the signed window')
    print('window hash matches the signed manifest')


if __name__ == '__main__':
    main('--hash' in sys.argv)
