#!/usr/bin/env python3
"""Bounded offline replay of one committed window; never repairs original evidence.

Writes only additional EFFECTIVE_EVIDENCE.md/json (exclusive creation). Use
--check for a read-only replay. Original sheng decode stays untouched; raw window
header, slots and heartbeat are independently validated before use.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

import heartbeat_record as hb

HEADER = struct.Struct('<8sIIQQ8I6I32s')
FIELDS = ('magic', 'version', 'header_bytes', 'boot_id', 'first_lba', 'window_blocks',
          'msg_first', 'msg_blocks', 'msg_bytes', 'slot_first', 'slot_blocks',
          'slot_count', 'slots_written', 'uptime_s', 'rounds', 'write_errors',
          'gpt_open', 'gpt_last', 'state', 'tag')
SLOT = re.compile(r'^#slot (\d+) t=(\d+)s tag=cheeseburger-P21$')
# Snapshot identity is bid=. Some unrelated fields (GADDON3 boot=1) are
# Boolean/counters, NOT a boot ID. Msgbuf first-commit identity is separate.
IDS = re.compile(r'\bbid=((?:0x)?[0-9a-fA-F]+)\b')
MSG_TIME = re.compile(r'^\[\s*(\d+\.\d+)\]:\s*')
FIRST_COMMIT = re.compile(r'CB persistent log:\s*first[^\n]*\bboot=((?:0x)?[0-9a-fA-F]+)\b')
CORE_FIELDS = {'valid': 16, 'rc': 10, 'rbpre': 16, 'sppre': 16,
               'rbpost': 16, 'sppost': 16, 'fence_rc': 10, 'fence_stage': 10, 'sample': 10}
DRAIN_REGISTERS = ('rptr', 'wptr', 'me', 'vbif', 'mask', 'ackme', 'ackvb',
                   'status', 'status3', 'endr', 'endw')
DRAIN_FIELDS = {**{name: 16 for name in DRAIN_REGISTERS}, 'stage': 10, 'reason': 10,
                'valid': 16, 'wait': 10, 'sample': 10}
STAGES = {0: 'none', 1: 'initial', 2: 'ack', 3: 'final', 4: 'drained', 5: 'resuming', 6: 'resumed',
          7: 'preidle'}
REASONS = dict(enumerate(('none', 'initialInvalid', 'ringBusy', 'controlMismatch', 'ackInvalid',
                          'ackTimeout', 'finalInvalid', 'cursorChanged', 'notIdle', 'Status3Busy',
                          'resumeInvalid', 'resumeControl', 'resumeCursor', 'PreIdleInvalid', 'PreIdleTimeout')))
LIMITS = [
    'Effective means valid for this committed header boot, not proven to be the latest device experiment.',
    'No candidate/image identity is inferred from a log boot ID; compare boot receipt and window hashes separately.',
    'Snapshots are bounded/live/non-atomic; stored repeated diagnostic samples are not fresh MMIO reads.',
    'The last persisted time is a lower bound, never the kernel stop time; console/msgbuf and heartbeat still share UFS.',
    'Ring overwrite, early failures and unobserved tail writes cannot be recovered; absence does not prove nonexecution.',
    'Legacy CORE rc/post/fence are untrusted (zero initialization/packed fields); use original msgbuf or valid CORE2.',
    'DRAIN valid means sampled; ffffffff is invalid MMIO, not a normal measured register value.',
]


def window_header(raw):
    """Validate the local version-3, no-export layout, without trusting retrieval.json."""
    if len(raw) < hb.BLOCK:
        raise ValueError('truncated header block')
    header = dict(zip(FIELDS, HEADER.unpack_from(raw)))
    if header['magic'] != b'SHLOG001' or header['version'] != 3:
        raise ValueError('no supported committed SHLOG001 version-3 header')
    if not HEADER.size + 20 <= header['header_bytes'] <= hb.BLOCK:
        raise ValueError('invalid header_bytes')
    if not header['boot_id'] or header['tag'].split(b'\0')[0] != b'cheeseburger-P21':
        raise ValueError('invalid board tag or zero boot ID')
    if header['state'] not in (1, 2, 3, 4):
        raise ValueError('invalid writer state')
    if len(raw) != header['window_blocks'] * hb.BLOCK or len(raw) > 32 * 1024 * 1024:
        raise ValueError('truncated/oversized committed window')
    if (header['msg_first'], header['msg_blocks'], header['slot_first'], header['slot_blocks']) not in (
            (1, 32, 33, 8), (1, 256, 257, 32)):
        raise ValueError('unsupported board msg/slot layout')
    if header['msg_bytes'] > header['msg_blocks'] * hb.BLOCK:
        raise ValueError('message length outside region')
    end = header['slot_first'] + header['slot_count'] * header['slot_blocks']
    if not 0 < header['slot_count'] or end > header['window_blocks']:
        raise ValueError('snapshot region outside window')
    if struct.unpack_from('<5I', raw, HEADER.size) != (0, 0, 0, 0, 0):
        raise ValueError('unsupported export region')
    header['magic'] = header['magic'].decode()
    header['tag'] = header['tag'].split(b'\0')[0].decode()
    return header


def snapshots(raw, header):
    """Physical ring order from header; reject stale, malformed or foreign slots."""
    records, rejected = [], []
    written, count = header['slots_written'], header['slot_count']
    for number in range(max(0, written - count), written):
        block = header['slot_first'] + (number % count) * header['slot_blocks']
        data = raw[block * hb.BLOCK:(block + header['slot_blocks']) * hb.BLOCK]
        text = data.split(b'\0', 1)[0].decode('utf-8', 'replace')
        lines = text.splitlines()
        match = SLOT.fullmatch(lines[0]) if lines else None
        identities = [int(value, 16) for value in IDS.findall(text)]
        reason = None
        if not match or int(match[1]) != number:
            reason = 'invalid_or_stale_slot_header'
        elif int(match[2]) > header['uptime_s']:
            reason = 'snapshot_time_exceeds_committed_header'
        elif not identities:
            reason = 'snapshot_boot_identity_missing'
        elif any(identity != header['boot_id'] for identity in identities):
            reason = 'snapshot_foreign_or_mixed_boot'
        if reason:
            rejected.append({'slot': number, 'window_block': block, 'reason': reason,
                             'first_line': lines[0] if lines else ''})
        else:
            records.append({'slot': number, 'window_block': block, 'sec': int(match[2]),
                            'boot_id': hex(header['boot_id']), 'text': text})
    return records, rejected


def msgbuf(raw, header):
    first = header['msg_first'] * hb.BLOCK
    text = raw[first:first + header['msg_bytes']].decode('utf-8', 'replace')
    commits = list(FIRST_COMMIT.finditer(text))
    if any(int(commit[1], 16) != header['boot_id'] for commit in commits):
        return '', {'accepted': False, 'reason': 'foreign_or_mixed_first_commit_boot'}
    return text, {'accepted': True, 'identity': 'committed_header_region',
                  'matching_first_commit': bool(commits),
                  'limit': 'Individual msgbuf lines lack boot IDs; scoped by validated committed raw header.'}


def clock_model(records, heartbeats, msg, boot_id):
    candidates, rejected = [], []
    for record in records:
        for line_number, line in enumerate(record['text'].splitlines(), 1):
            if not line.startswith('LOGTIME '):
                continue
            try:
                values = hb.key_values(line[8:])
                start, freq, sample, sec = (hb._number(values[key]) for key in ('start', 'freq', 'sample', 'sec'))
                if not start or not freq or sample < start or sec != record['sec'] or (sample - start) // freq < sec:
                    raise ValueError('invalid LOGTIME origin/sample')
                candidates.append({'method': 'LOGTIME', 'start_ticks': start, 'freq_hz': freq,
                                   'sample_ticks': sample, 'writer_sec': sec,
                                   'source': f"window.bin slot {record['slot']} line {line_number}", 'line': line,
                                   'extensions': {key: value for key, value in values.items()
                                                  if key not in ('start', 'freq', 'sample', 'sec')}})
            except (ValueError, KeyError):
                rejected.append({'line': line, 'reason': 'invalid_LOGTIME'})
    for record in heartbeats:
        if all(key in record for key in ('ticks', 'start', 'freq')):
            # sec was sampled before snprintf/diagnostic collection, ticks after it;
            # crossing an integer-second boundary is legitimate, not corruption.
            if record['start'] and record['ticks'] >= record['start'] and (record['ticks'] - record['start']) // record['freq'] >= record['sec']:
                candidates.append({'method': 'heartbeat_ticks', 'start_ticks': record['start'],
                                   'freq_hz': record['freq'], 'sample_ticks': record['ticks'],
                                   'writer_sec': record['sec'], 'source': f"window.bin block {record['window_block']}"})
            else:
                rejected.append({'line': record['line'], 'reason': 'inconsistent_heartbeat_clock'})
    origins = {(candidate['start_ticks'], candidate['freq_hz']) for candidate in candidates}
    if len(origins) > 1:
        return {'method': 'unavailable', 'reason': 'conflicting_current_boot_clock_origins',
                'candidates': candidates, 'rejected': rejected, 'origin_seconds': None}
    if origins:
        start, freq = next(iter(origins))
        return {'method': candidates[0]['method'], 'origin_seconds': start / freq,
                'start_ticks': start, 'freq_hz': freq, 'tick_source': 'CNTPCT_EL0',
                'precision_seconds': 1 / freq, 'estimated': False,
                'sources': candidates, 'rejected': rejected,
                'limit': 'Origin is writer open/start, not power-on. Tick samples are exact; integer sec fields are floor-rounded.'}
    for line_number, line in enumerate(msg.splitlines(), 1):
        timestamp, commit = MSG_TIME.match(line), FIRST_COMMIT.search(line)
        if timestamp and commit and int(commit[1], 16) == boot_id:
            return {'method': 'first_commit_estimate', 'origin_seconds': float(timestamp[1]),
                    'estimated': True, 'source': f'msgbuf.txt line {line_number}', 'line': line,
                    'rejected': rejected, 'error_bound_seconds': None,
                    'limit': 'First commit printf is AFTER first slot/msg/header writes and sync: an approximate upper bound on writer start. Normalized msg/event time is biased early by at least the first-round write/sync duration (unknown), plus printf/timestamp rounding. No finite error bound is proven; integer snapshot/heartbeat sec adds <1 s quantization.'}
    return {'method': 'unavailable', 'origin_seconds': None, 'reason': 'no_valid_LOGTIME_tick_clock_or_matching_first_commit',
            'rejected': rejected, 'limit': 'Do not invent CNTFRQ or use boot ID as writer origin.'}


def normalize_ticks(sample, clock):
    if sample is None or not clock.get('freq_hz'):
        return None
    return (sample - clock['start_ticks']) / clock['freq_hz']


def diagnostic(line, source, clock, snapshot_sec):
    kind, _, rest = line.partition(' ')
    item = {'kind': kind, 'source': source, 'line': line, 'snapshot_writer_sec': snapshot_sec}
    if kind == 'CORE':
        return {**item, 'trusted': False, 'reason': 'legacy_CORE_zero_or_packed_rc_and_unsampled_post_fence_untrusted'}
    fields = CORE_FIELDS if kind == 'CORE2' else DRAIN_FIELDS
    try:
        values = hb.key_values(rest)
        parsed = {key: hb._number(values[key], signed=key in ('rc', 'fence_rc'),
                                  hexadecimal=base == 16, bits=64 if key == 'sample' else 32)
                  for key, base in fields.items()}
    except (ValueError, KeyError):
        return {**item, 'trusted': False, 'reason': 'malformed_or_incomplete_diagnostic'}
    item.update(fields=parsed, extensions={key: value for key, value in values.items() if key not in fields},
                trusted=True, sample_writer_seconds=normalize_ticks(parsed['sample'], clock))
    valid = parsed['valid']
    if kind == 'CORE2':
        masks = {'rbpre': 1, 'sppre': 1, 'rbpost': 2, 'sppost': 2, 'rc': 4, 'fence_rc': 8, 'fence_stage': 8}
        item['valid_fields'] = [key for key, mask in masks.items() if valid & mask]
        item['effective_fields'] = {key: parsed[key] for key in item['valid_fields']}
        item['unsampled_fields'] = [key for key, mask in masks.items() if not valid & mask]
        item['sample_valid'] = bool(valid)
        item['unknown_valid_bits'] = valid & ~15
        item['invalid_mmio_fields'] = [key for key in ('rbpre', 'sppre', 'rbpost', 'sppost')
                                       if key in item['valid_fields'] and parsed[key] == 0xffffffff]
        if not valid:
            item['sample_writer_seconds'] = None
    else:
        item['stage_name'] = STAGES.get(parsed['stage'], 'unknown')
        item['reason_name'] = REASONS.get(parsed['reason'], 'unknown')
        item['valid_fields'] = [key for bit, key in enumerate(DRAIN_REGISTERS) if valid & (1 << bit)]
        item['effective_fields'] = {key: parsed[key] for key in ('stage', 'reason', 'wait') + tuple(item['valid_fields'])}
        item['unsampled_fields'] = [key for key in DRAIN_REGISTERS if key not in item['valid_fields']]
        item['invalid_mmio_fields'] = [key for key in item['valid_fields'] if parsed[key] == 0xffffffff]
        item['unknown_valid_bits'] = valid & ~0x7ff
        item['sample_valid'] = parsed['stage'] != 0
        if not item['sample_valid']:
            item['sample_writer_seconds'] = None
    return item


def gpu_failures(msg, records, clock, heartbeats=()):
    diagnostics = []
    for record in records:
        for number, line in enumerate(record['text'].splitlines(), 1):
            if re.match(r'^(CORE2|CORE|DRAIN) ', line):
                diagnostics.append(diagnostic(line, f"window.bin slot {record['slot']} line {number}", clock, record['sec']))
    events = []
    msglines = msg.splitlines()
    for number, line in enumerate(msglines, 1):
        if 'CB GPU' not in line:
            continue
        rc = re.search(r'\b(?:rc|exit)=(-\d+)\b', line)
        if not rc:
            continue
        timestamp = MSG_TIME.match(line)
        absolute = float(timestamp[1]) if timestamp else None
        relative = absolute - clock['origin_seconds'] if absolute is not None and clock['origin_seconds'] is not None else None
        events.append({'source': f'msgbuf.txt line {number}', 'line': line, 'rc': int(rc[1]),
                       'absolute_seconds': absolute, 'writer_seconds': relative,
                       'time_estimated': clock.get('estimated', False),
                       'core': bool(re.search(r'CB GPU (?:core|drain)\b', line)),
                       'context': [{'source': f'msgbuf.txt line {index + 1}', 'line': msglines[index]}
                                   for index in range(max(0, number - 3), min(len(msglines), number + 2))]})
    tick_failures = [record for record in heartbeats if record.get('gpu_valid', 0) & 4
                     and record.get('gpu_rc', 0) < 0]

    def first(core_only=False):
        eligible = [event for event in events if not core_only or event['core']]
        if eligible:
            chosen = eligible[0].copy()
            # Same boot, not necessarily same instant: do not fabricate sample correlation.
            chosen['diagnostics'] = diagnostics
            chosen['limit'] = 'First retained msgbuf failure, not necessarily first executed failure. CORE2/DRAIN records are same-boot contextual samples; inspect sample time, flags and repeated records.'
            return chosen
        eligible_diag = [item for item in diagnostics if item.get('trusted') and item['kind'] == 'CORE2'
                         and (('rc' in item['valid_fields'] and item['fields']['rc'] < 0)
                              or ('fence_rc' in item['valid_fields'] and item['fields']['fence_rc'] < 0))]
        if eligible_diag:
            chosen = eligible_diag[0]
            rc_field = 'rc' if 'rc' in chosen['valid_fields'] and chosen['fields']['rc'] < 0 else 'fence_rc'
            return {'source': chosen['source'], 'line': chosen['line'], 'rc': chosen['fields'][rc_field],
                    'rc_field': rc_field, 'writer_seconds': chosen['sample_writer_seconds'], 'diagnostics': diagnostics,
                    'limit': 'First retained valid CORE2 failure; original msgbuf failure was not captured.'}
        if tick_failures:
            chosen = tick_failures[0]
            return {'source': f"window.bin heartbeat block {chosen['window_block']}",
                    'line': chosen['line'], 'rc': chosen['gpu_rc'],
                    'writer_seconds': chosen.get('writer_seconds', chosen['sec']), 'diagnostics': diagnostics,
                    'limit': 'First retained heartbeat with CoreResultValid. Its tick time is an observation, not the original failure time; absent msgbuf/CORE2 does not prove absence of earlier failures.'}
        return None
    return {'first_gpu_failure': first(), 'first_core_failure': first(True), 'retained_msgbuf_failures': events,
            'heartbeat_core_failure_observations': tick_failures,
            'diagnostics': diagnostics, 'legacy_core_untrusted': any(item['kind'] == 'CORE' for item in diagnostics)}


def legacy_summary(records, msg):
    """Keep useful old UI counters, but only from validated effective snapshots."""
    text = '\n'.join(record['text'] for record in records)
    latest = records[-1]['text'] if records else ''
    swaps = re.findall(r'^SWAP check=(\d+) reject=(\d+) why=(\d+) transfer=(\d+) rc=(\S+)', text, re.M)
    matching = {}
    threads = {}
    observed = Counter()
    for line in text.splitlines():
        match = re.match(r'M(\d+) ', line)
        if match:
            matching[int(match[1])] = line
        match = re.match(r'STK ([SBC]) p(\d+) tid=(\d+) PC=([0-9a-f]+) LR=([0-9a-f]+) slide=([0-9a-f]+) e=(\d+)', line)
        if match:
            role, pid, tid, pc, lr, slide, error = match.groups()
            key = (role, pid, tid)
            threads[key] = dict(role=role, pid=int(pid), tid=int(tid), pc=pc, lr=lr, cache_slide=slide, read_error=int(error), frames=[])
            observed[(role, pc)] += 1
        match = re.match(r'FP(\d+) ([SBC]) (\d+) ((?:[0-9a-f]+ ?){4})$', line)
        if match:
            offset, role, tid, values = match.groups()
            for key in reversed(threads):
                if key[0] == role and key[2] == tid:
                    position = int(offset)
                    if position > 256:  # bounded live frame report, not arbitrary expansion
                        break
                    frames = threads[key]['frames']
                    frames.extend(['0'] * max(0, position + 4 - len(frames)))
                    frames[position:position + 4] = values.split()
                    break
    return {'snapshots_with_swap': len(swaps), 'max_swap_checks': max((int(s[0]) for s in swaps), default=0),
            'max_swap_rejects': max((int(s[1]) for s in swaps), default=0),
            'max_transfers': max((int(s[3]) for s in swaps), default=0),
            'latest_watchdog': re.findall(r'^WATCHDOG .*', latest, re.M),
            'latest_vsync': re.findall(r'^VS .*', latest, re.M),
            'latest_iokit_summary': re.findall(r'^IOKIT .*', latest, re.M),
            'retained_match_events': len(matching), 'iokit_matching': [matching[k] for k in sorted(matching)],
            'accelerator_match_events': [line for line in matching.values() if re.search(r'IOAccelerator|AGX|Metal', line)],
            'threads': list(threads.values()),
            'sampled_pc_counts': [{'role': role, 'pc': pc, 'samples': n} for (role, pc), n in observed.most_common(32)],
            'kernel_warnings': list(dict.fromkeys(line for line in msg.splitlines() if re.search(
                r'FIRST UI|panic\(|Library not loaded|Missing executable|rejecting class|No SEP', line)))[:80]}


def analyze(root):
    root = Path(root).resolve()
    path = root / 'window.bin'
    if path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError('window larger than authorized 32 MiB bound')
    raw = path.read_bytes()
    report = {'schema_version': 1, 'source': str(root), 'raw_window_sha256': hashlib.sha256(raw).hexdigest(),
              'limits': LIMITS.copy(), 'original_evidence_preserved': True}
    try:
        header = window_header(raw)
    except ValueError as error:
        return {**report, 'effective': False, 'reason': str(error), 'snapshots': [], 'heartbeat': {'records': []}}
    records, rejected = snapshots(raw, header)
    msg, msg_status = msgbuf(raw, header)
    free_first = header['slot_first'] + header['slot_blocks'] * header['slot_count']
    heartbeat = hb.collect(raw, header['boot_id'], free_first, header['window_blocks'] - free_first)
    clock = clock_model(records, heartbeat['records'], msg, header['boot_id'])
    for record in heartbeat['records']:
        record['writer_seconds'] = normalize_ticks(record.get('ticks'), clock) if record.get('ticks') is not None else record['sec']
    source_checks = []
    retrieval_path = root / 'retrieval.json'
    if retrieval_path.exists():
        retrieval = json.loads(retrieval_path.read_text())
        if retrieval.get('sha256') != report['raw_window_sha256']:
            source_checks.append('retrieval_window_hash_missing_or_mismatched; raw window is authoritative')
        if int(str(retrieval.get('boot_id', '0')), 16) != header['boot_id']:
            source_checks.append('retrieval_boot_missing_or_mismatched; never merge its records')
    # One deterministic expected directory, never next(glob()) or an unbounded scan.
    decoded = root / 'decoded' / f"cheeseburger-P21-{header['boot_id']:x}"
    for name, expected in (('msgbuf.txt', msg), ('slots.txt', '\n'.join(record['text'] for record in records))):
        source = decoded / name
        if source.exists() and source.stat().st_size <= 32 * 1024 * 1024:
            # Match the upstream decoder's bytes->text path. read_text() uses
            # universal newlines and would turn valid CRLF console-tail content
            # into LF, falsely flagging an otherwise identical raw slot join.
            # Do not strip whitespace: genuine slot-content changes must warn.
            actual = source.read_bytes().decode('utf-8', 'replace')
            if actual != expected:
                source_checks.append(f'{source}: differs from validated effective raw view; preserved, not trusted as replacement')
    msglines = []
    for number, line in enumerate(msg.splitlines(), 1):
        timestamp = MSG_TIME.match(line)
        absolute = float(timestamp[1]) if timestamp else None
        relative = absolute - clock['origin_seconds'] if absolute is not None and clock['origin_seconds'] is not None else None
        msglines.append({'source': f'msgbuf.txt line {number}', 'line': line,
                         'absolute_seconds': absolute, 'writer_seconds': relative})
    normalized_events = []
    for record in records:
        for number, line in enumerate(record['text'].splitlines(), 1):
            match = re.search(r'\bt=(\d+\.\d+)\b', line)
            if match:
                absolute = float(match[1])
                normalized_events.append({'source': f"window.bin slot {record['slot']} line {number}", 'line': line,
                                          'absolute_seconds': absolute, 'writer_seconds': absolute - clock['origin_seconds']
                                          if clock['origin_seconds'] is not None else None,
                                          'time_estimated': clock.get('estimated', False),
                                          'limit': 'Decimal t is CNT clock seconds in board event reports; raw value retained, no atomic sample inference.'})
    report.update(effective=True, boot_id=hex(header['boot_id']), header=header, source_checks=source_checks,
                  snapshots=records, rejected_snapshots=rejected, heartbeat=heartbeat, clock=clock,
                  msgbuf_status=msg_status, msgbuf_lines=msglines, normalized_events=normalized_events,
                  summary=legacy_summary(records, msg), **gpu_failures(msg, records, clock, heartbeat['records']))
    return report


def markdown(report):
    lines = ['# Effective evidence (additional; original evidence untouched)', '',
             f"Source: `{report['source']}`", f"Raw SHA-256: `{report['raw_window_sha256']}`",
             f"Valid committed boot: `{report.get('boot_id', 'NONE')}`; effective={report['effective']}", '']
    if not report['effective']:
        lines += [f"Rejected: {report['reason']}"]
    else:
        lines += ['## Attribution and clock', '', *report['source_checks'],
                  f"Snapshots accepted={len(report['snapshots'])}; rejected={len(report['rejected_snapshots'])}",
                  f"Msgbuf: {report['msgbuf_status']}",
                  f"Time method: {report['clock']['method']}; origin={report['clock']['origin_seconds']} s",
                  report['clock'].get('limit', report['clock'].get('reason', '')), '', '## Heartbeat subrings', '',
                  '| Subring | Retained | Missing records (interior) | Gap events | Prefix unobservable | Wrap/reset suspected |',
                  '|---|---:|---:|---:|---:|---|']
        for name, stats in report['heartbeat']['subrings'].items():
            lines.append(f"| {name} | {stats['retained']} | {stats['missing_records']} | {stats['gap_events']} | {stats['unobservable_prefix_records']} | {stats['sequence_wrap_or_reset_suspected']} |")
        lines += [f"Empty={report['heartbeat']['empty']}; invalid={report['heartbeat']['invalid']}; foreign={report['heartbeat']['foreign']}",
                  *report['heartbeat']['limits'], '', '## First retained failures', '']
        for key in ('first_gpu_failure', 'first_core_failure'):
            event = report[key]
            lines.append(f'### {key}')
            if event:
                lines += [f"{event['source']}; writer-relative={event.get('writer_seconds')} s", '```text', event['line'], '```', event['limit']]
                for context in event.get('context', []):
                    lines.append(f"{context['source']}: {context['line']}")
            else:
                lines.append('Not captured; absence is not proof of success or nonexecution.')
        lines += ['', '## CORE2 / DRAIN / legacy CORE (raw contextual samples)', '']
        seen = set()
        for item in report['diagnostics']:
            if item['line'] in seen:
                continue
            seen.add(item['line'])
            lines += [f"{item['source']}; snapshot t={item['snapshot_writer_sec']} s; sample t={item.get('sample_writer_seconds')} s",
                      '```text', item['line'], '```']
            if not item['trusted']:
                lines.append(f"UNTRUSTED: {item['reason']}")
            else:
                lines += [f"Valid fields: {', '.join(item['valid_fields']) or 'none'}; unsampled: {', '.join(item['unsampled_fields']) or 'none'}",
                          f"Invalid MMIO fields: {', '.join(item['invalid_mmio_fields']) or 'none'}; unknown valid bits: {item['unknown_valid_bits']:x}"]
                if item['kind'] == 'DRAIN':
                    lines.append(f"Stage={item['stage_name']}; reason={item['reason_name']}")
        lines += ['', '## Effective UI counters', '',
                  f"SWAP check={report['summary']['max_swap_checks']} reject={report['summary']['max_swap_rejects']} transfer={report['summary']['max_transfers']}"]
    lines += ['', '## Limits', '', *report['limits'], '',
              'EFFECTIVE_EVIDENCE.json contains validated raw slot text, msgbuf line provenance, normalized event times and rejected-record reasons.']
    return '\n'.join(lines) + '\n'


def save_report(root, report):
    root = Path(root)
    outputs = (root / 'EFFECTIVE_EVIDENCE.json', root / 'EFFECTIVE_EVIDENCE.md')
    if any(path.exists() for path in outputs):
        raise FileExistsError('additional evidence already exists; refusing overwrite (use --check for read-only replay)')
    # Exclusive creation prevents accidental overwrite even if two readers race.
    with outputs[0].open('x') as file:
        file.write(json.dumps(report, indent=2) + '\n')
    with outputs[1].open('x') as file:
        file.write(markdown(report))
    return outputs


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path, help='one retrieval directory containing window.bin')
    parser.add_argument('--check', action='store_true', help='read-only replay: print concise JSON, write nothing')
    args = parser.parse_args(argv)
    try:
        report = analyze(args.run)
        if args.check:
            brief = {key: report[key] for key in ('effective', 'raw_window_sha256')}
            brief.update(boot_id=report.get('boot_id'), clock=report.get('clock'),
                         accepted_snapshots=len(report['snapshots']), reason=report.get('reason'),
                         source_checks=report.get('source_checks', []),
                         subrings=report['heartbeat'].get('subrings'),
                         first_gpu_failure={key: report['first_gpu_failure'].get(key) for key in ('source', 'line', 'writer_seconds')}
                         if report.get('first_gpu_failure') else None,
                         first_core_failure={key: report['first_core_failure'].get(key) for key in ('source', 'line', 'writer_seconds')}
                         if report.get('first_core_failure') else None)
            print(json.dumps(brief, indent=2))
        else:
            paths = save_report(args.run.resolve(), report)
            print('Additional analysis saved:', paths[1])
        return 0 if report['effective'] else 2
    except (OSError, ValueError) as error:
        print(f'Analysis refused: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
