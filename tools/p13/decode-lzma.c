/* Host harness for the exact fixed EDK2 SDK decoder, not a replacement codec. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "LzmaDec.h"
static void *alloc(ISzAllocPtr p,size_t n){(void)p;return malloc(n);}
static void release(ISzAllocPtr p,void *v){(void)p;free(v);}
int main(int argc,char **argv){
 if(argc!=3)return 2;FILE *f=fopen(argv[1],"rb");if(!f)return 3;
 fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<13||n>268435456)return 4;
 unsigned char *in=malloc(n);if(!in||fread(in,1,n,f)!=(size_t)n)return 5;fclose(f);
 uint64_t size=0;for(int i=0;i<8;i++)size|=(uint64_t)in[5+i]<<(i*8);
 if(size!=124993536)return 6;unsigned char *out=malloc(size);if(!out)return 7;
 SizeT decoded=size,encoded=n-13;ELzmaStatus status;ISzAlloc a={alloc,release};
 int rc=LzmaDecode(out,&decoded,in+13,&encoded,in,5,LZMA_FINISH_END,&status,&a);
 if(rc||decoded!=size||encoded!=(size_t)n-13||status!=LZMA_STATUS_FINISHED_WITH_MARK)return 8;
 f=fopen(argv[2],"wb");if(!f||fwrite(out,1,size,f)!=size)return 9;fclose(f);
 printf("fixed SDK decode PASS bytes=%llu consumed=%zu\n",(unsigned long long)size,encoded);free(in);free(out);return 0;
}
