#!/usr/bin/env python3
"""Read named partitions only; accept backups after size and two remote hash checks."""
import argparse, hashlib, json, os, re, subprocess, time
from pathlib import Path
r = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('partitions', nargs='+', help='Actual by-name partition names, never block paths')
a = p.parse_args()
private = r / 'out/p3/private'
private.mkdir(parents=True, exist_ok=True)
os.chmod(private, 0o700)
out = r / 'backups/p3' / time.strftime('%Y%m%d-%H%M%S')
out.mkdir(parents=True, mode=0o700)
env = dict(os.environ, ADB_LIBUSB='1', ANDROID_USER_HOME=str(r/'.work/android'),
           ADB_VENDOR_KEYS=str(r/'.work/android/adbkey'), ADB_SERVER_SOCKET='tcp:localhost:5038',
           TMPDIR=str(r/'.work/tmp'))
adb = str(r/'.work/p3-tools-35/platform-tools/adb')
selected = json.loads((private/'selected-device.json').read_text())['serial']
base = [adb, '-s', selected]
records = []
def shell(command):
    v = subprocess.run(base + ['shell', command], env=env, capture_output=True, timeout=300)
    if v.returncode:
        raise RuntimeError('Read command failed: '+command+' status='+str(v.returncode))
    return v.stdout.decode().strip()
def remote_hash(path):
    value = shell('sha256sum '+path).split()[0]
    if not re.fullmatch('[0-9a-f]{64}', value):
        raise RuntimeError('Invalid remote SHA256')
    return value
if shell('id -u') != '0':
    raise SystemExit('Root Recovery ADB required; no raw partition reads attempted')
for name in a.partitions:
    if not re.fullmatch('[A-Za-z0-9_-]+', name):
        raise SystemExit('Invalid partition name')
    rec = dict(partition=name, verified=False, started_epoch=time.time())
    try:
        link = '/dev/block/bootdevice/by-name/'+name
        path = shell('readlink -f '+link)
        if not re.fullmatch(r'/dev/block/(?:sd[a-z][0-9]+|mmcblk[0-9]+p[0-9]+)', path):
            raise RuntimeError('Missing or unexpected actual block mapping')
        size = int(shell('blockdev --getsize64 '+path))
        if size <= 0:
            raise RuntimeError('Invalid partition size')
        rec.update(block_device=path, by_name=link, expected_bytes=size)
        before = remote_hash(path)
        cmd = base + ['exec-out', 'dd if='+path+' bs=1048576 2>/dev/null']
        rec['command'] = cmd
        partial = out/(name+'.img.partial')
        with partial.open('xb') as f, (out/(name+'.stderr.log')).open('xb') as err:
            os.chmod(partial, 0o600)
            os.chmod(out/(name+'.stderr.log'), 0o600)
            proc = subprocess.run(cmd, env=env, stdout=f, stderr=err, timeout=1800)
        rec['exit_code'] = proc.returncode
        rec['actual_bytes'] = partial.stat().st_size
        h = hashlib.sha256()
        with partial.open('rb') as f:
            for chunk in iter(lambda:f.read(1048576), b''):
                h.update(chunk)
        after = remote_hash(path)
        rec.update(sha256=h.hexdigest(), device_sha256_before=before, device_sha256_after=after)
        if proc.returncode or rec['actual_bytes'] != size or not before == after == h.hexdigest():
            raise RuntimeError('Partition size/hash/exit-code verification failed')
        final = out/(name+'.img')
        partial.rename(final)
        rec.update(verified=True, file=str(final.relative_to(r)))
    except (Exception, KeyboardInterrupt) as e:
        rec['error'] = str(e)
    records.append(rec)
    manifest = out/'manifest.json'
    temp = out/'manifest.json.partial'
    temp.write_text(json.dumps(records, indent=2)+'\n')
    os.chmod(temp, 0o600)
    temp.replace(manifest)
    print(name+': '+('VERIFIED' if rec['verified'] else 'FAILED'), flush=True)
    if not rec['verified']:
        raise SystemExit(1)
print('Manifest: '+str(manifest.relative_to(r)))
