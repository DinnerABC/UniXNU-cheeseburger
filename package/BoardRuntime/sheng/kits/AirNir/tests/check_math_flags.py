#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Ensure source math permissions do not silently permit extra rounding changes."""
import argparse
import ctypes
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
env = dict(os.environ, IR3_SHADER_DEBUG='asmroundtrip')


def run(command, code=0):
    r = subprocess.run([str(x) for x in command], env=env, capture_output=True, text=True, timeout=30)
    if r.returncode != code:
        raise RuntimeError(f'Unexpected status {r.returncode}: {r.stderr[-2500:]}')
    return r.stdout


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


source = Path(__file__).with_name('fma_vertex.ll').read_text()
call = '  %f = call <2 x float> @air.fma.v2f32(<2 x float> %a, <2 x float> %b, <2 x float> %c)'
rng = random.Random(20261019)
cases = [[f32(rng.uniform(-32, 32)) for _ in range(6)] for _ in range(4096)]
# This input gives zero for separate multiplication/addition, whereas fused
# arithmetic returns -2^-46. Both must compile without changing their meaning.
cases[0] = [f32(1 + 2**-23)] * 2 + [f32(1 - 2**-23)] * 2 + [-1.0] * 2
case_path = a.out / 'math.cases'
case_path.write_bytes(b''.join(struct.pack('<I6f', 0, *v) for v in cases))
reports = []
for flags in ('', 'nnan ninf nsz', 'fast'):
    tag = 'strict' if not flags else 'finite' if flags != 'fast' else 'fast'
    ll, bc, prefix = a.out / (tag + '.ll'), a.out / (tag + '.bc'), a.out / tag
    modifier = flags + ' ' if flags else ''
    ll.write_text(source.replace(call, f'  %product = fmul {modifier}<2 x float> %a, %b\n  %f = fadd {modifier}<2 x float> %product, %c'))
    run([a.llvm_bindir / 'llvm-as', ll, '-o', bc])
    meta = json.loads(run([a.compiler, bc, a.chip, prefix, '--compile', '--test-evaluate-int32', case_path]))
    raw = prefix.with_suffix('.eval').read_bytes()
    for i, values in enumerate(cases):
        got = struct.unpack_from('<4f', raw, i * 16)
        expected = [f32(f32(values[j] * values[2+j]) + values[4+j]) for j in range(2)]
        if got[:2] != tuple(expected):
            raise RuntimeError('Source fmul/fadd changed separate rounding')
    nir = prefix.with_suffix('.nir').read_text()
    math = [l for l in nir.splitlines() if '= fadd ' in l or '= fmul ' in l]
    if not math or any('// exact' not in l for l in math):
        raise RuntimeError('Separate rounding was not protected in actual NIR')
    if not flags and any('preserve:sz,inf,nan' not in l for l in math):
        raise RuntimeError('Strict source lost special-value preservation')
    if flags and any('preserve:' in l for l in math):
        raise RuntimeError('Explicit special-value source flags were not propagated')
    assembly = prefix.with_suffix('.asm').read_text()
    if 'mad.f32' in assembly or meta['reciprocal_lowerings']:
        raise RuntimeError('Unrequested contraction/reciprocal appeared')
    reports.append(dict(name=tag, numeric_cases=len(cases), separate_rounding=True,
                        exact_nir_controls=True, binary_bytes=meta['binary_bytes'], asm_roundtrip=meta['asm_roundtrip']))
# Allowed division must remain in pre-backend NIR and be legalized separately.
for flags, accepted in [('arcp', True), ('', False)]:
    tag = 'permitted-div' if accepted else 'strict-div'
    ll, bc, prefix = a.out / (tag + '.ll'), a.out / (tag + '.bc'), a.out / tag
    ll.write_text(source.replace(call, '  %f = fdiv ' + (flags + ' ' if flags else '') + '<2 x float> %a, %b'))
    run([a.llvm_bindir / 'llvm-as', ll, '-o', bc])
    output = run([a.compiler, bc, a.chip, prefix, '--compile'], code=0 if accepted else 4)
    if accepted:
        meta = json.loads(output)
        pre = prefix.with_suffix('.nir').read_text()
        post = prefix.with_suffix('.backend.nir').read_text()
        if '= fdiv ' not in pre or '= fdiv ' in post or '= frcp ' not in post or meta['reciprocal_lowerings'] != 2:
            raise RuntimeError('Division legalization did not match source arcp permission')
reports.append(dict(permitted_division_legalized=True, strict_division_refused=True))
(a.out / 'report.json').write_text(json.dumps(dict(reports=reports, generated_isa_executed=False), indent=2) + '\n')
print('PASS: per-instruction special-value controls, exact rounding and source-gated reciprocal lowering')
