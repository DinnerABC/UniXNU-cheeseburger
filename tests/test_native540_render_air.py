#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exact real AIR host policy/ISA/PM4 gates; NO device operation or raster claim."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import unittest
sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
TOOLS = REPO / 'tools/gpu-native540'
USER = REPO / 'package/Userland'
SAGIT = REPO.parent / 'upstream/ios_sagit'
PRIVATE = Path('/Users/dinnerabc/unixnu-p21/native540/render-air-20261106T100335Z')
OUT = Path(os.environ.get('NATIVE540_RENDER_AIR_OUT', str(PRIVATE / 'renderer-tests')))
SDK = Path('/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk')

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def run(command, name):
    p = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=180)
    (OUT / (name + '.log')).write_text(p.stdout + p.stderr)
    if p.returncode:
        raise RuntimeError(name + ': ' + p.stdout + p.stderr)
    return p.stdout

def packets(path):
    raw = Path(path).read_bytes()
    words = struct.unpack('<%dI' % (len(raw)//4), raw)
    out = []
    index = 0
    while index < len(words):
        h = words[index]
        index += 1
        if h >> 28 == 4:
            count = h & 127
            kind = ('reg', (h >> 8) & 0x3ffff)
        else:
            assert h >> 28 == 7
            count = h & 0x3fff
            kind = ('op', (h >> 16) & 127)
        assert index + count <= len(words)
        out.append((kind, list(words[index:index+count])))
        index += count
    return out

def draw_state(path):
    state, loads = {}, []
    for (kind, key), values in packets(path):
        if kind == 'reg':
            state.update({key+i: value for i, value in enumerate(values)})
        elif key == 0x30:
            loads.append(values)
        elif key == 0x38:
            return state, loads, values
    raise AssertionError('missing draw')

class RenderAIR(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=False)
        cls.fixture = (TOOLS / 'render_air_fixture.h').read_text()
        run(['clang++','-std=c++17','-DNATIVE540_ENCODER_ONLY','-Wall','-Wextra','-Werror',
             '-fsanitize=address,undefined','-I'+str(USER),'-I'+str(TOOLS),
             REPO/'tests/native540_render_air_model.cpp',USER/'NativeMetal540Render.cpp',
             USER/'NativeMetal540Client.cpp','-o',OUT/'air-model'], 'model-build')
        run([OUT/'air-model',OUT/'native.pm4',OUT/'old.pm4'],'model')
        # Independent ORIGINAL sagit full pipeline. Adapt host harness inputs,
        # never upstream source or metadata. Original optional constant-window
        # staging is retained and compared/documented rather than edited out.
        harness = (REPO/'tests/native540_render_reference.cpp').read_text()
        start = harness.index('    auto load=[]')
        end = harness.index('    GPU::Shader::Draw d{};', start)
        harness = harness[:start] + '''    std::memcpy(vs.get(),Native540RenderAIRFixture::vsReflection,2484);
    std::memcpy(fs.get(),Native540RenderAIRFixture::fsReflection,2484);
    vs->code=Native540RenderAIRFixture::vsCode;vs->immediates=Native540RenderAIRFixture::vsImmediates;
    fs->code=Native540RenderAIRFixture::fsCode;fs->immediates=Native540RenderAIRFixture::fsImmediates;
''' + harness[end:]
        harness = harness.replace('#include "AdrenoPipeline.h"',
            '#include "AdrenoPipeline.h"\n#include "render_air_fixture.h"')
        harness = harness.replace('Adreno::ShaderBuffers bindings{};Adreno::GraphicsTextures sampled{};',
            'Adreno::ShaderBuffers bindings{};bindings.addresses[0]=0x40109000;bindings.bytes[0]=48;'
            'bindings.addresses[8]=0x4010a000;bindings.bytes[8]=16;Adreno::GraphicsTextures sampled{};')
        # Original validDraw requires real logical descriptors; addresses remain
        # in the independent resolved bindings rather than forged program words.
        harness = harness.replace('GPU::Shader::Linkage linkage{};',
            'd.buffers[0]={2,0,48,0};d.fragmentBuffers[0]={3,0,16,0};GPU::Shader::Linkage linkage{};')
        (OUT/'reference.cpp').write_text(harness)
        run(['clang++','-std=c++17','-I'+str(USER),'-I'+str(TOOLS),
             '-I'+str(REPO/'package/BoardRuntime'),'-I'+str(SAGIT/'drivers/include'),
             '-I'+str(SAGIT/'drivers/gpu/QcomGPU'),'-include','GPUJITProtocol.h',
             OUT/'reference.cpp','-o',OUT/'reference'],'reference-build')
        run([OUT/'reference','unused-vs','unused-fs',OUT/'reference.pm4'],'reference')

    def test_exact_actual_full_metadata_isa_and_immediates(self):
        spec = importlib.util.spec_from_file_location('exact_render_export', TOOLS/'export_render_program.py')
        exporter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(exporter)
        for role, entry in [('vs','SolidColor_VertexFunc'),('fs','SolidColor_FragFunc')]:
            def array(suffix):
                match = re.search(r'uint32_t '+role+suffix+r'\[\]=\{([^}]+)\}', self.fixture)
                return [int(x.rstrip('u'),16) for x in match[1].split(',')]
            code, reflection, immediates = array('Code'), array('Reflection'), array('Immediates')
            pin = exporter.PINS[entry]['variants']['base']
            self.assertEqual(hashlib.sha256(struct.pack('<%dI'%len(code),*code)).hexdigest(),pin[0])
            self.assertEqual(hashlib.sha256(struct.pack('<621I',*reflection)).hexdigest(),pin[1])
            self.assertEqual(immediates,pin[2])
            self.assertEqual(reflection[21:24],[1,0,0])
            self.assertEqual(reflection[27:29],[0,0])
            self.assertEqual(reflection[612],0 if role=='vs' else 1)
        self.assertIn('673f85070fe9f7872793015eb230a5c3987d4a8c',self.fixture)

    def test_legacy_stream_byte_identical(self):
        baseline = Path('/Users/dinnerabc/unixnu-p21/native540/render-20261106T061514Z/native.pm4')
        self.assertEqual((OUT/'old.pm4').read_bytes(),baseline.read_bytes())

    def test_upstream_same_program_full_draw_differential(self):
        ns,nl,nd = draw_state(OUT/'native.pm4')
        rs,rl,rd = draw_state(OUT/'reference.pm4')
        for reg in (0xe5ac,0xe5c3):
            ns[reg]=rs[reg]=0
            ns[reg+1]=rs[reg+1]=0
        differences = {hex(k):[ns.get(k),rs.get(k)] for k in ns.keys()|rs.keys()
                       if k in rs and ns.get(k)!=rs.get(k)}
        self.assertEqual(differences,{})
        self.assertEqual(nd,rd)
        native_descriptors = [v for v in nl if (v[0]>>18)&15 == 14]
        original_descriptors = [v for v in rl if (v[0]>>18)&15 == 14]
        self.assertEqual(native_descriptors,original_descriptors)
        self.assertEqual(len(nl),6)
        self.assertEqual(len(rl),7)
        # ALL load packets, not just selected descriptors: normalize only the
        # two true shader object addresses; the sole extra original load is its
        # explicitly optional window bank. No metadata/code substitution.
        optional=[v for v in rl if (v[0]>>16)&3==2 and ((v[0]>>18)&15)==12 and v[1]&1]
        self.assertEqual(len(optional),1)
        self.assertEqual(optional[0][0],0x132001c)
        ordinary=[v for v in rl if v is not optional[0]]
        for a,b in zip(nl,ordinary):
            self.assertEqual(a[0],b[0])
            if (a[0]>>16)&3==2:
                self.assertEqual(a[3:],b[3:])
            elif ((a[0]>>18)&15)==12 and (a[0]>>16)&3==0:
                load_differences=[(i,x,y)for i,(x,y)in enumerate(zip(a,b))if x!=y]
                self.assertEqual(load_differences,[(131,0,1)])
            else:
                self.assertEqual(a,b)
        self.assertEqual(len(native_descriptors),2)
        self.assertEqual(native_descriptors[0][3],48<<16)
        self.assertEqual(native_descriptors[0][19],16<<16)
        native_constants = [v for v in nl if (v[0]>>16)&3 == 0 and v[1:3]==[1,0]
                            and ((v[0]>>18)&15) in (8,12)]
        original_constants = [v for v in rl if (v[0]>>16)&3 == 0 and v[1:3]==[1,0]
                              and ((v[0]>>18)&15) in (8,12)]
        self.assertEqual(len(native_constants),2)
        self.assertEqual(len(original_constants),2)
        constant_differences=[]
        for a,b in zip(native_constants,original_constants):
            self.assertEqual(len(a),len(b))
            diff=[(i,x,y)for i,(x,y)in enumerate(zip(a,b))if x!=y]
            # Original full draw optimizes FS via optional staged CP 64B
            # constant window and flag128=1. Our base uses actual guarded SSBO
            # fallback with flag128=0; all reflection/ISA is unchanged.
            self.assertEqual(diff,[] if (a[0]>>18)&15==8 else [(131,0,1)])
            constant_differences.extend(diff)
        self.assertFalse(any((v[0]>>18)&15==15 for v in nl))
        self.assertFalse(any((v[0]>>16)&3==2 and ((v[0]>>18)&15)==12 and v[1]&1 for v in nl))
        report=dict(runtimeVerified=False,deviceOperated=False,registerDifferences=differences,
            descriptorPacketsIdentical=True,constantDifferences=constant_differences,
            originalOptionalWindow='full original draws stage/enable window; port exact BASE guardedSSBO fallback disables it',
            rawReflectionEdited=False,nativeWords=(OUT/'native.pm4').stat().st_size//4,
            referenceWords=(OUT/'reference.pm4').stat().st_size//4)
        (OUT/'pm4-diff.json').write_text(json.dumps(report,indent=2)+'\n')

    def test_sanitized_failure_model_and_arm64(self):
        self.assertIn('2945 failures PASS',(OUT/'model.log').read_text())
        self.assertTrue(SDK.exists())
        run(['clang++','-std=c++17','-target','arm64-apple-ios17.7','-isysroot',SDK,
             '-DNATIVE540_ENCODER_ONLY','-Wall','-Wextra','-Werror','-I'+str(USER),
             '-c',USER/'NativeMetal540Render.cpp','-o',OUT/'render-ios.o'],'arm64')

    @classmethod
    def tearDownClass(cls):
        paths=[USER/'NativeMetal540Render.h',USER/'NativeMetal540Render.cpp',TOOLS/'render_air_fixture.h',
               REPO/'tests/native540_render_air_model.cpp',Path(__file__),
               SAGIT/'drivers/gpu/QcomGPU/AdrenoPipeline.h',SAGIT/'drivers/include/GPUShaderProtocol.h']
        (OUT/'provenance.json').write_text(json.dumps(dict(runtimeVerified=False,deviceOperated=False,
            sagit='673f85070fe9f7872793015eb230a5c3987d4a8c',hashes={str(p):sha(p)for p in paths}),indent=2)+'\n')

if __name__ == '__main__':
    unittest.main()
