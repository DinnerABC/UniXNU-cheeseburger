#!/usr/bin/env python3
"""Replay the unchanged 22/6/3 writer rings; no device and no external modules."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts/logging'))
import heartbeat_record as hb


class HeartbeatCodecTests(unittest.TestCase):
    def write(self, window, boot, seq, sec, tag, extensions=''):
        base, count = hb.SUBRINGS[hb.subring(tag)]
        at = (base + seq % count) * hb.BLOCK
        record = hb.encode(boot, seq, sec, tag, 2, 8)
        if extensions:
            line = record.split(b'\n', 1)[0] + b' ' + extensions.encode() + b'\n'
            record = line + bytes(hb.BLOCK - len(line))
        window[at:at + hb.BLOCK] = record

    def test_roundtrip_and_digit_tag(self):
        record = hb.parse(hb.encode(0x510EA570, 7, 123, 'cm3', 2, 8))
        self.assertEqual(record['boot_id'], '0x00000000510ea570')
        self.assertEqual((record['seq'], record['tag']), (7, 'cm3'))
        self.assertEqual(len(hb.encode(1, 1, 1, 'smc', 2, 4)), hb.BLOCK)

    def test_invalid_records(self):
        for block in (b'', b'XXXX' + bytes(100), hb.encode(1, 2, 3, 'tick', 2, 4)[:32],
                      hb.encode(0, 2, 3, 'tick', 2, 4), hb.encode(1, -2, 3, 'tick', 2, 4),
                      hb.encode(1, 2, 3, 'tick', 0, 4)):
            self.assertIsNone(hb.parse(block))
        with self.assertRaises(ValueError):
            hb.encode(1, 1, 1, 'tick', 2, 4, size=10)

    def test_extensions_unknown_and_reordered(self):
        window = bytearray(31 * hb.BLOCK)
        self.write(window, 0xAA, 9, 40, 'tick',
                   'newkey=foo/bar freq=100 start=1000 ticks=5000 g=-4/2/5/7ff '
                   'f=-3/3/7e/0/2/200/0/1 a=1 s=7 o=1 l=3 m=1')
        record = hb.collect(window, 0xAA, 0, 31)['records'][0]
        self.assertEqual(record['submit_rc'], -3)
        self.assertEqual(record['submit_irq'], 0x7e)
        self.assertEqual(record['submit_preirq'], 0x200)
        self.assertEqual(record['gpu_rc'], -4)
        self.assertEqual(record['gpu_valid'], 0x7ff)
        self.assertEqual(record['extensions']['newkey'], 'foo/bar')
        self.assertEqual(record['ticks'], 5000)

    def test_bad_extensions_fail_closed(self):
        line = hb.encode(1, 1, 1, 'tick', 2, 4).split(b'\n')[0]
        for extension in (b'seq=2', b'unknown', b'freq=0', b'f=-3/3/7e', b'g=bad/1/2/0',
                          b'ticks=-1', b'o=abc', b'o=1 o=2'):
            raw = line + b' ' + extension + b'\n'
            self.assertIsNone(hb.parse(raw + bytes(hb.BLOCK - len(raw))), extension)

    def test_steps_are_one_sequence_not_per_tag(self):
        window = bytearray(31 * hb.BLOCK)
        for seq, tag in enumerate(('reset', 'zap', 'reset', 'zapdone', 'submit', 'fencedone'), 1):
            self.write(window, 0xAA, seq, 40, tag)
        records = hb.collect(window, 0xAA, 0, 31)
        self.assertEqual(records['gaps']['steps'], 0)
        self.assertEqual(records['missing_records']['steps'], 0)
        self.assertEqual(records['subrings']['steps']['retained'], 6)
        self.assertNotIn('reset', records['gaps'])

    def test_reset_and_tick_have_independent_sequences(self):
        window = bytearray(31 * hb.BLOCK)
        self.write(window, 0xAA, 1, 10, 'tick')
        self.write(window, 0xAA, 2, 20, 'tick')
        self.write(window, 0xAA, 1, 15, 'mark')
        self.write(window, 0xAA, 2, 16, 'smc')
        result = hb.collect(window, 0xAA, 0, 31)
        self.assertEqual(result['gaps'], {'tick': 0, 'steps': 0, 'autorestart': 0})
        self.assertEqual(result['subrings']['autorestart']['tags'], ['mark', 'smc'])

    def test_missing_records_distinct_from_gap_events(self):
        window = bytearray(31 * hb.BLOCK)
        self.write(window, 0xAA, 1, 10, 'tick')
        self.write(window, 0xAA, 4, 40, 'tick')
        self.write(window, 0xAA, 6, 60, 'tick')
        result = hb.collect(window, 0xAA, 0, 31)
        self.assertEqual(result['gaps']['tick'], 2)
        self.assertEqual(result['missing_records']['tick'], 3)

    def test_foreign_invalid_empty_and_slot_mismatch(self):
        window = bytearray(31 * hb.BLOCK)
        self.write(window, 0xAA, 1, 10, 'tick')
        self.write(window, 0xBB, 2, 20, 'tick')
        window[3 * hb.BLOCK:4 * hb.BLOCK] = b'CBHB bad\n' + bytes(hb.BLOCK - 9)
        window[4 * hb.BLOCK:5 * hb.BLOCK] = hb.encode(0xAA, 3, 30, 'tick', 2, 4)
        result = hb.collect(window, 0xAA, 0, 31)
        self.assertEqual((len(result['records']), result['foreign'], result['invalid'], result['empty']), (1, 1, 2, 27))

    def test_modulo_wrap_is_overwrite_not_gap(self):
        window = bytearray(31 * hb.BLOCK)
        for seq in range(1, 45):
            self.write(window, 0xAA, seq, seq * 10, 'tick')
        for seq in range(1, 16):
            self.write(window, 0xAA, seq, seq * 3, ('submit', 'fencedone', 'reset')[seq % 3])
        result = hb.collect(window, 0xAA, 0, 31)
        self.assertEqual(result['gaps']['tick'], 0)
        self.assertEqual(result['gaps']['steps'], 0)
        self.assertEqual(result['subrings']['tick']['unobservable_prefix_records'], 22)
        self.assertTrue(result['subrings']['steps']['ring_overwrite_possible'])
        self.assertFalse(result['subrings']['steps']['sequence_wrap_or_reset_suspected'])

    def test_uint64_sequence_wrap_is_explicit(self):
        window = bytearray(31 * hb.BLOCK)
        self.write(window, 0xAA, (1 << 64) - 1, 10, 'tick')
        self.write(window, 0xAA, 0, 20, 'tick')
        result = hb.collect(window, 0xAA, 0, 31)
        self.assertTrue(result['subrings']['tick']['sequence_wrap_or_reset_suspected'])
        self.assertIsNone(result['subrings']['tick']['unobservable_prefix_records'])

    def test_legacy_shared_final_block(self):
        window = bytearray(4 * hb.BLOCK)
        window[3 * hb.BLOCK:] = hb.encode(0xAA, 2, 10, 'smc', 2, 4)
        result = hb.collect(window, 0xAA, 0, 4)
        self.assertEqual(result['layout'], 'legacy shared final block')
        self.assertEqual(len(result['records']), 1)
        self.assertEqual(result['subrings']['autorestart']['capacity'], 1)

    def test_truncated_block_and_bounds(self):
        result = hb.collect(hb.encode(0xAA, 1, 10, 'tick', 2, 4)[:80], 0xAA, 0, 1)
        self.assertEqual(result['invalid'], 1)
        self.assertFalse(result['records'])
        with self.assertRaises(ValueError):
            hb.collect(bytes(hb.BLOCK), 0xAA, 0, 2)


if __name__ == '__main__':
    unittest.main()
