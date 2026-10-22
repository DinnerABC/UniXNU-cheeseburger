#!/usr/bin/env python3
"""Capture version/preflight statuses; unavailable Xcode is evidence, not a build pass."""
import json
import platform
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / 'out'
out.mkdir(exist_ok=True)
commands = [
    ['uname', '-a'], ['sw_vers'], ['xcode-select', '-p'], ['xcodebuild', '-version'],
    ['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], ['make', '--version'],
    ['clang', '--version'], ['lld-link', '--version'], ['llvm-lib', '--version'],
    ['cmake', '--version'], ['ninja', '--version'], ['brew', 'list', '--versions'],
]
records = []
for command in commands:
    try:
        r = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        records.append({'command': command, 'exit_code': r.returncode, 'output': r.stdout})
    except OSError as e:
        records.append({'command': command, 'exit_code': None, 'error': str(e)})
(out / 'environment.json').write_text(json.dumps({
    'python': sys.version, 'python_executable': sys.executable,
    'platform': platform.platform(), 'checks': records,
}, indent=2) + '\n')
print(out / 'environment.json')
