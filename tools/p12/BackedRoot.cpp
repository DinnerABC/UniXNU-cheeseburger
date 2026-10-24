#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/sha.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <vector>
#include <fstream>
#include <mutex>
#include "verified_root.h"
using IOReturn=uint32_t;
constexpr IOReturn kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnIOError=2,kIOReturnNotPrivileged=3;
constexpr unsigned kDataMax=131072;
#include "RootDiagnostics.h"
static UfsRootDiagnostics gRootDiagnostic;
static std::vector<uint8_t> gpt;
static FILE *image;
static constexpr uint64_t first=2441472;
static IOReturn hostRead4k(uint8_t lun,uint64_t lba,uint32_t n,void *out){
 if(lun)return 2;
 if(lba*4096<gpt.size()&&uint64_t(n)*4096<=gpt.size()-lba*4096){memcpy(out,gpt.data()+lba*4096,n*4096);return 0;}
 if(lba<first)return 2;
 if(fseeko(image,(lba-first)*4096,SEEK_SET))return 2;
 return fread(out,4096,n,image)==n?0:2;
}
#include "RootLayout.h"
#include "RootWindowRead.h"
extern "C" int vr_host_open(const uint8_t*,size_t,const uint8_t*,uint64_t,vr_view*);
extern "C" int vr_host_block(const vr_view*,uint64_t,const void*,size_t);
static std::vector<uint8_t> load(const char *p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
struct Ops {
 const vr_view *view;std::vector<uint8_t> *out;bool tamper;std::mutex mutex;
 void lock(){mutex.lock();}void unlock(){mutex.unlock();}
 IOReturn read(uint64_t lba,uint32_t blocks,void *data){auto s=hostRead4k(0,lba,blocks,data);if(!s&&tamper)((uint8_t*)data)[0]^=1;return s;}
 IOReturn verify(uint64_t at,const void *data,size_t n){return vr_host_block(view,at,data,n)==VR_OK?kIOReturnSuccess:kIOReturnNotPrivileged;}
 uint64_t copy(uint64_t off,const void *data,uint64_t n){assert(off+n<=out->size());memcpy(out->data()+off,data,n);return n;}
};
int main(int argc,char **argv){
 if(argc!=6)return 2;
 gpt=load(argv[1]);image=fopen(argv[2],"rb");assert(image);
 RootLayout layout;const char *why="";assert(findRootLayout(&layout,&why)&&layout.manifestStatus==0&&layout.windows==2);
 auto envelope=load(argv[3]),key=load(argv[4]);assert(key.size()==VR_KEY_BYTES);
 vr_view view={};assert(vr_host_open(envelope.data(),envelope.size(),key.data(),1,&view)==VR_OK);
 const RootWindow *w=nullptr;for(unsigned i=0;i<layout.windows;i++)if(!strcmp(layout.window[i].name,argv[5]))w=&layout.window[i];
 assert(w&&w->size==view.image_bytes&&!memcmp(w->sha256,view.image_sha256,32));
 const uint64_t absolute=layout.userdataFirst*4096+w->offset;
 uint8_t bounce[kDataMax];
 for(uint64_t block: {uint64_t(0),uint64_t(1),view.image_bytes/512-1}){
  uint64_t count=block==view.image_bytes/512-1?1:15;std::vector<uint8_t> out(count*512,0xa5),expected(count*512);Ops ops{&view,&out,false,{}};
  auto result=ufsReadWindow(ops,absolute,w->size,block,count,bounce);assert(!result.status&&result.done==count*512);
  assert(!fseeko(image,w->offset+block*512,SEEK_SET));assert(fread(expected.data(),1,expected.size(),image)==expected.size());assert(out==expected);
 }
 std::vector<uint8_t> out(512,0xa5);Ops bad{&view,&out,true,{}};auto fail=ufsReadWindow(bad,absolute,w->size,0,1,bounce);assert(fail.status==3&&!fail.done);for(auto b:out)assert(b==0xa5);
 auto bounds=ufsReadWindow(bad,absolute,w->size,w->size/512,1,bounce);assert(bounds.status==1&&!bounds.done);
 fclose(image);printf("PASS real P3 GPT + SHNGROOT + signed %s policy + verified unaligned/tail reads; tampering rejected before copy; host only\n",argv[5]);
}
