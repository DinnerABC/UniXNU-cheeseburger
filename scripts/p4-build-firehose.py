#!/usr/bin/env python3
"""Build Apple's actual one-source libfirehose_kernel target using CLT, not xcodebuild."""
import subprocess,os,shutil,json,hashlib
from pathlib import Path
r=Path(__file__).resolve().parents[1];u=r/'.work/p4-unixnu';fake=u/'.cache/fakeroot';src=u/'refer/libdispatch';out=u/'build/firehose-clt';out.mkdir(parents=True,exist_ok=True)
def run(cmd):print(' '.join(str(x) for x in cmd),flush=True);subprocess.run([str(x) for x in cmd],check=True)
for source,target in [(u/'refer/libplatform/include',fake/'usr/local/include'),(u/'refer/libplatform/private',fake/'usr/local/include'),(u/'refer/libplatform/private/platform',fake/'System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders/platform')]:
 shutil.copytree(source,target,dirs_exist_ok=True)
headers=fake/'usr/local/include/os';headers.mkdir(parents=True,exist_ok=True)
for p in (src/'os').glob('*.h'):
 v=subprocess.run(['/opt/homebrew/opt/unifdef/bin/unifdef','-DKERNEL=1','-DOS_FIREHOSE_SPI=1','-DOS_VOUCHER_ACTIVITY_SPI_TYPES=1','-UOS_VOUCHER_ACTIVITY_SPI',str(p)],capture_output=True)
 if v.returncode not in [0,1]:raise RuntimeError('unifdef failed')
 (headers/p.name).write_bytes(v.stdout)
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
includes=[src,u/'.cache/xnu-work/libkern',u/'.cache/xnu-work/osfmk',u/'.cache/xnu-work/bsd',u/'.cache/xnu-work/pexpert',fake/'System/Library/Frameworks/Kernel.framework/PrivateHeaders',fake/'System/Library/Frameworks/Kernel.framework/Headers',fake/'usr/local/include',u/'.cache/xnu-work/EXTERNAL_HEADERS',Path(sdk)/'usr/include',u/'build/xnu/obj/EXPORT_HDRS/osfmk',u/'build/xnu/obj/EXPORT_HDRS/bsd',u/'build/xnu/obj/EXPORT_HDRS/pexpert',u/'build/xnu/obj/DEVELOPMENT_ARM64_MSM8998/osfmk',u/'build/xnu/obj/DEVELOPMENT_ARM64_MSM8998/bsd']
cmd=['/usr/bin/clang','-target','arm64-apple-ios17.0','-isysroot',sdk,'-c',src/'src/firehose/firehose_buffer.c','-o',out/'firehose_buffer.o','-Os','-mkernel','-nostdinc','-Wno-packed','-DKERNEL=1','-DDISPATCH_USE_DTRACE=0','-DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1','-DOS_ATOMIC_CONFIG_STARVATION_FREE_ONLY=0']
for p in includes:cmd+=['-I',p]
run(cmd)
lib=fake/'usr/local/lib/kernel/libfirehose_kernel.a';lib.parent.mkdir(parents=True,exist_ok=True);run(['/usr/bin/ar','rcs',lib,out/'firehose_buffer.o']);run(['xcrun','lipo','-info',lib])
(r/'out/p4/firehose-build.json').write_text(json.dumps(dict(command=[str(x) for x in cmd],exit_code=0,archive=str(lib.relative_to(r)),bytes=lib.stat().st_size,sha256=hashlib.sha256(lib.read_bytes()).hexdigest(),source_files=['src/firehose/firehose_buffer.c'],source_selection='Exact sole source of libfirehose_kernel PBXSourcesBuildPhase 6E040C5F1C499B1B00411A2E'),indent=2)+'\n')
