#!/usr/bin/env python3
"""Create the UP1-based touch source tree; refuse to overwrite existing work."""
from pathlib import Path
import shutil, subprocess
R=Path(__file__).resolve().parents[3]
S=R/"out/p21/touch-up1/source"
if S.exists(): raise SystemExit("source already exists; preserve the existing build")
S.mkdir(parents=True)
a=subprocess.Popen(["git","archive","f89c28c9f83ec171ba7ee43018b0cd20766d9698"],cwd=R,stdout=subprocess.PIPE)
t=subprocess.run(["tar","-xf","-","-C",str(S)],stdin=a.stdout,check=True);a.stdout.close()
if a.wait(): raise SystemExit("git archive failed")
for f in ["package/BoardRuntime/CheeseburgerTouch.cpp","package/BoardRuntime/SynapticsRMI4.h","package/BoardRuntime/BootLog.c","package/BoardRuntime/CompactObserver.c","package/BoardRuntime/HidInputProbe.cpp","package/BoardRuntime/LegacyDisplayProbe.cpp","package/BoardRuntime/NativeHidInput.cpp","scripts/p21-build-board-runtime.py","package/BoardRuntime/DisplayObservation.c","scripts/p22-compile-r5.py","patches/xnu/0007-cheeseburger-eight-core-topology.patch","soc/msm8998/patches/components/ArmCpu-smp-bootstrap-controller.patch"]:
 d=S/f;d.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(R/f,d)
shutil.copy2(R/"local.toml",S/"local.toml")
for dest,src in {".work/p21-official-framework":".work/p21-official-framework",".work/p4-unixnu":".work/p4-unixnu",".work/p20-r11":".work/p20-r11","out/p11/storage-abi":"out/p11/storage-abi","out/p12/policies":"out/p12/policies","out/p21/official-adaptation/resolved/feeds":"out/p21/official-adaptation/resolved/feeds"}.items():
 d=S/dest;d.parent.mkdir(parents=True,exist_ok=True);d.symlink_to(R/src,target_is_directory=True)
w=S/"out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger";w.mkdir(parents=True,exist_ok=True)
shutil.copy2(R/"out/p21/official-adaptation/resolved/cheeseburger/ipados-17.7.11-21H461/cheeseburger/.config",w/".config")

series=S/"patches/xnu/series";series.write_text(series.read_text().rstrip()+"\n0007-cheeseburger-eight-core-topology.patch\n")
