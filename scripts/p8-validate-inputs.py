#!/usr/bin/env python3
from pathlib import Path
import json,hashlib,subprocess
r=Path(__file__).resolve().parents[1]
expected={'.work/p4-unixnu/drivers/NativeUfs':'af785c1777161a62931425c2faaa536ce0bebd5b','.work/p4-unixnu/drivers/NativeRamBlock':'7194961a668e59018323b626be4fd7ce0a91e78f','.work/p4-unixnu/kits/ArmSmmu4K':'4d0d6598b1a36aeec3de01091893fa6660e49388','upstream/IOStorageFamily':'e45fbe30a7879e0b16e26dddb78538f1b00e0bff'}
for path,commit in expected.items():assert subprocess.check_output(['git','-C',str(r/path),'rev-parse','HEAD'],text=True).strip()==commit,path
pins={'out/p7/kernel/kernel.macho':'9ab8952643ec08de5464c0887ab1b2b616976e88c0e22bb10148167915195aaf','out/p7/adt/kernel.macho.adt':'2e3d21c3335dd10023917959736acea54a7b018e5dab7c5faa3b4fe8ffa8f1d4','out/p7/uefi/boot-p7-xnu.img':'5171c19816486446326b1d5bfb0c50bc9d11963b74ae4719111318932e7da455','out/p3/private/recovery-device.dtb':'743b9d0fee2a7aedb9db32dc57ddfb8197378b78ea003d8c653f492b7e5a2448'}
for p,sha in pins.items():assert hashlib.sha256((r/p).read_bytes()).hexdigest()==sha,p
(r/'out/p8/input-pins.json').write_text(json.dumps(dict(commits=expected,frozen_files=pins),indent=2)+'\n');print('Fixed source commits and P7/P3 input hashes verified')
