#!/usr/bin/env python3
"""Read the pinned log window from root Recovery. Never writes phone storage.

One new retrieval directory preserves window.bin and the upstream raw decode.
Additional EFFECTIVE_EVIDENCE.md/json is a validated view, not a replacement.
For saved windows use analyze-cheeseburger-run.py [--check] RUN (no device).
"""
from pathlib import Path
import datetime
import hashlib
import json
import runpy
import subprocess
import sys

import heartbeat_record

R = Path(__file__).resolve().parents[2]


def main():
    # Device imports/validation are intentionally delayed: importing this module
    # for offline model tests must not open ADB or create device receipts.
    plan = json.loads((R / 'out/p21/r14/log-window.json').read_text())
    sys.path.insert(0, str(R / 'out/p21/r14/scripts'))
    from device import Device
    device = Device('persistent-log-read')
    device.require_root_recovery()
    assert device.shell('getprop ro.product.device') == 'cheeseburger'
    assert device.shell('readlink -f /dev/block/bootdevice/by-name/userdata') == plan['partition']
    assert int(device.shell('blockdev --getsize64 /dev/block/sda14')) == 47938797568
    assert int(device.shell('cat /sys/class/block/sda14/start')) * 512 == plan['userdata_first_lba'] * 4096
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    out = R / 'out/p21/persistent-logs' / stamp
    out.mkdir(parents=True, exist_ok=False, mode=0o700)
    command = [str(R / '.work/p3-tools-35/platform-tools/adb'), '-s', device.serial, 'exec-out',
               f"dd if={plan['partition']} bs=4096 skip={plan['offset_bytes']//4096} count={plan['blocks']} 2>/tmp/p21-read-log-stderr"]
    result = subprocess.run(command, env=device.env, capture_output=True, timeout=120)
    raw = result.stdout
    (out / 'window.bin').write_bytes(raw)
    report = {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
              'partition_reads_only': True, 'receipt_dir': str(device.out.relative_to(R)),
              'read_returncode': result.returncode, 'read_stderr': result.stderr.decode(errors='replace')}
    analyzer = runpy.run_path(str(R / 'scripts/logging/analyze-cheeseburger-run.py'))
    try:
        if result.returncode or len(raw) != plan['bytes']:
            raise ValueError('failed or truncated authorized window read')
        header = analyzer['window_header'](raw)
        if header['first_lba'] != plan['absolute_first_lba'] or header['window_blocks'] != plan['blocks']:
            raise ValueError('committed header does not match pinned authorized region')
    except ValueError as error:
        report.update(result='NO_VALID_COMMITTED_LOG_HEADER', validation_error=str(error))
        (out / 'retrieval.json').write_text(json.dumps(report, indent=2) + '\n')
        effective = analyzer['analyze'](out)
        effective.update(effective=False, reason=str(error))
        analyzer['save_report'](out, effective)
        print(json.dumps(report, indent=2))
        return 2
    report.update(msg_blocks=header['msg_blocks'], slot_blocks=header['slot_blocks'],
                  slot_count=header['slot_count'], slots_written=header['slots_written'],
                  ring_overwritten=max(0, header['slots_written'] - header['slot_count']),
                  result='BOUNDED_LOG_WINDOW_READ_PASS', boot_id=hex(header['boot_id']),
                  rounds=header['rounds'], write_errors=header['write_errors'],
                  state=header['state'], tag=header['tag'])
    free_first = header['slot_first'] + header['slot_blocks'] * header['slot_count']
    heartbeat = heartbeat_record.collect(raw, header['boot_id'], free_first, header['window_blocks'] - free_first)
    heartbeat['last'] = heartbeat['records'][-1] if heartbeat['records'] else None
    report['heartbeat'] = heartbeat
    # Keep raw window and retrieval even if upstream decode fails. It is run only
    # in this newly-created directory; never against historical decoded evidence.
    decoder = R.parent / 'upstream/UniXNU-sheng/scripts/read-sheng-log.py'
    python = Path('/opt/homebrew/bin/python3.13')
    python = python if python.is_file() else Path(sys.executable)
    decode = subprocess.run([str(python), str(decoder), '--decode', str(out / 'window.bin'),
                             '--out', str(out / 'decoded')])
    report['decoder_returncode'] = decode.returncode
    (out / 'retrieval.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    print('Saved:', out)
    analyzer['save_report'](out, analyzer['analyze'](out))
    print('Additional analysis saved:', out / 'EFFECTIVE_EVIDENCE.md')
    return 0 if decode.returncode == 0 else 2


if __name__ == '__main__':
    raise SystemExit(main())
