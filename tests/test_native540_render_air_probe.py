#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
import datetime
import os
from pathlib import Path
import subprocess
import unittest
R=Path(__file__).resolve().parents[1]
O=Path('/Users/dinnerabc/unixnu-p21/native540')/('render-air-runtime-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
ENV=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
def run(args,label,env=ENV):
    p=subprocess.run(list(map(str,args)),capture_output=True,text=True,env=env)
    (O/(label+'.log')).write_text(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError(label+': '+p.stdout+p.stderr)
    return p.stdout
class RenderAIRRuntime(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        O.mkdir();cls.compiler=run(['xcrun','--find','clang++'],'compiler').strip()
        cls.hostsdk=run(['xcrun','--sdk','macosx','--show-sdk-path'],'host-sdk').strip()
        cls.sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'sdk').strip()
        sources=[R/'tests/native540_render_air_probe_model.cpp',R/'package/Userland/NativeMetal540RenderAIRProbe.cpp',
            R/'package/Userland/NativeMetal540Render.cpp',R/'package/Userland/NativeMetal540Client.cpp']
        run([cls.compiler,'-std=c++17','-O1','-g','-isysroot',cls.hostsdk,'-DNATIVE540_ENCODER_ONLY','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(R/'package/Userland'),*sources,'-o',O/'model'],'host-build')
    def test_runtime_and_transport_failures(self):
        result=run([O/'model'],'model',dict(ENV,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
        self.assertIn('PASS',result);self.assertIn('NOT GPU execution',result)
    def test_arm64_compile(self):
        run([self.compiler,'-std=c++17','-target','arm64-apple-ios17.7','-isysroot',self.sdk,'-DNATIVE540_ENCODER_ONLY',
            '-Wall','-Wextra','-Werror','-I'+str(R/'package/Userland'),'-c',R/'package/Userland/NativeMetal540RenderAIRProbe.cpp','-o',O/'probe.o'],'arm64')
    @classmethod
    def tearDownClass(cls):print('Local evidence: '+str(O))
if __name__=='__main__':unittest.main()
