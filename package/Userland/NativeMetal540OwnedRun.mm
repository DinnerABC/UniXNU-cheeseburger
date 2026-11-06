// SPDX-License-Identifier: GPL-2.0-only
#include "NativeMetal540OwnedRun.h"
#import "NativeMetal540Objects.h"
#import <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <atomic>
#include <stdio.h>
#include <string.h>
@interface OwnedContext : NSObject {
@public io_connect_t connection;std::atomic<bool> retired;
}
@end
@implementation OwnedContext
- (instancetype)init {if((self=[super init])){connection=0;retired=false;}return self;}
@end
static void ownedRecord(OwnedContext *c,const char *text){
 if(!c->connection)return;
 NativeAdrenoAbiMetadata m={};m.handle=1;m.bytes=(uint32_t)strnlen(text,sizeof(m.text)-1);
 memcpy(m.text,text,m.bytes);IOConnectCallStructMethod(c->connection,NativeAdrenoMethodSetMetadata,&m,sizeof(m),nullptr,nullptr);
}
int NativeMetal540RunOwnedObjects(unsigned service){
 @autoreleasepool {
  OwnedContext *context=[OwnedContext new];
  kern_return_t opened=IOServiceOpen((io_service_t)service,mach_task_self(),0x4147,&context->connection);
  if(opened||!context->connection)return opened?int(opened):-5440;
  Native540ObjectsTransport t;t.context=(__bridge void *)context;t.connectionOwner=context;t.verifiedCPUInhibit=true;
  t.call=[](void *opaque,uint32_t selector,const void *in,size_t bytes,void *out,size_t *n)->int{
   auto *c=(__bridge OwnedContext *)opaque;return int(IOConnectCallStructMethod(c->connection,selector,in,bytes,out,n));
  };
  t.mapView=[](void *opaque,uint64_t token,void **address,size_t *bytes,id __autoreleasing *owner)->int{
   auto *c=(__bridge OwnedContext *)opaque;mach_vm_address_t va=0;mach_vm_size_t size=0;
   kern_return_t rc=IOConnectMapMemory64(c->connection,(uint32_t)token,mach_task_self(),&va,&size,kIOMapAnywhere);
   *address=(void *)(uintptr_t)va;*bytes=(size_t)size;*owner=rc?nil:[NSObject new];return int(rc);
  };
  t.flush=[](void *,void *,size_t,bool)->int{__asm__ volatile("dsb sy":::"memory");return 0;};
  t.retireConnection=[](void *opaque,bool,NSArray *){
   auto *c=(__bridge OwnedContext *)opaque;if(!c->retired.exchange(true)&&c->connection){IOServiceClose(c->connection);c->connection=0;}
  };
  __block int result=-5441;
  @autoreleasepool {
   NSError *error=nil;Native540Device *device=[[Native540Device alloc] initControlledWithTransport:t error:&error];
   if(!device){ownedRecord(context,"M540 device-error");return -5442;}
   NSData *metallib=[NSData dataWithContentsOfFile:@"/System/Library/Frameworks/MLCompute.framework/default.metallib"];
   if(!metallib){ownedRecord(context,"M540 metallib-missing");return -5443;}
   dispatch_data_t data=dispatch_data_create(metallib.bytes,metallib.length,nullptr,DISPATCH_DATA_DESTRUCTOR_DEFAULT);
   Native540Library *library=[device newLibraryWithData:data error:&error];
   Native540Function *function=[library newFunctionWithName:@"padding_gradient"];
   Native540ComputePipeline *pipeline=[device newComputePipelineStateWithFunction:function error:&error];
   if(!library||!function||!pipeline){ownedRecord(context,"M540 library-pipeline-error");return -5444;}
   Native540Buffer *input=[device newBufferWithLength:3120 options:MTLResourceStorageModeShared];
   Native540Buffer *output=[device newBufferWithLength:1024 options:MTLResourceStorageModeShared];
   Native540Buffer *params=[device newBufferWithLength:48 options:MTLResourceStorageModeShared];
   if(!input||!output||!params){ownedRecord(context,"M540 buffer-error");return -5445;}
   auto *src=(volatile uint32_t *)input.contents;auto *dst=(volatile uint32_t *)output.contents;
   for(unsigned i=0;i<780;++i)src[i]=0x3f000000u|((i*0x0019660du+0x00034567u)&0x007fffffu);
   for(unsigned i=0;i<256;++i)dst[i]=0x13579bdf;
   const uint32_t paramWords[]={0,260,3,1,256,1,1,2,2,1,1,0};memcpy(params.contents,paramWords,sizeof paramWords);
   Native540CommandQueue *queue=[device newCommandQueue];Native540CommandBuffer *command=[queue commandBuffer];
   Native540ComputeEncoder *encoder=[command computeCommandEncoder];
   [encoder setComputePipelineState:pipeline];[encoder setBuffer:input offset:0 atIndex:0];
   [encoder setBuffer:output offset:0 atIndex:1];[encoder setBuffer:params offset:0 atIndex:2];
   [encoder dispatchThreadgroups:MTLSizeMake(4,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];[encoder endEncoding];
   ownedRecord(context,"M540 prepare");[command commit];ownedRecord(context,"M540 waiting");[command waitUntilCompleted];
   if(command.error){char first[64]={};snprintf(first,sizeof first,"ME %s",command.error.localizedDescription.UTF8String);ownedRecord(context,first);}
   unsigned verified=0,inputVerified=0;
   if(command.status==MTLCommandBufferStatusCompleted&&!command.error){
    for(unsigned i=0;i<256;++i){const unsigned index=i+262;const uint32_t expected=0x3f000000u|((index*0x0019660du+0x00034567u)&0x007fffffu);verified+=dst[i]==expected;}
    for(unsigned i=0;i<780;++i)inputVerified+=src[i]==(0x3f000000u|((i*0x0019660du+0x00034567u)&0x007fffffu));
   }
   if(command.status==MTLCommandBufferStatusCompleted&&!command.error){
    char text[64]={};snprintf(text,sizeof text,"M540 status%lu v%u in%u px%x/%x",(unsigned long)command.status,verified,inputVerified,dst[0],dst[255]);
    ownedRecord(context,text);
   } else ownedRecord(context,"M540 failed; readback not sampled");
   result=verified==256&&inputVerified==780?0:-5446;
  }
  if(!context->retired.load()){ownedRecord(context,"M540 retirement-pending");return -5447;}
  return result;
 }
}
