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
    rb'^CBHB0001 boot=([0-9a-f]{16}) seq=(\d+) sec=(\d+) tag=([a-z]+) state=(\d+) rounds=(\d+)$')


def parse(block):
    """Return the heartbeat fields, or None for an absent/invalid record."""
    if not isinstance(block, (bytes, bytearray)) or len(block) < 64:
        return None
    line = bytes(block).split(b'\n', 1)[0].rstrip(b'\0')
    m = _LINE.match(line)
    if not m:
        return None
    boot = m.group(1).decode('ascii')
    return {
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


def encode(boot_id, seq, sec, tag, state, rounds, size=4096):
    """Build a block in the same format the kernel writes (test/self-check)."""
    line = ('CBHB0001 boot=%016x seq=%d sec=%d tag=%s state=%d rounds=%d\n'
            % (boot_id, seq, sec, tag, state, rounds)).encode('ascii')
    return line + b'\0' * (size - len(line))
