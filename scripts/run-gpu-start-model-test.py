#!/usr/bin/env python3
"""Compile and run the host models for the CP startup, SMMU walker and PAS.

Offline only: no IOKit, no device. Results are written under
out/p21/gpu-cp/host-tests/<UTC>/ with source/header/binary hashes so the
failure-cleanup, walker and PAS state machines can be audited.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

R = Path(__file__).resolve().parents[1]
OUT = R / 'out/p21/gpu-cp/host-tests'
TESTS = [
    ('gpu_start_model', 'tests/gpu_start_model.cpp'),
    ('qcom_pas_model', 'tests/qcom_pas_model.cpp'),
    ('qcom_zap_model', 'tests/qcom_zap_model.cpp'),
]
HEADERS = [
    'package/BoardRuntime/CheeseburgerGpuStart.h',
    'package/BoardRuntime/CheeseburgerGpuWalk.h',
    'package/BoardRuntime/QcomPAS.h',
    'package/BoardRuntime/CheeseburgerZap.h',
]


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def compiler():
    for cand in ('/usr/bin/clang++', shutil.which('clang++')):
        if cand and Path(cand).is_file():
            return str(cand)
    sys.exit('clang++ not found')


def main():
    dst = OUT / time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())
    dst.mkdir(parents=True)
    cc = compiler()
    version = subprocess.run([cc, '--version'], capture_output=True, text=True).stdout.splitlines()[0]
    record = {
        'compiler': cc,
        'compiler_version': version,
        'headers': {h: sha(R / h) for h in HEADERS},
        'tests': {},
    }
    ok = True
    for name, src in TESTS:
        binary = dst / name
        build = subprocess.run([cc, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                                str(R / src), '-o', str(binary)], capture_output=True, text=True)
        entry = {
            'source': src,
            'source_sha256': sha(R / src),
            'build_stderr': build.stderr,
            'result': 'COMPILE_FAIL' if build.returncode else None,
            'stdout': '', 'stderr': '', 'returncode': None,
        }
        if build.returncode:
            ok = False
        else:
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            entry.update(stdout=run.stdout, stderr=run.stderr, returncode=run.returncode,
                         binary_sha256=sha(binary),
                         result='PASS' if run.returncode == 0 else 'FAIL')
            ok = ok and run.returncode == 0
        record['tests'][name] = entry
    record['result'] = 'HOST_MODELS_PASS' if ok else 'HOST_MODELS_FAIL'
    (dst / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    summary = {n: {'result': e['result'], 'stdout': e['stdout'].strip()} for n, e in record['tests'].items()}
    print(json.dumps({'result': record['result'], 'dir': str(dst.relative_to(R)), 'tests': summary}))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
