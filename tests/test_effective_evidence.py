#!/usr/bin/env python3
"""Host window model and read-only Wagtail replay; never invokes Device/ADB."""
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts/logging'))
import heartbeat_record as hb

SPEC = importlib.util.spec_from_file_location('analyze_run', ROOT / 'scripts/logging/analyze-cheeseburger-run.py')
analyzer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analyzer)


def model_window(boot=0xAA, slot_texts=None, msg=None, ticks=False):
    count, slot_first, slot_blocks = 3, 257, 32
    blocks = slot_first + slot_blocks * count + 31
    texts = slot_texts if slot_texts is not None else [
        '#slot 0 t=0s tag=cheeseburger-P21\nGPSVC bid=aa\nSWAP check=1 reject=0 why=0 transfer=1 rc=0\n',
        '#slot 1 t=10s tag=cheeseburger-P21\nGPSVC bid=aa\nSWAP check=7 reject=0 why=0 transfer=7 rc=0\n',
    ]
    msg = msg if msg is not None else (
        '[   50.250000]: CB persistent log: first commit synced, boot=aa\n'
        '[   52.000000]: CB GPU dpu capture rc=-7\n'
        '[   60.250000]: CB GPU core: resume rc=-4 rb=1/0 sp=1/0\n'
        '[   60.260000]: CB GPU compute: exit rc=-4\n')
    encoded_msg = msg.encode()
    raw = bytearray(blocks * hb.BLOCK)
    values = (b'SHLOG001', 3, analyzer.HEADER.size + 20, boot, 12345, blocks,
              1, 256, len(encoded_msg), slot_first, slot_blocks, count, len(texts),
              max([int(analyzer.SLOT.match(text.splitlines()[0])[2]) for text in texts
                   if analyzer.SLOT.match(text.splitlines()[0])] or [0]), len(texts), 0, 0, 0, 2,
              b'cheeseburger-P21')
    analyzer.HEADER.pack_into(raw, 0, *values)
    raw[hb.BLOCK:hb.BLOCK + len(encoded_msg)] = encoded_msg
    for number, text in enumerate(texts):
        at = (slot_first + number % count * slot_blocks) * hb.BLOCK
        raw[at:at + len(text.encode())] = text.encode()
    free = slot_first + count * slot_blocks
    for seq, sec in ((1, 0), (2, 10)):
        line = hb.encode(boot, seq, sec, 'tick', 2, 2)
        if ticks:
            text = line.split(b'\n')[0] + f' ticks={5000 + sec * 100} start=5000 freq=100\n'.encode()
            line = text + bytes(hb.BLOCK - len(text))
        at = (free + seq % 22) * hb.BLOCK
        raw[at:at + hb.BLOCK] = line
    return bytes(raw)


def replay(raw, retrieval=None):
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp)
        (path / 'window.bin').write_bytes(raw)
        if retrieval is not None:
            (path / 'retrieval.json').write_text(json.dumps(retrieval))
        return analyzer.analyze(path)


class EvidenceReplayTests(unittest.TestCase):
    def test_old_origin_bias_and_first_gpu_vs_core(self):
        report = replay(model_window())
        self.assertTrue(report['effective'])
        self.assertEqual(report['clock']['method'], 'first_commit_estimate')
        self.assertIsNone(report['clock']['error_bound_seconds'])
        self.assertIn('first-round write/sync', report['clock']['limit'])
        self.assertEqual(report['first_gpu_failure']['rc'], -7)
        self.assertEqual(report['first_core_failure']['rc'], -4)
        self.assertEqual(report['first_core_failure']['writer_seconds'], 10)
        self.assertEqual(report['summary']['max_transfers'], 7)

    def test_logtime_exact_origin_signed_rc_and_validity(self):
        core = ('CORE2 valid=5 rc=-4 rbpre=1 sppre=1 rbpost=0 sppost=0 '
                'fence_rc=0 fence_stage=0 sample=6025')
        drain = ('DRAIN stage=2 reason=4 valid=7ff rptr=0 wptr=0 me=f0f vbif=f000f mask=1 '
                 'ackme=ffffffff ackvb=f000f status=0 status3=0 endr=0 endw=0 wait=5 sample=6025')
        texts = ['#slot 0 t=0s tag=cheeseburger-P21\nLOGTIME start=5000 freq=100 sample=5000 sec=0 snapshot_errors=1 heartbeat_errors=2\nGPSVC bid=aa\nGADDON3 boot=1 bid=aa\n',
                 '#slot 1 t=10s tag=cheeseburger-P21\nLOGTIME start=5000 freq=100 sample=6050 sec=10\nGPSVC bid=aa\n'
                 + core + '\n' + drain + '\nE00 t=60.25 p1 s1\n']
        report = replay(model_window(slot_texts=texts, ticks=True))
        self.assertEqual(report['clock']['method'], 'LOGTIME')
        self.assertFalse(report['clock']['estimated'])
        self.assertEqual(report['first_core_failure']['writer_seconds'], 10.25)
        parsed_core, parsed_drain = report['diagnostics']
        self.assertEqual(parsed_core['fields']['rc'], -4)
        self.assertEqual(parsed_core['effective_fields']['rc'], -4)
        self.assertNotIn('rbpost', parsed_core['effective_fields'])
        self.assertNotIn('fence_rc', parsed_core['effective_fields'])
        self.assertEqual(parsed_core['sample_writer_seconds'], 10.25)
        self.assertEqual(parsed_drain['reason_name'], 'ackInvalid')
        self.assertEqual(parsed_drain['invalid_mmio_fields'], ['ackme'])
        self.assertEqual(report['normalized_events'][0]['writer_seconds'], 10.25)

    def test_preidle_stage_reason_and_sample_bits(self):
        # New stage is appended; all pre-existing numerical mappings stay fixed.
        self.assertEqual([analyzer.STAGES[index] for index in range(7)],
                         ['none', 'initial', 'ack', 'final', 'drained', 'resuming', 'resumed'])
        self.assertEqual([analyzer.REASONS[index] for index in range(13)],
                         ['none', 'initialInvalid', 'ringBusy', 'controlMismatch', 'ackInvalid',
                          'ackTimeout', 'finalInvalid', 'cursorChanged', 'notIdle', 'Status3Busy',
                          'resumeInvalid', 'resumeControl', 'resumeCursor'])
        for reason, name, status, invalid in ((13, 'PreIdleInvalid', 'ffffffff', ['status']),
                                              (14, 'PreIdleTimeout', 'c0000101', [])):
            with self.subTest(reason=reason):
                line = (f'DRAIN stage=7 reason={reason} valid=180 rptr=0 wptr=0 me=0 vbif=0 mask=0 '
                        f'ackme=0 ackvb=0 status={status} status3=0 endr=0 endw=0 wait=100000 sample=5000')
                text = '#slot 0 t=0s tag=cheeseburger-P21\nGPSVC bid=aa\n' + line + '\n'
                report = replay(model_window(slot_texts=[text], msg='', ticks=True))
                drain = report['diagnostics'][0]
                self.assertEqual(drain['stage_name'], 'preidle')
                self.assertEqual(drain['reason_name'], name)
                self.assertEqual(drain['valid_fields'], ['status', 'status3'])
                self.assertEqual(drain['invalid_mmio_fields'], invalid)
                self.assertNotIn('me', drain['effective_fields'])
                self.assertNotIn('ackme', drain['effective_fields'])
                self.assertEqual(drain['sample_writer_seconds'], 0)
                self.assertIn(f'Stage=preidle; reason={name}', analyzer.markdown(report))

    def test_legacy_core_is_untrusted_even_rc_zero(self):
        text = '#slot 0 t=0s tag=cheeseburger-P21\nCORE rbccu=1>0 sp=1>0 rc=0 fence=0/0 bid=aa\n'
        report = replay(model_window(slot_texts=[text]))
        self.assertTrue(report['legacy_core_untrusted'])
        self.assertFalse(report['diagnostics'][0]['trusted'])
        self.assertIn('UNTRUSTED', analyzer.markdown(report))
        self.assertEqual(report['first_core_failure']['rc'], -4)

    def test_zero_flags_cannot_fabricate_sample_or_failure(self):
        text = ('#slot 0 t=0s tag=cheeseburger-P21\nCORE2 valid=0 rc=-4 rbpre=0 sppre=0 rbpost=0 sppost=0 '
                'fence_rc=0 fence_stage=0 sample=0 bid=aa\n')
        report = replay(model_window(slot_texts=[text], msg=''))
        self.assertIsNone(report['first_core_failure'])
        self.assertIsNone(report['diagnostics'][0]['sample_writer_seconds'])
        self.assertEqual(report['diagnostics'][0]['effective_fields'], {})

    def test_bad_header_is_never_effective(self):
        raw = model_window()
        bad_magic = b'XXXXXXXX' + raw[8:]
        zero_boot = bytearray(raw)
        struct.pack_into('<Q', zero_boot, 16, 0)
        bad_geometry = bytearray(raw)
        struct.pack_into('<I', bad_geometry, 52, 999999)
        for bad in (raw[:50], raw[:-1], bad_magic, zero_boot, bad_geometry):
            report = replay(bad)
            self.assertFalse(report['effective'])
            self.assertEqual(report['heartbeat']['records'], [])

    def test_foreign_mixed_missing_stale_snapshots_rejected(self):
        for second in ('#slot 1 t=10s tag=cheeseburger-P21\nGPSVC bid=bb\n',
                       '#slot 1 t=10s tag=cheeseburger-P21\nGPSVC bid=aa\nCORE bid=bb\n',
                       '#slot 1 t=10s tag=cheeseburger-P21\nSWAP check=999 reject=0 why=0 transfer=999 rc=0\n',
                       '#slot 0 t=10s tag=cheeseburger-P21\nGPSVC bid=aa\n'):
            first = '#slot 0 t=0s tag=cheeseburger-P21\nGPSVC bid=aa\n'
            report = replay(model_window(slot_texts=[first, second]))
            self.assertEqual(len(report['snapshots']), 1)
            self.assertEqual(len(report['rejected_snapshots']), 1)
            self.assertNotEqual(report['summary']['max_transfers'], 999)

    def test_foreign_msgbuf_and_retrieval_never_merged(self):
        report = replay(model_window(msg='[ 50.000000]: CB persistent log: first commit synced, boot=bb\n'
                                   '[ 60.000000]: CB GPU core: resume rc=-4\n'),
                        retrieval={'boot_id': '0xbb', 'sha256': 'wrong', 'heartbeat': {'records': [{'boot_id': '0xbb'}]}})
        self.assertFalse(report['msgbuf_status']['accepted'])
        self.assertEqual(report['msgbuf_lines'], [])
        self.assertIsNone(report['first_core_failure'])
        self.assertEqual(report['clock']['method'], 'unavailable')
        self.assertEqual(len(report['source_checks']), 2)
        self.assertTrue(all(int(record['boot'], 16) == 0xAA for record in report['heartbeat']['records']))

    def test_conflicting_and_malformed_clock_fail_closed(self):
        texts = ['#slot 0 t=0s tag=cheeseburger-P21\nLOGTIME start=5000 freq=100 sample=5000 sec=0\nGPSVC bid=aa\n',
                 '#slot 1 t=10s tag=cheeseburger-P21\nLOGTIME start=6000 freq=100 sample=7000 sec=10\nGPSVC bid=aa\n']
        report = replay(model_window(slot_texts=texts))
        self.assertEqual(report['clock']['reason'], 'conflicting_current_boot_clock_origins')
        self.assertIsNone(report['first_core_failure']['writer_seconds'])
        bad = texts[0].replace('freq=100', 'freq=0')
        report = replay(model_window(slot_texts=[bad], msg=''))
        self.assertEqual(report['clock']['method'], 'unavailable')
        self.assertEqual(len(report['clock']['rejected']), 1)

    def test_modulo_snapshot_wrap_retained_only(self):
        texts = [f'#slot {n} t={n * 10}s tag=cheeseburger-P21\nGPSVC bid=aa\n' for n in range(7)]
        report = replay(model_window(slot_texts=texts))
        self.assertEqual([record['slot'] for record in report['snapshots']], [4, 5, 6])

    def test_save_only_additional_files_never_overwrites_originals(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path / 'window.bin').write_bytes(model_window())
            (path / 'DIAGNOSTIC_OVERVIEW.md').write_text('original overview')
            decoded = path / 'decoded/cheeseburger-P21-aa'
            decoded.mkdir(parents=True)
            (decoded / 'slots.txt').write_text('foreign/unvalidated original text')
            (decoded / 'msgbuf.txt').write_text('original msgbuf')
            before = {file: file.read_bytes() for file in path.rglob('*') if file.is_file()}
            report = analyzer.analyze(path)
            self.assertTrue(report['source_checks'])
            analyzer.save_report(path, report)
            self.assertTrue((path / 'EFFECTIVE_EVIDENCE.md').exists())
            self.assertTrue((path / 'EFFECTIVE_EVIDENCE.json').exists())
            for file, raw in before.items():
                self.assertEqual(file.read_bytes(), raw)
            with self.assertRaises(FileExistsError):
                analyzer.save_report(path, report)
            self.assertEqual(set(path.iterdir()) - {file for file in before if file.parent == path} - {path / 'decoded'},
                             {path / 'EFFECTIVE_EVIDENCE.md', path / 'EFFECTIVE_EVIDENCE.json'})

    def test_heartbeat_core_fallback_requires_result_valid(self):
        raw = bytearray(model_window(msg=''))
        header = analyzer.window_header(raw)
        first = header['slot_first'] + header['slot_count'] * header['slot_blocks']
        at = (first + 2) * hb.BLOCK
        for flags, rc in (('1', -32768), ('5', -4)):
            line = f'CBHB0001 boot=00000000000000aa seq=2 sec=10 tag=tick state=2 rounds=2 g={rc}/2/5/{flags} ticks=6100 start=5000 freq=100 he=2\n'.encode()
            raw[at:at + hb.BLOCK] = line + bytes(hb.BLOCK - len(line))
            report = replay(raw)
            self.assertEqual(report['clock']['method'], 'heartbeat_ticks')
            if flags == '1':
                self.assertIsNone(report['first_core_failure'])
            else:
                self.assertEqual(report['first_core_failure']['rc'], -4)
                self.assertEqual(report['first_core_failure']['writer_seconds'], 11)
                self.assertIn('observation', report['first_core_failure']['limit'])
            self.assertEqual(report['heartbeat']['records'][-1]['extensions']['he'], '2')

    def test_core_fence_failure_without_msgbuf(self):
        text = ('#slot 0 t=0s tag=cheeseburger-P21\nCORE2 valid=f rc=1 rbpre=1 sppre=1 rbpost=100001 sppost=100001 '
                'fence_rc=-6 fence_stage=4 sample=5000 bid=aa\n')
        report = replay(model_window(slot_texts=[text], msg='', ticks=True))
        self.assertEqual(report['first_core_failure']['rc'], -6)
        self.assertEqual(report['first_core_failure']['rc_field'], 'fence_rc')

    def test_signed_diagnostic_width_is_checked(self):
        text = ('#slot 0 t=0s tag=cheeseburger-P21\nCORE2 valid=4 rc=4294967292 rbpre=0 sppre=0 rbpost=0 sppost=0 '
                'fence_rc=0 fence_stage=0 sample=0 bid=aa\n')
        report = replay(model_window(slot_texts=[text], msg=''))
        self.assertFalse(report['diagnostics'][0]['trusted'])
        self.assertIsNone(report['first_core_failure'])

    def test_reader_import_does_not_access_device(self):
        spec = importlib.util.spec_from_file_location('read_run', ROOT / 'scripts/logging/read-cheeseburger-log.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertNotIn('device', module.__dict__)
        self.assertTrue(callable(module.main))

    def test_cli_check_is_read_only(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path / 'window.bin').write_bytes(model_window())
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/logging/analyze-cheeseburger-run.py'),
                                     '--check', str(path)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(json.loads(result.stdout)['effective'])
            self.assertEqual(list(path.iterdir()), [path / 'window.bin'])

    def test_wagtail_read_only_regression(self):
        # Explicit single known run; no scanning out/, no writes to real evidence.
        run = ROOT / 'out/p21/persistent-logs/20261105T144853Z'
        if not (run / 'window.bin').is_file():
            self.skipTest('private Wagtail window not present')
        raw_hash = __import__('hashlib').sha256((run / 'window.bin').read_bytes()).hexdigest()
        report = analyzer.analyze(run)
        self.assertEqual(report['raw_window_sha256'], raw_hash)
        self.assertEqual(report['boot_id'], '0x3de7e608')
        self.assertEqual(len(report['snapshots']), 35)
        self.assertEqual(report['first_core_failure']['rc'], -4)
        self.assertIn('resume rc=-4 rb=1/0 sp=1/0', report['first_core_failure']['line'])
        self.assertEqual(report['first_gpu_failure']['rc'], -7)
        self.assertTrue(report['legacy_core_untrusted'])
        self.assertEqual(report['heartbeat']['gaps']['steps'], 0)
        # Block 8161 contains a non-CBHB UP1GPU2 diagnostic, not empty or valid HB.
        self.assertEqual(report['heartbeat']['invalid'], 1)
        self.assertEqual(report['heartbeat']['foreign'], 7)
        self.assertEqual(report['summary']['max_transfers'], 7)


if __name__ == '__main__':
    unittest.main()
