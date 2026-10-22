#!/usr/bin/env python3
"""Fetch exact public source commits into .work/sources; keep logs inside the project."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from p1 import LOCK, OUT, ROOT, pin, run, write_json

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--references', action='store_true', help='also fetch the four read-only reference repositories')
args = parser.parse_args()
OUT.mkdir(exist_ok=True)
(OUT / 'tmp').mkdir(exist_ok=True)
os.environ['TMPDIR'] = str(OUT / 'tmp')
os.environ['GIT_TERMINAL_PROMPT'] = '0'
base = ROOT / '.work/sources'
base.mkdir(parents=True, exist_ok=True)
names = ['unixnu', 'edk2']
if args.references:
    names += ['reference_device', 'reference_uefi', 'msm8998', 'darwinboot']
result = {}
try:
    for name in names:
        path = base / name
        if not path.exists():
            path.mkdir()
            run(f'fetch-{name}-init', ['git', 'init', '-q', path])
            run(f'fetch-{name}-remote', ['git', '-C', path, 'remote', 'add', 'origin',
                                      'https://github.com/' + LOCK[name]['repository'] + '.git'])
        # Refuse to reset any tracked edits in an existing checkout.
        head = subprocess.run(['git', '-C', str(path), 'rev-parse', '--verify', 'HEAD'],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if head.returncode == 0:
            dirty = subprocess.check_output(['git', '-C', str(path), 'diff', 'HEAD', '--'], text=True)
            if dirty.strip():
                raise RuntimeError(f'{path}: tracked changes; will not reset')
        run(f'fetch-{name}-commit', ['git', '-C', path, 'fetch', '--depth', '1',
                                    'origin', LOCK[name]['commit']])
        run(f'fetch-{name}-checkout', ['git', '-C', path, 'checkout', '--detach', LOCK[name]['commit']])
        result[name] = {'path': str(path), 'commit': pin(path, name)}
    modules = ['MdePkg/Library/BaseFdtLib/libfdt', 'MdePkg/Library/MipiSysTLib/mipisyst',
               'BaseTools/Source/C/BrotliCompress/brotli',
               'MdeModulePkg/Library/BrotliCustomDecompressLib/brotli',
               'SecurityPkg/DeviceSecurity/SpdmLib/libspdm']
    run('fetch-edk2-submodules', ['git', '-C', base / 'edk2', 'submodule', 'update',
                                '--init', '--depth', '1', '--', *modules])
    write_json(OUT / 'fetch-result.json', {'result': 'PASS', 'sources': result})
except (OSError, RuntimeError, subprocess.SubprocessError) as error:
    write_json(OUT / 'fetch-result.json', {'result': 'FAIL', 'error': str(error), 'sources': result})
    sys.exit(1)
print(json.dumps(result, indent=2))
