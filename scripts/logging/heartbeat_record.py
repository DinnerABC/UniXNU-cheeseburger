"""Codec and bounded collection for the existing CBHB0001 heartbeat rings.

The channel avoids console/msgbuf but still shares UFS; a missing record is not
proof that the kernel stopped. No on-device format or physical layout changes.
"""
import re

MAGIC = b'CBHB0001'
BLOCK = 4096
# BootLog.c: independent sequence counters, NOT independent counters per tag.
SUBRINGS = {'tick': (0, 22), 'steps': (22, 6), 'autorestart': (28, 3)}
_REQUIRED = ('boot', 'seq', 'sec', 'tag', 'state', 'rounds')
_UINT = re.compile(r'^[0-9]+$')
_SIGNED = re.compile(r'^-?[0-9]+$')
_HEX = re.compile(r'^(?:0x)?[0-9a-fA-F]+$')


def key_values(text):
    """Strict key=value tokens; unknown extensions are preserved verbatim."""
    values = {}
    for token in text.split():
        key, sep, value = token.partition('=')
        if not sep or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', key) or not value or key in values:
            raise ValueError('invalid or duplicate key=value token')
        values[key] = value
    return values


def _number(value, signed=False, hexadecimal=False, bits=64):
    pattern = _HEX if hexadecimal else _SIGNED if signed else _UINT
    if not pattern.fullmatch(value):
        raise ValueError('invalid number')
    number = int(value, 16 if hexadecimal else 10)
    low, high = (-(1 << (bits - 1)), (1 << (bits - 1)) - 1) if signed else (0, (1 << bits) - 1)
    if not low <= number <= high:
        raise ValueError('number outside writer field width')
    return number


def parse(block):
    """Return validated fields, or None; malformed known extensions fail closed."""
    if not isinstance(block, (bytes, bytearray)) or len(block) < 64 or len(block) > BLOCK:
        return None
    raw = bytes(block)
    line = raw.split(b'\n', 1)[0].rstrip(b'\0')
    if any(raw[len(line):].strip(b'\n\0')):
        return None
    try:
        text = line.decode('ascii')
        if not text.startswith(MAGIC.decode() + ' '):
            return None
        kv = key_values(text[len(MAGIC) + 1:])
        if any(key not in kv for key in _REQUIRED) or not re.fullmatch(r'[0-9a-fA-F]{16}', kv['boot']):
            return None
        if not re.fullmatch(r'[a-z0-9_-]{1,32}', kv['tag']):
            return None
        boot = kv['boot'].lower()
        if not int(boot, 16):
            return None
        result = {'magic': MAGIC.decode(), 'boot': boot, 'boot_id': '0x' + boot,
                  'tag': kv['tag'], 'line': text,
                  'extensions': {key: value for key, value in kv.items() if key not in _REQUIRED}}
        for key in ('seq', 'sec', 'state', 'rounds'):
            result[key] = _number(kv[key], bits=32 if key in ('state', 'rounds') else 64)
        if result['state'] not in (1, 2, 3, 4):
            return None
        for key, name, signed, hexadecimal in (
                ('m', 'markers', False, False), ('l', 'last', False, True),
                ('o', 'opens', False, False), ('s', 'open_stage', True, False),
                ('a', 'abi_opens', False, False), ('ticks', 'ticks', False, False),
                ('start', 'start', False, False), ('freq', 'freq', False, False)):
            if key in kv:
                result[name] = _number(kv[key], signed=signed, hexadecimal=hexadecimal)
        if 'f' in kv:
            names = ('submit_rc', 'submit_stage', 'submit_irq', 'submit_cpirq', 'submit_seq',
                     'submit_preirq', 'submit_precpirq', 'submit_fenced')
            values = kv['f'].split('/')
            if len(values) != len(names):
                return None
            for index, (name, value) in enumerate(zip(names, values)):
                result[name] = _number(value, signed=index == 0, hexadecimal=index in (2, 3, 5, 6), bits=32)
        if 'g' in kv:
            values = kv['g'].split('/')
            if len(values) != 4:
                return None
            for index, (name, value) in enumerate(zip(('gpu_rc', 'gpu_stage', 'gpu_reason', 'gpu_valid'), values)):
                result[name] = _number(value, signed=index == 0, hexadecimal=index == 3, bits=32)
        if 'freq' in result and result['freq'] == 0:
            return None
        return result
    except (ValueError, UnicodeDecodeError):
        return None


def encode(boot_id, seq, sec, tag, state, rounds, size=BLOCK):
    """Build the unchanged basic writer format for host models."""
    line = ('CBHB0001 boot=%016x seq=%d sec=%d tag=%s state=%d rounds=%d\n'
            % (boot_id, seq, sec, tag, state, rounds)).encode('ascii')
    if size < len(line):
        raise ValueError('record does not fit block')
    return line + b'\0' * (size - len(line))


def subring(tag):
    return 'tick' if tag == 'tick' else 'autorestart' if tag in ('mark', 'smc') else 'steps'


def collect(window, boot_id, free_first, free_count):
    """Only valid current-header-boot records; losses are per producer subring.

    Missing counts cover retained interior sequence gaps only. Prefixes overwritten
    by modulo rings and writes after the last observation cannot be reconstructed.
    Legacy (<31 free blocks) writers share the final block; no isolation is assumed.
    """
    if free_first < 0 or free_count < 0 or free_first + free_count > (len(window) + BLOCK - 1) // BLOCK:
        raise ValueError('heartbeat region outside window')
    records, rejected = [], []
    counts = {'foreign': 0, 'empty': 0, 'invalid': 0}
    isolated = free_count >= 31
    for index in range(free_count):
        slot = free_first + index
        raw = window[slot * BLOCK:(slot + 1) * BLOCK]
        if len(raw) == BLOCK and not any(raw):
            counts['empty'] += 1
            continue
        record = parse(raw) if len(raw) == BLOCK else None
        reason = None
        if record is None:
            counts['invalid'] += 1
            reason = 'invalid_or_truncated_record'
        elif int(record['boot'], 16) != boot_id:
            counts['foreign'] += 1
            reason = 'foreign_boot'
        else:
            group = subring(record['tag'])
            base, capacity = SUBRINGS[group]
            if isolated and index != base + record['seq'] % capacity:
                counts['invalid'] += 1
                reason = 'subring_or_modulo_slot_mismatch'
            elif not isolated and index != free_count - 1:
                counts['invalid'] += 1
                reason = 'legacy_record_outside_final_block'
            else:
                record.update(subring=group, window_block=slot)
                records.append(record)
        if reason:
            rejected.append({'window_block': slot, 'reason': reason,
                             'line': record['line'] if record else bytes(raw).split(b'\n', 1)[0].rstrip(b'\0').decode('ascii', 'replace')[:512]})
    records.sort(key=lambda record: (record['sec'], record['subring'], record['seq']))
    stats = {}
    for group, (_, capacity) in SUBRINGS.items():
        kept = sorted((r for r in records if r['subring'] == group), key=lambda r: (r['sec'], r['seq']))
        sequences = [r['seq'] for r in kept]
        regression = any(b < a and rb['sec'] > ra['sec'] for a, b, ra, rb in
                         zip(sequences, sequences[1:], kept, kept[1:]))
        ordered = sequences if regression else sorted(set(sequences))
        intervals = [{'after_seq': a, 'before_seq': b, 'missing_records': b - a - 1}
                     for a, b in zip(ordered, ordered[1:]) if b > a + 1]
        stats[group] = {'tags': sorted({r['tag'] for r in kept}), 'retained': len(kept),
                        'capacity': capacity if isolated else 1, 'first_seq': sequences[0] if kept else None,
                        'last_seq': sequences[-1] if kept else None,
                        'missing_records': sum(gap['missing_records'] for gap in intervals),
                        'gap_events': len(intervals), 'gaps': intervals,
                        'duplicate_sequences': len(sequences) - len(set(sequences)),
                        'sequence_wrap_or_reset_suspected': regression,
                        'unobservable_prefix_records': min(sequences) - 1 if sequences and not regression else None,
                        'ring_overwrite_possible': bool(sequences and max(sequences) > (capacity if isolated else 1))}
    return {'records': records, **counts, 'rejected': rejected, 'subrings': stats,
            # Compatibility name, now explicitly producer/subring keyed, not tag keyed.
            'gaps': {name: stat['gap_events'] for name, stat in stats.items()},
            'missing_records': {name: stat['missing_records'] for name, stat in stats.items()},
            'free_first': free_first, 'free_count': free_count,
            'layout': '22 tick / 6 steps / 3 autorestart' if isolated else 'legacy shared final block',
            'limits': ['Missing records count retained interior sequence holes, not gap events.',
                       'All step tags share one sequence; mark/smc share another.',
                       'Modulo overwrite hides earlier records; prefix and unobserved tail are not write-loss totals.',
                       'uint64 sequence wrap/reset cannot yield an exact cross-boundary loss count.',
                       'sec is writer-relative integer seconds; UFS is shared, and last persistence is a lower bound.']}
