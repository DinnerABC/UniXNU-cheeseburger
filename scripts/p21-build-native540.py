#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the pinned sagit AIR/NIR backend on local disk, without installing packages."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import types

PIN = '673f85070fe9f7872793015eb230a5c3987d4a8c'
DEFAULT_ROOT = Path('/Users/dinnerabc/unixnu-p21/native540')
REPO = Path(__file__).resolve().parents[1]


def run(argv, **kw):
    argv = list(map(str, argv))
    # Upstream's automatic pip installation is deliberately not available.
    if any(x in ('pip', 'install') for x in argv):
        raise RuntimeError('Package installation is not authorized by this builder')
    kw.pop('log', None)
    print('+', ' '.join(argv), flush=True)
    subprocess.run(argv, check=True, **kw)


def digest(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sagit', type=Path, default=REPO.parent / 'upstream/ios_sagit')
    p.add_argument('--root', type=Path, default=DEFAULT_ROOT)
    p.add_argument('--meson', type=Path, required=True, help='Existing meson executable; no installation')
    p.add_argument('--llvm-config', type=Path, default=Path('/opt/homebrew/opt/llvm/bin/llvm-config'))
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--fetch', action='store_true', help='Explicitly fetch the pinned Mesa source if absent')
    a = p.parse_args()
    if not a.root.resolve().is_relative_to(Path('/Users/dinnerabc/unixnu-p21')):
        p.error('build root must be on the isolated local disk')
    if subprocess.check_output(['git', '-C', str(a.sagit), 'rev-parse', 'HEAD'], text=True).strip() != PIN:
        p.error('sagit revision does not match the reviewed pin')
    if subprocess.check_output([a.llvm_config, '--version'], text=True).split('.')[0] != '23':
        p.error('LLVM 23 required')
    run([a.meson, '--version'])
    recipe = a.sagit / 'tools/project/build/gpu.py'
    spec = json.loads((a.sagit / 'tools/gpu-compiler/Source.json').read_text())
    root = a.root / 'obj/gpu-compiler'
    mesa = root / 'mesa'
    root.mkdir(parents=True, exist_ok=True)
    if not (mesa / '.git').exists():
        if not a.fetch:
            p.error('pinned Mesa absent; supply --fetch to authorize source download')
        run(['git', 'clone', '--depth', '1', '--filter=blob:none', '--sparse', '--branch', spec['tag'], spec['repository'], mesa])
        run(['git', '-C', mesa, 'sparse-checkout', 'set', *spec['sparse_paths']])
    if subprocess.check_output(['git', '-C', str(mesa), 'rev-parse', 'HEAD'], text=True).strip() != spec['commit']:
        p.error('Mesa revision mismatch')
    # Execute the pinned source-adaptation blocks directly, not a copied fork.
    # The blocks operate only on our isolated Mesa tree. Skip all automatic
    # dependencies, host Gallium/DRM build and iOS18.6 cross-build sections.
    tree = ast.parse(recipe.read_text())
    functions = {n.name: n for n in tree.body if isinstance(n, ast.FunctionDef)}
    def block(name, stop):
        node = functions[name]
        lines = recipe.read_text().splitlines()
        body = '\n'.join(lines[node.lineno - 1:node.end_lineno])
        cut = body.index(stop) if stop else len(body)
        body = body[:cut]
        if name == 'prepare_gpu_compiler':
            begin = body.index("    venv = root / 'venv'")
            end = body.index('    # Preserve the upstream compiler', begin)
            body = body[:begin] + body[end:]
        exec(compile(body, str(recipe), 'exec'), namespace)
        return namespace[name]
    native = types.ModuleType('project.build.native')
    native.run, native.digest = run, digest
    native.host = lambda name: str(a.llvm_config) if name == 'llvm-config' else shutil.which(name) or name
    project = types.ModuleType('project'); build = types.ModuleType('project.build')
    build.native = native
    sys.modules.update({'project': project, 'project.build': build, 'project.build.native': native})
    state = types.SimpleNamespace(BUILD=a.root, TOOLS=a.sagit / 'tools', ROOT=a.sagit, DRIVERS_ROOT=a.sagit / 'drivers', JOBS=a.jobs)
    namespace = dict(hashlib=hashlib, json=json, os=os, Path=Path, re=__import__('re'), shutil=shutil,
                     shlex=__import__('shlex'), subprocess=subprocess, sys=sys, build_state=state)
    block('patch_gpu_register_delays', None)
    block('prepare_gpu_compiler', "    obj = root / 'host'")()
    block('prepare_gpu_jit_compiler', "    cross = root / 'ios.ini'")(mesa)
    # Host uses LLVM23 from the explicitly selected installation, never Apple's
    # system LLVM or the upstream iOS18.6 ABI/private Metal classes.
    llvm_lib = subprocess.check_output([a.llvm_config, '--libdir'], text=True).strip()
    mp = mesa / 'src/freedreno/meson.build'
    text = mp.read_text().replace("cpp.find_library('LLVM')", "cpp.find_library('LLVM', dirs: ['" + llvm_lib + "'])")
    text = text.replace("['-Wl,-install_name,@loader_path/QcomGPUCompiler.dylib', '-Wl,-adhoc_codesign']", "['-Wl,-rpath," + llvm_lib + "']")
    mp.write_text(text)
    host = root / 'host'
    env = dict(os.environ, PATH=str(a.meson.parent) + os.pathsep + '/opt/homebrew/opt/bison/bin:' + os.environ['PATH'])
    if not (host / 'build.ninja').exists():
        run([a.meson, 'setup', host, mesa, '--buildtype=release', '--wrap-mode=nofallback',
             '-Dgallium-drivers=', '-Dfreedreno-kmds=', '-Dtools=freedreno', '-Dplatforms=', '-Dvulkan-drivers=',
             '-Dllvm=disabled', '-Dglx=disabled', '-Degl=disabled', '-Dgles1=disabled', '-Dgles2=disabled',
             '-Dgbm=disabled', '-Dglvnd=disabled', '-Dgallium-va=disabled', '-Dgallium-vdpau=disabled',
             '-Dgallium-xa=disabled', '-Dbuild-tests=false', '-Dlibunwind=disabled', '-Dshader-cache=disabled',
             '-Dvalgrind=disabled', '-Dzstd=disabled', '-Dexpat=disabled'], env=env)
    run(['ninja', '-C', host, '-j', a.jobs, 'src/freedreno/QcomGPUCompiler.dylib'], env=env)
    binary = a.root / 'native540-compiler'
    lib = host / 'src/freedreno/QcomGPUCompiler.dylib'
    run(['clang++', '-std=c++17', '-O2', '-I' + str(a.sagit / 'tools/gpu-compiler'), '-I' + str(a.sagit / 'drivers/include'),
         REPO / 'tools/gpu-native540/main.cpp', lib, '-Wl,-rpath,' + str(lib.parent), '-Wl,-rpath,' + llvm_lib, '-o', binary])
    provenance = {'sagit': PIN, 'mesa': spec['commit'], 'recipe_sha256': digest(recipe),
                  'builder_sha256': digest(__file__), 'wrapper_source_sha256': digest(REPO / 'tools/gpu-native540/main.cpp'),
                  'adapter_sources': {str(p.relative_to(a.sagit)): digest(p) for p in sorted((a.sagit / 'tools/gpu-compiler').glob('*')) if p.is_file()},
                  'compiler_sha256': digest(binary), 'backend_sha256': digest(lib), 'gpu': 540,
                  'platform': 'macOS host, offline only', 'root': str(a.root)}
    (a.root / 'build.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print(json.dumps(provenance, indent=2))


if __name__ == '__main__':
    main()
