#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exact 21H461 SpriteKit AIR render registry, OFFLINE/BLOCKED only.

Never substitutes GLSL, edits reflection, emits Native540Render::Program, or
operates a device. All siblings/621 words/immediates survive export. Hash gates
are identity checks, not an IR3 security verifier or a general Metal validator.
Usage: export_render_program.py VS_PROGRAM_JSON FS_PROGRAM_JSON NEW_PRIVATE_DIR
       --library ORIGINAL_SPRITEKIT_METALLIB [--toolchain-root LOCAL_NATIVE540]
Exit 2 means valid exact artifacts exported, but the real pair is blocked.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

SAGIT = '673f85070fe9f7872793015eb230a5c3987d4a8c'
MESA = '769e51468b49b2a42f0a0eaf71cf9eed5ff4e5de'
SOURCE_SHA256 = 'eba5da56793ea170037bebe51b78e3afe6768e321becdbb3704d7d9063da4d46'
LIBRARY_SHA256 = 'c9478bb7d167a8fb71c4481c78591482372858d4b852d3696bad7fc9cda0b9b8'
LIBRARY_BYTES = 92788
ROOT = Path('/Users/dinnerabc/unixnu-p21/native540')
PREFIX = '''id stage codeWords instrlen constlen fullRegs halfRegs branchStack
threadSize subgroupSize immediateBase immediateCount driverBase driverCount localID
groupID vertexID position color fragCoord fragMask bufferMask writeMask textureCount
flags instanceID instanceMask varyingCount totalIn perspective linear centerRHW
sharedBytes threadgroupMask imageMask imageReadMask imageWriteMask imageUintMask
sampleMask samplerMask explicitSamplerMask cubeSamplerMask fetchMask queryMask'''.split()
TAIL = dict(attributeMask=372, implicitSamplerMask=437, constantSamplerMask=438,
    constantImplicitSamplerMask=439, constantExplicitSamplerMask=440,
    constantCubeSamplerMask=441, framebufferMask=442, colorMask=587, depthReg=596,
    multisampleMask=597, depthTextureMask=598, volumeMask=599, imageblockMask=600,
    imageblockBindings=601, imageblockSampleBytes=602, sampleFlags=603, sampleID=604,
    sampleMaskIn=605, sampleMaskOut=606, perspectiveCentroid=607, linearCentroid=608,
    perspectiveSample=609, linearSample=610, textureBindings=611, constantWindowCount=612)
FIELDS = dict(zip(PREFIX, range(44))) | TAIL
RENDER_FIELDS = '''stage codeWords instrlen constlen fullRegs halfRegs branchStack
threadSize subgroupSize immediateBase immediateCount driverBase driverCount vertexID
instanceID position color colorMask colorRegister fragCoord perspective linear
centerRHW flags'''.split()
# Independently recompiled by the pinned existing AIR builder; no rewritten
# source declarations. Exact full reflection pins fail on ANY unreviewed word.
PINS = {
    'SolidColor_VertexFunc': dict(stage=1, metalType=0, offset=7321, bytes=3296,
        airSHA256='a9c6c20d04b808dbcd07fc275420c0a5231039b36149b892aa67b31c35217232',
        variants={'base': (
            '8b34838aa3d3b3c8aa1d425cc368e34ef0807d7ef29c7cdb08dca462e9461340',
            'c7374e2982374f5fcb5a2100dab0802dc5d7854a514c061e914c136ea39ef520',
            [65535, 4294967292, 0])}),
    'FastSingle_FragFunc': dict(stage=2, metalType=1, offset=38489, bytes=3680,
        airSHA256='3515e9a4a81f419a64e9e97fed8a86953f2c92d4614356f2b55b375e9fc02e47',
        variants={
            'base': ('e923828a4944af1ed4046aafb287bbf8616a1f1ba60ed4631e6d31471d3c80ea',
                     '103538f53bb60f96576602fe0a9810ecb4224e52bcd63618eabe4e51fa83883e',
                     [3212836864, 1065353216]),
            'msaa': ('e923828a4944af1ed4046aafb287bbf8616a1f1ba60ed4631e6d31471d3c80ea',
                     '95f73ee619023d26ff4a6c8122f95e5ab2b2496e27e7e3e0bfc0d902e4a7af2d',
                     [3212836864, 1065353216])}),
    'SolidColor_FragFunc': dict(stage=2, metalType=1, offset=45993, bytes=3312,
        airSHA256='ff5f07427f82fecee981f05c368f124549900f932bdd6e6686c685292dea252a',
        variants={
            'base': ('bac67cd157e781fa27ff14b82b2bb8b08c8b069ee8f51973842ec4737c36581e',
                     '5a22c9aa2966762fb00a39070282688dbc0d8e470e3c41692c3445f491555b39', [0,4,8,12]),
            'msaa': ('bac67cd157e781fa27ff14b82b2bb8b08c8b069ee8f51973842ec4737c36581e',
                     '984d09e98ed463a419ee84c44b7e5ee28a36327ccb753755fab935928211a979', [0,4,8,12]),
            'constantWindow': ('3034b1723e50663a7d78115a11ceb7a1df1b8b3adf62fc39f1351e9b99859be6',
                               '35a6344f3804bf5badff22a085a3a3c4fcc3e429902882f7045d1711b04d5919', []),
            'msaaConstantWindow': ('3034b1723e50663a7d78115a11ceb7a1df1b8b3adf62fc39f1351e9b99859be6',
                                   '982d0641e691a2ffaf8d65f7b927ef1563d895ff2f283c053acd92e6fdeda84f', [])})}
TOP_FIELDS = {'schema', 'gpu', 'jitVersion', 'route', 'entry', 'runtimeVerified',
    'variants', 'lowerNS', 'backendNS', 'input', 'source', 'targetOSBuild', 'frontend'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()


def u32(value):
    return type(value) is int and 0 <= value <= 0xffffffff


def checked_hash(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{64}', value):
        raise ValueError('invalid provenance hash')
    return value


def require(condition, diagnostic):
    if not condition:
        raise ValueError(diagnostic)


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key: ' + key)
        result[key] = value
    return result


def read_manifest(path):
    path = Path(path)
    require(path.is_file() and not path.is_symlink(), 'manifest must be a regular file')
    require(0 < path.stat().st_size <= 1024 * 1024, 'manifest size exceeds bound')
    raw = path.read_bytes()
    require(len(raw) <= 1024 * 1024, 'manifest changed beyond size bound')
    return raw, json.loads(raw, object_pairs_hook=no_duplicates,
        parse_constant=lambda value: (_ for _ in ()).throw(ValueError('nonfinite JSON: ' + value)))


def validate_source(source):
    require(isinstance(source, dict), 'missing source pins')
    require(source.get('sagit') == SAGIT and source.get('mesa') == MESA and
            type(source.get('gpu')) is int and source['gpu'] == 540, 'wrong source revision/GPU')
    for key in ('compiler_sha256', 'backend_sha256', 'recipe_sha256',
                'builder_sha256', 'wrapper_source_sha256'):
        checked_hash(source.get(key))
    require(isinstance(source.get('adapter_sources'), dict) and source['adapter_sources'],
            'missing full adapter source pins')
    for value in source['adapter_sources'].values():
        checked_hash(value)
    require(digest(canonical(source)) == SOURCE_SHA256,
            'full source pins changed; independently review/recompile, never relabel')


def validate_reflection(info, stage):
    require(isinstance(info, dict) and set(info) == set(FIELDS) | {'descriptionWords'},
            'unknown/missing reflection fields')
    words = info['descriptionWords']
    require(isinstance(words, list) and len(words) == 621 and all(u32(x) for x in words),
            'exactly 621 uint32 reflection words required')
    for name, index in FIELDS.items():
        require(u32(info[name]) and info[name] == words[index], 'named/raw mismatch: ' + name)
    require(info['id'] == 0 and info['stage'] == stage, 'wrong shader stage/id')
    require(32 <= info['codeWords'] <= 4096 and info['codeWords'] % 32 == 0 and
            1 <= info['instrlen'] <= 128 and
            info['instrlen'] * 32 <= info['codeWords'] <= (info['instrlen'] + 1) * 32,
            'invalid code/instruction bounds')
    require(info['constlen'] <= 128 and info['constlen'] % 4 == 0 and
            1 <= info['fullRegs'] <= 63 and info['halfRegs'] <= 63 and
            info['branchStack'] <= 32 and info['threadSize'] <= 1 and
            info['subgroupSize'] in (32, 64), 'invalid allocation')
    spans = []
    for kind, maximum in [('immediate', 512), ('driver', 40 if stage == 1 else 4)]:
        base, count = info[kind + 'Base'], info[kind + 'Count']
        require(base <= info['constlen'] * 4 and count <= info['constlen'] * 4 - base and
                count <= maximum, 'constant span out of bounds')
        if count:
            spans.append((base, base + count))
    require(len(spans) < 2 or spans[0][1] <= spans[1][0] or spans[1][1] <= spans[0][0],
            'constant spans overlap')
    return struct.pack('<621I', *words)


def validate_manifest(manifest, entry):
    """Public metadata identity gate; does NOT attest original AIR or ISA bytes."""
    require(entry in PINS, 'unknown shader entry')
    require(isinstance(manifest, dict) and set(manifest) == TOP_FIELDS, 'unknown/missing manifest fields')
    for key, value in [('schema', 1), ('gpu', 540), ('jitVersion', 41)]:
        require(type(manifest[key]) is int and manifest[key] == value, 'wrong ' + key)
    require(manifest['entry'] == entry and manifest['route'] == 'AIR-NIR-IR3' and
            manifest['targetOSBuild'] == '21H461' and manifest['runtimeVerified'] is False and
            manifest['frontend'] == 'controlled-offline; no system MTLDevice publication',
            'wrong actual AIR/OS/compiler profile')
    for key in ('lowerNS', 'backendNS'):
        require(type(manifest[key]) is int and 0 <= manifest[key] < 2**64, 'invalid timing')
    validate_source(manifest['source'])
    inp = manifest['input']
    require(isinstance(inp, dict) and set(inp) == {'path', 'sha256', 'bytes'} and
            isinstance(inp['path'], str) and 0 < len(inp['path']) < 4096 and '\0' not in inp['path'] and
            inp['sha256'] == LIBRARY_SHA256 and type(inp['bytes']) is int and inp['bytes'] == LIBRARY_BYTES,
            'not the original 21H461 SpriteKit library')
    variants = manifest['variants']
    require(isinstance(variants, list) and all(isinstance(v, dict) for v in variants), 'invalid variants')
    require([v.get('name') for v in variants] == list(PINS[entry]['variants']),
            'missing/duplicate/reordered/unknown shader siblings')
    for variant in variants:
        require(set(variant) == {'name', 'info', 'immediates', 'shader', 'sha256'},
                'unknown/missing variant field')
        pin = PINS[entry]['variants'][variant['name']]
        raw = validate_reflection(variant['info'], PINS[entry]['stage'])
        require(digest(raw) == pin[1], 'unreviewed reflection word/feature; no support claimed')
        require(variant['shader'] == 'program.' + variant['name'] + '.ir3', 'unsafe shader path')
        require(checked_hash(variant['sha256']) == pin[0], 'ISA identity changed')
        immediates = variant['immediates']
        require(isinstance(immediates, list) and all(u32(x) for x in immediates) and
                len(immediates) == variant['info']['immediateCount'] and immediates == pin[2],
                'immediates changed or incomplete')
    return manifest


def read_codes(directory, manifest):
    codes = {}
    for variant in manifest['variants']:
        path = Path(directory) / variant['shader']
        require(path.is_file() and not path.is_symlink(), 'shader must be a regular local file')
        require(path.stat().st_size == variant['info']['codeWords'] * 4, 'ISA byte size mismatch')
        code = path.read_bytes()
        require(len(code) == variant['info']['codeWords'] * 4 and digest(code) == variant['sha256'],
                'ISA byte hash mismatch')
        codes[variant['name']] = code
    return codes


def verify_library(library):
    library = Path(library)
    require(library.is_file() and library.stat().st_size == LIBRARY_BYTES, 'wrong original library size')
    raw = library.read_bytes()
    require(digest(raw) == LIBRARY_SHA256, 'original metallib hash mismatch')
    for entry, pin in PINS.items():
        require(digest(raw[pin['offset']:pin['offset'] + pin['bytes']]) == pin['airSHA256'],
                'original selected AIR hash mismatch: ' + entry)


def verify_toolchain(root):
    root = Path(root)
    _, source = read_manifest(root / 'build.json')
    validate_source(source)
    for key, path in [('compiler_sha256', root / 'native540-compiler'),
                      ('backend_sha256', root / 'obj/gpu-compiler/host/src/freedreno/QcomGPUCompiler.dylib')]:
        require(path.is_file() and digest(path.read_bytes()) == source[key], 'actual toolchain hash mismatch')
    return source


def typed_needed(info):
    """Lossless named slices, NOT a supported encoder schema or wire ABI."""
    words = info['descriptionWords']
    varyings = [dict(zip(('semantic0', 'semantic1', 'semantic2', 'semantic3',
                          'components', 'mode', 'regid', 'mask', 'inloc', 'reserved'),
                         words[52 + i * 10:62 + i * 10])) for i in range(info['varyingCount'])]
    return dict(renderInfoProjection={key: words[588] if key == 'colorRegister' else info[key]
                                      for key in RENDER_FIELDS},
        buffers=[dict(slot=i, stride=words[44+i], writable=bool(info['writeMask'] & (1 << i)))
                 for i in range(8) if info['bufferMask'] & (1 << i)],
        memory=dict(bufferMask=info['bufferMask'], writeMask=info['writeMask'],
                    boundedBufferSizeConstants=bool(info['flags'] & (1 << 26)),
                    allocationWords=info['constlen']*4,
                    driverBase=info['driverBase'], driverCount=info['driverCount'],
                    immediateBase=info['immediateBase'], immediateCount=info['immediateCount']),
        varyings=varyings, attributeMask=info['attributeMask'],
        # Complete opaque records are retained even when inactive. No metadata
        # available here proves uniform resource byte bounds or sampler state.
        attributeWords=words[373:437], constantSamplerWords=words[443:587],
        colorRegisters=words[588:596], constantWindowWords=words[613:621],
        constantWindows=[dict(buffer=words[613+2*i], byteOffset=words[614+2*i])
                         for i in range(info['constantWindowCount'])],
        textureSampler={key: info[key] for key in ('textureCount', 'textureBindings', 'sampleMask',
            'samplerMask', 'implicitSamplerMask', 'explicitSamplerMask', 'constantSamplerMask')},
        uniforms=dict(supported=False, reason='no general uniform ABI admission in this exact registry'),
        supportsCurrentEncoder=False)


def link_report(vertex, fragment):
    """Mirror sagit link semantic matching for these exact already-pinned inputs."""
    vs, fs = typed_needed(vertex)['varyings'], typed_needed(fragment)['varyings']
    missing = []
    for input_ in fs:
        if not input_['mask']:
            continue
        output = next((v for v in vs if all(v['semantic'+str(i)] == input_['semantic'+str(i)]
                                          for i in range(4))), None)
        if output is None or output['components'] != input_['components'] or \
                input_['mask'] & ~output['mask'] or output['regid'] == 252:
            missing.append(input_)
    return dict(compatible=not missing, missingVertexOutputs=missing,
        reference='GPUShaderProtocol.h::link (sagit ' + SAGIT + ')',
        position=dict(regid=vertex['position'], mask=15),
        note='no invented outputs, renumbering, or metadata repair; packed link NOT emitted')


def blocked_report(vertex, fragment, fragment_entry='FastSingle_FragFunc'):
    link = link_report(vertex, fragment)
    matching = fragment_entry == 'SolidColor_FragFunc'
    report = dict(schema=1, profile='native540-render-air-exact-blocked-v1', status='blocked',
        targetOSBuild='21H461', gpu=540, jitVersion=41, route='AIR-NIR-IR3',
        entries=['SolidColor_VertexFunc', fragment_entry], supportsCurrentEncoder=False, runtimeVerified=False,
        deviceOperated=False, ir3SecurityValidated=False, reflectionEdited=False,
        admission='exact offline artifact identity only; NOT executable renderer admission',
        link=link, neededFeatures=[
            'VS read-only device buffer0 float4[vertexID], stride16: raster SSBO binding and bounded-size constants',
            'FS live perspective float2 texture coordinates and float4 color mix: semantic VPC linking/interpolation',
            'FS texture2d<float,sample>0 plus sampler0: real texture layout/binding and sampler state',
            'preserve independently allocated base/MSAA siblings; current encoder is single sample',
            'buffer/texture byte bounds, upload/cache/fence/resource lifetime and owned render objects'],
        blockers=['current Render::Info/plan/encode has no graphics SSBO/texture/user-varying support',
                  'VS flags 0x04000008 and FS flags 0x19 exceed current 0x8/0x18 exact shader profile',
                  'SolidColor_VertexFunc emits position only; FastSingle_FragFunc needs TWO absent semantic outputs'],
        references=['QcomMTL/QcomMTL.mm', 'QcomGPU/AdrenoPipeline.h::raster/constants/buffers',
                    'GPUShaderProtocol.h::link', 'GPUJITProtocol.h::QGPU_PROGRAM_FIELDS',
                    'tools/gpu-compiler/Compiler.cpp::compileAIR/compileEntries'],
        limitations=['offline compiler identity is not device execution or raster success',
                     'no executable header; typed-needed projections must not be cast to current Program',
                     'no system Metal publication; no GLSL identity substitution',
                     'raw621 is complete compiler output, not a separately captured pre-composition backend snapshot'])
    report['pairRole'] = 'matching SolidColor pipeline' if matching else 'cross-pipeline negative comparison, NOT a natural pair'
    if matching:
        report['neededFeatures'] = [
            'VS read-only buffer0 float4[vertexID] stride16, stage-specific bounded SSBO binding',
            'FS read-only buffer0 u_color float4 at byte0 (16 bytes); distinct fragment hardware bank base8',
            'buffer size constants at words96..103 and source-owned logical lengths; no arbitrary uniform ABI',
            'preserve base/MSAA/constantWindow/msaaConstantWindow; start ordinary single-sample base',
            'base FS flags0x04000018, constantWindowCount1(buffer0,byte0); optional window variants require explicit CP/cache window setup',
            'owned render PSO/encoder resource lifetime, upload/cache/fence and actual pixel readback']
        report['blockers'] = [
            'current Render::Info/plan/encode lacks stage graphics SSBO descriptors and bounded-size constants',
            'VS/FS bounded-size flags exceed current exact flags0x8/0x18',
            'all constant-window metadata/siblings must remain intact; current renderer has no window binding']
        report['nextRegistryPrerequisite'] = dict(linkCompatible=link['compatible'], noTextures=True,
            noUserVaryings=True, minimalLogicalInputs=[dict(stage='vertex',slot=0,stride=16),
                dict(stage='fragment',slot=0,bytes=16,hardwareBankBase=8)],
            semantics='VS position=buffer0[vertexID]; FS color=read-only float4 u_color[0]',
            recommendedVariant='base', supportsCurrentEncoder=False,
            note='matching zero-user-varying AIR pair, suitable NEXT implementation target, not runnable now')
    return report


def export_render_program(vertex_manifest, fragment_manifest, output, library, toolchain_root=ROOT):
    output = Path(output).absolute()
    require(not output.exists() and not output.is_symlink(), 'refusing to replace existing evidence')
    # Apple artifacts and all evidence only in a new directory under the owned
    # private render-air-<UTC> tree. Never write generated Apple artifacts to NAS.
    private_root = output if output.parent == ROOT else next(
        (p for p in output.parents if p.parent == ROOT), None)
    require(private_root is not None and re.fullmatch(r'render-air-\d{8}T\d{6}Z', private_root.name),
            'output must be within owned private render-air-<UTC> evidence')
    require(not any(p.is_symlink() for p in (output, *output.parents)), 'symlink evidence path refused')
    verify_library(library)
    source = verify_toolchain(toolchain_root)
    stages = []
    for stage, path in zip((1, 2), (vertex_manifest, fragment_manifest)):
        raw, manifest = read_manifest(path)
        entry = manifest.get('entry')
        require(entry in PINS and PINS[entry]['stage'] == stage, 'wrong selected render stage/entry')
        validate_manifest(manifest, entry)
        require(manifest['source'] == source, 'compiler/source mismatch')
        stages.append((entry, raw, manifest, read_codes(Path(path).parent, manifest)))
    report = blocked_report(stages[0][2]['variants'][0]['info'], stages[1][2]['variants'][0]['info'], stages[1][0])
    report.update(source=source, input=dict(sha256=LIBRARY_SHA256, bytes=LIBRARY_BYTES),
                  sourceSHA256=SOURCE_SHA256, exporterSHA256=digest(Path(__file__).read_bytes()), artifacts={})
    # Reserve once after ALL gates, never rename over another agent's directory.
    output.mkdir(parents=True, exist_ok=False)
    for entry, raw, manifest, codes in stages:
        directory = output / entry
        directory.mkdir()
        (directory / 'compiled-program.json').write_bytes(raw)
        for variant in manifest['variants']:
            name, info = variant['name'], variant['info']
            (directory / variant['shader']).write_bytes(codes[name])
            (directory / ('reflection.' + name + '.raw621.bin')).write_bytes(struct.pack('<621I', *info['descriptionWords']))
            (directory / ('immediates.' + name + '.bin')).write_bytes(struct.pack('<%dI' % len(variant['immediates']), *variant['immediates']))
            typed = dict(schema=1, entry=entry, variant=name, stage=info['stage'],
                supportsCurrentEncoder=False, originalAIR={key: PINS[entry][key] for key in
                    ('metalType', 'offset', 'bytes', 'airSHA256')},
                info=info, immediates=variant['immediates'], shaderSHA256=variant['sha256'],
                reflectionSHA256=digest(struct.pack('<621I', *info['descriptionWords'])), needed=typed_needed(info))
            (directory / ('typed-needed.' + name + '.json')).write_text(json.dumps(typed, indent=2) + '\n')
        for path in sorted(directory.iterdir()):
            report['artifacts'][str(path.relative_to(output))] = digest(path.read_bytes())
    result = output / 'blocked-report.json'
    result.write_text(json.dumps(report, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vertex_manifest', type=Path)
    parser.add_argument('fragment_manifest', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--library', required=True, type=Path)
    parser.add_argument('--toolchain-root', type=Path, default=ROOT)
    args = parser.parse_args()
    try:
        result = export_render_program(args.vertex_manifest, args.fragment_manifest,
                                       args.output, args.library, args.toolchain_root)
        print(str(result) + '\nBLOCKED: supportsCurrentEncoder=false; no executable shader exported')
        return 2
    except (ValueError, OSError, KeyError, TypeError, OverflowError, struct.error) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
