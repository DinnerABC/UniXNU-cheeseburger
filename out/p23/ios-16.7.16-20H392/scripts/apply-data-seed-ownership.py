#!/usr/bin/env python3
"""Apply the 20H392 /private/var ownership manifest to a packed Data seed.

  --dry-run: expand the seed, compare it with the manifest, print coverage.
             No chown, no repack, no root required.
  real run : sudo python3 apply-data-seed-ownership.py SEED.dbr MANIFEST.tsv OUTPUT.dbr
             Mounts the expanded seed read-write with owners enabled, applies
             uid/gid/mode, verifies, fsck -n, then repacks as DBRDISK1.
"""
import hashlib, json, os, plistlib, stat, subprocess, sys, tempfile
from pathlib import Path

R = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(R / '.work/p21-official-framework/drivers/DarwinBoot/Test'))
import packed_ramdisk  # noqa: E402

TOP_OVERRIDES = {
    'mobile': (501, 501, 0o711),
    'db': (0, 0, 0o755),
    'run': (0, 1, 0o775),
    'tmp': (0, 0, 0o1777),
}


def attach(image, mount, owners, readonly=False):
    command = ['hdiutil', 'attach']
    if readonly:
        command.append('-readonly')
    command += ['-imagekey', 'diskimage-class=CRawDiskImage', '-nobrowse',
                '-owners', owners, '-mountpoint', str(mount), '-plist', str(image)]
    info = plistlib.loads(subprocess.check_output(command))
    for entity in info['system-entities']:
        if os.path.realpath(entity.get('mount-point', '')) == os.path.realpath(str(mount)):
            return entity['dev-entry']
    raise SystemExit('could not find mounted volume')


def expand(blob, raw):
    logical, extents = packed_ramdisk.regions(blob)
    with raw.open('wb') as f:
        f.truncate(logical)
        for offset, data in extents:
            f.seek(offset)
            f.write(data)
    return logical


def walk_paths(root):
    paths = {}
    for current, dirs, files in os.walk(root, followlinks=False):
        rel_dir = Path(current).relative_to(root)
        dirs.sort()
        files.sort()
        for name in dirs + files:
            path = Path(current) / name
            rel = str(rel_dir / name)
            try:
                st = path.lstat()
            except OSError:
                continue
            if stat.S_ISLNK(st.st_mode):
                kind = 'l'
            elif stat.S_ISDIR(st.st_mode):
                kind = 'd'
            elif stat.S_ISREG(st.st_mode):
                kind = 'f'
            else:
                kind = '?'
            paths[rel] = kind
    return paths


def read_manifest(path):
    rows = []
    for line in path.read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        rel, kind, uid, gid, mode = line.split('\t')
        rows.append((rel, kind, int(uid), int(gid), int(mode, 8)))
    return rows


def main():
    args = [arg for arg in sys.argv[1:] if arg != '--dry-run']
    dry_run = '--dry-run' in sys.argv[1:]
    if len(args) != 3:
        sys.exit(__doc__)
    seed, manifest, output = (Path(arg).resolve() for arg in args)
    if not seed.is_file() or not manifest.is_file():
        sys.exit('missing seed or manifest')
    if not dry_run and output.exists():
        sys.exit('refuse to overwrite ' + str(output))
    if not dry_run and os.geteuid() != 0:
        sys.exit('real ownership pass requires root; rerun with sudo')
    rows = read_manifest(manifest)

    with tempfile.TemporaryDirectory(prefix='seed-owner-', dir='/tmp') as td:
        raw = Path(td) / 'seed.raw'
        logical = expand(seed.read_bytes(), raw)
        mount = Path(td) / 'mount'
        mount.mkdir()
        device = attach(raw, mount, 'off' if dry_run else 'on', readonly=dry_run)
        try:
            seed_paths = walk_paths(mount)
            matched = [(rel, kind, uid, gid, mode) for rel, kind, uid, gid, mode in rows
                       if rel in seed_paths]
            manifest_paths = {rel for rel, *_ in rows}
            manifest_missing = len(rows) - len(matched)
            extra = sorted(set(seed_paths) - {rel for rel, *_ in rows})
            type_mismatch = [rel for rel, kind, *_ in matched if seed_paths[rel] != kind]
            if dry_run:
                report = {
                    'result': 'IOS16_SEED_OWNERSHIP_DRY_RUN',
                    'seed': str(seed), 'manifest': str(manifest), 'logical_bytes': logical,
                    'seed_entries': len(seed_paths),
                    'manifest_entries': len(rows),
                    'matched': len(matched),
                    'manifest_not_in_seed': manifest_missing,
                    'seed_only': len(extra),
                    'seed_only_sample': extra[:40],
                    'type_mismatch': type_mismatch,
                    'top_overrides': {k: [uid, gid, format(mode, '04o')]
                                      for k, (uid, gid, mode) in TOP_OVERRIDES.items()},
                    'prefs_override': 'mobile/Library/Preferences/*.plist -> 501:501 0600',
                    'phone_operations': [],
                }
                print(json.dumps(report, indent=2))
                return

            applied, apply_errors = 0, []
            for rel, kind, uid, gid, mode in matched:
                path = mount / rel
                try:
                    os.chown(path, uid, gid, follow_symlinks=False)
                    if kind != 'l':
                        os.chmod(path, mode)
                    applied += 1
                except OSError as error:
                    apply_errors.append([rel, str(error)])
            for rel, (uid, gid, mode) in TOP_OVERRIDES.items():
                path = mount / rel
                if path.is_dir():
                    os.chown(path, uid, gid, follow_symlinks=False)
                    os.chmod(path, mode)
            for pref in sorted((mount / 'mobile/Library/Preferences').glob('*.plist')):
                if str(pref.relative_to(mount)) in manifest_paths:
                    continue
                os.chown(pref, 501, 501, follow_symlinks=False)
                os.chmod(pref, 0o600)
            volume_uuid = plistlib.loads(subprocess.check_output(
                ['diskutil', 'info', '-plist', device]))['VolumeUUID']
        finally:
            subprocess.run(['hdiutil', 'detach', device], check=True)

        fsck = subprocess.run(['fsck_apfs', '-n', str(raw)], capture_output=True, text=True)
        if fsck.returncode != 0:
            sys.exit('fsck_apfs failed after ownership pass: ' + fsck.stdout[-2000:])

        mismatch = []
        mount = Path(td) / 'verify'
        mount.mkdir()
        device = attach(raw, mount, 'on', readonly=True)
        try:
            for rel, kind, uid, gid, mode in matched:
                st = (mount / rel).lstat()
                if (st.st_uid, st.st_gid) != (uid, gid) or (kind != 'l' and stat.S_IMODE(st.st_mode) != mode):
                    mismatch.append(rel)
        finally:
            subprocess.run(['hdiutil', 'detach', device], check=True)

        metadata = packed_ramdisk.pack(raw, output)
    report = {
        'result': 'IOS16_SEED_OWNERSHIP_COMPLETE',
        'seed': str(seed), 'manifest': str(manifest),
        'output': {'path': str(output), 'bytes': output.stat().st_size,
                   'sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
                   'logical_bytes': logical},
        'packed': metadata,
        'volume_uuid': volume_uuid,
        'matched': len(matched), 'applied': applied,
        'manifest_not_in_seed': manifest_missing,
        'seed_only': len(extra),
        'seed_only_sample': extra[:40],
        'type_mismatch': type_mismatch,
        'metadata_mismatch': mismatch,
        'apply_errors': apply_errors,
        'top_overrides': {k: [uid, gid, format(mode, '04o')]
                          for k, (uid, gid, mode) in TOP_OVERRIDES.items()},
        'prefs_override': 'mobile/Library/Preferences/*.plist -> 501:501 0600',
        'fsck_apfs_exit': fsck.returncode,
        'ownership_preserved': not mismatch and not apply_errors,
        'phone_operations': [],
    }
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
