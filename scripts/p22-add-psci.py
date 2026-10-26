#!/usr/bin/env python3
"""Restore ArmCpu's PSCI input from a captured board FDT into an ADT copy."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/adt'))
from adt import decode, encode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fdt', type=Path, required=True)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('output already exists; preserve previous candidates')
    source = subprocess.run(['dtc', '-I', 'dtb', '-O', 'dts', str(args.fdt)],
                            check=True, capture_output=True, text=True).stdout
    match = re.search(r'\bpsci\s*\{([^{}]*)\}', source)
    if not match:
        parser.error('captured FDT has no PSCI node')
    method = re.search(r'\bmethod\s*=\s*"(smc|hvc)"\s*;', match[1])
    if not method or 'arm,psci-1.0' not in match[1]:
        parser.error('unsupported or missing captured PSCI binding')
    raw = args.input.read_bytes()
    node = decode(raw)
    if any(child['name'] == 'psci' for child in node['children']):
        parser.error('input already has PSCI; inspect rather than overwrite')
    node['children'].append({'name': 'psci', 'properties': {
        'name': b'psci\0', 'compatible': b'arm,psci-1.0\0',
        'method': method[1].encode() + b'\0'}, 'children': []})

    def emit(n):
        return encode(n['name'], {k: v for k, v in n['properties'].items()
                                 if k != 'name'}, [emit(c) for c in n['children']])

    result = emit(node)
    assert decode(result) == node
    unchanged = decode(result)
    unchanged['children'].pop()
    assert unchanged == decode(raw)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    report = {'change': 'add /psci only', 'method': method[1],
              'fdt_sha256': hashlib.sha256(args.fdt.read_bytes()).hexdigest(),
              'input_sha256': hashlib.sha256(raw).hexdigest(),
              'output_sha256': hashlib.sha256(result).hexdigest(),
              'hardware_verified': False}
    args.output.with_suffix(args.output.suffix + '.psci.json').write_text(
        json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
