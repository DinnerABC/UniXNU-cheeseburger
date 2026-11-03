#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Qualify exact 0/1-coefficient FMA simplification and every required flag."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--compiler', type=Path, required=True)
p.add_argument('--llvm-bindir', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--chip', required=True)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
env = {k: v for k, v in os.environ.items() if not k.startswith(('IR3_', 'FD_MESA_', 'NIR_', 'MESA_SHADER_'))}
env['IR3_SHADER_DEBUG'] = 'asmroundtrip'


def run(cmd):
    r = subprocess.run([str(x) for x in cmd], env=env, capture_output=True, text=True, timeout=30)
    if r.returncode:
        raise RuntimeError(f'Status {r.returncode}: {r.stderr[-3000:]}')
    return r.stdout


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


original = Path(__file__).with_name('fma_vertex.ll').read_text()
original = original.replace('  %atmp =', '  %iszero = icmp eq i16 %vertex, 0\n  %coefficient = select i1 %iszero, float 0.0, float 1.0\n  %atmp =')
original = original.replace('float %a0, i64 0', 'float %coefficient, i64 0').replace('float %a1, i64 1', 'float %coefficient, i64 1')
rng = random.Random(20261018)
cases = []
vertices = [0, 1, 65535, 65536, 65537, 0xffffffff]
for i in range(4096):
    vertex = vertices[i % len(vertices)]
    values = [rng.uniform(-32, 32) for _ in range(6)]
    if i < 64:
        values[2:] = [0.0, -0.0, -0.0, 0.0]
    cases.append((vertex, values))
case_path = a.out / 'coefficient.cases'
case_path.write_bytes(b''.join(struct.pack('<I6f', v, *values) for v, values in cases))
native_fma = ctypes.CDLL(None).fmaf
native_fma.argtypes = [ctypes.c_float] * 3
native_fma.restype = ctypes.c_float
reports = []
for flags in ('nnan ninf nsz', 'ninf nsz', 'nnan nsz', 'nnan ninf'):
    eligible = flags == 'nnan ninf nsz'
    tag = 'qualified' if eligible else 'without-' + next(f for f in ('nnan', 'ninf', 'nsz') if f not in flags.split())
    ll, bc, prefix = a.out / (tag + '.ll'), a.out / (tag + '.bc'), a.out / tag
    ll.write_text(original.replace('  %f = call ', '  %f = call ' + flags + ' '))
    run([a.llvm_bindir / 'llvm-as', ll, '-o', bc])
    meta = json.loads(run([a.compiler, bc, a.chip, prefix, '--compile', '--test-evaluate-int32', case_path]))
    raw = prefix.with_suffix('.eval').read_bytes()
    if len(raw) != len(cases) * 16:
        raise RuntimeError('Coefficient output framing')
    for i, (vertex, values) in enumerate(cases):
        # Source input is stored as float32; round values before oracle use.
        values = struct.unpack('<6f', struct.pack('<6f', *values))
        coefficient = 0.0 if vertex & 65535 == 0 else 1.0
        actual = struct.unpack_from('<4I', raw, i * 16)
        for j in range(2):
            expected = struct.unpack('<I', struct.pack('<f', native_fma(coefficient, values[2 + j], values[4 + j])))[0]
            if actual[j] != expected and not (eligible and (actual[j] & 0x7fffffff) == 0 and (expected & 0x7fffffff) == 0):
                raise RuntimeError(f'{tag}: coefficient case {i}, lane {j}, {actual[j]:08x} != {expected:08x}')
    nir = prefix.with_suffix('.nir').read_text()
    if eligible and any('// exact' not in line for line in nir.splitlines() if '= fadd ' in line):
        raise RuntimeError('Exact coefficient sum must forbid additional contraction/reassociation')
    if eligible and ('imul' in nir or 'umul' in nir):
        raise RuntimeError('Qualified 0/1 coefficient did not bypass integer FMA')
    if not eligible and 'imul' not in nir and 'umul' not in nir:
        raise RuntimeError('Missing required flag did not retain strong integer FMA')
    if not meta['asm_roundtrip'] or meta['gpu_executed']:
        raise RuntimeError('Coefficient binary was not checked honestly')
    reports.append(dict(name=tag, flags=flags, simplified=eligible, numeric_cases=len(cases),
                        binary_bytes=meta['binary_bytes'], binary_sha256=sha(prefix.with_suffix('.bin')),
                        zero_sign_ignored=eligible, asm_roundtrip=True, gpu_executed=False))
if any(r['binary_bytes'] < reports[0]['binary_bytes'] * 8 for r in reports[1:]):
    raise RuntimeError('Missing-flag controls failed to distinguish generic FMA cost')
(a.out / 'report.json').write_text(json.dumps(dict(cases_sha256=sha(case_path), reports=reports,
    finite_inputs_only=True, generated_isa_executed=False), indent=2) + '\n')
print('PASS: coefficient FMA: 4096 cases and roundtrip for optimized + three missing-flag controls')
