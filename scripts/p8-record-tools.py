#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,sys
r=Path(__file__).resolve().parents[1];checks={
'macos':['sw_vers'],'host_arch':['uname','-m'],'memory':['sysctl','-n','hw.memsize'],'xcode':['xcodebuild','-version'],'sdk':['xcrun','--sdk','iphoneos','--show-sdk-version'],'xcode_clang':['/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang','--version'],'llvm':['/opt/homebrew/opt/llvm/bin/clang','--version'],'python':[sys.executable,'--version'],'iig':['xcrun','-f','iig']}
v={}
for key,cmd in checks.items():
 p=subprocess.run(cmd,capture_output=True,text=True);v[key]=dict(command=cmd,exit_code=p.returncode,stdout=p.stdout,stderr=p.stderr)
(r/'out/p8/toolchain.json').write_text(json.dumps(v,indent=2)+'\n');print(json.dumps(v,indent=2))
