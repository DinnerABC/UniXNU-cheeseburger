#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Symbolize P21 fault addresses against the pinned21H461 dyld cache.

Read-only: the cache is opened ACCESS_READ; Apple bytes never leave this host.
Requires the slide from the kernel fault record (e.g. slide=19d78000).
"""
import argparse
import bisect
import json
import mmap
import struct
from pathlib import Path
import sys


def load(directory):
    streams, maps, images = [], [], []
    try:
        for path in sorted(directory.glob('dyld_shared_cache_arm64*')):
            if path.name.endswith('.symbols'):
                continue
            stream = path.open('rb')
            data = mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ)
            streams.append((stream, data))
            if data[:16].rstrip(b'\0') != b'dyld_v1   arm64':
                raise ValueError('Only the ordinary ARM64 cache is supported')
            start, count = struct.unpack_from('<2I', data, 16)
            for i in range(count):
                va, size, offset, _, _ = struct.unpack_from('<3Q2I', data, start + i * 32)
                maps.append((va, va + size, offset, data))
        main = streams[0][1]
        if main[88:104].hex() != '590d839a82d230f1a17d0cc053420f73':
            raise ValueError('Target21H461 cache UUID mismatch')
        at, count = struct.unpack_from('<2Q', main, 136)
        for i in range(count):
            uuid, va, _, name = struct.unpack_from('<16sQ2I', main, at + i * 32)
            end = main.find(b'\0', name, name + 4096)
            images.append((va, main[name:end].decode()))
    finally:
        for stream, data in streams:
            data.close()
            stream.close()
    images.sort()
    return maps, images


def read(maps, va, size):
    for start, end, offset, data in maps:
        if start <= va and va + size <= end:
            return data[offset + va - start:offset + va - start + size]
    return None


def image_at(images, va):
    names = [entry[1] for entry in images]
    starts = [entry[0] for entry in images]
    index = bisect.bisect_right(starts, va) - 1
    if index < 0:
        return None
    # End bound: the next image start (images are contiguous inside the cache).
    end = starts[index + 1] if index + 1 < len(starts) else 1 << 48
    if va >= end:
        return None
    return images[index][1], va - starts[index]


def local_symbols(directory, image_name):
    """Symbol address→name for one image from the local-symbol file."""
    streams, symbols = [], {}
    try:
        for path in sorted(directory.glob('dyld_shared_cache_arm64*')):
            stream = path.open('rb')
            data = mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ)
            streams.append((stream, data))
        if not streams:
            return symbols
        with (directory / 'dyld_shared_cache_arm64.symbols').open('rb') as raw:
            pass
        main = streams[0][1]
        if main[88:104].hex() != '590d839a82d230f1a17d0cc053420f73':
            raise ValueError('UUID mismatch')
        with (directory / 'dyld_shared_cache_arm64.symbols').open('rb') as stream:
            blob = stream.read()
        if blob[88:104] != main[400:416]:
            raise ValueError('Local symbols UUID mismatch')
        local, _ = struct.unpack_from('<2Q', blob, 72)
        no, _, so, _, eo, count = struct.unpack_from('<6I', blob, local)
        # image index keys in the local-symbol file follow the unslid entry
        # order of the main cache image table; rebuild that order.
        at, total = struct.unpack_from('<2Q', main, 136)
        order = []
        for i in range(total):
            uuid, va, _, name = struct.unpack_from('<16sQ2I', main, at + i * 32)
            end = main.find(b'\0', name, name + 4096)
            order.append(main[name:end].decode())
        want = {name: index for index, name in enumerate(order)}
        if image_name not in want:
            return symbols
        target = want[image_name]
        for i in range(count):
            image, start, n = struct.unpack_from('<Q2I', blob, local + eo + 16 * i)
            if image != target:
                continue
            for j in range(start, start + n):
                name, kind, section, _, value = struct.unpack_from('<IBBHQ', blob, local + no + 16 * j)
                if not kind & 224 and kind & 14 == 14 and section:
                    text = blob[local + so + name:blob.find(b'\0', local + so + name)].decode()
                    symbols.setdefault(value, []).append(text)
    finally:
        for stream, data in streams:
            data.close()
            stream.close()
    return symbols


def nearest(symbols, offset):
    values = sorted(symbols)
    index = bisect.bisect_right(values, offset) - 1
    if index < 0:
        return None
    base = values[index]
    names = symbols[base]
    return base, names[0], offset - base


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--slide', required=True, help='slide from the fault record, e.g. 19d78000')
    parser.add_argument('addresses', nargs='+', help='runtime addresses (0x optional)')
    args = parser.parse_args()
    slide = int(args.slide, 16)
    maps, images = load(args.cache)
    output = []
    for token in args.addresses:
        runtime = int(token, 16)
        unslid = runtime - slide
        image = image_at(images, unslid)
        if not image:
            output.append({'runtime': hex(runtime), 'unslid': hex(unslid), 'image': None})
            continue
        name, offset = image
        symbols = local_symbols(args.cache, name)
        hit = nearest(symbols, offset)
        entry = {'runtime': hex(runtime), 'unslid': hex(unslid), 'image': name, 'offset': hex(offset)}
        if hit:
            entry.update(symbol=hit[1], symbol_offset=hex(hit[2]), symbol_address=hex(hit[0]))
        output.append(entry)
    print(json.dumps({'result': 'SYMBOLIZED', 'slide': hex(slide), 'frames': output}, indent=2))


if __name__ == '__main__':
    main()
