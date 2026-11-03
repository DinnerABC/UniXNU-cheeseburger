/* SPDX-License-Identifier: MIT */
#include "Library.h"
#include <CommonCrypto/CommonDigest.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static unsigned positive,negative,hash_calls;
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;i++) p[i]=v>>(i*8); }
static void put64(uint8_t *p,uint64_t v) { put32(p,v);put32(p+4,v>>32); }
static void big32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;i++) p[3-i]=v>>(i*8); }
static int digest(const uint8_t *p,size_t n,uint8_t out[32],void *ctx) {
    hash_calls++;if(ctx) return 1;
    assert(n<=UINT32_MAX);return CC_SHA256(p,(CC_LONG)n,out)?0:1;
}
static size_t tag(uint8_t *p,size_t at,const char *name,const uint8_t *value,size_t n) {
    memcpy(p+at,name,4);p[at+4]=n;p[at+5]=n>>8;memcpy(p+at+6,value,n);return at+6+n;
}
static size_t fixture(uint8_t *p) {
    memset(p,0,512);
    const uint8_t magic[]={0x4d,0x54,0x4c,0x42,1,0,2,0,7,0,0,0x82,0x11,0,7,0};memcpy(p,magic,16);
    uint8_t type=0,hash[32],offsets[24]={0},module_size[8]={0};
    put32(p+256,0x0b17c0de);put32(p+264,20);put32(p+268,12);put32(p+272,0xffffffff);put32(p+276,0xdec04342);
    digest(p+256,32,hash,0);put64(module_size,32);
    size_t at=96;at=tag(p,at,"NAME",(const uint8_t *)"main",5);at=tag(p,at,"TYPE",&type,1);at=tag(p,at,"HASH",hash,32);
    at=tag(p,at,"OFFT",offsets,24);at=tag(p,at,"MDSZ",module_size,8);memcpy(p+at,"ENDT",4);at+=4;assert(at==200);
    put32(p+88,1);put32(p+92,108);
    put64(p+16,288);put64(p+24,88);put64(p+32,108);put64(p+40,248);put64(p+48,4);put64(p+56,252);put64(p+64,4);put64(p+72,256);put64(p+80,32);
    memcpy(p+200,"RLST\x10\0",6);put64(p+206,288);memcpy(p+222,"UUID\x10\0",6);memcpy(p+244,"ENDT",4);
    return 288;
}
static void refuse(const uint8_t *p,size_t n,air_library_sha256 hash,void *ctx) {
    struct air_library_view v,saved;memset(&v,0xa5,sizeof v);saved=v;
    uint32_t status=air_library_open(p,n,hash,ctx,&v);if(!status)fprintf(stderr,"Unexpected acceptance at negative %u\n",negative);assert(status && status<=AIR_LIBRARY_ALIAS && !memcmp(&v,&saved,sizeof v));negative++;
}
static void accept(const uint8_t *p,size_t n) {
    struct air_library_view v;struct air_library_function f;
    assert(!air_library_open(p,n,digest,0,&v) && v.count==1);
    assert(!air_library_get(&v,0,&f) && f.name_bytes==4 && !memcmp(f.name,"main",4) && f.stage==0 && f.bitcode_bytes==12 && f.wrapper_bytes==32);
    assert(f.bitcode>=p && f.bitcode+f.bitcode_bytes<=p+n);positive++;
    struct air_library_function saved;memset(&f,0xa5,sizeof f);saved=f;
    assert(air_library_get(&v,1,&f)==AIR_LIBRARY_INDEX && !memcmp(&f,&saved,sizeof f));negative++;
    assert(air_library_get(&v,UINT32_MAX,&f)==AIR_LIBRARY_INDEX && !memcmp(&f,&saved,sizeof f));negative++;
    v.reserved=1;assert(air_library_get(&v,0,&f)==AIR_LIBRARY_FORMAT && !memcmp(&f,&saved,sizeof f));negative++;
}
int main(int argc,char **argv) {
    uint8_t good[512],p[1024];size_t n=fixture(good);accept(good,n);
    for(size_t i=0;i<n;i++) refuse(good,i,digest,0);
    refuse(0,n,digest,0);refuse(good,n,0,0);refuse(good,n,digest,(void *)1);
    refuse(good,AIR_LIBRARY_MAX_BYTES+1,digest,0);refuse((const uint8_t *)(UINTPTR_MAX-1),8,digest,0);
    struct air_library_view v;assert(air_library_open(good,n,digest,0,(void *)(good+8))==AIR_LIBRARY_ALIAS);negative++;
    assert(!air_library_open(good,n,digest,0,&v));
    assert(air_library_get(&v,0,(void *)&v)==AIR_LIBRARY_ALIAS);negative++;
    assert(air_library_get(&v,0,(void *)good)==AIR_LIBRARY_ALIAS);negative++;
    const size_t fields[]={0,16,24,32,40,48,56,64,72,80,88,92,100,113,200,206,222,244,256,260,264,268,272,276};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++) { memcpy(p,good,n);p[fields[i]]^=0x80;refuse(p,n,digest,0); }
    memcpy(p,good,n);p[114]=3;refuse(p,n,digest,0); /* unsupported stage */
    memcpy(p,good,n);p[103]=0;refuse(p,n,digest,0); /* embedded NUL */
    memcpy(p,good,n);p[106]='x';refuse(p,n,digest,0); /* no final NUL */
    memcpy(p,good,n);memcpy(p+107,"NAME",4);refuse(p,n,digest,0); /* duplicate tag */
    memcpy(p,good,n);p[286]^=1;refuse(p,n,digest,0); /* whole wrapper, not only BC prefix */
    for(unsigned unaligned=1;unaligned<8;unaligned++) { memcpy(p+unaligned,good,n);accept(p+unaligned,n); }
    long page=sysconf(_SC_PAGESIZE);uint8_t *guard=mmap(0,(size_t)page*3,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(guard!=MAP_FAILED);
    assert(!mprotect(guard+page,(size_t)page,PROT_READ|PROT_WRITE));memcpy(guard+2*page-n,good,n);accept(guard+2*page-n,n);assert(!munmap(guard,(size_t)page*3));
    memset(p,0,sizeof p);big32(p,0xcafebabe);big32(p+4,2);big32(p+16,48);big32(p+20,16);big32(p+36,64);big32(p+40,(uint32_t)n);memcpy(p+64,good,n);accept(p,64+n);
    p[44]=1;refuse(p,64+n,digest,0);p[44]=0; /* oversized BE alignment */
    big32(p+36,60);refuse(p,64+n,digest,0);big32(p+36,64);
    big32(p+4,17);refuse(p,64+n,digest,0);big32(p+4,2);
    big32(p+20,(uint32_t)n);big32(p+36,48+(uint32_t)n);memcpy(p+48,good,n);memcpy(p+48+n,good,n);refuse(p,48+2*n,digest,0); /* ambiguous AIR slices */
    memcpy(p,good,200);memcpy(p+200,good+92,108);memcpy(p+308,good+200,88);
    put64(p+16,396);put64(p+32,216);put64(p+40,356);put64(p+56,360);put64(p+72,364);put64(p+314,396);put32(p+88,2);
    hash_calls=0;assert(!air_library_open(p,396,digest,0,&v) && v.count==2 && hash_calls==1);positive++;
    struct air_library_function duplicate;assert(!air_library_get(&v,1,&duplicate) && duplicate.wrapper_bytes==32);positive++;
    p[228]^=1;refuse(p,396,digest,0); /* repeated extent must have the same verified digest */
    uint32_t random=0x4a740;
    for(unsigned trial=0;trial<4096;trial++) {
        memcpy(p,good,n);for(unsigned j=0;j<8;j++){random=random*1664525u+1013904223u;p[random%n]^=random>>24;}
        struct air_library_view x,saved;memset(&x,0xa5,sizeof x);saved=x;
        uint32_t status=air_library_open(p,n,digest,0,&x);
        if(status) { assert(status<=AIR_LIBRARY_ALIAS && !memcmp(&x,&saved,sizeof x));negative++; }
        else { struct air_library_function f;assert(!air_library_get(&x,0,&f));positive++; }
    }
    for(int arg=1;arg<argc;arg++) {
        FILE *file=fopen(argv[arg],"rb");assert(file && !fseek(file,0,SEEK_END));long length=ftell(file);assert(length>0 && length<=AIR_LIBRARY_MAX_BYTES && !fseek(file,0,SEEK_SET));
        size_t pages=((size_t)length+(size_t)page-1)/(size_t)page*(size_t)page;
        uint8_t *map=mmap(0,pages+2*(size_t)page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(map!=MAP_FAILED && !mprotect(map+page,pages,PROT_READ|PROT_WRITE));
        uint8_t *bytes=map+page+pages-(size_t)length;assert(fread(bytes,1,(size_t)length,file)==(size_t)length && !fclose(file));assert(!mprotect(map+page,pages,PROT_READ));
        struct air_library_view original;assert(!air_library_open(bytes,(size_t)length,digest,0,&original));
        for(uint32_t i=0;i<original.count;i++) { struct air_library_function f;assert(!air_library_get(&original,i,&f) && f.index==i); }
        printf("PASS: %u original functions with readonly backing and a real final guard page\n",original.count);
        assert(!munmap(map,pages+2*(size_t)page));
    }
    printf("PASS: %u positive library profiles, %u transactional refusals, unaligned and guard-page bounds\n",positive,negative);
    return 0;
}
