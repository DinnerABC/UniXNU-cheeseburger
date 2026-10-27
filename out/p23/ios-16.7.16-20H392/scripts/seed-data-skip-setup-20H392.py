#!/usr/bin/env python3
"""Make a 20H392 Data seed that boots to SpringBoard instead of Setup.

Adapted from out/p23/ios-16.7.11/scripts/seed-data-skip-setup-20H360.py for
iOS 16.7.16 / 20H392. The preference set and container layout are unchanged;
only BUILD/VERSION and the verification report are adapted.

  seed-data-skip-setup-20H392.py BASE.dbr OUTPUT.dbr [--no-auto-dim]

BASE is a DBRDISK1 packed APFS container (drivers/DarwinBoot/Test/packed_ramdisk.py
format). It is expanded to a raw image, attached read-write on this Mac, the
preference files below are merged into /private/var/mobile/Library/Preferences,
and the result is packed again. The volume UUID is unchanged. Nothing is written
to Git or to the OS inputs; BASE is not modified.
"""
import hashlib, json, os, plistlib, subprocess, sys, tempfile
from pathlib import Path

R = Path(__file__).resolve().parents[4]
ROOT = R / '.work/p21-official-framework'
sys.path.insert(0, str(ROOT / 'drivers/DarwinBoot/Test'))
import packed_ramdisk

BUILD, VERSION = '20H392', '16.7.16'
MOBILE = 501


def merge_pref(var, name, values):
    path = var / 'mobile/Library/Preferences' / (name + '.plist')
    path.parent.mkdir(parents=True, exist_ok=True)
    data = plistlib.loads(path.read_bytes()) if path.exists() else {}
    data.update(values)
    path.write_bytes(plistlib.dumps(data, fmt=plistlib.FMT_BINARY))
    os.chmod(path, 0o600)
    print('pref', name, sorted(values))


def attach(image, mount, owners='off'):
    command = ['hdiutil', 'attach', '-imagekey', 'diskimage-class=CRawDiskImage',
               '-nobrowse', '-owners', owners, '-mountpoint', str(mount), '-plist', str(image)]
    info = plistlib.loads(subprocess.check_output(command))
    for entity in info['system-entities']:
        if os.path.realpath(entity.get('mount-point', '')) == os.path.realpath(str(mount)):
            return entity['dev-entry']
    raise SystemExit('could not find mounted Data volume')


def expand(blob, raw):
    logical, extents = packed_ramdisk.regions(blob)
    with raw.open('wb') as f:
        f.truncate(logical)
        for offset, data in extents:
            f.seek(offset)
            f.write(data)
    return logical


def main():
    no_auto_dim = '--no-auto-dim' in sys.argv[1:]
    positional = [arg for arg in sys.argv[1:] if arg != '--no-auto-dim']
    if len(positional) != 2:
        sys.exit(__doc__)
    base, output = Path(positional[0]).resolve(), Path(positional[1]).resolve()
    if output.exists():
        sys.exit('Refuse to overwrite ' + str(output))
    blob = base.read_bytes()
    with tempfile.TemporaryDirectory(prefix='data-seed-', dir='/tmp') as td:
        raw = Path(td) / 'seed.raw'
        expand(blob, raw)
        mount = Path(td) / 'mount'
        mount.mkdir()
        device = attach(raw, mount)
        try:
            if not (mount / 'mobile').is_dir():
                sys.exit('The Data seed has no /mobile; wrong base image?')
            merge_pref(mount, 'com.apple.migration', {
                'LastSystemVersion': BUILD,
                'DMUserDataDisposition': {'SyntheticDidUpgrade': False, 'buildVersion': BUILD}})
            merge_pref(mount, 'com.apple.springboard', {
                'BKNoWatchdogs': True,
                'FBLaunchWatchdogExceptions': {'com.apple.purplebuddy': 100.0},
                'MCForceShowCloudConfigurationUI': False,
                **({'SBDisableAutoDim': True, 'SBDontDimOrLockOnAC': True, 'SBDisableIdleSleep': True} if no_auto_dim else {})})
            merge_pref(mount, 'com.apple.purplebuddy', {
                'SetupDone': True, 'SetupFinishedAllSteps': True, 'SetupVersion': 11, 'daemonizeNeedsToRun': False})
            merge_pref(mount, 'com.apple.purplebuddy.notbackedup', {
                k: False for k in ('ShouldRunDiagnosticsMiniBuddy', 'ShouldRunHSA2MiniBuddy', 'ShouldRunKeychainSyncMiniBuddy',
                                   'ShouldRunMesaMiniBuddy', 'ShouldRunPaymentMiniBuddy', 'ShouldRunStandardMiniBuddy',
                                   'SilentICDPUpgrade')})
            merge_pref(mount, 'com.apple.managedconfiguration.notbackedup', {
                'LockdownActivationIndicatesCloudConfigurationAvailable': False})
            for sub in ('Bundle', 'Data/System', 'Shared/SystemGroup', 'Temp'):
                d = mount / 'containers' / sub
                if not d.is_dir():
                    d.mkdir(parents=True)
                    os.chmod(d, 0o755)
                    print('mkdir', d.relative_to(mount))
        finally:
            subprocess.run(['hdiutil', 'detach', device], check=True)
        if raw.read_bytes()[32:36] != b'NXSB':
            sys.exit('The edited image is no APFS container any more')
        metadata = packed_ramdisk.pack(raw, output)

    # Structural and preference verification of the packed output.
    with tempfile.TemporaryDirectory(prefix='ios16-verify-', dir='/tmp') as td:
        raw = Path(td) / 'seed.raw'
        logical = expand(output.read_bytes(), raw)
        mount = Path(td) / 'mount'
        mount.mkdir()
        device = attach(raw, mount)
        try:
            prefs = {}
            for name in ('com.apple.migration', 'com.apple.purplebuddy', 'com.apple.springboard',
                         'com.apple.purplebuddy.notbackedup', 'com.apple.managedconfiguration.notbackedup'):
                p = mount / 'mobile/Library/Preferences' / (name + '.plist')
                prefs[name] = plistlib.loads(p.read_bytes())
            volume_uuid = plistlib.loads(subprocess.check_output(
                ['diskutil', 'info', '-plist', device]))['VolumeUUID']
        finally:
            subprocess.run(['hdiutil', 'detach', device], check=True)
        fsck = subprocess.run(['fsck_apfs', '-n', str(raw)], capture_output=True, text=True)

    report = {
        'result': 'IOS16_SKIP_SETUP_SEED_VERIFIED',
        'base_dbr_sha256': hashlib.sha256(blob).hexdigest(),
        'output': {'bytes': output.stat().st_size,
                   'sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
                   'logical_bytes': logical},
        'packed': metadata,
        'build': BUILD, 'version': VERSION, 'skip_setup': True, 'no_auto_dim': no_auto_dim,
        'volume_uuid': volume_uuid,
        'migration': prefs['com.apple.migration'],
        'purplebuddy': prefs['com.apple.purplebuddy'],
        'springboard_keys': sorted(prefs['com.apple.springboard']),
        'fsck_apfs_exit': fsck.returncode,
        'fsck_apfs_tail': fsck.stdout.strip().splitlines()[-3:] if fsck.stdout.strip() else [],
        'ownership_preserved': False,
        'source': 'out/p23/ios-16.7.11/scripts/seed-data-skip-setup-20H360.py; local 20H392 constant adaptation',
        'phone_operations': [],
    }
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
