// SPDX-License-Identifier: GPL-2.0-only
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurfaceRef.h>
#import <objc/runtime.h>
#include "NativeMetal540ProtocolAudit.h"
#include <sys/sysctl.h>
#include <cassert>
#include <cstdio>
#include <initializer_list>
@protocol Native540NegativeDeviceAPI <NSObject>
- (BOOL)supportsVertexAmplificationCount:(NSUInteger)n;
- (id)newIndirectCommandBufferWithDescriptor:(id)d maxCommandCount:(NSUInteger)n options:(NSUInteger)o;
- (id)newIndirectCommandBufferWithDescriptor:(id)d maxCount:(NSUInteger)n options:(NSUInteger)o;
- (id)newFence;
- (id)newBufferWithBytesNoCopy:(void*)p length:(NSUInteger)n options:(NSUInteger)o deallocator:(void (^)(void*,NSUInteger))deallocator;
- (id)newDepthStencilStateWithDescriptor:(id)d;
- (id)newSamplerStateWithDescriptor:(id)d;
- (MTLSizeAndAlign)heapBufferSizeAndAlignWithLength:(NSUInteger)n options:(NSUInteger)o;
- (MTLSizeAndAlign)heapTextureSizeAndAlignWithDescriptor:(id)d;
- (BOOL)isDepth24Stencil8PixelFormatSupported;
- (NSUInteger)minimumTextureBufferAlignmentForPixelFormat:(NSUInteger)f;
- (id)newHeapWithDescriptor:(id)d;
- (MTLSize)sparseTileSizeWithTextureType:(NSUInteger)t pixelFormat:(NSUInteger)f sampleCount:(NSUInteger)n;
- (MTLSize)sparseTileSizeWithTextureType:(NSUInteger)t pixelFormat:(NSUInteger)f sampleCount:(NSUInteger)n sparsePageSize:(NSInteger)s;
- (BOOL)supportPriorityBand;
- (id)newIndirectArgumentBufferLayoutWithStructType:(id)t;
- (id)newIntersectionFunctionTableWithDescriptor:(id)d;
- (NSUInteger)dedicatedMemorySize;
- (NSUInteger)sharedMemorySize;
- (id)newBufferWithIOSurface:(IOSurfaceRef)s;
- (NSUInteger)doubleFPConfig;
- (NSUInteger)halfFPConfig;
- (NSUInteger)singleFPConfig;
- (BOOL)metalAssertionsEnabled;
- (void)setMetalAssertionsEnabled:(BOOL)enabled;
- (id)newIndirectComputeCommandEncoderWithBuffer:(id)b;
- (id)newIndirectRenderCommandEncoderWithBuffer:(id)b;
- (id)newVisibleFunctionTableWithDescriptor:(id)d;
@end
// Original 21H461 required protocol encodings, not the build SDK's protocols.
static const struct {const char *name,*types;} target[]={
    {"supportsVertexAmplificationCount:","B24@0:8Q16"},
    {"newIndirectCommandBufferWithDescriptor:maxCommandCount:options:","@40@0:8@16Q24Q32"},
    {"newFence","@16@0:8"},
    {"newBufferWithBytesNoCopy:length:options:deallocator:","@48@0:8^v16Q24Q32@?40"},
    {"newDepthStencilStateWithDescriptor:","@24@0:8@16"},
    {"newSamplerStateWithDescriptor:","@24@0:8@16"},
    {"heapBufferSizeAndAlignWithLength:options:","{?=QQ}32@0:8Q16Q24"},
    {"heapTextureSizeAndAlignWithDescriptor:","{?=QQ}24@0:8@16"},
    {"isDepth24Stencil8PixelFormatSupported","B16@0:8"},
    {"minimumTextureBufferAlignmentForPixelFormat:","Q24@0:8Q16"},
    {"newHeapWithDescriptor:","@24@0:8@16"},
    {"sparseTileSizeWithTextureType:pixelFormat:sampleCount:","{?=QQQ}40@0:8Q16Q24Q32"},
    {"sparseTileSizeWithTextureType:pixelFormat:sampleCount:sparsePageSize:","{?=QQQ}48@0:8Q16Q24Q32q40"},
    {"supportPriorityBand","B16@0:8"},
    {"newIndirectArgumentBufferLayoutWithStructType:","@24@0:8@16"},
    {"newIntersectionFunctionTableWithDescriptor:","@24@0:8@16"},
    {"dedicatedMemorySize","Q16@0:8"},
    {"sharedMemorySize","Q16@0:8"},
    {"newBufferWithIOSurface:","@24@0:8^{__IOSurface=}16"},
    {"doubleFPConfig","Q16@0:8"},
    {"halfFPConfig","Q16@0:8"},
    {"metalAssertionsEnabled","B16@0:8"},
    {"newIndirectCommandBufferWithDescriptor:maxCount:options:","@40@0:8@16Q24Q32"},
    {"newIndirectComputeCommandEncoderWithBuffer:","@24@0:8@16"},
    {"newIndirectRenderCommandEncoderWithBuffer:","@24@0:8@16"},
    {"newVisibleFunctionTableWithDescriptor:","@24@0:8@16"},
    {"setMetalAssertionsEnabled:","v20@0:8B16"},
    {"singleFPConfig","Q16@0:8"}
};
int main(){@autoreleasepool {
    Class cls=NSClassFromString(@"Native540AdapterMethods");assert(cls);
    unsigned ownCount=0;Method *own=class_copyMethodList(cls,&ownCount);
    for(const auto &entry:target){SEL sel=sel_registerName(entry.name);Method found=nullptr;
        for(unsigned i=0;i<ownCount;++i)if(method_getName(own[i])==sel)found=own[i];assert(found);
        auto actual=[NSMethodSignature signatureWithObjCTypes:method_getTypeEncoding(found)];
        auto wanted=[NSMethodSignature signatureWithObjCTypes:entry.types];
        assert(actual.numberOfArguments==wanted.numberOfArguments);
        assert(NativeMetal540ProtocolTypesMatch(wanted.methodReturnType,actual.methodReturnType));
        for(NSUInteger i=0;i<wanted.numberOfArguments;++i)
            assert(NativeMetal540ProtocolTypesMatch([wanted getArgumentTypeAtIndex:i],[actual getArgumentTypeAtIndex:i]));
    }free(own);
    id<Native540NegativeDeviceAPI> d=[cls new];assert(![d conformsToProtocol:objc_getProtocol("MTLDevice")]);
    for(NSUInteger n:{0ul,1ul,2ul,NSUIntegerMax})assert([d supportsVertexAmplificationCount:n]==(n==1));
    __block unsigned deallocations=0;unsigned char input[32];memset(input,0x5a,sizeof input);
    for(NSUInteger n:{0ul,1ul,16384ul,NSUIntegerMax})for(NSUInteger o:{0ul,NSUInteger(MTLResourceHazardTrackingModeTracked),NSUIntegerMax}){
        assert(![d newIndirectCommandBufferWithDescriptor:nil maxCommandCount:n options:o]);
        assert(![d newIndirectCommandBufferWithDescriptor:d maxCount:n options:o]);
        assert(![d newBufferWithBytesNoCopy:input length:n options:o deallocator:^(void*,NSUInteger){++deallocations;}]);
        assert(![d newBufferWithBytesNoCopy:nullptr length:n options:o deallocator:nil]);
        auto heap=[d heapBufferSizeAndAlignWithLength:n options:o];assert(!heap.size&&!heap.align);
    }
    assert(!deallocations);for(unsigned char c:input)assert(c==0x5a);
    assert(![d newFence]&&![d newHeapWithDescriptor:d]&&![d newDepthStencilStateWithDescriptor:d]&&![d newSamplerStateWithDescriptor:d]);
    assert(![d newIndirectArgumentBufferLayoutWithStructType:d]&&![d newIntersectionFunctionTableWithDescriptor:d]);
    assert(![d newIndirectComputeCommandEncoderWithBuffer:d]&&![d newIndirectRenderCommandEncoderWithBuffer:d]&&![d newVisibleFunctionTableWithDescriptor:d]);
    assert(![d newBufferWithIOSurface:nullptr]&&![d isDepth24Stencil8PixelFormatSupported]&&![d supportPriorityBand]);
    auto heap=[d heapTextureSizeAndAlignWithDescriptor:d];assert(!heap.size&&!heap.align);
    for(NSUInteger f:{0ul,NSUInteger(MTLPixelFormatRGBA8Unorm),NSUIntegerMax}){
        auto tile=[d sparseTileSizeWithTextureType:MTLTextureType2D pixelFormat:f sampleCount:1];assert(!tile.width&&!tile.height&&!tile.depth);
        for(NSInteger p:{NSIntegerMin,0l,1l,NSIntegerMax}){
            tile=[d sparseTileSizeWithTextureType:MTLTextureType2D pixelFormat:f sampleCount:1 sparsePageSize:p];assert(!tile.width&&!tile.height&&!tile.depth);
        }
        bool caught=false;@try{[d minimumTextureBufferAlignmentForPixelFormat:f];}@catch(NSException*){caught=true;}assert(caught);
    }
    assert(![d dedicatedMemorySize]&&![d doubleFPConfig]&&![d halfFPConfig]&&![d singleFPConfig]);
    uint64_t bytes=0;size_t n=sizeof bytes;assert(!sysctlbyname("hw.memsize",&bytes,&n,nullptr,0)&&n==sizeof bytes);
    assert([d sharedMemorySize]==bytes);
    assert(![d metalAssertionsEnabled]);[d setMetalAssertionsEnabled:NO];bool caught=false;
    @try{[d setMetalAssertionsEnabled:YES];}@catch(NSException*){caught=true;}assert(caught&&![d metalAssertionsEnabled]);
    puts("PASS 28 target ABI entries and explicit negative behavior; HOST contract NOT system registration");
}}
