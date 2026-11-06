#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded callback model + real public wrapper compilation; no installs/device.
A new local evidence directory is made for each invocation. Host software pixels
are NOT hardware/IOSurface coherence evidence. No existing build integration.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest

REPO = Path(__file__).resolve().parents[1]
USER = REPO / 'package/Userland'
TOOLS = REPO / 'tools/gpu-native540'
DEVELOPER = Path('/Applications/Xcode.app/Contents/Developer')
ENV = {**os.environ, 'DEVELOPER_DIR': str(DEVELOPER)}
STAMP = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
OUT = Path(os.environ.get('NATIVE540_SURFACE_OUT',
    f'/Users/dinnerabc/unixnu-p21/native540/surface-{STAMP}'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args, name, env=ENV):
    process = subprocess.run(list(map(str, args)), capture_output=True, text=True, env=env)
    (OUT / (name + '.log')).write_text(process.stdout + process.stderr)
    (OUT / (name + '-command.json')).write_text(json.dumps(list(map(str, args)), indent=2) + '\n')
    if process.returncode:
        raise RuntimeError(name + ': ' + process.stdout + process.stderr)
    return process.stdout


class Surface(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=False)
        cls.clang = run(['xcrun', '--find', 'clang++'], 'compiler').strip()
        cls.host_sdk = run(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], 'host-sdk').strip()
        cls.ios_sdk = run(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], 'ios-sdk').strip()
        run([cls.clang, '-std=c++17', '-O1', '-g', '-fobjc-arc', '-DNATIVE540_ENCODER_ONLY',
             '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-isysroot', cls.host_sdk, '-I'+str(USER), '-I'+str(TOOLS),
             REPO/'tests/native540_surface_model.mm', USER/'NativeMetal540Surface.mm',
             USER/'NativeMetal540Render.cpp', USER/'NativeMetal540Client.cpp',
             '-framework', 'Foundation', '-framework', 'IOSurface', '-framework', 'IOKit',
             '-o', OUT/'surface-model'], 'host-asan-ubsan-build')

    def model(self, suite):
        output = run([OUT/'surface-model', suite], suite,
                     {**ENV, 'ASAN_OPTIONS': 'detect_leaks=0:halt_on_error=1',
                      'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
        self.assertIn('PASS', output)
        self.assertIn('NOT GPU execution', output)

    def test_alignment_pitch_plane_allocation(self):
        self.model('layout')

    def test_one_shot_fixed_renderer_retention(self):
        self.model('success')

    def test_failures_timeout_context_retention(self):
        self.model('failures')

    def test_fence_notwritten_and_timestamps(self):
        self.model('notwritten')

    def test_arm64_ios17_7_public_wrapper_compile(self):
        for name, ext in [('NativeMetal540Surface', 'mm'), ('NativeMetal540Render', 'cpp'),
                          ('NativeMetal540Client', 'cpp')]:
            flags = ['-fobjc-arc', '-fblocks'] if ext == 'mm' else []
            run([self.clang, '-std=c++17', '-target', 'arm64-apple-ios17.7',
                 '-isysroot', self.ios_sdk, '-DNATIVE540_ENCODER_ONLY', '-Wall', '-Wextra', '-Werror',
                 *flags, '-I'+str(USER), '-I'+str(TOOLS), '-c', USER/(name+'.'+ext),
                 '-o', OUT/(name+'-ios17.7.o')], name+'-arm64-ios17.7')

    @classmethod
    def tearDownClass(cls):
        paths = [USER/'NativeMetal540Surface.h', USER/'NativeMetal540Surface.mm',
                 USER/'NativeMetal540Render.h', USER/'NativeMetal540Render.cpp',
                 USER/'NativeMetal540Client.h', USER/'NativeMetal540Client.cpp',
                 REPO/'package/BoardRuntime/NativeAdrenoABI.h', TOOLS/'render_fixture.h',
                 REPO/'tests/native540_surface_model.mm', Path(__file__).resolve()]
        provenance = {'deviceAccess': False, 'runtimeVerified': False, 'softwareModel': True,
                      'compiler': cls.clang, 'hostSDK': cls.host_sdk, 'iosSDK': cls.ios_sdk,
                      'iosTarget': 'arm64-apple-ios17.7', 'sanitizers': ['address', 'undefined'],
                      'pins': {str(p): sha(p) for p in paths}}
        (OUT/'provenance.json').write_text(json.dumps(provenance, indent=2)+'\n')
        print('Local evidence: '+str(OUT))


if __name__ == '__main__':
    unittest.main()
