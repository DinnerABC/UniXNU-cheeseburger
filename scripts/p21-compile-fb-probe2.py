#!/usr/bin/env python3
"""Build the fb-diag7 P21 candidate kernel from its isolated source tree.

Follows the same official framework API as scripts/p21-compile-r18.py, but the
tree root is the candidate's own source-tree copy so its board runtime (with the
S3320 reset drive) is what gets linked.
"""
from pathlib import Path
import os, json, shutil, subprocess, sys, time

P = Path(__file__).resolve().parents[1]
C = P / 'out/p21/fb-probe2'
R = C / 'source-tree'
U = P / '.work/p21-official-framework'
O = R / 'out/p21/official-adaptation'

sys.path.insert(0, str(U / 'scripts'))
from unixnu.cli import parser
from unixnu.config import Context
from unixnu import feeds, kernel, patches

os.environ['DEVELOPER_DIR'] = '/Applications/Xcode.app/Contents/Developer'
os.environ['PATH'] = ('/opt/homebrew/opt/python@3.13/libexec/bin:/opt/homebrew/opt/make/libexec/gnubin:'
                      '/opt/homebrew/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/lld/bin:'
                      '/opt/homebrew/opt/unifdef/bin:' + os.environ.get('PATH', ''))
# The official source preparation fetches DriverKit headers from GitHub; this
# host reaches it only through the local proxy.
# The local proxy currently drops GitHub TLS connections while a direct
# connection to raw.githubusercontent.com works, so no proxy is injected here.
for key in ('HTTP_PROXY', 'HTTPS_PROXY', 'http_proxy', 'https_proxy'):
    os.environ.pop(key, None)

a = parser().parse_args(['tree', '-D', str(R), '--out', str(O / 'resolved'), '-j', '6'])
c = Context(a)
m = feeds.resolve(c)
c.modules = m
patches.prepare(c, m)
subprocess.run([sys.executable, str(P / 'scripts/p21-restore-official-headers.py')], check=True)
# The archive recipe runs with cwd=tree; pin its board root to this candidate.
os.environ['P21_BOARD_ROOT'] = str(R)

shutil.copy2(P / 'scripts/p21-build-board-runtime.py', c.tree / 'scripts/p21-build-board-runtime.py')
assert c.example, 'Image pipeline remains guarded until firmware/ABI qualification'

# Restore the DriverKit headers from the already prepared fb-pdt tree when the
# network fetch fails; the blob hash is still verified by the script itself.
SEED = P / 'out/p21/fb-probe/source-tree/out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree/.cache/xnu-work'
if SEED.is_dir():
    prep = c.tree / 'scripts/prepare-xnu-source.py'
    text = prep.read_text()
    if 'P21_SEED_ROOT' not in text:
        text = text.replace("DESTINATION = Path('.cache/xnu-work/iokit/DriverKit')",
                            "DESTINATION = Path('.cache/xnu-work/iokit/DriverKit')\nP21_SEED_ROOT = Path(os.environ['P21_SEED_ROOT'])" if False else
                            "DESTINATION = Path('.cache/xnu-work/iokit/DriverKit')\nP21_SEED_ROOT = Path(os.environ.get('P21_SEED_ROOT', '.'))")
        text = text.replace("    data = fetch(entry['download_url'])",
                            "    seed = P21_SEED_ROOT / target\n    data = seed.read_bytes() if seed.is_file() else fetch(entry['download_url'])")
        prep.write_text(text)
    os.environ['P21_SEED_ROOT'] = str(SEED)
    print('seeded DriverKit headers from', SEED)

start = time.time()
kernel.build(c, m)
built = c.tree / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837'
assert built.is_file(), built
C.joinpath('native').mkdir(parents=True, exist_ok=True)
shutil.copy2(built, C / 'native/kernel.macho')

import hashlib
sha = hashlib.sha256((C / 'native/kernel.macho').read_bytes()).hexdigest()
report = {'result': 'OFFICIAL_KERNEL_SOURCE_BUILD_PASS', 'duration_seconds': round(time.time() - start, 1),
          'board_guard_retained': c.example, 'prelinked': False, 'booted': False,
          'tree': str(c.tree), 'kernel_sha256': sha,
          'kernel_bytes': (C / 'native/kernel.macho').stat().st_size}
(C / 'kernel-source-build.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))