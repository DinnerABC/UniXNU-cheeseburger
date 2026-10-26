/* P17: no persistent Data store provider in this board build. See build-store-provider.py. */
#include <stdint.h>

unsigned native_ufs_store_mode (void) { return 0; }

int native_ufs_store_open (uint64_t blocks, uint64_t *firstLba, const char **why)
{
  if (why) *why = "no provider";
  return 0;
}

int native_ufs_store_read (uint64_t block, uint32_t blocks, void *dst) { return 0; }
int native_ufs_store_write (uint64_t block, uint32_t blocks, const void *src) { return 0; }
int native_ufs_store_sync (void) { return 0; }
/* GPT checks are now supplied by the bounded log backend; Data store stays off. */
