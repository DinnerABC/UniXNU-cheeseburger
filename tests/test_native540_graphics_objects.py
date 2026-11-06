#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
import datetime
import os
from pathlib import Path
import subprocess
import unittest
R=Path(__file__).resolve().parents[1]
O=Path('/Users/dinnerabc/unixnu-p21/native540')/('graphics-objects-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
ENV=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
def run(args,label,env=ENV):
    p=subprocess.run(list(map(str,args)),capture_output=True,text=True,env=env)
    (O/(label+'.log')).write_text(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError(label+': '+p.stdout+p.stderr)
    return p.stdout
class GraphicsObjects(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        O.mkdir();cls.cc=run(['xcrun','--find','clang++'],'compiler').strip()
        cls.mac=run(['xcrun','--sdk','macosx','--show-sdk-path'],'macsdk').strip();cls.ios=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'iossdk').strip()
        common=[cls.cc,'-std=c++17','-O1','-g','-fobjc-arc','-fblocks','-DNATIVE540_ENCODER_ONLY','-Wall','-Wextra','-Werror',
            '-Wno-deprecated-declarations','-isysroot',cls.mac,'-I'+str(R/'package/Userland'),'-I'+str(R/'package/BoardRuntime'),
            '-fsanitize=address,undefined','-fno-omit-frame-pointer']
        sources=[R/'package/Userland/NativeMetal540Objects.mm',R/'package/Userland/NativeMetal540Render.cpp',R/'package/Userland/NativeMetal540Client.cpp']
        for model in ('native540_graphics_objects_model','native540_objects_model'):
            run([*common,R/('tests/'+model+'.mm'),*sources,'-framework','Foundation','-framework','Metal','-framework','IOSurface','-o',O/model],model+'-build')
    def test_graphics_objects_real_wrapper(self):
        result=run([O/'native540_graphics_objects_model','/private/tmp/p21-os-21h461/System/Library/Frameworks/SpriteKit.framework/default.metallib'],'graphics',dict(ENV,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1'))
        self.assertIn('PASS',result)
    def test_original_compute_objects(self):
        result=run([O/'native540_objects_model','/private/tmp/p21-os-21h461/System/Library/Frameworks/MLCompute.framework/default.metallib'],'compute',dict(ENV,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1'))
        self.assertIn('PASS',result)
    def test_arm64_objects_and_run(self):
        for name in ('NativeMetal540Objects','NativeMetal540GraphicsOwnedRun'):
            run([self.cc,'-std=c++17','-target','arm64-apple-ios17.7','-isysroot',self.ios,'-fobjc-arc','-fblocks','-DNATIVE540_ENCODER_ONLY',
                '-I'+str(R/'package/Userland'),'-I'+str(R/'package/BoardRuntime'),'-Wall','-Wextra','-Werror','-c',R/('package/Userland/'+name+'.mm'),'-o',O/(name+'.o')],name+'-arm64')
    @classmethod
    def tearDownClass(cls):print('Local evidence: '+str(O))
if __name__=='__main__':unittest.main()
