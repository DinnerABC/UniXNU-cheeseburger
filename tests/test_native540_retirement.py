#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exact retirement transport model; host ASan/UBSan only, no device/candidate.
Evidence uses a fresh local directory. Callback budgets do not preempt blocking
IOKit calls; the model injects timeout/error returns rather than accessing IOKit.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
USER = REPO / 'package/Userland'
MODEL = REPO / 'tests/native540_retirement_model.cpp'
ENV = dict(os.environ)
if sys.platform == 'darwin':
    ENV['DEVELOPER_DIR'] = '/Applications/Xcode.app/Contents/Developer'
OUT = None


def run(args, name, env=ENV):
    command = list(map(str, args))
    (OUT / (name + '-command.json')).write_text(json.dumps(command, indent=2) + '\n')
    process = subprocess.run(command, capture_output=True, text=True, env=env, timeout=60)
    (OUT / (name + '.log')).write_text(process.stdout + process.stderr)
    if process.returncode:
        raise RuntimeError(name + ': ' + process.stdout + process.stderr)
    return process.stdout


class Retirement(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        global OUT
        parent = os.environ.get('NATIVE540_RETIREMENT_OUT')
        if parent:
            Path(parent).mkdir(parents=True, exist_ok=True)
        OUT = Path(tempfile.mkdtemp(prefix='native540-retirement-', dir=parent))
        print('Local evidence: ' + str(OUT))
        if sys.platform == 'darwin':
            cls.clang = run(['xcrun', '--find', 'clang++'], 'compiler').strip()
            cls.host_sdk = run(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], 'host-sdk').strip()
            cls.ios_sdk = run(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], 'ios-sdk').strip()
            host_flags = ['-isysroot', cls.host_sdk]
        else:
            cls.clang = shutil.which('clang++')
            if not cls.clang:
                raise RuntimeError('clang++ is required for ASan/UBSan')
            cls.host_sdk = cls.ios_sdk = None
            host_flags = []
        run([cls.clang, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer', *host_flags,
             MODEL, '-o', OUT / 'retirement-model'], 'host-asan-ubsan-build')

    def model(self, suite):
        output = run([OUT / 'retirement-model', suite], suite,
                     {**ENV, 'ASAN_OPTIONS': ('detect_leaks=0' if sys.platform == 'darwin'
                                             else 'detect_leaks=1') + ':halt_on_error=1',
                      'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
        self.assertIn('PASS ' + suite, output)
        self.assertIn('NOT GPU execution or owner release', output)

    def test_exact_owned_import_ack_once(self):
        self.model('success')

    def test_fence_cache_unlock_uncertain_completefailed_gates(self):
        self.model('gates')

    def test_all_callback_failures_timeout_partial_ack_sticky(self):
        self.model('failures')

    def test_nonzero_output_length_never_acknowledges_success(self):
        self.model('malformed')

    def test_duplicate_stale_identity_ranges_capacity(self):
        self.model('identity')

    def test_finite_callback_budget_preflight(self):
        self.model('budget')

    @unittest.skipUnless(sys.platform == 'darwin', 'iOS SDK requires macOS')
    def test_arm64_ios17_7_header_compiles(self):
        run([self.clang, '-std=c++17', '-target', 'arm64-apple-ios17.7',
             '-isysroot', self.ios_sdk, '-Wall', '-Wextra', '-Werror',
             '-include', USER / 'NativeMetal540Retirement.h', '-x', 'c++', '-c', '/dev/null',
             '-o', OUT / 'retirement-ios17.7.o'], 'arm64-ios17.7-build')

    @classmethod
    def tearDownClass(cls):
        paths = [USER / 'NativeMetal540Retirement.h', USER / 'NativeMetal540Client.h',
                 REPO / 'package/BoardRuntime/NativeAdrenoABI.h', MODEL, Path(__file__).resolve()]
        provenance = {'deviceAccess': False, 'runtimeVerified': False, 'softwareModel': True,
                      'ownerRelease': False, 'compiler': cls.clang, 'hostSDK': cls.host_sdk,
                      'iosSDK': cls.ios_sdk, 'sanitizers': ['address', 'undefined'],
                      'pins': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}
        (OUT / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
