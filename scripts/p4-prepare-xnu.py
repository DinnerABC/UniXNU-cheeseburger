#!/usr/bin/env python3
"""Apply only generic XNU build/4K patches and create a separate MSM8998 target."""
from pathlib import Path
import subprocess,json,hashlib,difflib
r=Path(__file__).resolve().parents[1];u=r/'.work/p4-unixnu';x=u/'.cache/xnu-work'
patches=['source-machine-layer','apple-only-cpu-tunables','cxx-flag-dependencies','disabled-deferred-reclaim','external-archive-dependencies','kernel-sdk-root','local-availability','native-4k-bootstrap-capacity','native-4k-signature-hash-cache','native-4k-user-abi','non-apple-cache-monitor','non-apple-iommu-bootstrap','non-apple-pmap-field','public-entitlements-header','published-freezer-condition']
records=[]
for name in patches:
 p=u/'patches/xnu'/(name+'.patch')
 applied=subprocess.run(['git','-C',str(x),'apply','--reverse','--check',str(p)],capture_output=True).returncode==0
 if not applied:subprocess.run(['git','-C',str(x),'apply',str(p)],check=True)
 records.append(dict(file=str(p.relative_to(r)),sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
for p in sorted((u/'kits/NativePrelink/patches').glob('*.patch')):
 if subprocess.run(['git','-C',str(x),'apply','--reverse','--check',str(p)],capture_output=True).returncode:
  subprocess.run(['git','-C',str(x),'apply',str(p)],check=True)
 records.append(dict(file=str(p.relative_to(r)),sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
# These are generic build policy/architecture flags, not borrowed physical addresses.
header='''/* MSM8998: generic ARMv8-A, P3-observed eight CPUs/two affinity groups. */
#ifndef _PEXPERT_ARM_MSM8998_H
#define _PEXPERT_ARM_MSM8998_H
#define NO_MONITOR 1
#define NO_ECORE 1
#define ARM_ARCH_TIMER 1
#define __ARM_ARCH__ 8
#define __ARM_VMSA__ 8
#define __ARM_VFP__ 4
#define __ARM_COHERENT_CACHE__ 1
#define __ARM_DEBUG__ 7
#define __ARM64_PMAP_SUBPAGE_L1__ 1
/* PMU/KPC disabled for initial bringup; zero is a policy, not detected capacity. */
#define CORE_NCTRS 0
#endif
'''
(x/'pexpert/pexpert/arm64/MSM8998.h').write_text(header)
p=x/'makedefs/MakeInc.def';s=p.read_text()
if 'MACHINE_FLAGS_ARM64_MSM8998' not in s:
 s=s.replace('SUPPORTED_ARM64_MACHINE_CONFIGS = BCM2837 ', 'SUPPORTED_ARM64_MACHINE_CONFIGS = MSM8998 BCM2837 ')
 s+='\nMACHINE_FLAGS_ARM64_MSM8998 = -DARM64_BOARD_CONFIG_MSM8998 -march=armv8-a\n';p.write_text(s)
p=x/'pexpert/pexpert/arm64/board_config.h';s=p.read_text()
if 'ARM64_BOARD_CONFIG_MSM8998' not in s:
 marker='#ifdef ARM64_BOARD_CONFIG_BCM2837'
 s=s.replace(marker,'#ifdef ARM64_BOARD_CONFIG_MSM8998\n#include <pexpert/arm64/MSM8998.h>\n#define MAX_L2_CLINE 6\n#define MAX_CPUS 8\n#define MAX_CPU_CLUSTERS 2\n#endif\n\n'+marker);p.write_text(s)
p=x/'config/MASTER.arm64.msm8998'
if not p.exists():
 s=(x/'config/MASTER.arm64.bcm2837').read_text().replace('arm64 config_pmap_ppl xsmall','arm64 xsmall')
 p.write_text('# MSM8998 initial bringup; common ARM64 kernel option template, no BCM peripherals.\n'+s)
p=x/'pexpert/pexpert/arm64/Makefile';text=p.read_text()
if 'MSM8998.h' not in text:p.write_text(text.replace('BCM2837.h','BCM2837.h MSM8998.h'))
p=x/'osfmk/arm64/proc_reg.h';text=p.read_text()
if 'defined (ARM64_BOARD_CONFIG_MSM8998)' not in text:p.write_text(text.replace('#elif defined (BCM2837) /* Raspberry Pi 3 */','#elif defined (ARM64_BOARD_CONFIG_MSM8998)\n/* P4 Android sysfs confirms 64-byte cache lines. */\n#define MMU_I_CLINE 6\n#define MMU_CLINE 6\n\n#elif defined (BCM2837) /* Raspberry Pi 3 */'))
p=x/'config/MASTER.arm64.msm8998';text=p.read_text()
if 'nos_arm_asm' not in text:p.write_text(text.replace('arm64 xsmall','arm64 config_pmap_ppl nos_arm_asm nos_arm_pmap xsmall'))
p=x/'config/MASTER.arm64.msm8998';p.write_text(p.read_text().replace(' sched_hygiene_debug',''))
# Public SDK exposes these existing entitlement constants under CoreEntitlements.
p=x/'bsd/kern/kern_exec.c';p.write_text(p.read_text().replace('<CodeSignature/Entitlements.h>','<CoreEntitlements/CoreEntitlements.h>'))
# Save both tracked and newly-added changes, making the adaptation reviewable.
patch=subprocess.check_output(['git','-C',str(x),'diff','--binary'],text=True)
for f in ['pexpert/pexpert/arm64/MSM8998.h','config/MASTER.arm64.msm8998']:
 content=(x/f).read_text();patch+=''.join(difflib.unified_diff([],content.splitlines(True),fromfile='/dev/null',tofile='b/'+f))
(r/'patches/p4/xnu-build-and-msm8998.patch').write_text(patch)
(r/'out/p4/xnu-patch-inputs.json').write_text(json.dumps(records,indent=2)+'\n')
print('Generic patches applied; independent MSM8998 config created; no physical addresses added')
