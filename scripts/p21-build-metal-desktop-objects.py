#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the concrete desktop objects against the qualified iOS AIR compiler.

This is a reusable frontend library, not a system-device registration bypass.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

R=Path(__file__).resolve().parents[1]
S=R.parent/'upstream/ios_sagit'
PIN='673f85070fe9f7872793015eb230a5c3987d4a8c'


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler-build',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists() or not a.output.resolve().is_relative_to(Path('/Users/dinnerabc/unixnu-p21')):
        p.error('Use a new isolated local output')
    if subprocess.check_output(['git','-C',str(S),'rev-parse','HEAD'],text=True).strip()!=PIN:
        p.error('Pinned sagit sources required')
    report=json.loads((a.compiler_build/'build.json').read_text())
    compiler=a.compiler_build/'build/src/freedreno/QcomGPUCompiler.dylib'
    if report['result']!='TARGET_A540_COMPILER_BUILD_PASS' or sha(compiler)!=report['binary_sha256']:
        p.error('Actual target compiler identity mismatch')
    env=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
    sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True,env=env).strip()
    clang=subprocess.check_output(['xcrun','--find','clang++'],text=True,env=env).strip()
    a.output.mkdir();shutil.copy2(compiler,a.output/compiler.name)
    sources=[R/'package/Userland'/name for name in ('NativeMetal540DesktopObjects.mm','NativeMetal540DesktopTransport.mm','NativeMetal540DesktopLibrary.cpp',
        'NativeMetal540DesktopDraw.cpp','NativeMetal540DesktopSession.cpp','NativeMetal540Client.cpp')]
    binary=a.output/'NativeMetal540Desktop.dylib'
    command=[clang,'-std=c++17','-target','arm64-apple-ios17.7','-isysroot',sdk,'-O2','-fobjc-arc','-fblocks',
        '-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-dynamiclib','-Wl,-install_name,@loader_path/NativeMetal540Desktop.dylib',
        '-I'+str(R/'package/Userland'),'-I'+str(R/'package/BoardRuntime'),'-I'+str(S/'drivers/include'),
        '-I'+str(S/'tools/gpu-compiler'),'-I'+str(S/'drivers/gpu/QcomGPU'),*[str(x) for x in sources],str(a.output/compiler.name),
        '-framework','Foundation','-framework','Metal','-framework','IOSurface','-framework','IOKit','-o',str(binary)]
    subprocess.run(command,env=env,check=True)
    subprocess.run(['codesign','--force','--sign','-','--pagesize','4096',str(a.output/compiler.name)],env=env,check=True)
    subprocess.run(['codesign','--force','--sign','-','--pagesize','4096',str(binary)],env=env,check=True)
    for item in (binary,a.output/compiler.name):
        subprocess.run(['codesign','--verify','--strict',str(item)],check=True)
    linkage=subprocess.check_output(['otool','-L',str(binary)],text=True)
    if '/opt/homebrew' in linkage:
        p.error('Host LLVM must not enter product linkage')
    data=dict(result='DESKTOP_OBJECTS_TARGET_BUILD_PASS',sagit=PIN,target='arm64-ios17.7',
        sources={str(x.relative_to(R)):sha(x) for x in sources},libraries={x.name:sha(x) for x in (binary,a.output/compiler.name)},
        command=command,linkage=linkage,system_registered=False,device_loaded=False,desktop_takeover=False)
    (a.output/'build.json').write_text(json.dumps(data,indent=2)+'\n')
    print(json.dumps(data,indent=2))


if __name__=='__main__':
    main()
