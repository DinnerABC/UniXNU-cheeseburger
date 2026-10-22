#!/usr/bin/env python3
"""P1 non-flashing runner. All generated files stay inside this device repository."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
import tomllib

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'out'
LOCK = json.loads((ROOT / 'sources.lock.json').read_text())
BLOCKERS = {
    'kernel': ['Full Xcode and iphoneos SDK; record exact builds',
               'Qualified MSM8998 archives and driver configuration',
               'Independent IPSW identity, kernelcache SHA256 and prelink policy'],
    'firmware': ['Linux/GCC5 baseline for pinned edk2-msm8998',
                 'UniXNU/DarwinBoot compatibility adapter for existing EDK2',
                 'Vendor input inventory and runtime protocol evidence'],
    'payload': ['Qualified kernel.macho',
                'MSM8998 ADT generator and verified RAM/CPU/GIC/PSCI map',
                'Reviewed Trust Cache policy and any owner-only seed'],
    'bootimg': ['Qualified firmware and verified stock Android header/DTB evidence',
                'Packaging adapter: existing EDK2 FD already has an ARM64 header'],
    'userdata': ['Read-only GPT evidence for exact device variant',
                 'Qualified IPSW and root-volume policy',
                 'Outside P1 execution scope'],
}


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')


def run(label, command, *, cwd=ROOT, env=None, expected=0):
    """Stream complete stdout/stderr and save command, timing and real exit code."""
    OUT.mkdir(exist_ok=True)
    command = list(map(str, command))
    print('+ ' + ' '.join(command), flush=True)
    started = time.time()
    with (OUT / f'{label}.log').open('w') as log:
        proc = subprocess.Popen(command, cwd=cwd, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for line in proc.stdout:
            print(line, end='', flush=True)
            log.write(line)
        code = proc.wait()
    write_json(OUT / f'{label}.json', {
        'command': command, 'cwd': str(cwd), 'exit_code': code,
        'expected_exit_code': expected, 'started_epoch': started,
        'duration_seconds': time.time() - started,
    })
    if code != expected:
        raise RuntimeError(f'{label}: exit {code}, expected {expected}')


def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()


def pin(path, key):
    actual = git(path, 'rev-parse', 'HEAD')
    if actual != LOCK[key]['commit']:
        raise RuntimeError(f'{key}: expected {LOCK[key]["commit"]}, got {actual}')
    if git(path, 'diff', 'HEAD', '--'):
        raise RuntimeError(f'{key}: tracked source differs from pinned commit')
    return actual


def audit(upstream):
    pin(upstream, 'unixnu')
    board = tomllib.loads((ROOT / 'board.toml').read_text())
    if (board['board']['name'], board['board']['soc'], board['board'].get('example')) != (
            'cheeseburger', 'MSM8998', True):
        raise RuntimeError('P1 identity or incomplete-board guard changed')
    if set(board) != {'board', 'kernel'} or set(board['kernel']) != {'defconfig'}:
        raise RuntimeError('Unqualified board sections require independent review')
    sys.path.insert(0, str(upstream / 'scripts'))
    from unixnu.config import parse_config
    if parse_config((ROOT / 'configs/cheeseburger_defconfig').read_text()):
        raise RuntimeError('P1 intentionally enables no unqualified kernel drivers')
    base = [sys.executable, upstream / 'unixnu']
    args = ['-D', ROOT, '--out', OUT / 'upstream']
    run('defconfig', [*base, 'defconfig', 'cheeseburger_defconfig', *args])
    run('incomplete-board-refusal', [*base, 'build', 'kernel', *args], expected=1)
    if 'EXAMPLE device repository' not in (OUT / 'incomplete-board-refusal.log').read_text():
        raise RuntimeError('Upstream refused for an unexpected reason')
    return {'upstream': LOCK['unixnu'], 'check': 'defconfig parsing + expected pre-build refusal'}


def package(upstream, edk2):
    pin(upstream, 'unixnu')
    pin(edk2, 'edk2')
    if platform.system() != 'Darwin':
        raise RuntimeError('P1 qualified package host: macOS')
    for name in ['clang', 'lld-link', 'llvm-lib', 'llvm-rc', 'make']:
        if not shutil.which(name):
            raise RuntimeError(f'missing tool on PATH: {name}')
    make_version = subprocess.check_output(['make', '--version'], text=True)
    match = re.search(r'GNU Make (\d+)\.(\d+)', make_version)
    if not match or tuple(map(int, match.groups())) < (4, 3):
        raise RuntimeError('GNU make >=4.3 required; prepend brew make/libexec/gnubin')
    versions = {}
    for name, key in [('clang', 'llvm'), ('lld-link', 'lld')]:
        version = subprocess.check_output([name, '--version'], text=True)
        if not re.search(r'(?<![\d.])' + re.escape(LOCK['toolchain'][key]) + r'(?![\d.])', version):
            raise RuntimeError(f'Unqualified toolchain version: {version}')
        versions[name] = version
    required = ['MdePkg/Library/BaseFdtLib/libfdt', 'MdePkg/Library/MipiSysTLib/mipisyst',
                'BaseTools/Source/C/BrotliCompress/brotli']
    states = subprocess.check_output(['git', '-C', str(edk2), 'submodule', 'status', '--', *required], text=True)
    (OUT / 'edk2-submodules.txt').write_text(states)
    if len(states.splitlines()) != len(required) or any(not s.startswith(' ') for s in states.splitlines()):
        raise RuntimeError('Required EDK2 submodules missing or differ from pinned gitlinks')
    for path in required:
        if git(edk2 / path, 'diff', 'HEAD', '--'):
            raise RuntimeError(f'Modified submodule source: {path}')
    toolbin = OUT / 'toolchain'
    toolbin.mkdir(exist_ok=True)
    for name in ['clang', 'lld-link', 'llvm-lib', 'llvm-rc']:
        link = toolbin / name
        if link.is_symlink():
            link.unlink()
        link.symlink_to(shutil.which(name))
    env = dict(os.environ, UNIXNU_EDK2=str(edk2), UNIXNU_BUILD_DIR=str(OUT / 'package'),
               PYTHON_COMMAND=sys.executable, JOBS='4', CLANG_BIN=str(toolbin) + '/',
               SOURCE_DATE_EPOCH=git(upstream, 'log', '-1', '--format=%ct'))
    run('basetools', ['make', '-C', edk2 / 'BaseTools/Source/C', '-j4'], env=env)
    run('package', ['bash', upstream / 'package/build.sh', '-b', 'DEBUG', '-t', 'CLANGPDB'], env=env)
    products = OUT / 'package/Build/Out/AARCH64-DEBUG'
    manifest = {}
    for name in ['UniXNU', 'UniXNUDxe', 'HelloPayload', 'SampleDriver', 'XnuTest']:
        f = products / (name + '.efi')
        data = f.read_bytes()
        if data[:2] != b'MZ':
            raise RuntimeError(f'{f}: missing MZ header')
        offset = int.from_bytes(data[60:64], 'little')
        if (data[offset:offset+4] != b'PE\0\0' or
                int.from_bytes(data[offset+4:offset+6], 'little') != 0xaa64):
            raise RuntimeError(f'{f}: not AArch64 PE/COFF')
        manifest[str(f.relative_to(ROOT))] = {
            'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    write_json(OUT / 'package-artifacts.json', manifest)
    return {'products': manifest, 'toolchain': versions, 'sources': LOCK}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=['audit', 'package', *BLOCKERS])
    parser.add_argument('--upstream', type=Path)
    parser.add_argument('--edk2', type=Path)
    args = parser.parse_args()
    OUT.mkdir(exist_ok=True)
    # Tools that consult TMPDIR and Python bytecode remain inside the project too.
    (OUT / 'tmp').mkdir(exist_ok=True)
    os.environ['TMPDIR'] = str(OUT / 'tmp')
    sys.dont_write_bytecode = True
    report = {'stage': args.stage, 'hardware_validated': False,
              'host': platform.platform(), 'python': sys.version}
    code = 0
    try:
        if args.stage in BLOCKERS:
            report.update(result='BLOCKED', dependencies=BLOCKERS[args.stage])
            code = 78
        else:
            if not args.upstream:
                raise RuntimeError('--upstream is required')
            if args.stage == 'audit':
                report.update(audit(args.upstream.resolve()))
            else:
                if not args.edk2:
                    raise RuntimeError('--edk2 is required')
                report.update(package(args.upstream.resolve(), args.edk2.resolve()))
            report['result'] = 'PASS'
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        code = 1
        report.update(result='FAIL', error=str(error))
    report['exit_code'] = code
    write_json(OUT / f'{args.stage}-result.json', report)
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return code


if __name__ == '__main__':
    sys.exit(main())
