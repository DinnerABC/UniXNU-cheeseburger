// SPDX-License-Identifier: BSD-2-Clause-Patent
// Derived from fixed NativeVirtioBlock 98909eb9 query. UFS-only adapter.
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
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
static int verifiedVolumeQuery(dev_t device, const char *expectedUUID, uint8_t *manifestDigest)
{
  native_verified_block_root_device = static_cast<uint32_t>(device);
  native_verified_block_root_media_matches = 0;
  native_verified_block_root_provider_depth = 0;
  native_verified_block_root_uuid_match = 0;
  native_verified_block_wrong_uuid_refused = 0;
  bzero(native_verified_block_root_uuid, sizeof(native_verified_block_root_uuid));
  OSDictionary *matching = IOService::serviceMatching("IOMedia");
  OSDictionary *properties = OSDictionary::withCapacity(2);
  OSNumber *mjr = OSNumber::withNumber(static_cast<unsigned>(major(static_cast<uint32_t>(device))), 32);
  OSNumber *mnr = OSNumber::withNumber(static_cast<unsigned>(minor(static_cast<uint32_t>(device))), 32);
  if (!matching || !properties || !mjr || !mnr) {
    OSSafeReleaseNULL(mnr); OSSafeReleaseNULL(mjr); OSSafeReleaseNULL(properties); OSSafeReleaseNULL(matching);
    return 0;
  }
  bool ready = properties->setObject(kIOBSDMajorKey, mjr) && properties->setObject(kIOBSDMinorKey, mnr) &&
               matching->setObject("IOPropertyMatch", properties);
  mnr->release(); mjr->release(); properties->release();
  OSIterator *iterator = ready ? IOService::getMatchingServices(matching) : nullptr;
  matching->release();
  if (!iterator) return 0;
  bool authenticated = false;
  unsigned matches = 0, depth = 0;
  while (OSObject *object = iterator->getNextObject()) {
    IOService *media = OSDynamicCast(IOService, object);
    if (!media) continue;
    if (++matches != 1) { authenticated = false; break; }
    OSString *uuid = OSDynamicCast(OSString, media->getProperty("UUID"));
    if (!uuid || uuid->getLength() != 36 ||
        (expectedUUID && strcmp(expectedUUID, uuid->getCStringNoCopy()))) continue;
    strlcpy(native_verified_block_root_uuid, uuid->getCStringNoCopy(), sizeof(native_verified_block_root_uuid));
    for (IOService *provider = media; provider && depth < 64;
         provider = provider->getProvider()) {
      ++depth;
      int ufs = native_ufs_block_authenticates(provider, uuid->getCStringNoCopy(), manifestDigest);
      if (ufs >= 0) {
        authenticated = ufs == 1;
        native_verified_block_root_uuid_match = authenticated;
        char wrong[37];strlcpy(wrong,uuid->getCStringNoCopy(),sizeof(wrong));wrong[0] ^= 1;
        native_verified_block_wrong_uuid_refused = native_ufs_block_authenticates(provider,wrong,nullptr)==0;
        authenticated = authenticated && native_verified_block_wrong_uuid_refused;
        break;
      }
    }
  }
  iterator->release();
  native_verified_block_root_media_matches = matches;
  native_verified_block_root_provider_depth = depth;
  if (!authenticated || matches != 1) {
    if (manifestDigest) bzero(manifestDigest, SHA384_DIGEST_LENGTH);
    return 0;
  }
  return 1;
}

extern "C" int native_verified_block_root_authorized(dev_t device)
{
  return verifiedVolumeQuery(device, "04769D26-2CA0-4825-B443-E2E5F88300F2", nullptr);
}

extern "C" int native_verified_block_manifest_sha384(dev_t device, const char *uuid, uint8_t digest[48])
{
  if (!digest) return 0;
  bzero(digest, SHA384_DIGEST_LENGTH);
  if (!uuid || strlen(uuid) != 36) return 0;
  return verifiedVolumeQuery(device, uuid, digest);
}

