"""Decode the CBHB heartbeat block written at the last block of the log window.

The kernel writes one fixed block (inside the authorized 32 MiB window, outside
the header/message/slot structures) from a thread that never touches msgbuf or
the console, plus one marker from the auto-reset thread before its IOLog and
before the PSCI SMC. A missing or stopped heartbeat therefore separates a
console/msgbuf stall from a kernel/UFS stop; see AGENTS.md for the limits.
"""
import re

MAGIC = b'CBHB0001'
_LINE = re.compile(
    rb'^CBHB0001 boot=([0-9a-f]{16}) seq=(\d+) sec=(\d+) tag=([a-z0-9]+) state=(\d+) rounds=(\d+)'
    rb'(?: m=(\d+) l=([0-9a-f]+) o=(\d+) s=(-?\d+) a=(\d+))?'
    rb'(?: f=(-?\d+)/(\d+)/([0-9a-f]+)/([0-9a-f]+)/(\d+)/([0-9a-f]+)/([0-9a-f]+)/(\d+))?$')


def parse(block):
    """Return the heartbeat fields, or None for an absent/invalid record."""
    if not isinstance(block, (bytes, bytearray)) or len(block) < 64:
        return None
    line = bytes(block).split(b'\n', 1)[0].rstrip(b'\0')
    m = _LINE.match(line)
    if not m:
        return None
    boot = m.group(1).decode('ascii')
    result = {
        'magic': MAGIC.decode('ascii'),
        'boot': boot,
        'boot_id': '0x' + boot,
        'seq': int(m.group(2)),
        'sec': int(m.group(3)),
        'tag': m.group(4).decode('ascii'),
        'state': int(m.group(5)),
        'rounds': int(m.group(6)),
        'line': line.decode('ascii', 'replace'),
    }
    if m.group(7) is not None:
        result.update(markers=int(m.group(7)), last=int(m.group(8), 16), opens=int(m.group(9)),
                      open_stage=int(m.group(10)), abi_opens=int(m.group(11)))
    if m.lastindex and m.lastindex >= 19 and m.group(12) is not None:
        result.update(submit_rc=int(m.group(12)), submit_stage=int(m.group(13)),
                      submit_irq=int(m.group(14), 16), submit_cpirq=int(m.group(15), 16),
                      submit_seq=int(m.group(16)),
                      submit_preirq=int(m.group(17), 16), submit_precpirq=int(m.group(18), 16),
                      submit_fenced=int(m.group(19)))
    return result


def encode(boot_id, seq, sec, tag, state, rounds, size=4096):
    """Build a block in the same format the kernel writes (test/self-check)."""
    line = ('CBHB0001 boot=%016x seq=%d sec=%d tag=%s state=%d rounds=%d\n'
            % (boot_id, seq, sec, tag, state, rounds)).encode('ascii')
    return line + b'\0' * (size - len(line))


def collect(window, boot_id, free_first, free_count):
    """Collect the heartbeat slots, keep only this boot, and count losses.

    tick/step/auto-reset use disjoint sub-rings, so records never collide. A
    slot that is empty or holds another boot's record is reported, not merged.
    """
    records, foreign, empty = [], 0, 0
    for i in range(free_first, free_first + free_count):
        r = parse(window[i * 4096:(i + 1) * 4096])
        if r is None:
            empty += 1
        elif int(r['boot'], 16) != boot_id:
            foreign += 1
        else:
            records.append(r)
    records.sort(key=lambda r: (r['sec'], r['tag'], r['seq']))
    by_tag = {}
    for r in records:
        by_tag.setdefault(r['tag'], []).append(r['seq'])
    gaps = {tag: sum(1 for a, b in zip(v, v[1:]) if b != a + 1) for tag, v in by_tag.items()}
    return {'records': records, 'foreign': foreign, 'empty': empty, 'gaps': gaps,
            'free_first': free_first, 'free_count': free_count}
