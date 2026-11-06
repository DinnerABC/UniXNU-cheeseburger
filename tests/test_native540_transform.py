#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Public GLSL/compiler provenance and finite FD540 host model; not device proof."""
import ast
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'tools/gpu-native540/evidence/transform-glsl-20261106T044414Z'
LOCAL=Path('/Users/dinnerabc/unixnu-p21/native540/transform-20261106T044414Z')
SPEC=importlib.util.spec_from_file_location('transform_reflection',ROOT/'tools/gpu-native540/export_program.py')
PROFILE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(PROFILE)
GLSL=('#version 430\nlayout(local_size_x=64) in;\n'
      'layout(std430,binding=0) readonly buffer Input { uint src[]; };\n'
      'layout(std430,binding=1) writeonly buffer Output { uint dst[]; };\n'
      'void main() { uint i = gl_GlobalInvocationID.x; dst[i] = (src[i] ^ 0x5a5a1234u) + i; }\n')

def digest(b):return hashlib.sha256(b).hexdigest()
def validate(m,code,source,raw,final):
    if m['producer']!='qgpu_compile_glsl' or m['gpu']!=540 or m['jitVersion']!=41 or m['sagit']!=PROFILE.SAGIT or m['mesa']!=PROFILE.MESA:
        raise ValueError('compiler source/profile mismatch')
    if source!=GLSL.encode() or digest(source)!=m['input']['sha256']:raise ValueError('wrong exact source')
    p=m['variants'][0]['info'];words=PROFILE.validate_reflection(p)
    if p['bufferMask']!=3 or p['writeMask']!=2 or words[44:52]!=[4,4,0,0,0,0,0,0] or p['groupID']==252:
        raise ValueError('wrong dual-SSBO global-ID reflection')
    if len(code)!=4*p['codeWords'] or digest(code)!=m['variants'][0]['sha256']:raise ValueError('ISA hash/length mismatch')
    if final!=struct.pack('<621I',*words) or digest(final)!=m['finalReflectionSHA256'] or digest(raw)!=m['backendReflectionSHA256']:
        raise ValueError('raw/final hash mismatch')
    rawWords=list(struct.unpack('<621I',raw));want=rawWords[:]
    if rawWords[21] or rawWords[22] or any(rawWords[44:52]) or rawWords[27]:raise ValueError('bare backend reflection changed')
    if rawWords[52:372]!=[x for _ in range(32) for x in (0,0,0,0,0,0,252,0,0,0)]:raise ValueError('bare backend varying contract changed')
    want[21]=3;want[22]=2;want[44]=4;want[45]=4
    # Exact QGPUVarying[32]: 10 words each, absent register sentinel252.
    for i in range(52,372):want[i]=0
    if want!=words:raise ValueError('backend allocation/sysval/constants changed')
    if m['variants'][0]['immediates']!=[0,0x5a5a1234] or p['immediateBase']!=112 or p['immediateCount']!=2:
        raise ValueError('constant reflection mismatch')
    return p

class TransformTests(unittest.TestCase):
    def inputs(self):
        return (json.loads((EVIDENCE/'manifest.json').read_text()),(EVIDENCE/'transform.ir3').read_bytes(),
                (EVIDENCE/'transform.comp').read_bytes(),(EVIDENCE/'backend-reflection.bin').read_bytes(),(EVIDENCE/'final-reflection.bin').read_bytes())
    def test_real_compiler_provenance(self):
        m,code,source,raw,final=self.inputs();p=validate(m,code,source,raw,final)
        self.assertEqual((p['codeWords'],p['instrlen'],p['constlen'],p['fullRegs'],p['halfRegs'],p['localID'],p['groupID'],p['flags']),(96,2,32,4,1,0,192,0x04000008))
        self.assertEqual(m['dispatch'],dict(groups=[4,1,1],local=[64,1,1]));self.assertTrue(m['jitValid']);self.assertFalse(m['runtimeVerified']);self.assertFalse(m['metalCompiler'])
        h=(ROOT/'tools/gpu-native540/transform_fixture.h').read_text();self.assertEqual(h,(EVIDENCE/'transform_fixture.h').read_text())
        self.assertIn(digest(code),h);self.assertIn(digest(source),h);self.assertIn(digest(final),h)
        encoded=re.search(r'code\[\]=\{([^}]+)\}',h).group(1);isa=[int(v.strip().removesuffix('u'),16) for v in encoded.split(',')]
        self.assertEqual(struct.pack('<96I',*isa),code)
        asm=(EVIDENCE/'transform.asm').read_text()
        for instruction in ('ldgb.untyped.4d.u32.1 r3.x, g[0]','stgb.untyped.4d.u32.1 g[1]','xor.b r3.x, r3.x, c28.y','add.u r3.x, r3.x, r1.y','shl.b r1.y, r48.x, 6'):self.assertIn(instruction,asm)
        self.assertNotIn('stib.b',asm)
        self.assertEqual(digest((EVIDENCE/'transform.jit').read_bytes()),m['finalJIT']['sha256'])
        wrapper=(ROOT/'tools/gpu-native540/compile_transform.cpp').read_text()
        admitted=wrapper.split('static constexpr char sourceText[] =',1)[1].split('static constexpr char sagit',1)[0]
        self.assertEqual(''.join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"',admitted)),GLSL)
        prov=json.loads((EVIDENCE/'provenance.json').read_text());self.assertEqual(prov['wrapper_source_sha256'],digest(wrapper.encode()))
    def test_wrong_source_metadata_permissions_lengths_hashes(self):
        for mode in ('source','length','isa','readwrite','mask','stride','constlen','sysval','raw','final','immediate'):
            with self.subTest(mode=mode):
                m,code,src,raw,final=self.inputs();p=m['variants'][0]['info']
                if mode=='source':src=src.replace(b'readonly',b'writeonly')
                elif mode=='length':code=code[:-4]
                elif mode=='isa':code=bytes(len(code))
                elif mode=='readwrite':p['writeMask']=3;p['descriptionWords'][22]=3
                elif mode=='mask':p['bufferMask']=1;p['descriptionWords'][21]=1
                elif mode=='stride':p['descriptionWords'][45]=8
                elif mode=='constlen':p['constlen']=28;p['descriptionWords'][4]=28
                elif mode=='sysval':p['groupID']=252;p['descriptionWords'][15]=252
                elif mode=='raw':raw=bytes(len(raw))
                elif mode=='final':final=bytes(len(final))
                else:m['variants'][0]['immediates'][1]^=1
                with self.assertRaises(ValueError):validate(m,code,src,raw,final)
    def test_host_model_sanitized(self):
        with tempfile.TemporaryDirectory(prefix='model-',dir=LOCAL) as name:
            binary=Path(name)/'model';cmd=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                '-I'+str(ROOT/'package/Userland'),str(ROOT/'tests/native540_transform_model.cpp'),str(ROOT/'package/Userland/NativeMetal540Client.cpp'),'-o',str(binary)]
            subprocess.run(cmd,check=True)
            env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
            subprocess.run([binary],check=True,env=env)
    def test_arm64_ios_compile(self):
        sdk=Path('/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk')
        if not sdk.exists():
            # Use existing host SDK lookup only, never install/download a SDK.
            sdk=Path(subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip())
        with tempfile.TemporaryDirectory(prefix='arm64-',dir=LOCAL) as name:
            subprocess.run(['clang++','-std=c++17','-target','arm64-apple-ios17.7','-isysroot',str(sdk),'-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'package/Userland'),'-c',str(ROOT/'package/Userland/NativeMetal540Transform.cpp'),'-o',str(Path(name)/'Transform.o')],check=True)
    def test_actual_wrapper_exact_source_rejection(self):
        binary=LOCAL/'native540-compile-transform'
        with tempfile.TemporaryDirectory(prefix='compiler-',dir=LOCAL) as name:
            d=Path(name);src=d/'input.comp';src.write_text(GLSL)
            result=subprocess.run([binary,src,d/'good'],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
            for file in ('transform.ir3','backend-reflection.bin','final-reflection.bin','transform_fixture.h'):
                self.assertEqual((d/'good'/file).read_bytes(),(EVIDENCE/file).read_bytes())
            self.assertNotEqual(subprocess.run([binary,src,d/'good'],capture_output=True).returncode,0)
            for i,bad in enumerate((GLSL.replace('430','450'),GLSL.replace('gl_GlobalInvocationID','gl_LocalInvocationID'),
                                    GLSL.replace('binding=1','binding=0'),GLSL.replace('readonly','writeonly'),GLSL+'\x00','',GLSL.replace('5a5a1234','deadbeef'))):
                src.write_text(bad);out=d/('bad'+str(i));self.assertNotEqual(subprocess.run([binary,src,out],capture_output=True).returncode,0);self.assertFalse(out.exists())

if __name__=='__main__':unittest.main()
