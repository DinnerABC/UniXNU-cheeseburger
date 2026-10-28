#!/usr/bin/env python3
"""Poll the single ADB service every few seconds; read the persistent window as soon as the
device is in root Recovery. No long sleeps, no second ADB server, no partition writes."""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

R = Path(__file__).resolve().parents[2]
ADB = R / '.work/p3-tools-35/platform-tools/adb'
FASTBOOT = R / '.work/p3-tools-35/platform-tools/fastboot'
SERIAL = '3b25cc7c'
READER = R / 'scripts/logging/read-cheeseburger-log.py'
CONFIG = R / 'out/unattended/config.json'


def adb_env(server):
    return dict(os.environ, ADB_LIBUSB='1', ADB_SERVER_SOCKET=server,
                ADB_VENDOR_KEYS=str(R / '.work/android/adbkey'),
                ANDROID_USER_HOME=str(R / '.work/android'), TMPDIR=str(R / '.work/tmp'))


def adb(server, *args, timeout=10):
    return subprocess.run([str(ADB), '-s', SERIAL, *args], env=adb_env(server),
                          capture_output=True, text=True, timeout=timeout)


def in_root_recovery(server):
    try:
        state = adb(server, 'get-state').stdout.strip()
        if state not in ('device', 'recovery'):
            return False
        if adb(server, 'shell', 'id -u').stdout.strip() != '0':
            return False
        if adb(server, 'shell', 'getprop ro.bootmode').stdout.strip() != 'recovery':
            return False
        return adb(server, 'shell', 'getprop ro.product.device').stdout.strip() == 'cheeseburger'
    except Exception:
        return False


def fastboot_present():
    try:
        out = subprocess.run([str(FASTBOOT), 'devices'], capture_output=True, text=True, timeout=5).stdout
        return SERIAL in out
    except Exception:
        return False


def latest_retrieval():
    logs = sorted((R / 'out/p21/persistent-logs').glob('*/retrieval.json'), key=lambda p: p.stat().st_mtime)
    return logs[-1] if logs else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--poll', type=int, default=2, help='seconds between ADB checks')
    ap.add_argument('--timeout', type=int, default=900, help='give up after this many seconds')
    ap.add_argument('--server', default=os.environ.get('CHEESEBURGER_ADB_SERVER', 'tcp:localhost:5037'),
                    help='the single ADB service in use')
    ap.add_argument('--state', default=str(R / 'out/unattended/state.json'))
    args = ap.parse_args()
    state_path = Path(args.state)
    state = json.loads(state_path.read_text()) if state_path.is_file() else {}
    last = state.get('log_window_sha256')
    deadline = time.time() + args.timeout
    polls = 0
    while time.time() < deadline:
        polls += 1
        if fastboot_present():
            # A reset can land in the bootloader; send it back to Recovery.
            subprocess.run([str(FASTBOOT), 'reboot', 'recovery'], capture_output=True, text=True, timeout=30)
            time.sleep(args.poll)
            continue
        if not in_root_recovery(args.server):
            time.sleep(args.poll)
            continue
        env = dict(os.environ, CHEESEBURGER_ADB_SERVER=args.server)
        proc = subprocess.run([sys.executable, str(READER)], env=env, capture_output=True, text=True)
        if proc.returncode:
            print(json.dumps({'result': 'LOG_READ_FAILED', 'polls': polls, 'stderr': proc.stderr[-600:]}), flush=True)
            return 1
        retrieval = latest_retrieval()
        info = json.loads(retrieval.read_text()) if retrieval else {}
        sha = info.get('sha256')
        if sha and sha != last:
            state.update(log_window_sha256=sha, retrieval=str(retrieval),
                         updated=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()))
            state_path.write_text(json.dumps(state, indent=2) + '\n')
            print(json.dumps({'result': 'NEW_LOG', 'sha256': sha, 'retrieval': str(retrieval),
                              'server': args.server, 'polls': polls}), flush=True)
            return 0
        print(json.dumps({'result': 'NO_NEW_LOG', 'sha256': sha, 'retrieval': str(retrieval),
                          'server': args.server, 'polls': polls}), flush=True)
        return 1
    print(json.dumps({'result': 'NO_RECOVERY', 'timeout': args.timeout, 'polls': polls}), flush=True)
    return 1


if __name__ == '__main__':
    sys.exit(main())
