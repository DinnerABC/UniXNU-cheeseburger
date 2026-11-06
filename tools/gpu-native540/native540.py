#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fail-closed subprocess wrapper for pinned native540-compiler output."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

PIN = '673f85070fe9f7872793015eb230a5c3987d4a8c'
ROOT = Path('/Users/dinnerabc/unixnu-p21/native540')


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def validate(directory, manifest):
    if manifest.get('schema') != 1 or manifest.get('gpu') != 540 or manifest.get('jitVersion') != 41 or manifest.get('route') != 'AIR-NIR-IR3':
        raise ValueError('wrong compiler profile')
    variants = manifest.get('variants', [])
    if not variants or len(variants) > 4:
        raise ValueError('invalid variant count')
    names = [v['name'] for v in variants]
    if len(names) != len(set(names)) or names[0] != 'base' or set(names) - {'base', 'msaa', 'constantWindow', 'msaaConstantWindow'}:
        raise ValueError('invalid variant names')
    stage = variants[0]['info']['stage']
    if stage not in (0, 1, 2) or ((stage == 2) != ('msaa' in names)):
        raise ValueError('fragment variant mismatch')
    for v in variants:
        p = v['info']
        w = p['codeWords']
        if p['stage'] != stage or not isinstance(w, int) or not 32 <= w <= 4096 or w % 32:
            raise ValueError('invalid shader size or stage')
        if not 1 <= p['instrlen'] <= 128 or not p['instrlen'] * 32 <= w <= (p['instrlen'] + 1) * 32:
            raise ValueError('invalid instruction length')
        if not 0 <= p['constlen'] <= 128 or p['constlen'] % 4 or not 1 <= p['fullRegs'] <= 63 or not 0 <= p['halfRegs'] <= 63:
            raise ValueError('invalid register/constant allocation')
        if p['threadSize'] not in (0, 1) or p['subgroupSize'] not in (32, 64):
            raise ValueError('invalid thread profile')
        if any(not 0 <= p[k] <= 252 for k in ('localID', 'groupID', 'vertexID', 'position', 'color', 'fragCoord', 'instanceID')):
            raise ValueError('invalid sysval register')
        words = p.get('descriptionWords', [])
        if len(words) != 621 or any(type(x) is not int or not 0 <= x <= 0xffffffff for x in words):
            raise ValueError('incomplete reflection description')
        # Verify named metadata is the same production description, not a
        # second untrusted allocation that could alter command state.
        for index, key in enumerate(('id', 'stage', 'codeWords', 'instrlen', 'constlen', 'fullRegs', 'halfRegs', 'branchStack',
                                     'threadSize', 'subgroupSize', 'immediateBase', 'immediateCount', 'driverBase', 'driverCount',
                                     'localID', 'groupID', 'vertexID', 'position', 'color', 'fragCoord', 'fragMask', 'bufferMask', 'writeMask', 'textureCount', 'flags')):
            if words[index] != p[key]:
                raise ValueError('reflection field mismatch')
        for k in ('immediate', 'driver'):
            if not 0 <= p[k + 'Base'] <= p['constlen'] * 4 or not 0 <= p[k + 'Count'] <= p['constlen'] * 4 - p[k + 'Base']:
                raise ValueError('constant span exceeds allocation')
        if len(v['immediates']) != p['immediateCount'] or any(type(x) is not int or not 0 <= x <= 0xffffffff for x in v['immediates']):
            raise ValueError('invalid immediate output')
        code = directory / ('program.' + v['name'] + '.ir3')
        if not code.is_file() or code.stat().st_size != w * 4:
            raise ValueError('shader output size mismatch')
        v['shader'] = code.name
        v['sha256'] = sha(code)
    return manifest


def compile_library(library, entry, output, compiler=ROOT / 'native540-compiler', timeout=120):
    library = Path(library).resolve()
    output = Path(output).resolve()
    if output.exists():
        raise ValueError('output already exists; preserve previous evidence')
    provenance = json.loads((compiler.parent / 'build.json').read_text())
    if provenance.get('sagit') != PIN or provenance.get('gpu') != 540 or sha(compiler) != provenance['compiler_sha256']:
        raise ValueError('unpinned compiler')
    backend = compiler.parent / 'obj/gpu-compiler/host/src/freedreno/QcomGPUCompiler.dylib'
    if sha(backend) != provenance['backend_sha256']:
        raise ValueError('backend hash mismatch')
    if not entry or len(entry.encode()) >= 96 or '\0' in entry:
        raise ValueError('invalid entry')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.native540-', dir=output.parent) as tmp:
        tmp = Path(tmp)
        if not 0 < library.stat().st_size <= 64 * 1024 * 1024:
            raise ValueError('input size exceeds archive bound')
        snapshot = tmp / 'input.metallib'
        shutil.copyfile(library, snapshot)
        input_hash = sha(snapshot)
        input_bytes = snapshot.stat().st_size
        proc = subprocess.run([compiler, snapshot, entry, tmp / 'program'], capture_output=True, text=True, timeout=timeout)
        if proc.returncode:
            raise ValueError('AIR-NIR-IR3 compiler failed: ' + proc.stderr.strip())
        manifest = validate(tmp, json.loads((tmp / 'program.json').read_text()))
        manifest.update(input={'path': str(library), 'sha256': input_hash, 'bytes': input_bytes},
                        source=provenance, runtimeVerified=False, targetOSBuild='21H461',
                        frontend='controlled-offline; no system MTLDevice publication')
        snapshot.unlink()
        (tmp / 'program.json').write_text(json.dumps(manifest, indent=2) + '\n')
        # Atomic publication only after all shader variants and metadata pass.
        Path(tmp).rename(output)
    return output / 'program.json'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('library', type=Path)
    p.add_argument('entry')
    p.add_argument('output', type=Path)
    p.add_argument('--compiler', type=Path, default=ROOT / 'native540-compiler')
    a = p.parse_args()
    try:
        print(compile_library(a.library, a.entry, a.output, a.compiler))
    except (ValueError, OSError, subprocess.TimeoutExpired) as e:
        p.exit(1, str(e) + '\n')


if __name__ == '__main__':
    main()
