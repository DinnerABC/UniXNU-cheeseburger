#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Desktop runtime gates; host evidence is never a desktop takeover result."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest

R=Path(__file__).resolve().parents[1]
S=R.parent/'upstream/ios_sagit'
LOCAL=Path('/Users/dinnerabc/unixnu-p21/native540')
LIB=LOCAL/'obj/gpu-compiler/host/src/freedreno/QcomGPUCompiler.dylib'
O=LOCAL/('desktop-tests-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
ENV=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')


def run(args,name):
    p=subprocess.run(list(map(str,args)),env=ENV,capture_output=True,text=True)
    (O/(name+'.log')).write_text(p.stdout+p.stderr)
    if p.returncode:
        raise RuntimeError(name+': '+p.stdout+p.stderr)
    return p.stdout


class Desktop(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        O.mkdir()
        cls.cc=run(['xcrun','--find','clang++'],'compiler').strip()
        cls.sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'sdk').strip()
        cls.mac=run(['xcrun','--sdk','macosx','--show-sdk-path'],'mac-sdk').strip()
        cls.includes=['-I'+str(R/'package/Userland'),'-I'+str(R/'package/BoardRuntime'),
                      '-I'+str(S/'drivers/include'),'-I'+str(S/'drivers/gpu/QcomGPU'),'-I'+str(S/'tools/gpu-compiler')]
        cls.sources=[R/'package/Userland/NativeMetal540DesktopSession.cpp',R/'package/Userland/NativeMetal540Client.cpp']

    def test_lifecycle_model(self):
        run([self.cc,'-std=c++17','-isysroot',self.mac,'-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             *self.includes,R/'tests/native540_desktop_session_model.cpp',*self.sources,'-o',O/'session'],'session-build')
        self.assertIn('2000 reuse rounds',run([O/'session'],'session'))

    def test_real_quartzcore_encoding(self):
        original=Path(os.environ.get('NATIVE540_QUARTZCORE_LIBRARY',
            '/Volumes/DawnUpdate21H461.J120J121J207J208SystemCryptex/System/Library/Frameworks/QuartzCore.framework/default.metallib'))
        self.assertTrue(original.is_file(),'Original21H461 QuartzCore AIR library is required; no synthetic replacement')
        run([self.cc,'-std=c++17','-isysroot',self.mac,'-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*self.includes,
             R/'tests/native540_desktop_draw_model.cpp',R/'package/Userland/NativeMetal540DesktopDraw.cpp',R/'package/Userland/NativeMetal540DesktopLibrary.cpp',*self.sources,
             LIB,'-Wl,-rpath,'+str(LIB.parent),'-o',O/'draw'],'draw-build')
        self.assertIn('full-HD PM4',run([O/'draw',original],'draw'))
        (O/'quartzcore-identity.json').write_text(json.dumps(dict(bytes=original.stat().st_size,
            sha256=hashlib.sha256(original.read_bytes()).hexdigest(),device_execution=False),indent=2)+'\n')

    def test_actual_objc_objects(self):
        original=Path(os.environ.get('NATIVE540_QUARTZCORE_LIBRARY',
            '/Volumes/DawnUpdate21H461.J120J121J207J208SystemCryptex/System/Library/Frameworks/QuartzCore.framework/default.metallib'))
        sources=[R/'package/Userland/NativeMetal540DesktopObjects.mm',R/'package/Userland/NativeMetal540DesktopDraw.cpp',
                 R/'package/Userland/NativeMetal540DesktopLibrary.cpp',*self.sources]
        run([self.cc,'-std=c++17','-isysroot',self.mac,'-O1','-g','-fobjc-arc','-fblocks','-Wall','-Wextra','-Werror',
             '-Wno-deprecated-declarations','-fsanitize=address,undefined',*self.includes,
             R/'tests/native540_desktop_objects_model.mm',*sources,LIB,'-Wl,-rpath,'+str(LIB.parent),
             '-framework','Foundation','-framework','Metal','-framework','IOSurface','-o',O/'objects'],'objects-build')
        env=dict(ENV,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1')
        result=subprocess.run([str(O/'objects'),str(original)],env=env,capture_output=True,text=True)
        (O/'objects.log').write_text(result.stdout+result.stderr)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('PASS actual ObjC desktop',result.stdout)
        run([self.cc,'-std=c++17','-target','arm64-apple-ios17.7','-isysroot',self.sdk,'-fobjc-arc','-fblocks',
             '-Wall','-Wextra','-Werror','-Wno-deprecated-declarations',*self.includes,'-c',sources[0],'-o',O/'objects-ios.o'],'objects-ios')

    def test_arm64_product_compile(self):
        for name in ('NativeMetal540DesktopSession','NativeMetal540DesktopDraw','NativeMetal540DesktopLibrary'):
            run([self.cc,'-std=c++17','-target','arm64-apple-ios17.7','-isysroot',self.sdk,'-Wall','-Wextra','-Werror',
                 *self.includes,'-c',R/('package/Userland/'+name+'.cpp'),'-o',O/(name+'.o')],name+'-ios')

    @classmethod
    def tearDownClass(cls):
        print('Desktop host evidence: '+str(O))


if __name__=='__main__':
    unittest.main()
