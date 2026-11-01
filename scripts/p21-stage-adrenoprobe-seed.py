#!/usr/bin/env python3
"""Stage the owner build of adrenoprobed and its launchd jobs into a copy of the
10 GiB Data seed.

The kernel's boot-time GPU addon install writes these files while it is still
inside the Data mount, which hangs this board before the persistent log writer
runs. Staging them in the seed removes that write; the owner trust module still
authorizes the binary, and `sheng-gpu-addon=1` only has to mount the daemon
directory over /System/Library/LaunchDaemons.

BASE is never modified; the output identity changes, so the device reseeds Data.
The raw image is expanded under /tmp because macOS refuses to mount a volume
below the NAS share.
"""
from pathlib import Path
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile

R = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(R / '.work/p21-official-framework/drivers/DarwinBoot/Test'))
import packed_ramdisk  # noqa: E402

C = R / 'out/p21/atos-gate'
BASE = Path(os.environ.get('P21_SEED_BASE', C / 'data-seed/owner-data-10g.dbr'))
BUILD = Path(os.environ.get('P21_PROBE_BUILD', C / 'userland'))
OUT = Path(os.environ.get('P21_SEED_OUT', C / 'data-seed/owner-data-10g-adrenoprobe.dbr'))


def main():
    owner = json.loads((BUILD / 'build.json').read_text())
    daemon = (BUILD / 'adrenoprobed').read_bytes()
    if hashlib.sha256(daemon).hexdigest() != owner['daemon_sha256']:
        sys.exit('daemon differs from build.json')
    blob = BASE.read_bytes()
    logical, extents = packed_ramdisk.regions(blob)
    with tempfile.TemporaryDirectory(prefix='seed-stage-', dir='/tmp') as td:
        raw = Path(td) / 'seed.raw'
        with raw.open('wb') as f:
            f.truncate(logical)
            for offset, data in extents:
                f.seek(offset)
                f.write(data)
        mount = Path(td) / 'mount'
        mount.mkdir()
        attached = plistlib.loads(subprocess.check_output(
            ['hdiutil', 'attach', '-imagekey', 'diskimage-class=CRawDiskImage', '-nobrowse', '-owners', 'off',
             '-mountpoint', str(mount), '-plist', str(raw)]))
        device = attached['system-entities'][0]['dev-entry']
        try:
            if not (mount / 'mobile').is_dir():
                sys.exit('the Data seed has no /mobile')
            # xpc is the mount source for the owner service-cache union; the
            # kernel writes the augmented launchd.plist into it at boot.
            for directory in ('mobile/ngpu', 'mobile/ngpu/bin', 'mobile/ngpu/daemons', 'mobile/ngpu/xpc',
                              'mobile/ngpu/developer/usr/local/libexec',
                              'mobile/ngpu/cryptex/App/usr/libexec'):
                (mount / directory).mkdir(parents=True, exist_ok=True)
                os.chmod(mount / directory, 0o755)
            (mount / 'mobile/ngpu/bin/adrenoprobed').write_bytes(daemon)
            os.chmod(mount / 'mobile/ngpu/bin/adrenoprobed', 0o755)
            # The sealed service cache starts com.apple.factory.NFQRCoded at boot
            # and expects this developer path; the image ships /Developer empty.
            (mount / 'mobile/ngpu/developer/usr/local/libexec/NFQRCoded').write_bytes(daemon)
            os.chmod(mount / 'mobile/ngpu/developer/usr/local/libexec/NFQRCoded', 0o755)
            # The cache's boot-time XPC-activity job starts this App-cryptex path
            # through xpcproxy; the image does not mount that cryptex.
            (mount / 'mobile/ngpu/cryptex/App/usr/libexec/webbookmarksd').write_bytes(daemon)
            os.chmod(mount / 'mobile/ngpu/cryptex/App/usr/libexec/webbookmarksd', 0o755)
            # The kernel's Extensions union presents this directory to the
            # original Metal discovery, which loads the owner bundle in backboardd.
            addons = mount / 'mobile/ngpu/addons'
            addons.mkdir(parents=True, exist_ok=True)
            os.chmod(addons, 0o755)
            shutil.rmtree(addons / 'NativeAdrenoMetal.bundle', ignore_errors=True)
            shutil.copytree(BUILD / 'NativeAdrenoMetal.bundle', addons / 'NativeAdrenoMetal.bundle')
            for path in [addons / 'NativeAdrenoMetal.bundle',
                         *(addons / 'NativeAdrenoMetal.bundle').rglob('*')]:
                os.chmod(path, 0o755 if path.is_dir() or path.name == 'NativeAdrenoMetal' else 0o644)
            # Copy only the addon's own daemon plists (build.json), never stale
            # shadows of system jobs from an earlier experiment.
            for entry in owner.get('addon_files', []):
                if not entry.startswith('daemons/'):
                    continue
                plist = BUILD / Path(entry).name
                shutil.copyfile(plist, mount / 'mobile/ngpu/daemons' / plist.name)
                os.chmod(mount / 'mobile/ngpu/daemons' / plist.name, 0o644)
        finally:
            subprocess.run(['hdiutil', 'detach', device], check=True)
        if raw.read_bytes()[32:36] != b'NXSB':
            sys.exit('the edited image is no longer an APFS container')
        OUT.unlink(missing_ok=True)  # owner derivative only; BASE is never touched
        metadata = packed_ramdisk.pack(raw, OUT)
    report = dict(result='ADRENOPROBE_SEED_STAGE_PASS', base_sha256=hashlib.sha256(blob).hexdigest(),
                  output=metadata, daemon_sha256=owner['daemon_sha256'],
                  daemon_cdhash=owner['daemon_cdhash'],
                  files=['mobile/ngpu/bin/adrenoprobed', 'mobile/ngpu/daemons/*.plist',
                         'mobile/ngpu/developer/usr/local/libexec/NFQRCoded',
                         'mobile/ngpu/cryptex/App/usr/libexec/webbookmarksd',
                         'mobile/ngpu/addons/NativeAdrenoMetal.bundle'],
                  seed_identity_changed=True, hardware_executed=False)
    OUT.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
