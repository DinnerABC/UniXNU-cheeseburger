from pathlib import Path
import difflib,hashlib,json
r=Path(__file__).resolve().parents[1];u=r/'.work/p4-unixnu';o=r/'out/p14';src=u/'drivers/NativeVirtioBlock/NativeVirtioBlock.cpp';original=src.read_text()
# Reuse fixed upstream query and UFS dispatch; omit unused VirtIO transport/class entirely.
query=original[original.index('static int verifiedVolumeQuery'):original.index('extern "C" int native_verified_block_volume_device')]
a=query.index('      NativeVirtioBlock *block');b=query.index('\n    }\n  }',a)
query=query[:a]+'''      int ufs = native_ufs_block_authenticates(provider, uuid->getCStringNoCopy(), manifestDigest);
      if (ufs >= 0) {
        authenticated = ufs == 1;
        native_verified_block_root_uuid_match = authenticated;
        char wrong[37];strlcpy(wrong,uuid->getCStringNoCopy(),sizeof(wrong));wrong[0] ^= 1;
        native_verified_block_wrong_uuid_refused = native_ufs_block_authenticates(provider,wrong,nullptr)==0;
        authenticated = authenticated && native_verified_block_wrong_uuid_refused;
        break;
      }'''+query[b:]
query=query.replace('return verifiedVolumeQuery(device, nullptr, nullptr);','return verifiedVolumeQuery(device, "04769D26-2CA0-4825-B443-E2E5F88300F2", nullptr);')
# Exact root UUID comes from the existing independently signed OS policy, not a guessed identity.
assert json.loads((r/'out/p12/policies/OS/verification.json').read_text())['volume_uuid']=='04769D26-2CA0-4825-B443-E2E5F88300F2'
header='''// SPDX-License-Identifier: BSD-2-Clause-Patent
// Derived from fixed NativeVirtioBlock 98909eb9 query. UFS-only adapter.
#include <IOKit/IOLib.h>
#include <IOKit/IOBSD.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <libkern/crypto/sha2.h>
#include <sys/types.h>
#include <sys/param.h>
extern "C" {
int native_ufs_block_authenticates(IOService *,const char *,uint8_t *);
volatile uint32_t native_verified_block_root_device=0;
volatile uint32_t native_verified_block_root_media_matches=0;
volatile uint32_t native_verified_block_root_provider_depth=0;
volatile uint32_t native_verified_block_root_uuid_match=0;
volatile uint32_t native_verified_block_wrong_uuid_refused=0;
char native_verified_block_root_uuid[37]={};
}
'''
(r/'drivers/p14/RootAuth/RootAuth.cpp').write_text(header+query)
p=u/'.cache/xnu-work/bsd/kern/bsd_init.c';old=p.read_text();assert 'native_owner_root_checkpoint' not in old
start=old.index('\t\t\tif (autherr) {',old.index('int autherr = VNOP_IOCTL(rootvnode, FSIOC_KERNEL_ROOTAUTH'))
end=old.index('\n\t\t}\n\t}',start)
new='''
#define XNU_OWNER_ROOT_AUDIT 1
#define XNU_OWNER_ROOT_TRUST 1
            extern int native_verified_block_root_authorized(dev_t);
            extern volatile unsigned native_verified_block_root_device, native_verified_block_root_media_matches;
            extern volatile unsigned native_verified_block_root_provider_depth, native_verified_block_root_uuid_match;
            extern char native_verified_block_root_uuid[37];
            extern volatile int native_owner_root_apple_autherr, native_owner_root_decision, native_owner_root_readonly;
            extern volatile int native_owner_root_accepted, native_owner_root_trust_enabled;
            extern volatile dev_t native_owner_root_mount_device, native_owner_root_selected_device;
            extern void native_owner_root_checkpoint(int,int,dev_t);
            extern void native_owner_root_accepted_checkpoint(int,int);
            mount_t root_mount = vnode_mount(rootvnode);
            vnode_t device_vnode = vfs_devvp(root_mount);
            dev_t device = device_vnode ? vnode_specrdev(device_vnode) : NODEV;
            int readonly = (vfs_flags(root_mount) & MNT_RDONLY) != 0;
            int verified = readonly && device_vnode && device == rootdev && native_verified_block_root_authorized(device);
            if (device_vnode) vnode_put(device_vnode);
            int owner_authorized = p14RootDecision(readonly,(uint32_t)device,(uint32_t)rootdev,(uint32_t)NODEV,
                native_verified_block_root_media_matches,verified);
            native_owner_root_mount_device=device;native_owner_root_selected_device=rootdev;
            native_owner_root_apple_autherr=autherr;native_owner_root_decision=owner_authorized;
            native_owner_root_readonly=readonly;
            native_owner_root_checkpoint(autherr,owner_authorized,device);
            printf("P14 rootauth apple=%d owner=%d ro=%d mount=%x selected=%x verified=%x\\n",
                autherr,owner_authorized,readonly,(unsigned)device,(unsigned)rootdev,native_verified_block_root_device);
            printf("P14 rootauth matches=%u depth=%u uuid=%s match=%u\\n",native_verified_block_root_media_matches,
                native_verified_block_root_provider_depth,native_verified_block_root_uuid,native_verified_block_root_uuid_match);
            if (autherr && !owner_authorized) {
                panic("rootvp not authenticated after mounting; P14 apple=%d owner=%d ro=%d mount=%x selected=%x verified=%x matches=%u depth=%u uuid=%s",
                    autherr,owner_authorized,readonly,(unsigned)device,(unsigned)rootdev,native_verified_block_root_device,
                    native_verified_block_root_media_matches,native_verified_block_root_provider_depth,native_verified_block_root_uuid);
            }
            native_owner_root_accepted=owner_authorized;
            native_owner_root_accepted_checkpoint(autherr,owner_authorized);
            printf("P14 root authentication accepted: apple=%d owner=%d\\n",autherr,owner_authorized);'''
s=old[:start]+new+old[end:]
s=s.replace('#include <sys/param.h>','#include <sys/param.h>\n#include "'+str(r/'drivers/p14/RootAuth/RootDecision.h')+'"',1)
assert 'p14RootDecision' in s and 'RootDecision.h' in s
s+='''
/* P14: official owner-root checkpoint ABI; Apple result remains independent. */
volatile int native_owner_root_trust_enabled=1;
volatile int native_owner_root_accepted=0;
volatile int native_owner_root_apple_autherr=-1,native_owner_root_decision=-1,native_owner_root_readonly=-1;
volatile dev_t native_owner_root_mount_device=NODEV,native_owner_root_selected_device=NODEV;
void native_owner_root_checkpoint(int,int,dev_t);
void native_owner_root_accepted_checkpoint(int,int);
__attribute__((noinline)) void native_owner_root_checkpoint(int a,int o,dev_t d){__asm__ volatile(""::"r"(a),"r"(o),"r"(d):"memory");}
__attribute__((noinline)) void native_owner_root_accepted_checkpoint(int a,int o){__asm__ volatile(""::"r"(a),"r"(o):"memory");}
'''
p.write_text(s);(r/'patches/p14/owner-root-binding.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='a/bsd/kern/bsd_init.c',tofile='b/bsd/kern/bsd_init.c')))
(o/'source-provenance.json').write_text(json.dumps(dict(upstream_commit='98909eb9198a4360d7a43be831887cb8b74d814d',upstream_query_sha256=hashlib.sha256(original.encode()).hexdigest(),device_query_sha256=hashlib.sha256((header+query).encode()).hexdigest(),official_owner_patch_reference='offline-owner-root-binding-audit.patch',adaptations=['UFS-only provider dispatch; no VirtIO transport','pinned signed OS UUID for root query','mounted dev must equal selected rootdev','wrong UUID must be refused by real UFS provider','nonzero Apple autherr retained, separately authorized owner trust'],apple_autherr_modified=False,ufs_source_changed=False),indent=2)+'\n')
print('P14 UFS owner identity bridge and scoped root enforcement prepared')
