#!/usr/bin/env python3
"""Build the P21 atos-gate candidate kernel on the local disk.

The NAS holds the authoritative sources and this candidate's artifacts. XNU
compilation, its object directory and the Mach-O link run in an isolated local
build tree (default /Users/dinnerabc/unixnu-p21/atos-gate/source-tree); only the
few inputs this round changed are synced there, and the kernel plus its build
records are copied back to NAS afterwards. Override the build location with
P21_LOCAL_ROOT; it must not resolve to the NAS share.
"""
from pathlib import Path
import fcntl, hashlib, os, json, shutil, subprocess, sys, time

P = Path(__file__).resolve().parents[1]
C = Path(os.environ.get('P21_CANDIDATE', str(P / 'out/p21/atos-gate'))).resolve()
NAS_TREE = P / 'out/p21/atos-gate/source-tree'
LOCAL = Path(os.environ.get('P21_LOCAL_ROOT', '/Users/dinnerabc/unixnu-p21/atos-gate'))
R = LOCAL / 'source-tree'
U = P / '.work/p21-official-framework'
O = R / 'out/p21/official-adaptation'
assert R.is_dir(), f'local build tree missing: {R}'
assert not str(R.resolve()).startswith('/Volumes/'), f'build tree must be local, got {R.resolve()}'
assert (R / 'board.toml').is_file() and (R / 'package/BoardRuntime').is_dir(), R
# Keep a kernel-enforced lock for the whole process; a stale file is harmless.
lock = LOCAL / '.build.lock'
lock_handle = lock.open('a+')
try:
    fcntl.flock(lock_handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
except BlockingIOError:
    raise SystemExit(f'local build tree busy: {R}')
lock_handle.seek(0); lock_handle.truncate(); lock_handle.write(str(os.getpid())); lock_handle.flush()
# The build tree must read its board sources from itself, not from an earlier
# candidate or the NAS share.
kits = R / 'out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree/kits/cheeseburger'
kits.parent.mkdir(parents=True, exist_ok=True)
if not kits.is_symlink() or kits.resolve() != (R / 'package').resolve():
    if kits.is_symlink() or kits.exists():
        kits.unlink()
    kits.symlink_to(R / 'package')
assert kits.resolve() == (R / 'package').resolve(), kits.resolve()

def sync(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['rsync', '-a', str(source) + '/', str(target) + '/'], check=True)

# BoardRuntime is the only board source this round changes. Mirror it from the
# authoritative repo into the local build tree and the NAS evidence tree.
sync(P / 'package/BoardRuntime', R / 'package/BoardRuntime')
shutil.copy2(P / 'scripts/p21-build-board-runtime.py', R / 'scripts/p21-build-board-runtime.py')
assert (C / 'patches/xnu/series').is_file(), 'atos-gate XNU patch series missing'
changed = []
for src in sorted((P / 'package/BoardRuntime').iterdir()):
    if src.suffix in ('.c', '.cpp', '.h', '.inc'):
        built = R / 'package/BoardRuntime' / src.name
        assert hashlib.sha256(src.read_bytes()).hexdigest() == hashlib.sha256(built.read_bytes()).hexdigest(), src.name
        changed.append(src.name)
head = subprocess.run(['git', '-C', str(P), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
dirty = subprocess.run(['git', '-C', str(P), 'status', '--porcelain'], capture_output=True, text=True).stdout.count('\n')
print(json.dumps({
    'local_root': str(LOCAL), 'local_tree': str(R), 'nas_candidate': str(C),
    'nas_evidence_tree': str(NAS_TREE), 'git_head': head, 'workspace_dirty_entries': dirty,
    'synced_sources': changed, 'patch_series': str(C / 'patches/xnu/series'),
    'kits_cheeseburger': str(kits.resolve())}, indent=2), flush=True)

sys.path.insert(0, str(U / 'scripts'))
from unixnu.cli import parser
from unixnu.config import Context
from unixnu import feeds, kernel, patches

os.environ['DEVELOPER_DIR'] = '/Applications/Xcode.app/Contents/Developer'
os.environ['PATH'] = ('/opt/homebrew/opt/python@3.13/libexec/bin:/opt/homebrew/opt/make/libexec/gnubin:'
                      '/opt/homebrew/bin:/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/lld/bin:'
                      '/opt/homebrew/opt/unifdef/bin:' + os.environ.get('PATH', ''))
# Reuse verified local headers; use the configured proxy if a fetch is required.
for key in ('HTTP_PROXY', 'HTTPS_PROXY', 'http_proxy', 'https_proxy'):
    os.environ.setdefault(key, 'http://127.0.0.1:7897')

a = parser().parse_args(['tree', '-D', str(R), '--out', str(O / 'resolved'), '-j', '6'])
c = Context(a)
m = feeds.resolve(c)
c.modules = m
patches.prepare(c, m)
subprocess.run([sys.executable, str(P / 'scripts/p21-restore-official-headers.py')], check=True,
               env=dict(os.environ, P21_HEADER_TARGET_ROOT=str(R)))
# The archive recipe runs with cwd=tree; pin its board root to this candidate.
os.environ['P21_BOARD_ROOT'] = str(R)

shutil.copy2(P / 'scripts/p21-build-board-runtime.py', c.tree / 'scripts/p21-build-board-runtime.py')
assert c.example, 'Image pipeline remains guarded until firmware/ABI qualification'

# Restore the DriverKit headers from an already prepared tree when the network
# fetch fails; the blob hash is still verified by the script itself. The local
# header seed is a one-time copy of the verified p4 cache; the p4 cache on NAS
# is the fallback.
SEED = LOCAL / 'header-seed'
if not (SEED / '.cache/xnu-work/iokit/DriverKit').is_dir():
    SEED = R / '.work/p4-unixnu'
if not (SEED / '.cache/xnu-work/iokit/DriverKit').is_dir():
    SEED = NAS_TREE / 'out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/tree'
if (SEED / '.cache/xnu-work/iokit/DriverKit').is_dir():
    prep = c.tree / 'scripts/prepare-xnu-source.py'
    text = prep.read_text()
    if 'P21_SEED_ROOT' not in text:
        text = text.replace("DESTINATION = Path('.cache/xnu-work/iokit/DriverKit')",
                            "DESTINATION = Path('.cache/xnu-work/iokit/DriverKit')\nP21_SEED_ROOT = Path(os.environ.get('P21_SEED_ROOT', '.'))")
        text = text.replace("    data = fetch(entry['download_url'])",
                            "    seed = P21_SEED_ROOT / target\n    data = seed.read_bytes() if seed.is_file() else fetch(entry['download_url'])")
        prep.write_text(text)
    os.environ['P21_SEED_ROOT'] = str(SEED)
    print('seeded DriverKit headers from', SEED)

start = time.time()
# The board series is regenerated with the source tree; keep the local P8
# console switch in its own stable series so a tree refresh cannot drop it.
extra = C / 'patches/xnu/series'
if extra.is_file():
    c.board.setdefault('kernel', {}).setdefault('patch_series', []).append(str(extra))
kernel.build(c, m)

def sha256_of(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

# The compiled tree must be the patched local tree, not a stale copy from an
# earlier candidate: check the round's XNU patch and the board sources.
exec_c = c.tree / '.cache/xnu-work/bsd/kern/kern_exec.c'
assert 'native_gpu_addon_cache(' in exec_c.read_text(), 'launchd cache patch missing from built sources'
kits_built = c.tree / 'kits/cheeseburger'
assert kits_built.resolve() == (R / 'package').resolve(), kits_built.resolve()
for src in sorted((P / 'package/BoardRuntime').iterdir()):
    if src.suffix in ('.c', '.cpp', '.h', '.inc'):
        built_src = kits_built / 'BoardRuntime' / src.name
        assert sha256_of(src) == sha256_of(built_src), f'compiled board source differs: {src.name}'

built = c.tree / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837'
assert built.is_file(), built
C.joinpath('native').mkdir(parents=True, exist_ok=True)
shutil.copy2(built, C / 'native/kernel.macho')

import hashlib
def sha_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
# Copy the build output and the board record that names the actual compile
# directory back to the NAS evidence tree, then verify both sides. The object
# directory itself stays local; prelink and the gate use these two files.
evidence = C / 'source-evidence' / c.tree.relative_to(R)
copied = {}
for rel in ('build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837',
            'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf',
            'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/pexpert/DEVELOPMENT/pe_init.o.json'):
    src = c.tree / rel
    if not src.is_file():
        continue
    dst = evidence / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    assert sha_file(src) == sha_file(dst), rel
    copied[rel] = sha_file(dst)
sha = sha_file(C / 'native/kernel.macho')
assert sha == sha_file(built) == copied['build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837']
report = {'result': 'OFFICIAL_KERNEL_SOURCE_BUILD_PASS', 'duration_seconds': round(time.time() - start, 1),
          'board_guard_retained': c.example, 'prelinked': False, 'booted': False,
          'tree': str(c.tree), 'local_root': str(LOCAL), 'nas_candidate': str(C),
          'nas_evidence_tree': str(evidence), 'build_on_nas': str(c.tree).startswith('/Volumes/'),
          'sync_back': copied, 'kernel_sha256': sha,
          'kernel_bytes': (C / 'native/kernel.macho').stat().st_size}
(C / 'kernel-source-build.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
