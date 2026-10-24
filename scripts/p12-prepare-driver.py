from pathlib import Path
import shutil,json,hashlib
r=Path(__file__).resolve().parents[1];d=r/'drivers/p12/NativeUfs';d.mkdir(parents=True,exist_ok=True);u=r/'.work/p4-unixnu/drivers/NativeUfs'
for p in (r/'drivers/p11/NativeUfs').glob('*.h'):shutil.copy2(p,d/p.name)
for n in ['RootLayout.h','RootWindowRead.h','RootDiagnostics.h','RootPublication.h']:shutil.copy2(u/n,d/n)
s=(r/'drivers/p11/NativeUfs/NativeUfs.cpp').read_text();s=s.replace('#include "ReadPolicy.h"','''#include <libkern/crypto/sha2.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSIterator.h>
#include <IOKit/IOBSD.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <sys/param.h>
#include <kern/thread.h>
#include "verified_root.h"
#include "VerifiedBlockPolicy.h"
#define NATIVE_VERIFIED_BLOCK 1
extern "C" { extern const struct vr_crypto vr_xnu_crypto; extern const uint8_t verified_root_key[]; }
#include "ReadPolicy.h"''')
s=s.replace('bool poisoned=false,installed=false;', 'UInt64 primaryLast=0;bool primary4k=false;\n bool poisoned=false,installed=false;')
s=s.replace('bool start(IOService*) override;','bool prepareRootWindows();\n IOReturn readRaw4k(UInt8,UInt64,UInt32,void*);\n bool start(IOService*) override;')
s=s.replace('capacities[capacityCount++]={lun,last,size};','capacities[capacityCount++]={lun,last,size};\n  if(lun==0&&size==4096){primaryLast=last;primary4k=true;}')
s=s.replace(' unsigned found=0;\n for(unsigned i=0;', ' if(primary4k)prepareRootWindows();\n unsigned found=0;\n for(unsigned i=0;')
start=s.index('extern "C" {\nextern kmod_info_t g_kernel_kmod_info;');original=(u/'NativeUfs.cpp').read_text()
block=original[original.index('class NativeUfsBlock :'):original.index('// Memory snapshot (native-ufs-probe=13)')]
block=block.replace('bool configure (const char *name, uint64_t absoluteOffset, uint64_t size);','uint8_t expectedImageHash[32] = {};\n  bool configure (const char *name, uint64_t absoluteOffset, uint64_t size, const uint8_t digest[32]);')
block=block.replace('uint64_t absoluteOffset, uint64_t size)\n{','uint64_t absoluteOffset, uint64_t size, const uint8_t digest[32])\n{\n  memcpy(expectedImageHash,digest,32);')
block=block.replace('return status == VR_OK && verified.image_bytes == bytes;','return status == VR_OK && verified.image_bytes == bytes && memcmp(verified.image_sha256,expectedImageHash,32)==0;')
block=block.replace('if (boot_progress_poll) boot_progress_poll ();','// No optional progress callback in this device adapter.')
method=original[original.index('bool NativeUfs::startRoot (IOService *provider)'):original.index('\nvoid NativeUfs::stop (IOService *provider)',original.index('bool NativeUfs::startRoot'))]
a=method.index('  RootLayout *layout');method='bool NativeUfs::prepareRootWindows ()\n{\n  if(gRootHost && gRootHost!=this)return false;\n  gRootHost=this;const char *why="";\n'+method[a:]
method=method.replace('  native_ufs_root_step = 3;','')
a=method.index('  gStoreGeometry =');b=method.index('  const char *names',a);method=method[:a]+method[b:]
a=method.index('  if (!IOService::start (provider))');b=method.index('  // Prepare all windows',a);method=method[:a]+method[b:]
method=method.replace('layout->window[index].size))','layout->window[index].size,layout->window[index].sha256))')
method=method.replace('    IOService::stop (provider);','')
pre='''
#include "RootDiagnostics.h"
static UfsRootDiagnostics gRootDiagnostic;
static bool gRootDiagnosticEnabled=false;
static const char *native_ufs_root_failure=nullptr;
static NativeUfs *gRootHost=nullptr;
static IOReturn hostRead4k(uint8_t lun,uint64_t lba,uint32_t blocks,void *out){
 return gRootHost?gRootHost->readRaw4k(lun,lba,blocks,out):kIOReturnNotReady;
}
#include "RootLayout.h"
#include "RootWindowRead.h"
#include "RootPublication.h"
IOReturn NativeUfs::readRaw4k(UInt8 lun,UInt64 lba,UInt32 count,void *out){
 if(lun||!primary4k||!out||count>kDataMax/4096||!p8BlockRange(lba,count,primaryLast,4096))return kIOReturnBadArgument;
 UInt8 cdb[16]={};if(!p11Read10(cdb,lba,count))return kIOReturnUnsupported;
 IOLockLock(lock);bool ok=command(0,cdb,count*4096,out);IOLockUnlock(lock);
 return ok?kIOReturnSuccess:kIOReturnIOError;
}
'''
# The class registration of all three classes remains in this one translation unit.
s=s[:start]+pre+block+method+s[start:]
(d/'NativeUfs.cpp').write_text(s)
(d/'VerifiedBlockPolicy.h').write_text('''#pragma once
#include <stdint.h>
struct NativeVerifiedBlockPolicy { const uint8_t *manifest; uint64_t manifestBytes; };
extern "C" { extern const NativeVerifiedBlockPolicy verified_root_policies[]; extern const uint64_t verified_root_policy_count; }
''')
(o:=r/'out/p12/driver').mkdir(exist_ok=True)
(o/'source-provenance.json').write_text(json.dumps({'p11_source_sha256':hashlib.sha256((r/'drivers/p11/NativeUfs/NativeUfs.cpp').read_bytes()).hexdigest(),'official_root_source_sha256':hashlib.sha256((u/'NativeUfs.cpp').read_bytes()).hexdigest(),'adapter_sha256':hashlib.sha256(s.encode()).hexdigest(),'root_only_no_sm8550_mmio_copied':True,'persistent_store_code_excluded':True,'all_root_reads_require_verified_blocks':True},indent=2)+'\n')
print('P12 adapter generated: P11 transport plus fixed production root parser/window/publisher; verification mandatory')
