#!/usr/bin/env python3
"""Wait for the pinned device to leave the parked state. If it appears in fastboot, send it to
Recovery. If it appears in root Recovery, report RECOVERED and exit. Never writes partitions."""
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


def env():
    return dict(os.environ, ADB_LIBUSB='1', ADB_VENDOR_KEYS=str(R / '.work/android/adbkey'),
                ANDROID_USER_HOME=str(R / '.work/android'), TMPDIR=str(R / '.work/tmp'))


def adb(server, *args, timeout=12):
    e = env()
    e['ADB_SERVER_SOCKET'] = server
    return subprocess.run([str(ADB), '-s', SERIAL, *args], env=e, capture_output=True, text=True, timeout=timeout)


def main():
    deadline = time.time() + (int(sys.argv[1]) if len(sys.argv) > 1 else 7200)
    last = None
    while time.time() < deadline:
        try:
            fb = subprocess.run([str(FASTBOOT), 'devices'], capture_output=True, text=True, timeout=10).stdout
        except Exception:
            fb = ''
        if SERIAL in fb:
            print(json.dumps({'event': 'FASTBOOT_RETURN', 'action': 'reboot recovery'}), flush=True)
            subprocess.run([str(FASTBOOT), '-s', SERIAL, 'reboot', 'recovery'], capture_output=True, text=True, timeout=30, check=True)
            time.sleep(5)
            last = 'fastboot'
            continue
        for server in (os.environ.get('ADB_SERVER_SOCKET', 'tcp:localhost:5037'),):
            try:
                if adb(server, 'get-state').stdout.strip() not in ('device', 'recovery'):
                    continue
                boot = adb(server, 'shell', 'getprop ro.bootmode').stdout.strip()
                state = 'recovery' if boot == 'recovery' else 'android'
                if last != state:
                    print(json.dumps({'event': 'ADB_RETURN', 'server': server, 'state': state}), flush=True)
                    last = state
                if state == 'recovery':
                    print(json.dumps({'event': 'RECOVERED', 'server': server}), flush=True)
                    return 0
            except Exception:
                continue
        time.sleep(10)
    print(json.dumps({'event': 'WATCH_TIMEOUT'}), flush=True)
    return 1


if __name__ == '__main__':
    sys.exit(main())
