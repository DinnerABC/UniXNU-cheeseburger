#include <stdio.h>
#include <stdlib.h>
#include "ArmSmmu4KLive.h"
int main(void){
 void *p=aligned_alloc(4096,128*4096); if(!p)return 1;
 ArmSmmu4KLiveState s={0}; ArmSmmu4KLiveArena a={{32,36,0x10000000,8192},0,0x8000000,0,p,128,0,0};
 if(ArmSmmu4KLivePrepare(&a,&s)!=ArmSmmu4KBudget)return 2;
 a.config.leaf_budget=32768; a.capacity_pages=32;
 if(ArmSmmu4KLivePrepare(&a,&s)!=ArmSmmu4KCapacity)return 3;
 a.capacity_pages=128;
 if(ArmSmmu4KLivePrepare(&a,&s)!=ArmSmmu4KSuccess||s.tables.table_pages!=66)return 4;
 printf("PASS 128MiB budget=32768 capacity=128 used=%zu\n",s.tables.table_pages); free(p);return 0;
}
