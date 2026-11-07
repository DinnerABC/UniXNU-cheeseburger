#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Audit the target's LLVM C exports without loading or changing Apple code."""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct
import subprocess


def exports(directory, install_name):
    streams, maps = [], []
    try:
        for path in sorted(directory.glob('dyld_shared_cache_arm64*')):
            if path.name.endswith('.symbols'):
                continue
            stream = path.open('rb')
            data = mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ)
            streams.append((stream, data))
            if data[:16].rstrip(b'\0') != b'dyld_v1   arm64':
                raise ValueError('Only the qualified ordinary ARM64 cache is supported')
            start, count = struct.unpack_from('<2I', data, 16)
            if count > 64 or start + count * 32 > len(data):
                raise ValueError('Invalid cache mapping table')
            for i in range(count):
                va, size, offset, _, _ = struct.unpack_from('<3Q2I', data, start + i * 32)
                if offset > len(data) or size > len(data) - offset:
                    raise ValueError('Unbacked cache mapping')
                maps.append((va, va + size, offset, data))
        main = streams[0][1]
        if main[88:104].hex() != '590d839a82d230f1a17d0cc053420f73':
            raise ValueError('Target21H461 cache UUID mismatch')

        def read(va, count):
            for start, end, offset, data in maps:
                if start <= va and count <= end - va:
                    return data[offset + va - start:offset + va - start + count]
            raise ValueError('Unbacked image range')

        at, count = struct.unpack_from('<2Q', main, 136)
        if count > 100000 or at + count * 32 > len(main):
            raise ValueError('Invalid image table')
        found = []
        for i in range(count):
            uuid, va, _, name = struct.unpack_from('<16sQ2I', main, at + i * 32)
            end = main.find(b'\0', name, name + 4096)
            if end < 0:
                raise ValueError('Invalid image name')
            if main[name:end].decode() == install_name:
                found.append((uuid, va))
        if len(found) != 1:
            raise ValueError('Require one original LLVM image')
        uuid, va = found[0]
        header = read(va, 32)
        ncmds, nbytes = struct.unpack_from('<2I', header, 16)
        if ncmds > 4096 or nbytes > 1024 * 1024:
            raise ValueError('Invalid image commands')
        commands = read(va + 32, nbytes)
        pos, link, trie, actual_uuid = 0, None, None, None
        for _ in range(ncmds):
            cmd, size = struct.unpack_from('<2I', commands, pos)
            if size < 8 or size > len(commands) - pos:
                raise ValueError('Invalid load command')
            if cmd == 0x19 and commands[pos + 8:pos + 24].split(b'\0')[0] == b'__LINKEDIT':
                link = struct.unpack_from('<4Q', commands, pos + 24)
            if cmd == 0x1b:
                actual_uuid = commands[pos + 8:pos + 24]
            if cmd == 0x80000033:
                trie = struct.unpack_from('<2I', commands, pos + 8)
            elif cmd == 0x80000022 and trie is None:
                candidate = struct.unpack_from('<2I', commands, pos + 40)
                if candidate[1]:
                    trie = candidate
            pos += size
        if pos != len(commands) or uuid != actual_uuid or not link or not trie:
            raise ValueError('Incomplete original image identity')
        lva, _, fileoff, filesize = link
        off, size = trie
        if off < fileoff or size > filesize - (off - fileoff):
            raise ValueError('Export trie outside LINKEDIT')
        data = read(lva + off - fileoff, size)

        def uleb(at):
            value, shift = 0, 0
            while at < len(data) and shift < 64:
                byte = data[at]
                at += 1
                value |= (byte & 127) << shift
                if byte < 128:
                    return value, at
                shift += 7
            raise ValueError('Invalid export integer')

        pending, visited, names = [(0, '')], set(), []
        while pending:
            at, prefix = pending.pop()
            if at in visited or len(visited) >= 100000 or len(prefix) > 4096:
                raise ValueError('Invalid export trie traversal')
            visited.add(at)
            terminal, start = uleb(at)
            end = start + terminal
            if end >= len(data):
                raise ValueError('Invalid export terminal')
            if terminal:
                flags, _ = uleb(start)
                if not flags & 8:
                    names.append(prefix)
            children, at = data[end], end + 1
            for _ in range(children):
                end = data.index(0, at)
                label = data[at:end].decode('ascii')
                child, at = uleb(end + 1)
                pending.append((child, prefix + label))
        return uuid.hex(), sorted(set(names))
    finally:
        for stream, data in streams:
            data.close()
            stream.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--backend', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--nm', default='/opt/homebrew/opt/llvm/bin/llvm-nm')
    a = p.parse_args()
    if a.output.exists():
        p.error('Preserve existing audit output')
    uuid, names = exports(a.cache, '/usr/lib/libLLVM.dylib')
    result = subprocess.run([a.nm, '--undefined-only', str(a.backend)],
                            capture_output=True, text=True, check=True)
    required = sorted({line.split()[-1] for line in result.stdout.splitlines()
                       if line.split() and line.split()[-1].startswith('_LLVM')})
    if not required:
        p.error('Backend has no LLVM C imports to audit')
    missing = sorted(set(required) - set(names))
    report = dict(result='TARGET_LLVM_EXPORTS_PASS' if not missing else 'TARGET_LLVM_EXPORTS_FAIL',
                  target='21H461', image='/usr/lib/libLLVM.dylib', image_uuid=uuid,
                  backend_sha256=hashlib.sha256(a.backend.read_bytes()).hexdigest(),
                  required=required, missing=missing,
                  scope='Original export presence only; C enum/API semantics and device loading remain separate gates')
    a.output.mkdir(parents=True)
    (a.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    # Only a linker declaration, not an Apple binary or SDK replacement.
    tbd = '--- !tapi-tbd\ntbd-version: 4\ntargets: [ arm64-ios ]\ninstall-name: /usr/lib/libLLVM.dylib\nexports:\n  - targets: [ arm64-ios ]\n    symbols: [ ' + ', '.join(required) + ' ]\n...\n'
    if not missing:
        (a.output / 'libLLVM.tbd').write_text(tbd)
    print(json.dumps(report, indent=2))
    return bool(missing)


if __name__ == '__main__':
    raise SystemExit(main())
