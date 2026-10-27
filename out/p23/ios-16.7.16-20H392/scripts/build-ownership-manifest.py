#!/usr/bin/env python3
"""Record uid/gid/mode of 20H392 /private/var for the Data seed ownership pass.

The source OS volume is mounted read-only with ownership enabled. Directories
that cannot be listed as the host user are recorded themselves but not
descended; the 20H360 manifest had the same 19-path limitation.
"""
import json, os, plistlib, stat, subprocess, sys, tempfile
from pathlib import Path

R = Path(__file__).resolve().parents[4]
BASE = R / 'out/p23/ios-16.7.16-20H392'
OS_DMG = BASE / 'extracted/087-86698-044.dmg'
OUT_TSV = BASE / 'data-seed/ownership-20H392.tsv'
OUT_JSON = BASE / 'data-seed/ownership-20H392.json'


def attach(image, mount, owners):
    info = plistlib.loads(subprocess.check_output([
        'hdiutil', 'attach', '-readonly', '-nobrowse', '-owners', owners,
        '-mountpoint', str(mount), '-plist', str(image)]))
    for entity in info['system-entities']:
        if os.path.realpath(entity.get('mount-point', '')) == os.path.realpath(str(mount)):
            return entity['dev-entry']
    raise SystemExit('could not find mounted source volume')


def main():
    if OUT_TSV.exists() or OUT_JSON.exists():
        raise SystemExit('refuse to overwrite an existing ownership manifest')
    mount = Path(tempfile.mkdtemp(prefix='ios16-owners-', dir='/tmp'))
    device = attach(OS_DMG, mount, 'on')
    var = mount / 'private/var'
    rows, errors = [], []

    def onerror(error):
        errors.append([str(error.filename), str(error)])

    try:
        if not var.is_dir():
            raise SystemExit('source volume has no /private/var')
        for current, dirs, files in os.walk(var, followlinks=False, onerror=onerror):
            rel_dir = Path(current).relative_to(var)
            dirs.sort()
            files.sort()
            for name in dirs + files:
                path = Path(current) / name
                rel = rel_dir / name
                try:
                    st = path.lstat()
                except OSError as error:
                    errors.append([str(rel), str(error)])
                    if name in dirs:
                        dirs.remove(name)
                    continue
                if stat.S_ISLNK(st.st_mode):
                    kind = 'l'
                elif stat.S_ISDIR(st.st_mode):
                    kind = 'd'
                elif stat.S_ISREG(st.st_mode):
                    kind = 'f'
                else:
                    kind = '?'
                rows.append((str(rel), kind, st.st_uid, st.st_gid,
                             format(stat.S_IMODE(st.st_mode), '04o')))
    finally:
        subprocess.run(['hdiutil', 'detach', device], check=True)
        for path in (mount,):
            try:
                path.rmdir()
            except OSError:
                pass

    rows.sort()
    by_type, by_uid_gid = {}, {}
    for _, kind, uid, gid, _ in rows:
        by_type[kind] = by_type.get(kind, 0) + 1
        key = f'{uid}:{gid}'
        by_uid_gid[key] = by_uid_gid.get(key, 0) + 1
    inaccessible = [[name, message] for name, message in errors if 'Permission denied' in message]
    summary = {
        'result': 'IOS16_VAR_OWNERSHIP_MANIFEST',
        'source': '20H392 /private/var, mounted -owners on',
        'entries': len(rows),
        'by_type': dict(sorted(by_type.items())),
        'by_uid_gid': dict(sorted(by_uid_gid.items(), key=lambda item: -item[1])),
        'inaccessible': len(inaccessible),
        'inaccessible_sample': inaccessible[:20],
        'note': 'Directories with mode 0700 cannot be descended as uid 501; the directory itself is recorded. '
                'Empty service dirs keep the seed build ownership unless overridden.',
        'phone_operations': [],
    }
    OUT_TSV.write_text(
        '# relative_path\ttype\tuid\tgid\tmode (source iOS 16.7.16 /private/var)\n'
        + ''.join(f'{rel}\t{kind}\t{uid}\t{gid}\t{mode}\n' for rel, kind, uid, gid, mode in rows))
    OUT_JSON.write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
