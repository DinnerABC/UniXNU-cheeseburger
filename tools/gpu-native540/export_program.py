#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Export pinned compiler-owned A540 compute metadata/ISA for OFFLINE use only.

Finite buffer/shared/uniform-dispatch subset, not a full upstream validator or
IR3 security verifier. No upload, device discovery, compiler invocation or ABI.
All omitted reflection fields must be canonical inactive values. 621 words are
metadata, NEVER ISA. Output is a new directory; previous evidence is preserved.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile

SAGIT = '673f85070fe9f7872793015eb230a5c3987d4a8c'
MESA = '769e51468b49b2a42f0a0eaf71cf9eed5ff4e5de'
FLAGS = 4 | 8 | 32 | 64 | 128 | (1 << 26)
PREFIX_FIELDS = '''id stage codeWords instrlen constlen fullRegs halfRegs branchStack
threadSize subgroupSize immediateBase immediateCount driverBase driverCount localID
groupID vertexID position color fragCoord fragMask bufferMask writeMask textureCount
flags instanceID instanceMask varyingCount totalIn perspective linear centerRHW
sharedBytes threadgroupMask imageMask imageReadMask imageWriteMask imageUintMask
sampleMask samplerMask explicitSamplerMask cubeSamplerMask fetchMask queryMask'''.split()
TAIL_FIELDS = dict(attributeMask=372, implicitSamplerMask=437, constantSamplerMask=438,
    constantImplicitSamplerMask=439, constantExplicitSamplerMask=440,
    constantCubeSamplerMask=441, framebufferMask=442, colorMask=587, depthReg=596,
    multisampleMask=597, depthTextureMask=598, volumeMask=599, imageblockMask=600,
    imageblockBindings=601, imageblockSampleBytes=602, sampleFlags=603, sampleID=604,
    sampleMaskIn=605, sampleMaskOut=606, perspectiveCentroid=607, linearCentroid=608,
    perspectiveSample=609, linearSample=610, textureBindings=611, constantWindowCount=612)
TYPED_FIELDS = '''codeWords instrlen constlen fullRegs halfRegs branchStack threadSize
subgroupSize immediateBase immediateCount driverBase driverCount localID groupID
bufferMask writeMask flags sharedBytes threadgroupMask'''.split()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def u32(value):
    return type(value) is int and 0 <= value <= 0xffffffff


def checked_hash(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{64}', value):
        raise ValueError('missing/invalid provenance hash')
    return value


def memory_prefix(p):
    return 112 if p['flags'] & (1 << 26) else 80 if p['flags'] & 128 else 16 if p['threadgroupMask'] else 0


def validate_reflection(p):
    fields = dict(enumerate(PREFIX_FIELDS))
    mapping = {name: index for index, name in fields.items()} | TAIL_FIELDS
    if set(p) != set(mapping) | {'descriptionWords'}:
        raise ValueError('unknown/missing reflection field; unsupported profile')
    words = p['descriptionWords']
    if not isinstance(words, list) or len(words) != 621 or not all(u32(x) for x in words):
        raise ValueError('reflection must contain exactly 621 uint32 metadata words')
    for name, index in mapping.items():
        if not u32(p[name]) or p[name] != words[index]:
            raise ValueError('named/raw reflection mismatch: ' + name)
    live = {mapping[name] for name in TYPED_FIELDS} | set(range(44, 52))
    sentinels = {mapping[name] for name in ('vertexID', 'position', 'color', 'fragCoord',
                                          'instanceID', 'perspective', 'linear', 'centerRHW')}
    for index, value in enumerate(words):
        if index in live:
            continue
        if value != (252 if index in sentinels else 0):
            raise ValueError('unsupported/noncanonical reflection word ' + str(index))
    n = p['constlen'] * 4
    if (not 32 <= p['codeWords'] <= 4096 or p['codeWords'] % 32 or
        not 1 <= p['instrlen'] <= 128 or not p['instrlen'] * 32 <= p['codeWords'] <= (p['instrlen'] + 1) * 32 or
        p['constlen'] > 128 or p['constlen'] % 4 or not 1 <= p['fullRegs'] <= 63 or p['halfRegs'] > 63 or
        p['branchStack'] > 32 or p['threadSize'] > 1 or p['subgroupSize'] not in (32, 64) or
        p['bufferMask'] > 255 or p['writeMask'] & ~p['bufferMask'] or p['flags'] & ~FLAGS or
        p['localID'] > 252 or p['groupID'] > 252 or
        (p['localID'] != 252 and (p['localID'] > 249 or p['localID'] + 3 > p['fullRegs'] * 4)) or
        p['sharedBytes'] > 32768 or p['sharedBytes'] % 4 or p['threadgroupMask'] > 255 or
        bool(p['threadgroupMask']) != bool(p['flags'] & 64) or
        (p['threadgroupMask'] and p['sharedBytes'] % 16) or
        (p['flags'] & 32 and p['branchStack'] > 3) or
        (p['flags'] & (1 << 26) and not p['flags'] & 8)):
        raise ValueError('invalid/unsupported compute allocation or flags')
    spans = []
    for name, limit in [('immediate', 512), ('driver', 16)]:
        base, count = p[name + 'Base'], p[name + 'Count']
        if base > n or count > n - base or count > limit or (count and base < memory_prefix(p)):
            raise ValueError('constant span/prefix violation')
        if count:
            spans.append((base, base + count))
    if n < memory_prefix(p) or (len(spans) == 2 and spans[0][0] < spans[1][1] and spans[1][0] < spans[0][1]):
        raise ValueError('constant overlap/allocation violation')
    for i, stride in enumerate(words[44:52]):
        active = bool(p['bufferMask'] & (1 << i))
        if (active and stride not in (2, 4, 8, 16)) or (not active and stride):
            raise ValueError('invalid/inactive buffer stride')
    return words


def export_program(manifest_path, output):
    manifest_path, output = Path(manifest_path).resolve(), Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError('output exists; refusing to replace evidence')
    raw = manifest_path.read_bytes()
    if len(raw) > 1024 * 1024:
        raise ValueError('manifest size exceeds bound')
    m = json.loads(raw)
    if (type(m.get('schema')) is not int or m['schema'] != 1 or m.get('gpu') != 540 or
        m.get('jitVersion') != 41 or m.get('route') != 'AIR-NIR-IR3' or m.get('runtimeVerified') is not False or
        m.get('targetOSBuild') != '21H461'):
        raise ValueError('wrong fixed compiler/OS profile')
    source = m['source']
    if source.get('sagit') != SAGIT or source.get('mesa') != MESA or source.get('gpu') != 540:
        raise ValueError('unpinned compiler source')
    for name in ('compiler_sha256', 'backend_sha256', 'recipe_sha256', 'builder_sha256', 'wrapper_source_sha256'):
        checked_hash(source[name])
    checked_hash(m['input']['sha256'])
    entry = m['entry']
    if not isinstance(entry, str) or not entry or len(entry.encode()) >= 96 or '\0' in entry:
        raise ValueError('invalid entry')
    variants = m['variants']
    if not isinstance(variants, list) or len(variants) != 1 or variants[0]['name'] != 'base':
        raise ValueError('only ordinary compute base variant supported')
    v, p = variants[0], variants[0]['info']
    reflection = validate_reflection(p)
    if (not isinstance(v['immediates'], list) or len(v['immediates']) != p['immediateCount'] or
        not all(u32(x) for x in v['immediates'])):
        raise ValueError('invalid immediate words')
    if v['shader'] != 'program.base.ir3':
        raise ValueError('invalid shader path')
    code = (manifest_path.parent / v['shader']).read_bytes()
    if len(code) != p['codeWords'] * 4 or digest(code) != checked_hash(v['sha256']):
        raise ValueError('ISA size/hash mismatch')
    isa = struct.unpack('<' + 'I' * p['codeWords'], code)
    # Deliberately separate ISA and reflection; only typed admitted fields are
    # consumed by encoder. Complete original reflection remains in metadata.
    def array(values):
        return ', '.join('0x%08xu' % x for x in values)
    header = ('// Generated offline only; not IR3 validation or device execution.\n'
              '// SPDX-License-Identifier: GPL-2.0-only\n#pragma once\n'
              '#include "NativeMetal540Compute.h"\nnamespace Native540Export {\n'
              'inline constexpr uint32_t code[] = {' + array(isa) + '};\n'
              'inline constexpr uint32_t immediates[] = {' + array(v['immediates'] or [0]) + '};\n'
              'inline constexpr NativeMetal540::Program program = {{' +
              array([p[k] for k in TYPED_FIELDS]) + ', {' + array(reflection[44:52]) + '}}, code, immediates};\n'
              'inline constexpr char codeSHA256[] = "' + digest(code) + '";\n'
              'inline constexpr char sagitRevision[] = "' + SAGIT + '";\n}\n').encode()
    metadata = dict(schema=1, profile='native540-compute-finite-subset-v1', entry=entry,
        targetOSBuild='21H461', gpu=540, runtimeVerified=False, ir3SecurityValidated=False,
        restrictions=['offline only', 'uniform dispatch only', '8 logical buffers; 16 hardware descriptors',
                      'no textures/images/samplers/graphics/tiles/matrices/windows',
                      'owner must attest ISA upload, physical disjointness and resource lifetime'],
        source=source, input=m['input'], manifest_sha256=digest(raw),
        reflection_sha256=digest(struct.pack('<621I', *reflection)), reflection=p,
        shader_sha256=digest(code), codeWords=p['codeWords'], immediateWords=v['immediates'],
        header_sha256=digest(header), exporter_sha256=digest(Path(__file__).read_bytes()))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.native540-export-', dir=output.parent) as name:
        tmp = Path(name)
        (tmp / 'program.h').write_bytes(header)
        (tmp / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        # mkdir reserves the destination atomically, including against races;
        # never rename over an existing empty directory or symlink.
        output.mkdir()
        for item in tmp.iterdir():
            item.rename(output / item.name)
    return output / 'program.h'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        print(export_program(args.manifest, args.output))
    except (ValueError, OSError, KeyError, TypeError, struct.error) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
