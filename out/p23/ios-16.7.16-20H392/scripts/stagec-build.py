#!/usr/bin/env python3
"""Stage C XNU 8796 driver: merged config, iOS 16.0 deployment target, bootstrap and final passes.

The tree is assembled by stagec-xnu-tree.py and already carries the 61-patch 8796 series; this
driver only compiles it. It never touches the P21/P23 trees, the device or any partition.
"""
from pathlib import Path
import json
import os
import subprocess
import sys
import time

R = Path(__file__).resolve().parents[4]
STAGE = Path(__file__).resolve().parents[1]
TREE = STAGE / 'tree'
FW = R / '.work/p21-official-framework'
LOGS = STAGE / 'logs'
SERIES = STAGE / 'patches-8796/series-8796.list'
CONFIG = STAGE / '.config'
COMMIT_FILE = STAGE / 'patches-8796/tree.json'

FRAGMENTS = [
    FW / 'profiles/arch/arm64/configs/common.config',
    R / 'soc/msm8998/configs/msm8998.config',
    FW / 'profiles/os/ipados-17.7.11-21H461/configs/ipados.config',
    R / 'configs/cheeseburger_defconfig',
]

TOOLCHAIN_PATH = ':'.join(['/opt/homebrew/opt/python@3.13/libexec/bin', '/opt/homebrew/opt/make/libexec/gnubin',
                           '/opt/homebrew/bin', '/opt/homebrew/opt/llvm/bin', '/opt/homebrew/opt/lld/bin',
                           '/opt/homebrew/opt/unifdef/bin'])


def xnu_commit():
    return subprocess.run(['git', '-C', str(TREE / 'refer/xnu'), 'rev-parse', 'HEAD'],
                          check=True, capture_output=True, text=True).stdout.strip()


def merge_config():
    sys.path.insert(0, str(FW / 'scripts'))
    from unixnu.config import parse_config
    chunks, merged = [], {}
    for path in FRAGMENTS:
        if not path.is_file():
            raise SystemExit(f'config fragment missing: {path}')
        text = path.read_text()
        merged.update(parse_config(text))
        chunks.append(text.rstrip() + '\n')
    CONFIG.write_text('\n'.join(chunks))
    return merged


def deployment_target():
    """iOS 16.0 for the 20H392 userspace; the copied script hardcodes 17.0 for P21."""
    script = TREE / 'scripts/build-xnu-4k-baseline.sh'
    text = script.read_text()
    patched = text.replace('-miphoneos-version-min=17.0', '-miphoneos-version-min=16.0') \
                  .replace('-Wl,-ios_version_min,17.0', '-Wl,-ios_version_min,16.0')
    if patched == text and '-miphoneos-version-min=16.0' not in text:
        raise SystemExit('deployment target line not found in build script')
    if patched != text:
        script.write_text(patched)
    return '-miphoneos-version-min=16.0' in script.read_text()


def host_compiler_flags():
    """8796 adds an Apple-only alias that Xcode 26 and Homebrew LLVM both reject; keep the real flag."""
    path = TREE / '.cache/xnu-work/makedefs/MakeInc.def'
    text = path.read_text()
    line = '\t-enable-trivial-auto-var-init-zero-knowing-it-will-be-removed-from-clang \\\n'
    if line in text:
        path.write_text(text.replace(line, '', 1))
        return True
    return False


def host_source_compat():
    """Adopt the three source forms that later xnu releases already use, so 8796 compiles with clang 21.

    1. uio_stackbuf_t as a 1-byte array element with 8-byte alignment is now a hard error;
       xnu 10063 replaced it with the uint64_t UIO_STACKBUF form.
    2. Xcode 26's iig emits rpc.kernelContent, which 10063 added to struct IORPC under __KERNEL__.
    """
    import re
    work = TREE / '.cache/xnu-work'
    debug = work / 'osfmk/kern/debug.c'
    debug_text = debug.read_text()
    marker = '\t\t\tprintf("panic(cpu %d caller 0x%lx): %s\\n", cpu_number(), panic_caller, board_panic_msg);'
    call = ('\n\t\t\textern void boot_log_panic_flush_text(const char *) __attribute__((weak_import));'
            '\n\t\t\tif (&boot_log_panic_flush_text) boot_log_panic_flush_text(board_panic_msg);')
    if 'boot_log_panic_flush_text(board_panic_msg)' not in debug_text:
        if marker not in debug_text:
            raise SystemExit('board panic capture anchor missing')
        debug.write_text(debug_text.replace(marker, marker + call, 1))
    header = work / 'bsd/sys/uio_internal.h'
    text = header.read_text()
    typedef = 'typedef char __attribute__((aligned(_Alignof(union iovecs)))) uio_stackbuf_t;'
    macro = ('#define UIO_STACKBUF(name, niov) \\\n'
             '\tuint64_t name[(UIO_SIZEOF(niov) + sizeof(uint64_t) - 1) / sizeof(uint64_t)]')
    rewritten = 0
    if typedef in text:
        text = text.replace(typedef, macro, 1)
        header.write_text(text)
    pattern = re.compile(r'uio_stackbuf_t\s+(\w+)\s*\[UIO_SIZEOF\((.*?)\)\];')
    for path in work.rglob('*'):
        if path.suffix not in ('.c', '.cpp', '.h'):
            continue
        source = path.read_text()
        updated, count = pattern.subn(lambda m: f'UIO_STACKBUF({m.group(1)}, {m.group(2)});', source)
        if count:
            path.write_text(updated)
            rewritten += count
    rpc = work / 'iokit/IOKit/IORPC.h'
    rpc_text = rpc.read_text()
    if 'kernelContent' not in rpc_text:
        anchor = '\tuint32_t           replySize;\n};'
        if anchor not in rpc_text:
            raise SystemExit('IORPC.h anchor missing')
        rpc_text = rpc_text.replace(anchor, '\tuint32_t           replySize;\n'
                                    '#ifdef KERNEL\n\tIORPCMessage     * kernelContent;\n#endif /* KERNEL */\n};', 1)
        rpc.write_text(rpc_text)
    # 8796 predates CFLAGS_EXTRA (10063 MakeInc.def); the board includes and warning
    # suppressions travel through it from XNU_EXTRA_CFLAGS.
    makeinc = work / 'makedefs/MakeInc.def'
    make_text = makeinc.read_text()
    defines = '\t\t  $(BOUND_CHECKS_DEFINES) \\\n\t\t  $(DEFINES)\n'
    if 'CFLAGS_EXTRA' not in make_text:
        if defines not in make_text:
            raise SystemExit('MakeInc.def CFLAGS anchor missing')
        make_text = make_text.replace(defines, '\t\t  $(BOUND_CHECKS_DEFINES) \\\n\t\t  $(DEFINES) \\\n\t\t  $(CFLAGS_EXTRA)\n', 1)
        makeinc.write_text(make_text)
    # The Apple 8796.141.3 tarball lists an arm64 KPC source it does not ship (restored in 10063).
    conf = work / 'osfmk/conf/files.arm64'
    conf_text = conf.read_text()
    stale = 'osfmk/arm64/kpc.c\t\toptional kpc nos_arm_asm\n'
    if stale in conf_text:
        conf.write_text(conf_text.replace(stale, '# arm64 kpc.c is not part of the 8796.141.3 source drop\n', 1))
    # This open source release omits the ARM PMU implementation. Do not select
    # KPC on the generic BCM2837 target used for the Snapdragon build.
    master = work / 'config/MASTER.arm64.bcm2837'
    master_text = master.read_text()
    for selected in ('mach_kdp config_serial_kdp kperf kpc ]',
                     'mach_kdp config_serial_kdp kperf ]'):
        master_text = master_text.replace(selected, 'mach_kdp config_serial_kdp ]')
    if master_text != master.read_text():
        master.write_text(master_text)
    ast = work / 'osfmk/kern/ast.c'
    ast_text = ast.read_text()
    include = '#include <kperf/kperf_kpc.h>'
    if '#if KPC\n' + include not in ast_text:
        ast_text = ast_text.replace(include, '#if KPC\n' + include + '\n#endif', 1)
        block = '\tif (reasons & AST_KPERF) {\n\t\tthread_ast_clear(thread, AST_KPERF);\n\t\tkperf_kpc_thread_ast(thread);\n\t}'
        if block not in ast_text:
            raise SystemExit('AST KPC guard anchor missing')
        ast_text = ast_text.replace(block, '#if KPC\n' + block + '\n#endif', 1)
        ast.write_text(ast_text)
    processor = work / 'osfmk/kern/processor.c'
    processor_text = processor.read_text()
    timer = '\t[RUNNING_TIMER_KPERF] = kperf_timer_expire,'
    if '#if KPERF\n' + timer not in processor_text:
        if timer not in processor_text:
            raise SystemExit('KPERF timer anchor missing')
        processor.write_text(processor_text.replace(timer, '#if KPERF\n' + timer + '\n#endif', 1))
    task = work / 'osfmk/kern/task.c'
    task_text = task.read_text()
    task_kpc = '\tnew_task->t_kpc = 0;'
    if '#if KPC\n' + task_kpc not in task_text:
        if task_kpc not in task_text:
            raise SystemExit('task KPC anchor missing')
        task.write_text(task_text.replace(task_kpc, '#if KPC\n' + task_kpc + '\n#endif', 1))
    for relative, macro, calls in (
        ('osfmk/arm64/pcb.c', 'KPC', ('kpc_off_cpu(old);',)),
        ('osfmk/kern/sched_prim.c', 'KPERF', ('kperf_running_setup(processor, processor->last_dispatch);',)),
        ('bsd/kern/kern_ktrace.c', 'KPERF', ('kperf_reset();', 'kperf_disable_sampling();')),
    ):
        path = work / relative
        source = path.read_text()
        for call in calls:
            guarded = '#if ' + macro + '\n' + call + '\n#endif'
            if guarded not in source:
                if call not in source:
                    raise SystemExit('missing performance guard anchor: ' + call)
                source = source.replace(call, guarded)
        path.write_text(source)
    # Apple xnu's later vfs_subr.c implementation, required by the 20H392 APFS donor.
    vfs = work / 'bsd/vfs/vfs_subr.c'
    vfs_text = vfs.read_text()
    if 'vfs_getvfs_with_vfsops(' not in vfs_text:
        anchor = 'static struct mount *\nvfs_getvfs_locked(fsid_t *fsid)'
        implementation = ('struct mount *\nvfs_getvfs_with_vfsops(fsid_t *fsid, const struct vfsops * const ops)\n'
                          '{\n\tmount_t mp = mount_list_lookupby_fsid(fsid, 0, 0);\n'
                          '\tif (mp != NULL && mp->mnt_op != ops) {\n\t\tmp = NULL;\n\t}\n'
                          '\treturn mp;\n}\n\n')
        if anchor not in vfs_text:
            raise SystemExit('vfs backport anchor missing')
        vfs.write_text(vfs_text.replace(anchor, implementation + anchor, 1))
    # 8796 does not emit the per-object compile records (10063 MakeInc.rule -MJ) the archive builders replay.
    rules = work / 'makedefs/MakeInc.rule'
    rules_text = rules.read_text()
    if '-MJ' not in rules_text:
        rules_text = rules_text.replace(' -MD -MF $(@:o=d) -MP ', ' -MD -MF $(@:o=d) -MJ $(@:o=o.json) -MP ')
        rules.write_text(rules_text)
    return rewritten


def environment(config, jobs):
    env = {k: v for k, v in os.environ.items() if k.lower() not in ('http_proxy', 'https_proxy', 'all_proxy')}
    env['DEVELOPER_DIR'] = '/Applications/Xcode.app/Contents/Developer'
    env['PATH'] = TOOLCHAIN_PATH + ':' + env.get('PATH', '')
    for key, value in config.items():
        if not key.startswith('XNU_'):
            continue
        if isinstance(value, str):
            value = value.replace('{tree}', str(TREE))
        env[key] = '1' if value is True else '0' if value is False else str(value)
    env.update({
        'UNIXNU_XNU_TAG': 'xnu-8796.141.3',
        'UNIXNU_XNU_COMMIT': xnu_commit(),
        'UNIXNU_XNU_DARWIN': '22.6.0',
        'UNIXNU_XNU_VERSION': '8796.141.3',
        'UNIXNU_PATCH_LIST': str(SERIES),
        'UNIXNU_GENERATED_DIR': str(TREE / 'build/generated'),
        'JOBS': str(jobs),
    })
    (TREE / 'build/generated').mkdir(parents=True, exist_ok=True)
    return env


def run_build(stage, env, extra, log_name):
    LOGS.mkdir(parents=True, exist_ok=True)
    argv = ['bash', str(TREE / 'scripts/build-xnu-4k-baseline.sh')]
    log_path = LOGS / log_name
    print(f'{stage}: {log_path}', flush=True)
    with log_path.open('w') as log:
        process = subprocess.Popen(argv, cwd=TREE, env={**env, **extra}, stdout=log,
                                   stderr=subprocess.STDOUT)
        return process


ARCHIVES = [
    ['build-corecrypto-14.py'],
    ['build-corecrypto-14.py', '--namespace'],
    ['build-arm-driver.py', 'ArmDTPlatform'],
    ['build-arm-driver.py', 'ArmCpu'],
    ['build-arm-driver.py', 'RamBlock'],
    ['build-arm-driver.py', 'AppleKeyStoreState'],
    ['stagec-build-board-runtime.py'],
]


def run_command(parts, env, log_name):
    LOGS.mkdir(parents=True, exist_ok=True)
    script = TREE / 'scripts' / parts[0]
    argv = [sys.executable if script.suffix == '.py' else 'bash', str(script), *parts[1:]]
    with (LOGS / log_name).open('w') as log:
        return subprocess.Popen(argv, cwd=TREE, env=env, stdout=log, stderr=subprocess.STDOUT).wait()


def main():
    step = sys.argv[1] if len(sys.argv) > 1 else 'config'
    jobs = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    if step == 'config':
        config = merge_config()
        report = {'result': 'STAGEC_CONFIG_MERGED', 'config': str(CONFIG.relative_to(R)),
                  'fragments': [str(p.relative_to(R)) for p in FRAGMENTS],
                  'deployment_target_ios16': deployment_target(),
                  'xnu_commit': xnu_commit(),
                  'flags': {k: v for k, v in config.items() if k.startswith('XNU_')}}
        print(json.dumps(report, indent=2))
        return
    config = merge_config()
    if not deployment_target():
        raise SystemExit('deployment target not iOS 16.0')
    host_compiler_flags()
    host_source_compat()
    env = environment(config, jobs)
    if not SERIES.is_file():
        raise SystemExit(f'patch series missing: {SERIES}')
    start = time.time()
    if step == 'archives':
        storage = TREE / 'build/storage-abi/include/IOKit/storage'
        if (TREE / 'refer/IOStorageFamily').is_dir() and not storage.is_dir():
            if run_command(['prepare-storage-headers.py'], env, 'storage-headers.log'):
                raise SystemExit('storage header preparation failed')
        results = []
        for index, parts in enumerate(ARCHIVES, 1):
            name = f'archive-{index:02d}-{Path(parts[0]).stem}'
            print(f'{name}: {" ".join(parts)}', flush=True)
            code = run_command(parts, env, f'{name}.log')
            results.append({'command': parts, 'exit': code})
            if code:
                report = {'result': 'STAGEC_ARCHIVES_FAILED', 'failed': parts,
                          'log': str(LOGS / f'{name}.log'), 'results': results}
                (LOGS / 'archives-result.json').write_text(json.dumps(report, indent=2) + '\n')
                print(json.dumps(report, indent=2))
                sys.exit(code)
        report = {'result': 'STAGEC_ARCHIVES_PASS', 'seconds': round(time.time() - start, 1),
                  'results': results}
        (LOGS / 'archives-result.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
        return
    if step == 'bootstrap':
        process = run_build('bootstrap', env, {'XNU_BOOTSTRAP': '1', 'XNU_BOOT_PROGRESS': '0'},
                            'kernel-bootstrap.log')
    elif step == 'final':
        process = run_build('final', env, {'XNU_BOOTSTRAP': '0'}, 'kernel.log')
    else:
        raise SystemExit(f'unknown step {step}; use config, bootstrap or final')
    code = process.wait()
    record = TREE / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json'
    # The bootstrap pass links without the board archives, so its link is expected to fail once
    # every compile record exists (unixnu kernel.build accepts the same failure).
    accepted = code == 0 or (step == 'bootstrap' and record.is_file())
    report = {'result': 'STAGEC_BUILD_PASS' if accepted else 'STAGEC_BUILD_FAILED',
              'step': step, 'exit': code, 'records': record.is_file(),
              'seconds': round(time.time() - start, 1),
              'log': str(LOGS / ('kernel-bootstrap.log' if step == 'bootstrap' else 'kernel.log')),
              'xnu_commit': env['UNIXNU_XNU_COMMIT'], 'jobs': jobs}
    (LOGS / f'{step}-result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    sys.exit(0 if accepted else code)


if __name__ == '__main__':
    main()
