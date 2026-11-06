#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Immutable real AIR provenance + transport/LLVM host model; NOT GPU proof."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
EVIDENCE=ROOT/'tools/gpu-native540/evidence/air-simple-20261106T050950Z/padding-gradient-copy'
SPEC=importlib.util.spec_from_file_location('air_reflection',ROOT/'tools/gpu-native540/export_program.py')
PROFILE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(PROFILE)
ISA_HASH='8786fdb358bab5ee91dc7b79b26d6e736aa1fb79cec6bd7799fe36b94e1c528b'
HEADER_HASH='147093a54e4f0caeee3bee9f8f8c37f188cb7682998bca7d668ed018e3893f72'

def digest(data):return hashlib.sha256(data).hexdigest()

def validate(manifest,code):
    if (manifest['entry']!='padding_gradient' or manifest['route']!='AIR-NIR-IR3' or
        manifest['gpu']!=540 or manifest['jitVersion']!=41 or manifest['runtimeVerified'] is not False or
        manifest['targetOSBuild']!='21H461' or manifest['source']['sagit']!=PROFILE.SAGIT or manifest['source']['mesa']!=PROFILE.MESA or
        manifest['input']['sha256']!='fa9101b5c07bd91720de2cbbcb170b3acd061d8eae4bf13bc6f9f7fc07794e9b'):
        raise ValueError('wrong fixed real AIR source/compiler profile')
    if len(manifest['variants'])!=1 or manifest['variants'][0]['name']!='base':raise ValueError('unexpected variant')
    v=manifest['variants'][0];p=v['info'];words=PROFILE.validate_reflection(p)
    keys=['codeWords','instrlen','constlen','fullRegs','halfRegs','branchStack','threadSize','subgroupSize','immediateBase','immediateCount','driverBase','driverCount','localID','groupID','bufferMask','writeMask','flags','sharedBytes','threadgroupMask']
    if [p[k] for k in keys]!=[416,13,36,6,4,1,0,64,128,10,112,12,0,192,7,2,0x0400008c,0,0]:
        raise ValueError('actual fixed AIR allocation changed')
    if words[44:52]!=[4,4,4,0,0,0,0,0] or v['immediates']!=[16,0,20,24,65535,4294967292,28,4,36,8]:
        raise ValueError('strides/immediates changed')
    if len(code)!=416*4 or digest(code)!=ISA_HASH or v['sha256']!=ISA_HASH:raise ValueError('actual ISA changed')
    return p

class AIRProbeTests(unittest.TestCase):
    def inputs(self):return json.loads((EVIDENCE/'compiled-program.json').read_text()),(EVIDENCE/'program.base.ir3').read_bytes()
    def test_real_air_exact_isa_all621_and_input_identity(self):
        m,code=self.inputs();validate(m,code)
        h=(EVIDENCE/'NativeMetal540AIRFixture.h').read_bytes();self.assertEqual(digest(h),HEADER_HASH)
        values=re.search(r'code\[\] = \{([^}]+)\}',h.decode()).group(1)
        isa=[int(v.strip().removesuffix('u'),16) for v in values.split(',')]
        self.assertEqual(struct.pack('<416I',*isa),code)
        receipt=json.loads((EVIDENCE/'fixture-manifest.json').read_text())
        self.assertEqual(receipt['air']['bytes'],4080)
        self.assertEqual(receipt['air']['sha256'],'a858cdfb6fef8374c586b86ce4ff6547af6acf4f9da40206c8726c8e88a1ccb9')
        self.assertEqual(receipt['air']['llvmBodySHA256'],'d61ae067525ba3d6e6cbc861867897e21a8634bdeb845509300f907e27ab6d1a')
        self.assertEqual(receipt['paramsWords'],[0,260,3,1,256,1,1,2,2,1,1,0])
        self.assertEqual(receipt['driverConstants'],dict(base=112,count=12,words=[4,1,1,1,0,0,0,64,64,1,1,6]))
        self.assertFalse(receipt['reflectionEdited']);self.assertFalse(receipt['runtimeVerified'])
        for name,value in receipt['hashes'].items():self.assertEqual(digest((EVIDENCE/name).read_bytes()),value)
        wrapper=(ROOT/'tools/gpu-native540/air_fixture.h').read_text()
        self.assertIn('evidence/air-simple-20261106T050950Z/padding-gradient-copy/NativeMetal540AIRFixture.h',wrapper)
    def test_corrupt_producer_isa_allocations_reflection_rejected(self):
        for mode in ('entry','route','library','isa','short','mask','write','stride','driver','immediate','shared','unsupported'):
            with self.subTest(mode=mode):
                m,code=self.inputs();p=m['variants'][0]['info']
                if mode=='entry':m['entry']='memcpy'
                elif mode=='route':m['route']='GLSL-NIR-A540-IR3'
                elif mode=='library':m['input']['sha256']='0'*64
                elif mode=='isa':code=bytes(len(code))
                elif mode=='short':code=code[:-4]
                elif mode=='mask':p['bufferMask']=3;p['descriptionWords'][21]=3
                elif mode=='write':p['writeMask']=3;p['descriptionWords'][22]=3
                elif mode=='stride':p['descriptionWords'][44]=8
                elif mode=='driver':p['driverCount']=8;p['descriptionWords'][13]=8
                elif mode=='immediate':m['variants'][0]['immediates'][0]^=1
                elif mode=='shared':p['sharedBytes']=4;p['descriptionWords'][32]=4
                else:p['descriptionWords'][52]=1
                with self.assertRaises(ValueError):validate(m,code)
    def test_no_resource_cleanup_or_abi_changes_in_new_probe(self):
        h=(ROOT/'package/Userland/NativeMetal540AIRProbe.h').read_text()
        cpp=(ROOT/'package/Userland/NativeMetal540AIRProbe.cpp').read_text()
        for forbidden in ('NativeAdrenoMethodBoFree','NativeAdrenoMethodBoCpuUnmap','NativeAdrenoMethodBoImport','fastboot','MTLCreateSystemDefaultDevice'):
            self.assertNotIn(forbidden,cpp)
        self.assertEqual(cpp.count('client.submit('),1);self.assertEqual(cpp.count('client.wait('),1)
        self.assertIn('NativeMetal540Client::Call call=nullptr',h)
        self.assertNotIn('~NativeMetal540AIRProbe',h)
        self.assertNotIn('GLSL',cpp+h)
        # Output initialization is sentinel only; oracle appears in readback.
        self.assertEqual(cpp.count('F::expected('),1)
        self.assertGreater(cpp.index('F::expected('),cpp.index('stage(Readback)'))
    def test_host_model_and_sanitized_all_failpaths(self):
        before={p.name:digest(p.read_bytes()) for p in EVIDENCE.iterdir() if p.is_file()}
        with tempfile.TemporaryDirectory(prefix='native540-air-model-') as name:
            for sanitized in (False,True):
                binary=Path(name)/('model-san' if sanitized else 'model')
                cmd=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(ROOT/'package/Userland'),
                    str(ROOT/'tests/native540_air_probe_model.cpp'),str(ROOT/'package/Userland/NativeMetal540Client.cpp'),'-o',str(binary)]
                if sanitized:cmd.extend(['-fsanitize=address,undefined','-fno-omit-frame-pointer'])
                subprocess.run(cmd,check=True)
                run=subprocess.run([binary],check=True,capture_output=True,text=True,
                    env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
                self.assertIn('NOT GPU execution',run.stdout)
                print(('ASan/UBSan ' if sanitized else 'host ')+run.stdout.strip())
        self.assertEqual(before,{p.name:digest(p.read_bytes()) for p in EVIDENCE.iterdir() if p.is_file()})
    def test_arm64_ios17_7_object_compile_only(self):
        sdk=Path('/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk')
        if not sdk.exists():sdk=Path(subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip())
        with tempfile.TemporaryDirectory(prefix='native540-air-arm64-') as name:
            subprocess.run(['clang++','-std=c++17','-target','arm64-apple-ios17.7','-isysroot',str(sdk),'-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'package/Userland'),'-c',str(ROOT/'package/Userland/NativeMetal540AIRProbe.cpp'),'-o',str(Path(name)/'AIRProbe.o')],check=True)

if __name__=='__main__':unittest.main()
