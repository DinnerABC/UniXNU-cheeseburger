#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the pinned A540 compiler for21H461 using audited LLVM C imports."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mesa',type=Path,required=True)
    p.add_argument('--llvm-audit',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--meson',type=Path,required=True)
    a=p.parse_args()
    root=Path('/Users/dinnerabc/unixnu-p21').resolve()
    if not a.output.resolve().is_relative_to(root) or a.output.exists():
        p.error('Use a new isolated local build directory')
    audit=json.loads((a.llvm_audit/'report.json').read_text())
    if audit['result']!='TARGET_LLVM_EXPORTS_PASS' or audit['target']!='21H461' or audit['missing']:
        p.error('Actual target LLVM export gate required')
    revision=subprocess.check_output(['git','-C',str(a.mesa),'rev-parse','HEAD'],text=True).strip()
    if revision!='769e51468b49b2a42f0a0eaf71cf9eed5ff4e5de':
        p.error('Pinned Mesa24.3.4 required')
    env=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',
             PATH=str(a.meson.parent)+':/opt/homebrew/opt/bison/bin:'+os.environ['PATH'])
    sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True,env=env).strip()
    clang=subprocess.check_output(['xcrun','--find','clang'],text=True,env=env).strip()
    a.output.mkdir()
    mesa=a.output/'mesa'
    shutil.copytree(a.mesa,mesa,symlinks=True,ignore=shutil.ignore_patterns('.git','__pycache__'))
    recipe=mesa/'src/freedreno/meson.build'
    text=recipe.read_text()
    start=text.index('qcom_llvm = ')
    end=text.index('\n',start)
    text=text[:start]+"qcom_llvm = declare_dependency(link_args: ['"+str(a.llvm_audit/'libLLVM.tbd')+"'])"+text[end:]
    text=text.replace("link_args: host_machine.system() == 'darwin' ? ['-Wl,-rpath,/opt/homebrew/Cellar/llvm/23.1.3_2/lib'] : [],",
                      "link_args: ['-Wl,-install_name,@loader_path/QcomGPUCompiler.dylib'],")
    if '-rpath,/opt/homebrew' in text:
        p.error('Host LLVM cannot be linked into target compiler')
    recipe.write_text(text)
    target=[clang,'--target=arm64-apple-ios17.7','-isysroot',sdk]
    cross=a.output/'ios.ini'
    cross.write_text('[binaries]\nc = '+repr(target)+'\nobjc = '+repr(target)+'\ncpp = '+repr(target+['--driver-mode=g++','-stdlib=libc++'])+
        "\nar = '/usr/bin/ar'\nstrip = '/usr/bin/strip'\npkg-config = 'false'\n[host_machine]\nsystem = 'darwin'\ncpu_family = 'aarch64'\ncpu = 'arm64'\nendian = 'little'\n[properties]\nneeds_exe_wrapper = true\n[built-in options]\nc_args = ['-march=armv8-a+crc+crypto']\ncpp_args = ['-march=armv8-a+crc+crypto']\n")
    build=a.output/'build'
    commands=[ [str(a.meson),'setup',str(build),str(mesa),'--cross-file',str(cross),'--buildtype=release','--wrap-mode=nofallback',
        '-Dgallium-drivers=','-Dfreedreno-kmds=','-Dtools=freedreno','-Dplatforms=','-Dvulkan-drivers=',
        '-Dllvm=disabled','-Dglx=disabled','-Degl=disabled','-Dgles1=disabled','-Dgles2=disabled','-Dgbm=disabled',
        '-Dglvnd=disabled','-Dgallium-va=disabled','-Dgallium-vdpau=disabled','-Dgallium-xa=disabled',
        '-Dbuild-tests=false','-Dlibunwind=disabled','-Dshader-cache=disabled','-Dvalgrind=disabled','-Dzstd=disabled','-Dexpat=disabled'],
        ['/opt/homebrew/bin/ninja','-C',str(build),'-j','4','src/freedreno/QcomGPUCompiler.dylib'] ]
    for command in commands:
        print('+',' '.join(command),flush=True)
        subprocess.run(command,env=env,check=True)
    binary=build/'src/freedreno/QcomGPUCompiler.dylib'
    linkage=subprocess.check_output(['/usr/bin/otool','-L',str(binary)],text=True)
    if '/usr/lib/libLLVM.dylib' not in linkage or '/opt/homebrew' in linkage:
        p.error('Target compiler must link the actual iOS LLVM only')
    record=dict(result='TARGET_A540_COMPILER_BUILD_PASS',target='arm64-apple-ios17.7',mesa=revision,
        shader_runtime_semantics_verified=False,device_loaded=False,binary_sha256=sha(binary),
        llvm_image_uuid=audit['image_uuid'],llvm_audit_sha256=sha(a.llvm_audit/'report.json'),
        declaration_sha256=sha(a.llvm_audit/'libLLVM.tbd'),recipe_sha256=sha(recipe),linkage=linkage,
        source_changes=subprocess.check_output(['git','-C',str(a.mesa),'diff','--stat'],text=True))
    (a.output/'build.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))


if __name__=='__main__':
    main()
