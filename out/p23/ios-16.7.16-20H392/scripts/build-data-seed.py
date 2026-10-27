#!/usr/bin/env python3
"""Create a 1 GiB iOS 16.7.16 (20H392) Data seed from the IPSW OS volume's initial /private/var.

This is a host-side development seed.  The source volume is mounted with -owners off,
so root-only directories are readable by the host user; as a consequence every copied
inode is owned by the host uid/gid (501:20) instead of the original 0:0/501:501/service
uids.  Modes, symlinks and the path inventory are preserved, the five Setup/migration
preferences are written for 20H392/16.7.16, and the result is packed as DBRDISK1.
A root-ownership pass is a separately documented prerequisite for production use.
"""
from pathlib import Path
import hashlib
import json
import os
import plistlib
import shutil
import stat
import struct
import subprocess
import sys
import tempfile

R = Path(__file__).resolve().parents[4]
BASE = R / 'out/p23/ios-16.7.16-20H392'
SEED_DIR = BASE / 'data-seed'
RAW = SEED_DIR / 'owner-data-1g.cdr'
DBR = SEED_DIR / 'owner-data-1g.dbr'
SKIP = SEED_DIR / 'skip-setup-1g.dbr'
SKIP_JSON = SEED_DIR / 'skip-setup-1g.json'
OS_DMG = BASE / 'extracted/087-86698-044.dmg'
EXCLUDE_TOP = {'staged_system_apps'}
EXTRA_DIRS = [
    'containers', 'containers/Bundle', 'containers/Data', 'containers/Data/System',
    'containers/Data/Application', 'containers/Shared', 'containers/Shared/SystemGroup',
    'containers/Temp', 'keybags', 'keybags/backup', 'PersonaVolumes', 'preboot',
    'db/dslocal', 'db/dslocal/nodes', 'db/dslocal/nodes/Default',
    'db/dslocal/nodes/Default/users', 'db/dslocal/nodes/Default/groups',
    'db/dslocal/nodes/Default/config', 'mobile/tmp', 'run', 'tmp',
]
MODES = {'tmp': 0o1777, 'run': 0o775, 'mobile/tmp': 0o700}

if RAW.exists() or DBR.exists() or SKIP.exists():
    raise SystemExit('refuse to overwrite an existing Data seed')
SEED_DIR.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(R / '.work/p21-official-framework/drivers/DarwinBoot/Test'))
import packed_ramdisk  # noqa: E402


def attach(image, mount, owners='off'):
    command = ['hdiutil', 'attach']
    if str(image).endswith('.raw') or str(image).endswith('.cdr'):
        command += ['-imagekey', 'diskimage-class=CRawDiskImage']
    command += ['-readonly' if str(image).endswith('.dmg') else '',
                '-nobrowse', '-owners', owners, '-mountpoint', str(mount), '-plist', str(image)]
    command = [part for part in command if part]
    info = plistlib.loads(subprocess.check_output(command))
    for entity in info['system-entities']:
        if os.path.realpath(entity.get('mount-point', '')) == os.path.realpath(str(mount)):
            return entity['dev-entry']
    raise SystemExit('could not find mounted Data volume')


def mount_source():
    mount = Path(tempfile.mkdtemp(prefix='ios16-var-', dir='/tmp'))
    device = attach(OS_DMG, mount, 'off')
    return mount, device


mount, source_device = mount_source()
var = mount / 'private/var'
subprocess.run(['hdiutil', 'create', '-size', '1g', '-layout', 'NONE', '-fs', 'APFS',
                '-volname', 'OwnerData', '-type', 'UDTO', str(RAW)], check=True)
dest_mount = Path(tempfile.mkdtemp(prefix='ios16-data-mount-', dir='/tmp'))
dest_device = attach(RAW, dest_mount, 'off')
copied = {'dirs': 0, 'files': 0, 'symlinks': 0, 'skipped': []}
try:
    for current, dirs, files in os.walk(var, followlinks=False):
        rel = Path(current).relative_to(var)
        if rel == Path('.'):
            dirs[:] = [d for d in dirs if d not in EXCLUDE_TOP]
        target_dir = dest_mount / rel
        target_dir.mkdir(parents=True, exist_ok=True)
        for name in list(dirs) + files:
            src, dst = Path(current) / name, target_dir / name
            try:
                if src.is_symlink():
                    os.symlink(os.readlink(src), dst)
                    copied['symlinks'] += 1
                elif src.is_dir():
                    dst.mkdir(exist_ok=True)
                    os.chmod(dst, stat.S_IMODE(src.stat().st_mode))
                    copied['dirs'] += 1
                elif src.is_file():
                    shutil.copy2(src, dst)
                    copied['files'] += 1
                else:
                    copied['skipped'].append(str(src.relative_to(var)))
                    if name in dirs:
                        dirs.remove(name)
            except OSError as error:
                copied['skipped'].append(f'{src.relative_to(var)}: {error}')
                if name in dirs:
                    dirs.remove(name)
    for name in EXTRA_DIRS:
        path = dest_mount / name
        path.mkdir(parents=True, exist_ok=True)
        copied['dirs'] += 1
    for name, mode in MODES.items():
        os.chmod(dest_mount / name, mode)
    os.chmod(dest_mount, 0o755)
finally:
    subprocess.run(['hdiutil', 'detach', dest_device], check=True)
    subprocess.run(['hdiutil', 'detach', source_device], check=True)
    shutil.rmtree(mount)
    shutil.rmtree(dest_mount, ignore_errors=True)

with RAW.open('rb') as f:
    head = f.read(64)
if RAW.stat().st_size != 1 << 30 or head[32:36] != b'NXSB':
    raise SystemExit('host formatting did not produce the requested raw APFS container')
raw_sha = hashlib.sha256()
with RAW.open('rb') as f:
    while chunk := f.read(8 << 20):
        raw_sha.update(chunk)
metadata = packed_ramdisk.pack(RAW, DBR)
base_report = {
    'result': 'IOS16_DATA_SEED_PACKED_HOST_ONLY',
    'source': 'iOS 16.7.16 (20H392) OS volume /private/var, staged_system_apps excluded',
    'raw_bytes': RAW.stat().st_size, 'raw_sha256': raw_sha.hexdigest(),
    'dbr': str(DBR.relative_to(R)), 'dbr_sha256': metadata['sha256'],
    'dbr_encoded_bytes': metadata['encoded_bytes'],
    'inventory': copied,
    'ownership': 'host 501:20 (owners-off source copy); original uid/gid preservation requires administrator authorization',
    'ownership_preserved': False,
    'extra_dirs': EXTRA_DIRS,
    'phone_operations': [],
}
(SEED_DIR / 'owner-data-1g.json').write_text(json.dumps(base_report, indent=2) + '\n')
print(json.dumps(base_report, indent=2))

# Adapt the official skip-setup tool with the 20H392/16.7.16 constants.
env = dict(os.environ)
subprocess.run([sys.executable, str(BASE / 'scripts/seed-data-skip-setup-20H392.py'),
                str(DBR), str(SKIP), '--no-auto-dim'], check=True, env=env)

# Structural and preference verification of the packed output.
sys.path.insert(0, str(R / '.work/p21-official-framework/drivers/DarwinBoot/Test'))
logical, extents = packed_ramdisk.regions(SKIP.read_bytes())
with tempfile.TemporaryDirectory(prefix='ios16-verify-', dir='/tmp') as td:
    raw = Path(td) / 'seed.raw'
    with raw.open('wb') as f:
        f.truncate(logical)
        for offset, data in extents:
            f.seek(offset)
            f.write(data)
    mount = Path(td) / 'mount'
    mount.mkdir()
    device = attach(raw, mount, 'off')
    try:
        prefs = {}
        for name in ('com.apple.migration', 'com.apple.purplebuddy', 'com.apple.springboard',
                     'com.apple.purplebuddy.notbackedup', 'com.apple.managedconfiguration.notbackedup'):
            p = mount / 'mobile/Library/Preferences' / (name + '.plist')
            prefs[name] = plistlib.loads(p.read_bytes())
        fsck = subprocess.run(['fsck_apfs', '-n', str(raw)], capture_output=True, text=True)
        volume_uuid = plistlib.loads(subprocess.check_output(
            ['diskutil', 'info', '-plist', device]))['VolumeUUID']
    finally:
        subprocess.run(['hdiutil', 'detach', device], check=True)
report = {
    'result': 'IOS16_SKIP_SETUP_SEED_VERIFIED',
    'base_dbr_sha256': metadata['sha256'],
    'output': {'bytes': SKIP.stat().st_size, 'sha256': hashlib.sha256(SKIP.read_bytes()).hexdigest(),
               'logical_bytes': logical},
    'build': '20H392', 'version': '16.7.16', 'skip_setup': True, 'no_auto_dim': True,
    'volume_uuid': volume_uuid,
    'migration': prefs['com.apple.migration'],
    'purplebuddy': prefs['com.apple.purplebuddy'],
    'springboard_keys': sorted(prefs['com.apple.springboard']),
    'fsck_apfs_exit': fsck.returncode,
    'fsck_apfs_tail': fsck.stdout.strip().splitlines()[-3:] if fsck.stdout.strip() else [],
    'ownership_preserved': False,
    'phone_operations': [],
}
SKIP_JSON.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
