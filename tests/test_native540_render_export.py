#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Public manifest-only fail-closed tests. No Apple AIR/ISA input or device."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

REPO = Path(__file__).resolve().parents[1]
TOOLS = REPO / 'tools/gpu-native540'
SPEC = importlib.util.spec_from_file_location('render_export', TOOLS / 'export_render_program.py')
export = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(export)


class RenderExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.public = {entry: json.loads((TOOLS / 'evidence' / ('verified-' + entry + '.json')).read_text())
                      for entry in ('SolidColor_VertexFunc', 'FastSingle_FragFunc')}
        # PUBLIC metadata only, transcribed from actual pinned AIR compilation.
        # No Apple AIR or ISA, and never usable as executable fixture identity.
        solid = copy.deepcopy(cls.public['FastSingle_FragFunc'])
        solid['entry'] = 'SolidColor_FragFunc'
        solid['variants'] = []
        for name, (code_hash, reflection_hash, immediates) in export.PINS[solid['entry']]['variants'].items():
            window = 'ConstantWindow' in name or name == 'constantWindow'
            msaa = name in ('msaa', 'msaaConstantWindow')
            words = [0] * 621
            for index in (14,15,16,17,19,25,29,30,31,589,590,591,592,593,594,595):
                words[index] = 252
            values = dict(stage=2,codeWords=32 if window else 96,instrlen=1 if window else 3,
                constlen=36,fullRegs=1 if window else 6,subgroupSize=64,
                immediateBase=0 if window else 132,immediateCount=len(immediates),
                color=0 if window else 16,bufferMask=1,flags=201326616 if window else 67108888,
                colorMask=1,constantWindowCount=1,sampleFlags=256 if msaa else 0)
            for key, value in values.items():
                words[export.FIELDS[key]] = value
            words[44] = 16
            words[588] = values['color']
            info = {key: words[index] for key, index in export.FIELDS.items()}
            info['descriptionWords'] = words
            assert export.digest(struct.pack('<621I', *words)) == reflection_hash
            solid['variants'].append(dict(name=name,info=info,immediates=immediates,
                shader='program.'+name+'.ir3',sha256=code_hash))
        cls.public[solid['entry']] = solid

    def manifests(self):
        return copy.deepcopy(self.public)

    def test_exact_public_metadata_is_identity_admitted_not_encoder_supported(self):
        for entry, manifest in self.manifests().items():
            self.assertIs(export.validate_manifest(manifest, entry), manifest)
            for variant in manifest['variants']:
                needed = export.typed_needed(variant['info'])
                self.assertIs(needed['supportsCurrentEncoder'], False)
                self.assertEqual(list(needed['renderInfoProjection']), export.RENDER_FIELDS)
        self.assertEqual(export.SAGIT, '673f85070fe9f7872793015eb230a5c3987d4a8c')

    def test_profile_boundary(self):
        entry = 'SolidColor_VertexFunc'
        changes = [('schema', True), ('schema', 2), ('gpu', 530), ('gpu', 540.0),
                   ('jitVersion', 40), ('route', 'GLSL-NIR-IR3'), ('route', 'Metal'),
                   ('entry', 'unknown'), ('entry', 'padding_gradient'),
                   ('targetOSBuild', '20H392'), ('targetOSBuild', '22H374'),
                   ('runtimeVerified', True), ('runtimeVerified', 0),
                   ('frontend', 'supported Metal'), ('lowerNS', -1), ('backendNS', True)]
        for key, value in changes:
            with self.subTest(key=key, value=value):
                manifest = self.manifests()[entry]
                manifest[key] = value
                with self.assertRaises(ValueError):
                    export.validate_manifest(manifest, entry)

    def test_full_source_not_just_revision_or_hash_shape(self):
        entry = 'SolidColor_VertexFunc'
        original = self.public[entry]
        for key in original['source']:
            with self.subTest(key=key):
                manifest = self.manifests()[entry]
                if key == 'adapter_sources':
                    manifest['source'][key][next(iter(manifest['source'][key]))] = '0' * 64
                elif key == 'gpu':
                    manifest['source'][key] = 530
                else:
                    manifest['source'][key] = '0' * 64
                with self.assertRaises(ValueError):
                    export.validate_manifest(manifest, entry)
        manifest = self.manifests()[entry]
        manifest['source']['adapter_sources']['new-unreviewed-source.cpp'] = 'a' * 64
        with self.assertRaises(ValueError):
            export.validate_manifest(manifest, entry)

    def test_original_input_identity(self):
        entry = 'FastSingle_FragFunc'
        for key, value in [('sha256', '0' * 64), ('bytes', 92787), ('bytes', 92788.0),
                           ('path', ''), ('path', 'x\0y')]:
            with self.subTest(key=key):
                manifest = self.manifests()[entry]
                manifest['input'][key] = value
                with self.assertRaises(ValueError):
                    export.validate_manifest(manifest, entry)

    def test_unknown_or_missing_fields_at_every_level(self):
        entry = 'FastSingle_FragFunc'
        for level in ('manifest', 'input', 'variant', 'info'):
            for action in ('add', 'remove'):
                with self.subTest(level=level, action=action):
                    manifest = self.manifests()[entry]
                    target = {'manifest': manifest, 'input': manifest['input'],
                              'variant': manifest['variants'][0], 'info': manifest['variants'][0]['info']}[level]
                    if action == 'add':
                        target['unsupportedUniform'] = 1
                    else:
                        target.pop(next(iter(target)))
                    with self.assertRaises(ValueError):
                        export.validate_manifest(manifest, entry)

    def test_all_621_words_all_siblings_fail_on_change(self):
        # Includes unnamed attributes, samplers, uniforms/windows, inactive
        # slots and reserved tails, not just the 24 Render::Info fields.
        for entry, original in self.public.items():
            for index in range(len(original['variants'])):
                for word in range(621):
                    with self.subTest(entry=entry, variant=index, word=word):
                        manifest = self.manifests()[entry]
                        manifest['variants'][index]['info']['descriptionWords'][word] ^= 1
                        with self.assertRaises(ValueError):
                            export.validate_manifest(manifest, entry)

    def test_coherent_named_raw_changes_still_rejected(self):
        # Editing BOTH views must not bypass the reviewed full-reflection pin.
        for entry, original in self.public.items():
            for name, word in export.FIELDS.items():
                with self.subTest(entry=entry, field=name):
                    manifest = self.manifests()[entry]
                    info = manifest['variants'][0]['info']
                    info[name] ^= 1
                    info['descriptionWords'][word] = info[name]
                    with self.assertRaises(ValueError):
                        export.validate_manifest(manifest, entry)

    def test_reflection_uint32_length_and_constant_bounds(self):
        entry = 'SolidColor_VertexFunc'
        for words in ([0] * 620, [0] * 622, [0] * 620 + [True],
                      [0] * 620 + [-1], [0] * 620 + [2**32], 'not words'):
            manifest = self.manifests()[entry]
            manifest['variants'][0]['info']['descriptionWords'] = words
            with self.assertRaises(ValueError):
                export.validate_manifest(manifest, entry)
        for field, value in [('immediateBase', 145), ('immediateCount', 513),
                             ('driverBase', 132), ('driverCount', 41),
                             ('codeWords', 4097), ('fullRegs', 64), ('constlen', 129)]:
            info = copy.deepcopy(self.public[entry]['variants'][0]['info'])
            info[field] = info['descriptionWords'][export.FIELDS[field]] = value
            with self.assertRaises(ValueError):
                export.validate_reflection(info, 1)

    def test_variant_identity_and_immediates(self):
        entry = 'FastSingle_FragFunc'
        for action in ('remove', 'duplicate', 'reverse', 'unknown', 'hash', 'immediate', 'boolean'):
            manifest = self.manifests()[entry]
            variants = manifest['variants']
            if action == 'remove':
                variants.pop()
            elif action == 'duplicate':
                variants.append(copy.deepcopy(variants[0]))
            elif action == 'reverse':
                variants.reverse()
            elif action == 'unknown':
                variants[1]['name'] = 'constantWindow'
            elif action == 'hash':
                variants[1]['sha256'] = '0' * 64
            elif action == 'immediate':
                variants[1]['immediates'][0] ^= 1
            else:
                variants[1]['immediates'][0] = True
            with self.subTest(action=action), self.assertRaises(ValueError):
                export.validate_manifest(manifest, entry)

    def test_path_traversal_absolute_and_symlink_shader_refused(self):
        entry = 'SolidColor_VertexFunc'
        for path in ('../program.base.ir3', '/tmp/program.base.ir3', 'subdir/program.base.ir3'):
            manifest = self.manifests()[entry]
            manifest['variants'][0]['shader'] = path
            with self.assertRaises(ValueError):
                export.validate_manifest(manifest, entry)
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            target = directory / 'not-Apple.bin'
            target.write_bytes(bytes(640))
            (directory / 'program.base.ir3').symlink_to(target)
            with self.assertRaisesRegex(ValueError, 'regular'):
                export.read_codes(directory, self.public[entry])

    def test_isa_size_and_bytes_hash_fail_closed_no_synthetic_metal(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            for size in (639, 640, 641):
                (directory / 'program.base.ir3').write_bytes(bytes(size))
                with self.subTest(size=size), self.assertRaises(ValueError):
                    export.read_codes(directory, self.public['SolidColor_VertexFunc'])

    def test_needed_slices_and_real_link_failure(self):
        vs = self.public['SolidColor_VertexFunc']['variants'][0]['info']
        fs = self.public['FastSingle_FragFunc']['variants'][0]['info']
        self.assertEqual(export.typed_needed(vs)['buffers'], [dict(slot=0, stride=16, writable=False)])
        self.assertEqual(export.typed_needed(fs)['textureSampler']['sampleMask'], 1)
        inputs = export.typed_needed(fs)['varyings']
        self.assertEqual([i['components'] for i in inputs], [2, 4])
        self.assertEqual([i['mask'] for i in inputs], [3, 15])
        self.assertEqual([i['inloc'] for i in inputs], [0, 2])
        self.assertEqual([i['mode'] for i in inputs], [0, 0])
        report = export.blocked_report(vs, fs)
        self.assertFalse(report['link']['compatible'])
        self.assertEqual(report['link']['missingVertexOutputs'], inputs)
        self.assertEqual(report['status'], 'blocked')
        for flag in ('supportsCurrentEncoder', 'runtimeVerified', 'deviceOperated',
                     'reflectionEdited', 'ir3SecurityValidated'):
            self.assertIs(report[flag], False)
        # Inspection/export must NEVER mutate compiler-owned metadata.
        self.assertEqual(vs, self.public['SolidColor_VertexFunc']['variants'][0]['info'])
        self.assertEqual(fs, self.public['FastSingle_FragFunc']['variants'][0]['info'])

    def test_matching_solidcolor_zero_texture_pair_next_registry_not_runnable(self):
        vs = self.public['SolidColor_VertexFunc']['variants'][0]['info']
        manifest = self.public['SolidColor_FragFunc']
        export.validate_manifest(manifest, 'SolidColor_FragFunc')
        fs = manifest['variants'][0]['info']
        report = export.blocked_report(vs, fs, 'SolidColor_FragFunc')
        self.assertTrue(report['link']['compatible'])
        self.assertFalse(report['supportsCurrentEncoder'])
        self.assertEqual(report['entries'], ['SolidColor_VertexFunc', 'SolidColor_FragFunc'])
        self.assertTrue(report['nextRegistryPrerequisite']['noTextures'])
        self.assertTrue(report['nextRegistryPrerequisite']['noUserVaryings'])
        self.assertEqual(export.typed_needed(fs)['constantWindows'], [dict(buffer=0, byteOffset=0)])
        self.assertEqual(export.typed_needed(fs)['buffers'], [dict(slot=0,stride=16,writable=False)])
        self.assertEqual([v['name'] for v in manifest['variants']],
                         ['base','msaa','constantWindow','msaaConstantWindow'])

    def test_duplicate_json_and_bound(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / 'manifest.json'
            for text in ('{"a":1,"a":2}', '{"nested":{"x":1,"x":2}}', '{"a":NaN}',
                         ' ' * (1024*1024 + 1)):
                path.write_text(text)
                with self.assertRaises(ValueError):
                    export.read_manifest(path)

    def test_export_cannot_write_public_or_replace_evidence(self):
        with tempfile.TemporaryDirectory() as name:
            output = Path(name) / 'public-output'
            with self.assertRaisesRegex(ValueError, 'owned private'):
                export.export_render_program('missing-vs', 'missing-fs', output, 'missing-library')
            self.assertFalse(output.exists())
            output.mkdir()
            marker = output / 'marker'
            marker.write_text('preserve')
            with self.assertRaisesRegex(ValueError, 'replace existing'):
                export.export_render_program('missing-vs', 'missing-fs', output, 'missing-library')
            self.assertEqual(marker.read_text(), 'preserve')


if __name__ == '__main__':
    unittest.main()
