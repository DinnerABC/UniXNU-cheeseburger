#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exhaustive195 target metadata and real ObjC mock runtime, not GPU proof."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
R=Path(__file__).resolve().parents[1]
AUDIT=Path('/Users/dinnerabc/unixnu-p21/native540/factory-query-audit-20261106T1005/queries.json')
ENV=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
class Capabilities(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out=Path(tempfile.mkdtemp(prefix='capabilities-',dir='/Users/dinnerabc/unixnu-p21/native540'))
        def run(args,label,env=ENV):
            p=subprocess.run(list(map(str,args)),capture_output=True,text=True,env=env)
            (cls.out/(label+'.log')).write_text(p.stdout+p.stderr)
            if p.returncode:raise RuntimeError(label+': '+p.stdout+p.stderr)
            return p.stdout
        cls.command=staticmethod(run)
        cls.cc=run(['xcrun','--find','clang++'],'compiler').strip()
        cls.mac=run(['xcrun','--sdk','macosx','--show-sdk-path'],'macsdk').strip()
        cls.ios=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'iossdk').strip()
        cls.source=R/'package/Userland/NativeMetal540Capabilities.mm';cls.binary=cls.out/'capabilities-model'
        run([cls.cc,'-std=c++17','-O1','-g','-fobjc-arc','-Wall','-Wextra','-Werror',
             '-DNATIVE540_CAPABILITIES_HOST_MODEL','-isysroot',cls.mac,
             '-I'+str(R/'package/Userland'),'-fsanitize=address,undefined','-fno-omit-frame-pointer',
             R/'tests/native540_capabilities_model.mm',cls.source,'-framework','Foundation','-o',cls.binary], 'model-build')
    def test_exact_target195_policy_and_imp_pins(self):
        self.assertTrue(AUDIT.exists(),'required pinned target query audit missing')
        audit=json.loads(AUDIT.read_text());self.assertEqual(audit['target'],'21H461');source=self.source.read_text()
        rows=re.findall(r'    \{"(supports[^" ]+)", "([^" ]+)", "([^" ]+)", "([^" ]+)", "([^"\n]+)", (true|false), (Required|0)\},',source)
        self.assertEqual(len(rows),195);self.assertEqual(len({r[0] for r in rows}),195)
        self.assertEqual([r[0] for r in rows],[q['selector'] for q in audit['queries']])
        for row,q in zip(rows,audit['queries']):
            self.assertEqual(row[1],'B16@0:8');self.assertEqual(tuple(row[:4]),(q['selector'],q['type'],q['imp'],q['sha256']))
            self.assertTrue(row[4]);self.assertEqual(row[6],'Required' if row[5]=='true' else '0')
        self.assertEqual([r[0] for r in rows if r[5]=='true'],['supportsRenderToLinearTextures'])
        self.assertIn(hashlib.sha256(AUDIT.read_bytes()).hexdigest(),source)
        for key in ('helper_sha256','function_starts_sha256'):self.assertIn(audit[key],source)
        self.assertIn(audit['query_init']['sha256'],source)
        self.assertIn('673f85070fe9f7872793015eb230a5c3987d4a8c',source)
    def test_explicit_current_backend_negatives(self):
        source=self.source.read_text()
        for selector in ('supportsReadWriteBufferArguments','supportsBufferBoundsChecking','supportsArgumentBuffersTier2',
                         'supportsComputeMemoryBarrier','supportsRenderMemoryBarrier','supportsBCTextureCompression',
                         'supportsProgrammableSamplePositions'):
            line=next(l for l in source.splitlines() if l.startswith('    {"'+selector+'"'))
            self.assertIn(', false, 0}',line);self.assertIn('Current',line)
        for forbidden in ('return YES;','class_replaceMethod','method_setImplementation'):self.assertNotIn(forbidden,source)
    def test_runtime_exhaustive_and_full_preflight_failures(self):
        for scenario in ('ok','missing','type','conflict','registered','size','subivar','inherited','invalid','evidence'):
            with self.subTest(scenario=scenario):
                self.assertIn('PASS '+scenario,self.command([self.binary,scenario],scenario,
                    dict(ENV,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1')))
    def test_arm64_ios_production(self):
        self.command([self.cc,'-std=c++17','-target','arm64-apple-ios17.7','-isysroot',self.ios,
                  '-fobjc-arc','-Wall','-Wextra','-Werror','-I'+str(R/'package/Userland'),
                  '-c',self.source,'-o',self.out/'NativeMetal540Capabilities-arm64.o'],'arm64-production')
    @classmethod
    def tearDownClass(cls):print('Local source-only evidence: '+str(cls.out))
if __name__=='__main__':unittest.main()
