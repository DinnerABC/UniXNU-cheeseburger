// SPDX-License-Identifier: GPL-2.0-only
#include "../package/BoardRuntime/CheeseburgerGpuCompute540.h"
#include <cassert>
#include <cstdio>
int main(){
 uint32_t words[2048]={};CheeseburgerGpu::Encoder540 e(words,2048);
 auto v=CheeseburgerGpu::ScanBand540::DefaultVariant();
 assert(CheeseburgerGpu::ScanBand540::encodeVariant(e,0x2300000,0x2407000,4096,64,1,0x2040000,v,CheeseburgerGpu::Probe540::Code));
 unsigned direct=0;
 for(unsigned i=0;i<e.size();){
  uint32_t h=words[i++];
  if((h>>28)==4){i+=(h&0x7f);continue;}
  assert((h>>28)==7);unsigned n=h&0x3fff,op=(h>>16)&0x7f;assert(i+n<=e.size());
  if(op==0x30&&n==35&&words[i]==((13u<<18)|(1u<<22))&&words[i+1]==0){
   for(unsigned j=0;j<32;++j)assert(words[i+3+j]==CheeseburgerGpu::Probe540::Code[j]);++direct;
  }
  i+=n;
 }
 assert(direct==1);std::printf("A540 direct ISA packet model PASS words=%u\n",e.size());
}
