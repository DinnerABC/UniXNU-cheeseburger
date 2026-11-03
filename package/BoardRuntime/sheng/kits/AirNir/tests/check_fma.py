#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check integer-built FMA NIR before/after int64 lowering against native fmaf."""
import argparse
import ctypes
import hashlib
import itertools
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--compiler', required=True, type=Path)
p.add_argument('--llvm-bindir', required=True, type=Path)
p.add_argument('--out', required=True, type=Path)
p.add_argument('--chip', required=True)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
env = {k: v for k, v in os.environ.items() if not k.startswith(('IR3_', 'FD_MESA_', 'NIR_', 'MESA_SHADER_'))}
env['IR3_SHADER_DEBUG'] = 'asmroundtrip'


def run(cmd, timeout=30):
    r = subprocess.run([str(x) for x in cmd], capture_output=True, text=True, env=env, timeout=timeout)
    if r.returncode:
        raise RuntimeError(f'Status {r.returncode}: {cmd[0]}\n{r.stderr[-3000:]}')
    return r.stdout


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def f32(bits):
    return struct.unpack('<f', struct.pack('<I', bits))[0]


def bits(value):
    try:
        return struct.unpack('<I', struct.pack('<f', value))[0]
    except OverflowError:
        return 0xff800000 if value < 0 else 0x7f800000


def nan(value):
    return value & 0x7fffffff > 0x7f800000


native_fma = ctypes.CDLL(None).fmaf
native_fma.argtypes = [ctypes.c_float] * 3
native_fma.restype = ctypes.c_float
# A known cancellation example must distinguish a fused result from two
# separately rounded operations. Also test finite overflow cancellation.
known = [(0x3f800001, 0x3f7ffffe, 0xbf800000, 0xa8800000),
         (0x7f7fffff, 0x40000000, 0xff7fffff, 0x7f7fffff)]
for x, y, z, expected in known:
    got = bits(native_fma(f32(x), f32(y), f32(z)))
    unfused = bits(f32(bits(f32(x) * f32(y))) + f32(z))
    if got != expected or unfused == expected:
        raise RuntimeError('Host reference did not pass fused/unfused distinction')

special = [0, 1, 2, 0x003fffff, 0x007fffff, 0x00800000, 0x00800001,
           0x3eaaaaab, 0x3f000000, 0x3f7fffff, 0x3f800000, 0x3f800001,
           0x40000000, 0x7f7fffff, 0x7f800000, 0x7fc00000]
special += [v | 0x80000000 for v in special]
triples = list(itertools.product(special, repeat=3))
counts = {'special_cartesian': len(triples)}
rng = random.Random(20261017)
triples += [tuple(rng.getrandbits(32) for _ in range(3)) for _ in range(32768)]
counts['random_raw_bits'] = 32768
for _ in range(32768):
    x, y = [(rng.randrange(60, 191) << 23) | rng.getrandbits(23) | (rng.getrandbits(1) << 31) for _ in range(2)]
    z = bits(f32(x) * f32(y)) ^ 0x80000000
    triples.append((x, y, z))
counts['product_cancellation'] = 32768
for i in range(32768):
    exp = rng.randrange(25, 254)
    y = (exp << 23) | rng.getrandbits(23)
    z = ((exp - 24) << 23) + (i % 3 - 1)
    z |= rng.getrandbits(1) << 31
    triples.append((0x3f800000, y, z))
counts['halfway_and_neighbors'] = 32768
offset = counts['special_cartesian']
triples[offset:offset+2] = [(x, y, z) for x, y, z, _ in known]
counts['random_raw_bits'] -= 2
counts['directed_fused_controls'] = 2
if len(triples) != 131072:
    raise RuntimeError('Fixed bounded test inventory differs')
case_file = a.out / 'fma.cases'
records = []
for first, second in zip(triples[::2], triples[1::2]):
    records.append(struct.pack('<7I', 0, first[0], second[0], first[1], second[1], first[2], second[2]))
case_file.write_bytes(b''.join(records))
reference = [bits(native_fma(*(f32(x) for x in t))) for t in triples]
source = Path(__file__).with_name('fma_vertex.ll')
bc = a.out / 'fma.bc'
run([a.llvm_bindir / 'llvm-as', source, '-o', bc])
reports = []
for mode in ('--test-evaluate', '--test-evaluate-int32'):
    name = 'wide-intermediates' if mode == '--test-evaluate' else 'lowered-int32'
    prefix = a.out / name
    for suffix in ('.bin', '.asm', '.nir', '.eval'):
        prefix.with_suffix(suffix).unlink(missing_ok=True)
    start = time.monotonic()
    meta = json.loads(run([a.compiler, bc, a.chip, prefix, '--compile', mode, case_file]))
    elapsed = time.monotonic() - start
    raw = prefix.with_suffix('.eval').read_bytes()
    if len(raw) != len(records) * 16:
        raise RuntimeError('FMA test record output length differs')
    for i, expected in enumerate(reference):
        got = struct.unpack_from('<I', raw, (i // 2) * 16 + (i % 2) * 4)[0]
        if got != expected and not (nan(got) and nan(expected)):
            raise RuntimeError(f'{name}, case {i}: {[hex(x) for x in triples[i]]}, got {got:08x}, expected {expected:08x}')
    nir = prefix.with_suffix('.nir').read_text()
    assembly = prefix.with_suffix('.asm').read_text()
    if ' = ffma ' in nir or 'mad.f32' in assembly or not meta['asm_roundtrip'] or meta['gpu_executed']:
        raise RuntimeError('FMA must compile as integer arithmetic with real encoding roundtrip')
    reports.append(dict(mode=name, cases=len(reference), cases_by_kind=counts, seconds=elapsed,
                        ieee_rne_equal=True, nan_payload_compared=False,
                        signed_zeros_compared=True, subnormals_compared=True,
                        binary_bytes=prefix.with_suffix('.bin').stat().st_size,
                        binary_sha256=sha(prefix.with_suffix('.bin')), nir_sha256=sha(prefix.with_suffix('.nir')),
                        asm_roundtrip=True, gpu_executed=False))
    print(f'PASS: {name}: {len(reference)} FMA results vs host fmaf, {elapsed:.2f}s', flush=True)
report = dict(reports=reports, source_sha256=sha(source), cases_sha256=sha(case_file),
              fused_unfused_negative_controls=len(known), host_only=True,
              complete_input_domain_exhausted=False, generated_isa_executed=False)
(a.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
