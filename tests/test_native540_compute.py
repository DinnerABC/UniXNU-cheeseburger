#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Real pinned CS export + refusal tests; host PM4 model, never device work."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
TOOL = REPO / 'tools/gpu-native540/export_program.py'
spec = importlib.util.spec_from_file_location('export_native540_compute', TOOL)
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)
REAL = Path('/Users/dinnerabc/unixnu-p21/native540/verified-arithmetic_reduce_gradient_all')


class ExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.evidence = json.loads((REPO / 'tools/gpu-native540/evidence/verified-arithmetic_reduce_gradient_all.json').read_text())

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='native540-compute-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = self.root / 'program.json'
        self.output = self.root / 'export'
        self.m = copy.deepcopy(self.evidence)
        if not (REAL / 'program.base.ir3').is_file():
            self.skipTest('private pinned IR3 unavailable; real export/model not tested')
        shutil.copyfile(REAL / 'program.base.ir3', self.root / 'program.base.ir3')

    def run_export(self):
        self.manifest.write_text(json.dumps(self.m))
        return exporter.export_program(self.manifest, self.output)

    def reject(self):
        with self.assertRaises((ValueError, KeyError, TypeError, OSError)):
            self.run_export()
        self.assertFalse(self.output.exists(), 'rejected export must not publish output')

    def mutate(self, key, value):
        p = self.m['variants'][0]['info']
        p[key] = value
        mapping = {name: index for index, name in enumerate(exporter.PREFIX_FIELDS)} | exporter.TAIL_FIELDS
        p['descriptionWords'][mapping[key]] = value

    def test_real_export_hashes_no_reflection_as_isa(self):
        header = self.run_export()
        meta = json.loads((self.output / 'metadata.json').read_text())
        self.assertEqual(meta['header_sha256'], hashlib.sha256(header.read_bytes()).hexdigest())
        self.assertEqual(meta['shader_sha256'], self.m['variants'][0]['sha256'])
        self.assertEqual(meta['reflection_sha256'], hashlib.sha256(struct.pack('<621I', *meta['reflection']['descriptionWords'])).hexdigest())
        self.assertEqual(meta['codeWords'], 960)
        self.assertFalse(meta['runtimeVerified'])
        self.assertFalse(meta['ir3SecurityValidated'])
        self.assertNotIn('descriptionWords', header.read_text())
        previous = header.read_bytes()
        with self.assertRaises(ValueError):
            self.run_export()
        self.assertEqual(previous, header.read_bytes())

    def test_full_named_reflection_consistency(self):
        names = exporter.PREFIX_FIELDS + list(exporter.TAIL_FIELDS)
        for name in names:
            with self.subTest(field=name):
                self.m = copy.deepcopy(self.evidence)
                self.m['variants'][0]['info'][name] ^= 1
                self.reject()

    def test_omitted_raw_words_not_silently_ignored(self):
        live = {exporter.PREFIX_FIELDS.index(name) for name in exporter.TYPED_FIELDS} | set(range(44,52))
        for index in set(range(621)) - live:
            with self.subTest(word=index):
                self.m = copy.deepcopy(self.evidence)
                self.m['variants'][0]['info']['descriptionWords'][index] ^= 1
                self.reject()

    def test_invalid_structural_subset(self):
        mutations = [('codeWords', 621), ('instrlen', 0), ('instrlen', 129),
            ('constlen', 35), ('constlen', 128+4), ('fullRegs', 0), ('halfRegs', 64),
            ('branchStack', 4), ('threadSize', 2), ('subgroupSize', 128),
            ('immediateBase', 112), ('driverBase', 108), ('driverCount', 17),
            ('localID', 250), ('groupID', 253), ('bufferMask', 256), ('writeMask', 8),
            ('flags', 0x440000ec), ('sharedBytes', 4), ('sharedBytes', 32784),
            ('threadgroupMask', 0), ('threadgroupMask', 256),
            ('textureCount', 1), ('imageMask', 1), ('sampleFlags', 1),
            ('constantWindowCount', 1), ('textureBindings', 1)]
        for name, value in mutations:
            with self.subTest(field=name, value=value):
                self.m = copy.deepcopy(self.evidence)
                self.mutate(name, value)
                self.reject()
        for index, stride in [(44,3), (47,4)]:
            self.m = copy.deepcopy(self.evidence)
            self.m['variants'][0]['info']['descriptionWords'][index] = stride
            self.reject()

    def test_invalid_files_and_provenance(self):
        for key, value in [('schema', True), ('gpu', 740), ('jitVersion', 42),
                           ('targetOSBuild', '20H392'), ('runtimeVerified', True)]:
            self.m = copy.deepcopy(self.evidence)
            self.m[key] = value
            self.reject()
        for key, value in [('sagit', 'wrong'), ('mesa', 'wrong'), ('compiler_sha256', '0')]:
            self.m = copy.deepcopy(self.evidence)
            self.m['source'][key] = value
            self.reject()
        self.m = copy.deepcopy(self.evidence)
        self.m['variants'][0]['shader'] = '../program.base.ir3'
        self.reject()
        self.m = copy.deepcopy(self.evidence)
        self.m['variants'][0]['immediates'][0] = -1
        self.reject()
        self.m = copy.deepcopy(self.evidence)
        self.m['variants'].append(copy.deepcopy(self.m['variants'][0]))
        self.reject()
        self.m = copy.deepcopy(self.evidence)
        self.m['variants'][0]['info']['surpriseFeature'] = 0
        self.reject()
        self.m = copy.deepcopy(self.evidence)
        p = self.m['variants'][0]['info']
        p['descriptionWords'][5] = True
        p['fullRegs'] = True
        self.reject()
        self.m = copy.deepcopy(self.evidence)
        self.m['variants'][0]['info']['descriptionWords'].pop()
        self.reject()
        self.m = copy.deepcopy(self.evidence)
        code = bytearray((self.root / 'program.base.ir3').read_bytes())
        code[0] ^= 1
        (self.root / 'program.base.ir3').write_bytes(code)
        self.reject()  # same-length ISA corruption must fail the recorded hash
        (self.root / 'program.base.ir3').write_bytes(b'not ISA')
        self.reject()

    def test_refuse_existing_directory_file_and_symlink(self):
        for kind in ('directory', 'file', 'symlink'):
            target = self.root / kind
            if kind == 'directory':
                target.mkdir()
            elif kind == 'file':
                target.write_text('retain')
            else:
                target.symlink_to(self.root / 'does-not-exist')
            self.output = target
            self.manifest.write_text(json.dumps(self.m))
            with self.assertRaises(ValueError):
                exporter.export_program(self.manifest, target)
            self.assertTrue(target.exists() or target.is_symlink())

    def test_host_model_and_arm64_headers(self):
        header = self.run_export()
        clang = shutil.which('clang++')
        self.assertIsNotNone(clang, 'clang++ required for real host model')
        source = REPO / 'tests/native540_compute_model.cpp'
        flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror',
                 '-I' + str(REPO / 'package/Userland'),
                 '-DNATIVE540_EXPORTED_HEADER="' + str(header) + '"']
        binary = self.root / 'model'
        subprocess.run([clang, *flags, '-fsanitize=address,undefined', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        # Use a tiny real client+generated-program translation unit, not the
        # host model's iostream/sysroot, when testing the iOS arm64 header ABI.
        unit = self.root / 'client.cpp'
        unit.write_text('#include "' + str(REPO / 'package/Userland/NativeMetal540Client.h') + '"\n'
                        '#include "' + str(header) + '"\n'
                        'bool encodeClient(const NativeMetal540::Resources &r, uint32_t *w, uint32_t &n) {\n'
                        'NativeMetal540::Dispatch d; d.threadgroupLengths[0]=4096;\n'
                        'return NativeMetal540::encode(Native540Export::program,d,r,w,2048,n); }\n')
        xcrun = shutil.which('xcrun')
        if not xcrun:
            print('arm64 iOS header check unavailable: no xcrun')
            return
        sdk = subprocess.run([xcrun, '--sdk', 'iphoneos', '--show-sdk-path'], capture_output=True, text=True)
        if sdk.returncode:
            # Do not change the host's selected developer directory. Installed
            # Xcode can provide an iOS SDK while CLT remains the active default.
            import os
            env = dict(os.environ, DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
            sdk = subprocess.run([xcrun, '--sdk', 'iphoneos', '--show-sdk-path'],
                                 capture_output=True, text=True, env=env)
        if sdk.returncode:
            print('arm64 iOS header check unavailable: no iphoneos SDK')
            return
        subprocess.run([clang, *flags, '-target', 'arm64-apple-ios17.7', '-isysroot', sdk.stdout.strip(),
                        '-c', str(unit), '-o', str(self.root / 'client.o')], check=True)
        print('native540 client + generated compute header arm64 iOS17.7 compile PASS')


if __name__ == '__main__':
    unittest.main(verbosity=2)
