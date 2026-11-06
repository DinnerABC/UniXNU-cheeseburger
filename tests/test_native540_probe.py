#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Actual GLSL fixture provenance + finite host transport/ISA model, never device."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('probe_export',ROOT/'tools/gpu-native540/export_probe.py')
exporter=importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)

class ProbeTests(unittest.TestCase):
    def test_actual_compiler_fixture_export(self):
        with tempfile.TemporaryDirectory() as name:
            output=Path(name)/'export'
            meta=json.loads(exporter.export_probe(output).read_text())
            self.assertEqual(meta['producer'],'qgpu_compile_glsl')
            self.assertEqual(meta['route'],'GLSL-NIR-A540-IR3')
            self.assertTrue(meta['jitValid'])
            self.assertFalse(meta['metalCompiler'])
            self.assertFalse(meta['runtimeVerified'])
            self.assertEqual(meta['codeWords'],64)
            self.assertEqual(hashlib.sha256((output/'probe.ir3').read_bytes()).hexdigest(),meta['shader_sha256'])
            self.assertEqual(meta['dispatch']['local'],[64,1,1])
            self.assertEqual(meta['outputBytes'],256)
            self.assertEqual(meta['reflection']['flags'],0x04000008)
            self.assertEqual(meta['reflection']['groupID'],192)
            self.assertEqual(meta['reflection']['constlen'],28)
            self.assertEqual(meta['variants'][0]['immediates'],[])
            # Historical global fixture remains exportable without replacing evidence.
            m, raw, p, reflection, isa, immediates, code = exporter.load_program(exporter.DEFAULT_MANIFEST)
            self.assertEqual((output/'program.h').read_bytes(), exporter.render_header(p, reflection, isa, immediates, code))
            local = Path(name)/'local'
            lm = json.loads(exporter.export_probe(local, exporter.LOCAL_MANIFEST).read_text())
            self.assertEqual((local/'program.h').read_bytes(), (ROOT/'tools/gpu-native540/probe_fixture.h').read_bytes())
            self.assertEqual((local/'probe.comp').read_text(), exporter.LOCAL_GLSL)
            self.assertEqual(lm['reflection']['groupID'],252)
            self.assertEqual(lm['reflection']['localID'],0)
            self.assertEqual(lm['reflection']['constlen'],28)
            self.assertEqual(lm['reflection']['instrlen'],1)
            self.assertIn('gl_LocalInvocationID.x', lm['sourceProducer'])
            evidence = exporter.LOCAL_MANIFEST.parent
            self.assertEqual(hashlib.sha256((evidence/'probe.jit').read_bytes()).hexdigest(), lm['finalJIT']['sha256'])
            self.assertEqual((evidence/'final-reflection.bin').read_bytes(), __import__('struct').pack('<621I', *lm['reflection']['descriptionWords']))
            self.assertEqual((evidence/'backend-reflection.bin').read_bytes(), __import__('struct').pack('<621I', *lm['backendReflection']['descriptionWords']))
            asm = (evidence/'probe.asm').read_text()
            self.assertNotIn('r48', asm)
            self.assertIn('c24.x', asm)
            self.assertIn('stgb.untyped.4d.u32.1', asm)
            with self.assertRaises(ValueError):exporter.export_probe(output)
    def test_symlink_output_refused(self):
        with tempfile.TemporaryDirectory() as name:
            output=Path(name)/'link';output.symlink_to(Path(name)/'missing')
            with self.assertRaises(ValueError):exporter.export_probe(output)
    def test_metadata_and_isa_tampering_rejected(self):
        import shutil
        for mode in ['flags','constant','sysval','raw','isa','immediate','source']:
            with self.subTest(mode=mode),tempfile.TemporaryDirectory() as name:
                d=Path(name);src=exporter.DEFAULT_MANIFEST.parent
                for n in ['manifest.json','probe.ir3','probe.comp']:shutil.copy2(src/n,d/n)
                m=json.loads((d/'manifest.json').read_text());p=m['variants'][0]['info']
                if mode=='flags':p['flags']=8;p['descriptionWords'][24]=8
                elif mode=='constant':p['constlen']=32;p['descriptionWords'][4]=32
                elif mode=='sysval':p['groupID']=252;p['descriptionWords'][15]=252
                elif mode=='raw':m['backendReflection']['descriptionWords'][44]=4
                elif mode=='immediate':m['variants'][0]['immediates']=[0]
                elif mode=='isa':(d/'probe.ir3').write_bytes(bytes(256))
                else:(d/'probe.comp').write_text(exporter.GLSL.replace('deadbeef','12345678'))
                (d/'manifest.json').write_text(json.dumps(m))
                with self.assertRaises(ValueError):exporter.export_probe(d/'output',d/'manifest.json')
    def test_host_model(self):
        with tempfile.TemporaryDirectory() as name:
            binary=Path(name)/'model'
            subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                '-I'+str(ROOT/'package/Userland'),str(ROOT/'tests/native540_probe_model.cpp'),
                str(ROOT/'package/Userland/NativeMetal540Client.cpp'),'-o',str(binary)],check=True)
            subprocess.run([binary],check=True)

    def test_global_four_group_model(self):
        with tempfile.TemporaryDirectory() as name:
            binary=Path(name)/'global-model'
            subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                '-DNATIVE540_PROBE_GLOBAL=1','-I'+str(ROOT/'package/Userland'),
                str(ROOT/'tests/native540_probe_model.cpp'),
                str(ROOT/'package/Userland/NativeMetal540Client.cpp'),'-o',str(binary)],check=True)
            subprocess.run([binary],check=True)

if __name__=='__main__':unittest.main()
