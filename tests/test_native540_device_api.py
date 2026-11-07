#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
import datetime
import json
import re
import os
from pathlib import Path
import subprocess
import unittest
R=Path(__file__).resolve().parents[1]
O=Path('/Users/dinnerabc/unixnu-p21/native540')/('device-api-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
ENV=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
def run(args,label,env=ENV):
    p=subprocess.run(list(map(str,args)),capture_output=True,text=True,env=env)
    (O/(label+'.log')).write_text(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError(label+': '+p.stdout+p.stderr)
    return p.stdout
class DeviceAPI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        O.mkdir();cls.cc=run(['xcrun','--find','clang++'],'compiler').strip()
        cls.mac=run(['xcrun','--sdk','macosx','--show-sdk-path'],'macsdk').strip();cls.ios=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],'iossdk').strip()
    def test_actual_methods_and_rejections(self):
        names=('NativeMetal540DeviceAdapter.mm','NativeMetal540Capabilities.mm','NativeMetal540ProtocolAudit.mm','NativeMetal540Objects.mm','NativeMetal540Render.cpp','NativeMetal540Client.cpp','NativeMetal540UnknownSelector.cpp')
        run([self.cc,'-std=c++17','-O1','-g','-fobjc-arc','-fblocks','-DNATIVE540_ENCODER_ONLY','-Wall','-Wextra','-Werror',
             '-Wno-deprecated-declarations','-isysroot',self.mac,'-I'+str(R/'package/Userland'),'-I'+str(R/'package/BoardRuntime'),
             '-fsanitize=address,undefined','-fno-omit-frame-pointer',R/'tests/native540_device_api_model.mm',
             *[R/'package/Userland'/x for x in names],'-framework','Foundation','-framework','Metal','-framework','IOKit','-framework','IOSurface','-o',O/'model'],'build')
        result=run([O/'model'],'behavior',dict(ENV,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1'))
        self.assertIn('PASS 28 target ABI entries',result)
    def test_original_target_protocol_pins(self):
        original=Path('/Users/dinnerabc/unixnu-p21/native540/protocol-audit-21h461-20261106T1223/protocols.json')
        protocols=json.loads(original.read_text());methods={m['selector']:m['type'] for p in protocols for m in p['methods']}
        rows=re.findall(r'\{"([^"]+)","([^"]+)"\}',(R/'tests/native540_device_api_model.mm').read_text())
        self.assertEqual(len(rows),42)
        for selector,encoding in rows:self.assertEqual(methods[selector],encoding,selector)
    def test_arm64(self):
        for name in ('NativeMetal540DeviceAdapter','NativeMetal540ProtocolAudit'):
            run([self.cc,'-std=c++17','-target','arm64-apple-ios17.7','-isysroot',self.ios,'-fobjc-arc','-fblocks','-DNATIVE540_ENCODER_ONLY',
                 '-I'+str(R/'package/Userland'),'-I'+str(R/'package/BoardRuntime'),'-Wall','-Wextra','-Werror','-Wno-deprecated-declarations',
                 '-c',R/('package/Userland/'+name+'.mm'),'-o',O/(name+'.o')],name+'-arm64')
    @classmethod
    def tearDownClass(cls):print('Local evidence: '+str(O))
if __name__=='__main__':unittest.main()
