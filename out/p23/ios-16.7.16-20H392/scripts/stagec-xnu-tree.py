#!/usr/bin/env python3
"""Assemble the Stage C xnu-8796 tree and apply the project series strictly.

The tree is self-contained under out/p23/ios-16.7.16-20H392/tree.  Official
framework scripts are copied, modules are linked, and the xnu sources come from
the retained Apple tarball extraction.  The 61-patch series is applied with
``git apply`` after a per-patch ``--check``; no fuzz, no rejects.

Nothing outside the Stage C directory is written.  The P21 tree, the P23
ios-16.7.11 outputs and the phone are untouched.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

R = Path(__file__).resolve().parents[4]
STAGE = R / 'out/p23/ios-16.7.16-20H392'
TREE = STAGE / 'tree'
FW = R / '.work/p21-official-framework'
P21 = R / 'out/p21/official-adaptation/resolved'
FEEDS = P21 / 'feeds'
SOURCE = STAGE / 'source/xnu-xnu-8796.141.3'
SERIES = STAGE / 'patches-8796/series-8796.list'
STAMP = STAGE / 'patches-8796/tree.json'
GIT_ENV = {
    'GIT_AUTHOR_NAME': 'UniXNU Stage C',
    'GIT_AUTHOR_EMAIL': 'stage-c@localhost',
    'GIT_COMMITTER_NAME': 'UniXNU Stage C',
    'GIT_COMMITTER_EMAIL': 'stage-c@localhost',
}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(cmd, cwd=None, check=True, env=None):
    environment = dict(os.environ)
    if env:
        environment.update(env)
    result = subprocess.run([str(part) for part in cmd], cwd=cwd, capture_output=True, text=True, env=environment)
    if check and result.returncode:
        raise SystemExit(f'command failed ({result.returncode}): {cmd}\n{result.stdout}\n{result.stderr}')
    return result


def link(target, path):
    path = Path(path)
    target = Path(target)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() or path.exists():
        if path.is_symlink() and path.resolve() == target.resolve():
            return
        if path.is_dir() and not path.is_symlink():
            raise SystemExit(f'refusing to replace a real directory: {path}')
        path.unlink()
    path.symlink_to(target.resolve())


def copy_scripts():
    source = FW / 'scripts'
    destination = TREE / 'scripts'
    copied = 0
    for path in sorted(source.rglob('*')):
        relative = path.relative_to(source)
        if path.is_dir() or '__pycache__' in path.parts:
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.is_file() and target.read_bytes() == path.read_bytes():
            continue
        shutil.copy2(path, target)
        copied += 1
    return copied


def assemble_links():
    for name in ('refer', 'build', '.cache', 'kits', 'drivers', 'patches'):
        (TREE / name).mkdir(parents=True, exist_ok=True)
    link(R / 'package', TREE / 'package')
    link(R / 'package', TREE / 'kits/cheeseburger')
    link(FW / 'kits/XnuPrelink', TREE / 'kits/XnuPrelink')
    link(FW / 'kits/XnuPrelink', TREE / 'kits/NativePrelink')
    link(FW / 'kits/VerifiedRoot', TREE / 'kits/VerifiedRoot')
    link(FW / 'components.toml', TREE / 'components.toml')
    for name in ('ArmCpu', 'ArmDTPlatform', 'RamBlock', 'AppleKeyStoreState', 'SoftwareAES',
                 'VirtioMmioBlock', 'VirtioMmioConsole', 'QcomGeniSpi'):
        if (FW / 'drivers' / name).is_dir():
            link(FW / 'drivers' / name, TREE / 'drivers' / name)
    link(R / 'patches/xnu', TREE / 'patches/xnu')
    for name, directory in (('libplatform', 'libplatform-0f31118b'),
                            ('libdispatch', 'libdispatch-d8f167ce'),
                            ('AvailabilityVersions', 'AvailabilityVersions-6928d18a'),
                            ('IOStorageFamily', 'IOStorageFamily-67f9003c'),
                            ('corecrypto-2023', 'corecrypto-2023-c0651bae')):
        feed = FEEDS / directory
        if not feed.is_dir():
            raise SystemExit(f'missing feed: {feed}')
        link(feed, TREE / 'refer' / name)
    sources = TREE / '.cache/sources'
    sources.mkdir(parents=True, exist_ok=True)
    for name in ('xnu-driverkit-10063.121.3.json', 'xnu-driverkit-crypto-10063.121.3.json',
                 'xnu-trustcache-8796.101.5.json'):
        source = P21 / 'cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree/.cache/sources' / name
        if source.is_file():
            shutil.copy2(source, sources / name)


def git_repository():
    repo = TREE / 'refer/xnu'
    if (repo / '.git').is_dir():
        head = run(['git', 'rev-parse', 'HEAD'], cwd=repo, check=False)
        if head.returncode == 0:
            return repo
        run(['git', 'add', '-A', '-f', '.'], cwd=repo)
        run(['git', 'commit', '--quiet', '-m', 'xnu-8796.141.3 Apple source tarball'], cwd=repo, env=GIT_ENV)
        return repo
    if repo.exists():
        shutil.rmtree(repo)
    shutil.copytree(SOURCE, repo, symlinks=True)
    run(['git', 'init', '--quiet', str(repo)])
    run(['git', 'add', '-A', '-f', '.'], cwd=repo)
    run(['git', 'commit', '--quiet', '-m', 'xnu-8796.141.3 Apple source tarball'], cwd=repo, env=GIT_ENV)
    return repo


def work_tree(commit):
    work = TREE / '.cache/xnu-work'
    if work.exists() and ((work / '.git').is_dir()):
        head = run(['git', 'rev-parse', 'HEAD'], cwd=work).stdout.strip()
        if head == commit:
            return work
        shutil.rmtree(work)
    work.parent.mkdir(parents=True, exist_ok=True)
    run(['git', 'clone', '--quiet', '--shared', '--no-checkout', str(TREE / 'refer/xnu'), str(work)])
    run(['git', 'checkout', '--quiet', '--detach', commit], cwd=work)
    return work


def series_rows():
    rows = []
    for line in SERIES.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        path = Path(line)
        if not path.is_absolute():
            path = R / path
        if not path.is_file():
            raise SystemExit(f'series entry missing: {line}')
        rows.append({'path': path, 'relative': line, 'sha256': sha256(path)})
    return rows


def apply_series(work, commit):
    rows = series_rows()
    signature = {'xnu_commit': commit,
                 'patches': [{'path': row['relative'], 'sha256': row['sha256']} for row in rows]}
    if STAMP.is_file():
        try:
            if json.loads(STAMP.read_text()).get('signature') == signature:
                return rows, True
        except ValueError:
            pass
    run(['git', 'reset', '--hard', '--quiet', commit], cwd=work)
    run(['git', 'clean', '-fdxq'], cwd=work)
    for number, row in enumerate(rows, 1):
        check = run(['git', 'apply', '--check', '--whitespace=nowarn', str(row['path'])], cwd=work, check=False)
        if check.returncode:
            raise SystemExit(f'hunk {number} {row["relative"]} does not apply:\n{check.stderr}')
        applied = run(['git', 'apply', '--whitespace=nowarn', str(row['path'])], cwd=work, check=False)
        if applied.returncode:
            raise SystemExit(f'apply {number} {row["relative"]} failed:\n{applied.stderr}')
    rejects = [str(p.relative_to(work)) for p in work.rglob('*.rej')]
    if rejects:
        raise SystemExit('rejects produced: ' + ', '.join(rejects))
    STAMP.write_text(json.dumps({'signature': signature, 'applied': len(rows),
                                 'work_tree': str(work.relative_to(R)), 'tree': str(TREE.relative_to(R))},
                                indent=2) + '\n')
    return rows, False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rebuild-repository', action='store_true')
    args = parser.parse_args()
    if not SOURCE.is_dir():
        raise SystemExit(f'missing Apple source: {SOURCE}')
    if args.rebuild_repository:
        for path in (TREE / 'refer/xnu', TREE / '.cache/xnu-work'):
            if path.exists():
                shutil.rmtree(path)
        STAMP.unlink(missing_ok=True)
    TREE.mkdir(parents=True, exist_ok=True)
    copied = copy_scripts()
    assemble_links()
    repo = git_repository()
    commit = run(['git', 'rev-parse', 'HEAD'], cwd=repo).stdout.strip()
    work = work_tree(commit)
    rows, reused = apply_series(work, commit)
    lines = [f'xnu_commit: {commit}', f'series_patches: {len(rows)}', f'applied_now: {not reused}']
    (STAGE / 'logs').mkdir(parents=True, exist_ok=True)
    (STAGE / 'logs/stagec-tree.log').write_text('\n'.join(lines) + '\n')
    print(json.dumps({'result': 'STAGEC_XNU_TREE_READY', 'tree': str(TREE.relative_to(R)),
                      'xnu_commit': commit, 'scripts_copied': copied, 'patches': len(rows),
                      'patches_applied_now': not reused}, indent=2))


if __name__ == '__main__':
    main()
