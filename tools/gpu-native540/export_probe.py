#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Export actual pinned GLSL430 -> NIR -> A540 IR3, never AIR/Metal/device output.

The fixed standalone backend accepts GLSL430, not the requested450. Only the
version line changed in the historical globalID source; the second exact source
uses localID, equivalent only for this one group. Ordinary SSBO declarations are frontend-owned in sagit
Compiler.cpp::compileEntries; compile_probe's exact-source frontend supplies
bufferMask/writeMask/stride and removes undeclared varying tails by that same
upstream rule. Both original backend and composed621-word reflections survive.
No allocation, flags, sysval, constant or ISA field may be repaired by exporter.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PIN = '673f85070fe9f7872793015eb230a5c3987d4a8c'
MESA = '769e51468b49b2a42f0a0eaf71cf9eed5ff4e5de'
DEFAULT_MANIFEST = ROOT / 'tools/gpu-native540/evidence/probe-glsl-20261106T0100/manifest.json'
LOCAL_MANIFEST = ROOT / 'tools/gpu-native540/evidence/probe-glsl-local-20261106T0300/manifest.json'
GLSL = ('#version 430\nlayout(local_size_x=64) in;\n'
        'layout(std430,binding=0) buffer B { uint v[]; };\n'
        'void main() { v[gl_GlobalInvocationID.x] = 0xdeadbeefu; }\n')
LOCAL_GLSL = GLSL.replace('gl_GlobalInvocationID.x', 'gl_LocalInvocationID.x')
ADMITTED_SOURCES = (GLSL, LOCAL_GLSL)
spec = importlib.util.spec_from_file_location('native540_export_program', Path(__file__).with_name('export_program.py'))
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def load_program(manifest_path):
    path = Path(manifest_path).resolve()
    raw = path.read_bytes()
    if len(raw) > 1024 * 1024:
        raise ValueError('manifest exceeds bound')
    m = json.loads(raw)
    if (m.get('schema') != 1 or m.get('gpu') != 540 or m.get('jitVersion') != 41 or
        m.get('route') != 'GLSL-NIR-A540-IR3' or m.get('producer') != 'qgpu_compile_glsl' or
        m.get('jitValid') is not True or m.get('metalCompiler') is not False or
        m.get('runtimeVerified') is not False or m.get('compiledGLSLVersion') != 430 or
        m.get('requestedGLSLVersion') != 450 or m.get('entry') != 'Probe540' or
        m.get('dispatch') != {'groups': [1, 1, 1], 'local': [64, 1, 1]} or
        m.get('outputBytes') != 256 or m.get('expected') != 0xdeadbeef or m.get('initial') != 0x13579bdf):
        raise ValueError('wrong actual GLSL probe profile')
    source = m['source']
    if source['sagit'] != PIN or source['mesa'] != MESA or source['gpu'] != 540:
        raise ValueError('unpinned compiler')
    for name in ('compiler_sha256', 'backend_sha256', 'wrapper_source_sha256'):
        profile.checked_hash(source[name])
    if source['backend_sha256'] != json.loads((ROOT/'tools/gpu-native540/evidence/build.json').read_text())['backend_sha256']:
        raise ValueError('wrong existing compiled backend')
    glsl = (path.parent/'probe.comp').read_bytes()
    if glsl not in tuple(s.encode() for s in ADMITTED_SOURCES) or digest(glsl) != m['input']['sha256'] or m['input']['shader'] != 'probe.comp':
        raise ValueError('not the exact actual source')
    variants = m['variants']
    if len(variants) != 1 or variants[0]['name'] != 'base' or variants[0]['shader'] != 'probe.ir3':
        raise ValueError('ordinary compute base only')
    v = variants[0]
    p = v['info']
    reflection = profile.validate_reflection(p)
    if p['localID'] != 0 or p['groupID'] != (252 if glsl == LOCAL_GLSL.encode() else 192):
        raise ValueError('sysval allocation disagrees with exact source')
    if p['bufferMask'] != 1 or p['writeMask'] != 1 or reflection[44:52] != [4, 0, 0, 0, 0, 0, 0, 0]:
        raise ValueError('wrong actual buffer interface')
    backend = m['backendReflection']
    # Lossless backend record: all composed differences MUST be exactly the
    # source-declared interface merge already implemented by fixed upstream.
    composed = json.loads(json.dumps(backend))
    for field, index, expected in [('bufferMask', 21, 1), ('writeMask', 22, 1)]:
        if composed[field] != 0 or composed['descriptionWords'][index] != 0:
            raise ValueError('unexpected backend/frontend interface contract')
        composed[field] = expected
        composed['descriptionWords'][index] = expected
    if composed['descriptionWords'][44:52] != [0]*8:
        raise ValueError('unexpected backend strides')
    composed['descriptionWords'][44] = 4
    # No varyings declared: compileEntries line71 zeroes the entire unused
    # QGPUVarying array. Require the exact raw backend absent-reg sentinel252.
    if composed['descriptionWords'][52:372] != [x for _ in range(32) for x in [0,0,0,0,0,0,252,0,0,0]]:
        raise ValueError('unexpected raw backend varyings')
    composed['descriptionWords'][52:372] = [0]*320
    if composed != p:
        raise ValueError('compiler metadata altered beyond source reflection composition')
    immediates = v['immediates']
    if len(immediates) != p['immediateCount'] or not all(profile.u32(x) for x in immediates):
        raise ValueError('immediate mismatch')
    code = (path.parent/'probe.ir3').read_bytes()
    if len(code) != p['codeWords']*4 or digest(code) != profile.checked_hash(v['sha256']):
        raise ValueError('compiler ISA size/hash mismatch')
    isa = struct.unpack('<'+'I'*p['codeWords'], code)
    # Only this tiny shader's generated ISA profile, not general IR3 validation.
    stores = [(lo, hi) for lo, hi in zip(isa[::2], isa[1::2]) if hi >> 29 == 6]
    if len(stores) != 1 or not stores[0][0] & 1 or (stores[0][1] >> 22) & 31 != 28:
        raise ValueError('not actual A5xx cat6 stgb; reject old A6xx stib.b')
    if glsl == LOCAL_GLSL.encode():
        jit = m['finalJIT']
        data = (path.parent/'probe.jit').read_bytes()
        if (jit['file'] != 'probe.jit' or jit['valid'] is not True or
            len(data) != jit['bytes'] or digest(data) != profile.checked_hash(jit['sha256']) or
            (path.parent/'backend-reflection.bin').read_bytes() != struct.pack('<621I', *backend['descriptionWords']) or
            (path.parent/'final-reflection.bin').read_bytes() != struct.pack('<621I', *reflection)):
            raise ValueError('actual raw reflection/JIT identity mismatch')
    return m, raw, p, reflection, isa, immediates, code


def render_header(p, reflection, isa, immediates, code, glsl=GLSL):
    if glsl not in ADMITTED_SOURCES:
        raise ValueError("unadmitted header source")
    local = glsl == LOCAL_GLSL
    def array(values):
        return ','.join('0x%08xu'%x for x in values)
    return ('// SPDX-License-Identifier: GPL-2.0-only\n#pragma once\n'
            '#include "NativeMetal540Compute.h"\n'
            '// Actual GLSL430 -> NIR -> A540 IR3 (not AIR/metallib/Metal device).\n'
            '// Fixed frontend supports430, requested450 failed; no silent rewrite.\n'
            '// Source reflection composed as sagit Compiler.cpp::compileEntries.\n'
            '// Actual A5xx stgb cat6, NOT the historical A6xx stib.b probe.\n'
            + ('// Exact localID source; one group only, groupID absent (252).\n' if local else '') +
            'namespace Native540ProbeFixture {\n'
            'inline constexpr uint32_t code[]={' + array(isa) + '};\n'
            'inline constexpr uint32_t immediates[]={' + array(immediates or [0]) + '};\n'
            'inline constexpr NativeMetal540::Program program={{' +
            array([p[k] for k in profile.TYPED_FIELDS]) + ',{' + array(reflection[44:52]) + '}},code,immediates};\n'
            'inline constexpr char codeSHA256[]="' + digest(code) + '";\n'
            'inline constexpr char sourceSHA256[]="' + digest(glsl.encode()) + '";\n'
            'inline constexpr char producer[]="qgpu_compile_glsl; GLSL430-NIR-A540-IR3; ' +
            ('exact gl_LocalInvocationID.x; ' if local else '') + 'NOT Metalcompiler";\n'
            'inline constexpr uint32_t outputWords=64, expected=0xdeadbeefu, sentinel=0x13579bdfu;\n'
            'static_assert(sizeof(code)/sizeof(code[0])==program.info.codeWords,"exact compiler code");\n'
            '}\n').encode()


def export_probe(output, manifest=DEFAULT_MANIFEST):
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError('refusing to overwrite fixture evidence')
    m, raw, p, reflection, isa, immediates, code = load_program(manifest)
    glsl = next(s for s in ADMITTED_SOURCES if digest(s.encode()) == m['input']['sha256'])
    header = render_header(p, reflection, isa, immediates, code, glsl)
    meta = dict(m)
    meta.update(shader_sha256=digest(code), source_sha256=m['input']['sha256'],
                codeWords=p['codeWords'], reflection=p,
                reflection_sha256=digest(struct.pack('<621I', *reflection)),
                manifest_sha256=digest(raw), header_sha256=digest(header),
                ir3SecurityValidated=False, modelIsDevice=False,
                restrictions=['one64-thread group', 'SSBO0 uint output; no input buffers',
                    'host model is simulated transport/ISA, not device execution',
                    'not AIR/metallib/Metaldevice completion'])
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.probe-export-', dir=output.parent) as name:
        tmp = Path(name)
        (tmp/'probe.ir3').write_bytes(code)
        (tmp/'program.h').write_bytes(header)
        (tmp/'probe.comp').write_text(glsl)
        (tmp/'metadata.json').write_text(json.dumps(meta, indent=2)+'\n')
        (tmp/'manifest.json').write_bytes(raw)
        if glsl == LOCAL_GLSL:
            for filename in ('probe.jit', 'backend-reflection.bin', 'final-reflection.bin', 'probe.asm'):
                (tmp/filename).write_bytes((Path(manifest).resolve().parent/filename).read_bytes())
        output.mkdir()
        for item in tmp.iterdir():
            item.rename(output/item.name)
    return output/'metadata.json'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--manifest', type=Path, default=DEFAULT_MANIFEST)
    args = parser.parse_args()
    try:
        print(export_probe(args.output, args.manifest))
    except (ValueError, OSError, KeyError, TypeError, struct.error) as error:
        parser.exit(1, str(error)+'\n')
