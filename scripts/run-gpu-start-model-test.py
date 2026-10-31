#!/usr/bin/env python3
"""Compile and run the host register model for CheeseburgerGpu::Startup.

Offline only: no IOKit, no device. The result is written under
out/p21/gpu-cp/host-tests/<UTC>/ together with the source/binary hashes so the
failure-cleanup design can be audited.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

R = Path(__file__).resolve().parents[1]
SRC = R / 'tests/gpu_start_model.cpp'
HEADER = R / 'package/BoardRuntime/CheeseburgerGpuStart.h'
OUT = R / 'out/p21/gpu-cp/host-tests'


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
    binary = dst / 'gpu_start_model'
    build = subprocess.run([cc, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                            str(SRC), '-o', str(binary)],
                           capture_output=True, text=True)
    record = {
        'result': 'HOST_MODEL_COMPILE_FAIL' if build.returncode else None,
        'compiler': cc,
        'compiler_version': subprocess.run([cc, '--version'], capture_output=True, text=True).stdout.splitlines()[0],
        'source': str(SRC.relative_to(R)),
        'source_sha256': sha(SRC),
        'header': str(HEADER.relative_to(R)),
        'header_sha256': sha(HEADER),
        'build_stdout': build.stdout,
        'build_stderr': build.stderr,
        'run_stdout': '',
        'run_returncode': None,
    }
    if build.returncode:
        (dst / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
        print(json.dumps(record))
        return 2
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    record['run_stdout'] = run.stdout
    record['run_stderr'] = run.stderr
    record['run_returncode'] = run.returncode
    record['binary_sha256'] = sha(binary)
    record['result'] = 'HOST_MODEL_PASS' if run.returncode == 0 else 'HOST_MODEL_FAIL'
    (dst / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'result': record['result'], 'dir': str(dst.relative_to(R)),
                      'stdout': run.stdout.strip(), 'returncode': run.returncode}))
    return run.returncode


if __name__ == '__main__':
    sys.exit(main())
