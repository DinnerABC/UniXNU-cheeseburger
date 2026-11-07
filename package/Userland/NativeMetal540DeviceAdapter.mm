// SPDX-License-Identifier: GPL-2.0-only
#include "NativeMetal540DeviceAdapter.h"
#include "NativeMetal540Capabilities.h"
#include "NativeMetal540ProtocolAudit.h"
#import "NativeMetal540Objects.h"
#import <objc/runtime.h>
#import <IOKit/IOKitLib.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <sys/sysctl.h>
#include <mutex>
@interface _MTLDevice : NSObject
- (void)initFeatureQueries;
- (void)initWorkarounds;
@end
namespace {
struct Limits {
    uint32_t maxFramebufferStorageBits,linearTextureArrayAlignmentBytes,linearTextureArrayAlignmentSlice;
    uint32_t maxTileBuffers,maxTileTextures,maxTileSamplers,maxTileInlineDataSize,minTilePixels;
    uint32_t maxColorAttachments,maxVertexAttributes,maxVertexBuffers,maxVertexTextures,maxVertexSamplers,maxVertexInlineDataSize,maxInterpolants;
    uint32_t maxFragmentBuffers,maxFragmentTextures,maxFragmentSamplers,maxFragmentInlineDataSize;
    uint32_t maxComputeBuffers,maxComputeTextures,maxComputeSamplers,maxComputeInlineDataSize,maxComputeLocalMemorySizes;
    uint32_t maxTotalComputeThreadsPerThreadgroup,maxComputeThreadgroupMemory;float maxLineWidth,maxPointSize;
    uint32_t maxVisibilityQueryOffset,padmaxBufferLength,minConstantBufferAlignmentBytes,minBufferNoCopyAlignmentBytes;
    uint32_t maxTextureWidth1D,maxTextureWidth2D,maxTextureHeight2D,maxTextureWidth3D,maxTextureHeight3D,maxTextureDepth3D,maxTextureDimensionCube,maxTextureLayers;
    uint32_t linearTextureAlignmentBytes,iosurfaceTextureAlignmentBytes,iosurfaceReadOnlyTextureAlignmentBytes,deviceLinearTextureAlignmentBytes,deviceLinearReadOnlyTextureAlignmentBytes;
    uint32_t maxFunctionConstantIndices,maxComputeThreadgroupMemoryAlignmentBytes,maxInterpolatedComponents,maxTessellationFactor;
    uint32_t maxIndirectBuffers,maxIndirectTextures,maxIndirectSamplers,maxIndirectSamplersPerDevice,maxFenceInstances,maxViewportCount,maxCustomSamplePositions;
    uint32_t maxVertexAmplificationFactor,maxVertexAmplificationCount,maxTextureBufferWidth,maxComputeAttributes,maxIOCommandsInFlight,maxPredicatedNestingDepth;
    uint32_t maxAccelerationStructureLevels,maxConstantBufferArguments;uint64_t maxBufferLength;
};
static_assert(sizeof(Limits)==264&&offsetof(Limits,maxBufferLength)==256&&offsetof(Limits,maxTextureWidth2D)==132,"original21H461limits");
const char *limitsType="r^{?=IIIIIIIIIIIIIIIIIIIIIIIIIIffIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIQ}16@0:8";
char contextKey;
Class adapterClass=nullptr;
std::mutex classLock;
}
@interface Native540AdapterContext : NSObject { @public Native540Device *backend;bool desktop;Limits limits;bool featuresInitialized;io_service_t service;uint64_t registryID; }
@end
@implementation Native540AdapterContext
- (instancetype)init {if((self=[super init])){limits={};desktop=false;featuresInitialized=false;service=0;registryID=0;}return self;}
- (void)dealloc {if(service)IOObjectRelease(service);}
@end
static Native540AdapterContext *context(id self){return objc_getAssociatedObject(self,&contextKey);}
@interface Native540AdapterMethods : NSObject
- (NSUInteger)featureProfile;
- (const Limits *)limits;
- (void)initLimits;
- (void)initFeatureQueries;
- (NSString *)name;
- (uint64_t)registryID;
- (uint32_t)acceleratorPort;
- (BOOL)hasUnifiedMemory;
- (BOOL)isHeadless;
- (BOOL)isRemovable;
- (BOOL)isLowPower;
- (uint64_t)recommendedMaxWorkingSetSize;
- (BOOL)supportsSampleCount:(NSUInteger)count;
- (id)newBufferWithBytes:(const void*)bytes length:(NSUInteger)length options:(MTLResourceOptions)options;
- (NSUInteger)maxBufferLength;
- (NSUInteger)currentAllocatedSize;
- (MTLSize)maxThreadsPerThreadgroup;
- (NSUInteger)maxThreadgroupMemoryLength;
- (BOOL)supportsFamily:(NSInteger)family;
- (BOOL)supportsFeatureSet:(MTLFeatureSet)set;
- (id)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options;
- (id)newTextureWithDescriptor:(MTLTextureDescriptor*)descriptor;
- (id)newTextureWithDescriptor:(MTLTextureDescriptor*)descriptor iosurface:(IOSurfaceRef)surface plane:(NSUInteger)plane;
- (id)newLibraryWithData:(dispatch_data_t)data error:(NSError**)error;
- (id)newLibraryWithFile:(NSString*)path error:(NSError**)error;
- (id)newLibraryWithURL:(NSURL*)url error:(NSError**)error;
- (id)newDefaultLibrary;
- (id)newDefaultLibraryWithBundle:(NSBundle*)bundle error:(NSError**)error;
- (id)newLibraryWithSource:(NSString*)source options:(MTLCompileOptions*)options error:(NSError**)error;
- (void)newLibraryWithSource:(NSString*)source options:(MTLCompileOptions*)options completionHandler:(void (^)(id,NSError*))handler;
- (id)newComputePipelineStateWithFunction:(id)function error:(NSError**)error;
- (id)newComputePipelineStateWithFunction:(id)function options:(MTLPipelineOption)options reflection:(MTLComputePipelineReflection**)reflection error:(NSError**)error;
- (id)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor*)descriptor options:(MTLPipelineOption)options reflection:(MTLComputePipelineReflection**)reflection error:(NSError**)error;
- (void)newComputePipelineStateWithFunction:(id)function completionHandler:(void (^)(id,NSError*))handler;
- (void)newComputePipelineStateWithFunction:(id)function options:(MTLPipelineOption)options completionHandler:(void (^)(id,MTLComputePipelineReflection*,NSError*))handler;
- (void)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor*)descriptor options:(MTLPipelineOption)options completionHandler:(void (^)(id,MTLComputePipelineReflection*,NSError*))handler;
- (id)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor error:(NSError**)error;
- (id)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor options:(MTLPipelineOption)options reflection:(MTLRenderPipelineReflection**)reflection error:(NSError**)error;
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor completionHandler:(void (^)(id,NSError*))handler;
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)descriptor options:(MTLPipelineOption)options completionHandler:(void (^)(id,MTLRenderPipelineReflection*,NSError*))handler;
- (id)newCommandQueue;
- (id)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)count;
- (BOOL)supportsTextureSampleCount:(NSUInteger)count;
- (int)retireResources;
- (NativeMetal540SessionRetirement::Record)retirementResult;
- (BOOL)supportsVertexAmplificationCount:(NSUInteger)count;
- (id)newIndirectCommandBufferWithDescriptor:(id)descriptor maxCommandCount:(NSUInteger)count options:(NSUInteger)options;
- (id)newIndirectCommandBufferWithDescriptor:(id)descriptor maxCount:(NSUInteger)count options:(NSUInteger)options;
- (id)newFence;
- (id)newBufferWithBytesNoCopy:(void*)bytes length:(NSUInteger)length options:(NSUInteger)options deallocator:(void (^)(void*,NSUInteger))deallocator;
- (id)newDepthStencilStateWithDescriptor:(id)descriptor;
- (id)newSamplerStateWithDescriptor:(id)descriptor;
- (MTLSizeAndAlign)heapBufferSizeAndAlignWithLength:(NSUInteger)length options:(NSUInteger)options;
- (MTLSizeAndAlign)heapTextureSizeAndAlignWithDescriptor:(id)descriptor;
- (BOOL)isDepth24Stencil8PixelFormatSupported;
- (NSUInteger)minimumTextureBufferAlignmentForPixelFormat:(NSUInteger)format;
- (id)newHeapWithDescriptor:(id)descriptor;
- (MTLSize)sparseTileSizeWithTextureType:(NSUInteger)type pixelFormat:(NSUInteger)format sampleCount:(NSUInteger)samples;
- (MTLSize)sparseTileSizeWithTextureType:(NSUInteger)type pixelFormat:(NSUInteger)format sampleCount:(NSUInteger)samples sparsePageSize:(NSInteger)size;
- (BOOL)supportPriorityBand;
- (id)newIndirectArgumentBufferLayoutWithStructType:(id)type;
- (id)newIntersectionFunctionTableWithDescriptor:(id)descriptor;
- (NSUInteger)dedicatedMemorySize;
- (NSUInteger)sharedMemorySize;
- (id)newBufferWithIOSurface:(IOSurfaceRef)surface;
- (NSUInteger)doubleFPConfig;
- (NSUInteger)halfFPConfig;
- (NSUInteger)singleFPConfig;
- (BOOL)metalAssertionsEnabled;
- (void)setMetalAssertionsEnabled:(BOOL)enabled;
- (id)newIndirectComputeCommandEncoderWithBuffer:(id)buffer;
- (id)newIndirectRenderCommandEncoderWithBuffer:(id)buffer;
- (id)newVisibleFunctionTableWithDescriptor:(id)descriptor;
@end
@implementation Native540AdapterMethods
- (NSUInteger)featureProfile{return NSUIntegerMax;}
- (const Limits *)limits{return &context(self)->limits;}
- (void)initLimits {
    auto c=context(self);if(!c)return;
    Limits v{};v.maxFramebufferStorageBits=32;v.maxColorAttachments=1;
    v.maxVertexBuffers=v.maxFragmentBuffers=1;v.maxComputeBuffers=3;
    v.maxTotalComputeThreadsPerThreadgroup=64;v.maxTextureWidth2D=v.maxTextureHeight2D=64;v.maxTextureLayers=1;
    v.maxLineWidth=v.maxPointSize=1;v.minConstantBufferAlignmentBytes=4;v.maxViewportCount=1;
    v.maxVertexAmplificationFactor=v.maxVertexAmplificationCount=1;
    v.maxBufferLength=16384;
    if(c->desktop){v.maxColorAttachments=8;v.maxVertexAttributes=16;v.maxVertexBuffers=v.maxFragmentBuffers=8;
        v.maxVertexTextures=v.maxFragmentTextures=v.maxVertexSamplers=v.maxFragmentSamplers=8;
        v.maxTextureWidth2D=v.maxTextureHeight2D=16384;v.maxBufferLength=16u*1024*1024;v.maxIOCommandsInFlight=1;
        v.linearTextureAlignmentBytes=v.iosurfaceTextureAlignmentBytes=64;}
    c->limits=v;
}
- (void)initFeatureQueries {
    auto c=context(self);if(!c||c->featuresInitialized)return;
    c->featuresInitialized=true;
    IMP imp=class_getMethodImplementation(class_getSuperclass(object_getClass(self)),@selector(initFeatureQueries));
    ((void(*)(id,SEL))imp)(self,@selector(initFeatureQueries));
}
- (NSString*)name{return @"Adreno 540 (bounded native backend)";}
- (uint64_t)registryID{return context(self)->registryID;}
- (uint32_t)acceleratorPort{return context(self)->service;}
- (BOOL)hasUnifiedMemory{return YES;}
- (BOOL)isHeadless{return NO;}
- (BOOL)isRemovable{return NO;}
- (BOOL)isLowPower{return NO;}
- (uint64_t)recommendedMaxWorkingSetSize{return 64ull*16384;}
- (BOOL)supportsSampleCount:(NSUInteger)count{return count==1;}
- (id)newBufferWithBytes:(const void*)bytes length:(NSUInteger)n options:(MTLResourceOptions)o {
    if(!bytes||!n||n>[self maxBufferLength])return nil;
    return [context(self)->backend newBufferWithBytes:bytes length:n options:o];
}
- (NSUInteger)maxBufferLength{return context(self)->desktop?16u*1024*1024:16384;}
- (NSUInteger)currentAllocatedSize{return [context(self)->backend currentAllocatedSize];}
- (MTLSize)maxThreadsPerThreadgroup{return MTLSizeMake(64,1,1);}
- (NSUInteger)maxThreadgroupMemoryLength{return 0;}
- (BOOL)supportsFamily:(NSInteger)family{(void)family;return NO;}
- (BOOL)supportsFeatureSet:(MTLFeatureSet)set{(void)set;return NO;}
- (id)newBufferWithLength:(NSUInteger)n options:(MTLResourceOptions)o{return n<=[self maxBufferLength]?[context(self)->backend newBufferWithLength:n options:o]:nil;}
- (id)newTextureWithDescriptor:(MTLTextureDescriptor*)d{return [context(self)->backend newTextureWithDescriptor:d];}
- (id)newTextureWithDescriptor:(MTLTextureDescriptor*)d iosurface:(IOSurfaceRef)s plane:(NSUInteger)p{return [context(self)->backend newTextureWithDescriptor:d iosurface:s plane:p];}
- (id)newLibraryWithData:(dispatch_data_t)d error:(NSError**)e{return [context(self)->backend newLibraryWithData:d error:e];}
- (id)newLibraryWithFile:(NSString*)p error:(NSError**)e{return [context(self)->backend newLibraryWithFile:p error:e];}
- (id)newLibraryWithURL:(NSURL*)u error:(NSError**)e{return [context(self)->backend newLibraryWithURL:u error:e];}
- (id)newDefaultLibrary{return [context(self)->backend newDefaultLibrary];}
- (id)newDefaultLibraryWithBundle:(NSBundle*)b error:(NSError**)e{return [context(self)->backend newDefaultLibraryWithBundle:b error:e];}
- (id)newLibraryWithSource:(NSString*)s options:(MTLCompileOptions*)o error:(NSError**)e{return [context(self)->backend newLibraryWithSource:s options:o error:e];}
- (void)newLibraryWithSource:(NSString*)s options:(MTLCompileOptions*)o completionHandler:(void (^)(id,NSError*))h {
    if(!h)return;id owner=self;[context(self)->backend newLibraryWithSource:s options:o completionHandler:^(Native540Library *p,NSError *e){(void)owner;h(p,e);}];
}
- (id)newComputePipelineStateWithFunction:(id)f error:(NSError**)e{return [context(self)->backend newComputePipelineStateWithFunction:f error:e];}
- (id)newComputePipelineStateWithFunction:(id)f options:(MTLPipelineOption)o reflection:(MTLComputePipelineReflection**)r error:(NSError**)e{return [context(self)->backend newComputePipelineStateWithFunction:f options:o reflection:r error:e];}
- (id)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor*)d options:(MTLPipelineOption)o reflection:(MTLComputePipelineReflection**)r error:(NSError**)e{return [context(self)->backend newComputePipelineStateWithDescriptor:d options:o reflection:r error:e];}
- (void)newComputePipelineStateWithFunction:(id)f completionHandler:(void (^)(id,NSError*))h {
    if(!h)return;id owner=self;[context(self)->backend newComputePipelineStateWithFunction:f completionHandler:^(Native540ComputePipeline *p,NSError *e){(void)owner;h(p,e);}];
}
- (void)newComputePipelineStateWithFunction:(id)f options:(MTLPipelineOption)o completionHandler:(void (^)(id,MTLComputePipelineReflection*,NSError*))h {
    if(!h)return;id owner=self;[context(self)->backend newComputePipelineStateWithFunction:f options:o completionHandler:^(Native540ComputePipeline *p,MTLComputePipelineReflection *r,NSError *e){(void)owner;h(p,r,e);}];
}
- (void)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor*)d options:(MTLPipelineOption)o completionHandler:(void (^)(id,MTLComputePipelineReflection*,NSError*))h {
    if(!h)return;id owner=self;[context(self)->backend newComputePipelineStateWithDescriptor:d options:o completionHandler:^(Native540ComputePipeline *p,MTLComputePipelineReflection *r,NSError *e){(void)owner;h(p,r,e);}];
}
- (id)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)d error:(NSError**)e{return [context(self)->backend newRenderPipelineStateWithDescriptor:d error:e];}
- (id)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)d options:(MTLPipelineOption)o reflection:(MTLRenderPipelineReflection**)r error:(NSError**)e{return [context(self)->backend newRenderPipelineStateWithDescriptor:d options:o reflection:r error:e];}
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)d completionHandler:(void (^)(id,NSError*))h {
    if(!h)return;id owner=self;[context(self)->backend newRenderPipelineStateWithDescriptor:d completionHandler:^(Native540RenderPipeline *p,NSError *e){(void)owner;h(p,e);}];
}
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor*)d options:(MTLPipelineOption)o completionHandler:(void (^)(id,MTLRenderPipelineReflection*,NSError*))h {
    if(!h)return;id owner=self;[context(self)->backend newRenderPipelineStateWithDescriptor:d options:o completionHandler:^(Native540RenderPipeline *p,MTLRenderPipelineReflection *r,NSError *e){(void)owner;h(p,r,e);}];
}
- (id)newCommandQueue{return [context(self)->backend newCommandQueue];}
- (id)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)count {
    auto c=context(self);SEL selector=_cmd;
    if(!c||!c->desktop||![c->backend respondsToSelector:selector])return nil;
    return ((id(*)(id,SEL,NSUInteger))[c->backend methodForSelector:selector])(c->backend,selector,count);
}
- (BOOL)supportsTextureSampleCount:(NSUInteger)count{return count==1;}
- (int)retireResources{return [context(self)->backend retireResources];}
- (NativeMetal540SessionRetirement::Record)retirementResult{return [context(self)->backend retirementResult];}
// Unsupported creation APIs fail explicitly; no dummy resource or Apple tier.
- (BOOL)supportsVertexAmplificationCount:(NSUInteger)n{return n==1;}
- (id)newIndirectCommandBufferWithDescriptor:(id)d maxCommandCount:(NSUInteger)n options:(NSUInteger)o{(void)d;(void)n;(void)o;return nil;}
- (id)newIndirectCommandBufferWithDescriptor:(id)d maxCount:(NSUInteger)n options:(NSUInteger)o{(void)d;(void)n;(void)o;return nil;}
- (id)newFence{return nil;}
- (id)newBufferWithBytesNoCopy:(void*)p length:(NSUInteger)n options:(NSUInteger)o deallocator:(void (^)(void*,NSUInteger))deallocator{(void)p;(void)n;(void)o;(void)deallocator;return nil;}
- (id)newDepthStencilStateWithDescriptor:(id)d{(void)d;return nil;}
- (id)newSamplerStateWithDescriptor:(id)d {
    auto c=context(self);if(!c||!c->desktop||![c->backend respondsToSelector:_cmd])return nil;
    return ((id(*)(id,SEL,id))[c->backend methodForSelector:_cmd])(c->backend,_cmd,d);
}
- (MTLSizeAndAlign)heapBufferSizeAndAlignWithLength:(NSUInteger)n options:(NSUInteger)o{(void)n;(void)o;return {0,0};}
- (MTLSizeAndAlign)heapTextureSizeAndAlignWithDescriptor:(id)d{(void)d;return {0,0};}
- (BOOL)isDepth24Stencil8PixelFormatSupported{return NO;}
- (NSUInteger)minimumTextureBufferAlignmentForPixelFormat:(NSUInteger)f{(void)f;[NSException raise:NSInvalidArgumentException format:@"Texture buffers are not provided by this backend"];return 0;}
- (id)newHeapWithDescriptor:(id)d{(void)d;return nil;}
- (MTLSize)sparseTileSizeWithTextureType:(NSUInteger)t pixelFormat:(NSUInteger)f sampleCount:(NSUInteger)n{(void)t;(void)f;(void)n;return {0,0,0};}
- (MTLSize)sparseTileSizeWithTextureType:(NSUInteger)t pixelFormat:(NSUInteger)f sampleCount:(NSUInteger)n sparsePageSize:(NSInteger)s{(void)t;(void)f;(void)n;(void)s;return {0,0,0};}
- (BOOL)supportPriorityBand{return NO;}
- (id)newIndirectArgumentBufferLayoutWithStructType:(id)t{(void)t;return nil;}
- (id)newIntersectionFunctionTableWithDescriptor:(id)d{(void)d;return nil;}
- (NSUInteger)dedicatedMemorySize{return 0;}
- (NSUInteger)sharedMemorySize{uint64_t bytes=0;size_t n=sizeof bytes;return sysctlbyname("hw.memsize",&bytes,&n,nullptr,0)==0&&n==sizeof bytes?NSUInteger(bytes):0;}
- (id)newBufferWithIOSurface:(IOSurfaceRef)s{(void)s;return nil;}
- (NSUInteger)doubleFPConfig{return 0;}
- (NSUInteger)halfFPConfig{return 0;}
- (NSUInteger)singleFPConfig{return 0;}
- (BOOL)metalAssertionsEnabled{return NO;}
- (void)setMetalAssertionsEnabled:(BOOL)enabled{if(enabled)[NSException raise:NSInvalidArgumentException format:@"Metal assertions are not provided by this backend"];}
- (id)newIndirectComputeCommandEncoderWithBuffer:(id)b{(void)b;return nil;}
- (id)newIndirectRenderCommandEncoderWithBuffer:(id)b{(void)b;return nil;}
- (id)newVisibleFunctionTableWithDescriptor:(id)d{(void)d;return nil;}
@end
static id createInitializedBackend(id ownedBackend,uint32_t service,bool desktop,NSError **error){
    Native540Device *backend=(Native540Device*)ownedBackend;
    if(error)*error=nil;
    auto fail=[&](NSString *why)->id{if(error)*error=[NSError errorWithDomain:@"Native540DeviceAdapter" code:-5500 userInfo:@{NSLocalizedDescriptionKey:why}];return nil;};
    if(!backend||!service)return fail(@"Missing owned native backend or accelerator");
    Class base=objc_lookUpClass("_MTLDevice");
#ifndef NATIVE540_DESKTOP_HOST_MODEL
    Method lm=base?class_getInstanceMethod(base,sel_registerName("limits")):nullptr;
    NSOperatingSystemVersion version=NSProcessInfo.processInfo.operatingSystemVersion;
    if(version.majorVersion!=17||version.minorVersion!=7)return fail(@"Only qualified17.7 target is admitted");
    if(!base||class_getInstanceSize(base)!=536||!lm||strcmp(method_getTypeEncoding(lm),limitsType))return fail(@"Target21H461 Metal layout/encoding mismatch");
    const struct {const char *name,*type;} required[]={
        {"initFeatureQueries","v16@0:8"},{"initGPUFamilySupport","v16@0:8"}};
    for(const auto &q:required){Method m=class_getInstanceMethod(base,sel_registerName(q.name));
        if(!m||strcmp(method_getTypeEncoding(m),q.type))return fail(@"Target Metal initializer ABI mismatch");}
#endif
    {
        std::lock_guard<std::mutex> guard(classLock);
        if(!adapterClass){
            Class cls=objc_allocateClassPair(base,"NativeMetal540InitializedDevice21H461",0);if(!cls)return fail(@"Adapter class allocation failed");
#ifndef NATIVE540_DESKTOP_HOST_MODEL
            NativeMetal540Capabilities::Report report{};
            NativeMetal540Capabilities::Evidence evidence(NativeMetal540Capabilities::LinearRGBA8RasterCode|
                NativeMetal540Capabilities::LinearRGBA8DevicePixels|NativeMetal540Capabilities::RestrictedDescriptorAdmission);
            if(!NativeMetal540InstallQueries(cls,base,evidence,report)){objc_disposeClassPair(cls);return fail(@"Target capability-query preflight rejected");}
#endif
            unsigned count=0;Method *methods=class_copyMethodList(Native540AdapterMethods.class,&count);bool ok=true;
            for(unsigned i=0;i<count&&ok;++i){SEL s=method_getName(methods[i]);const char *types=method_getTypeEncoding(methods[i]);
                if(s==sel_registerName("limits"))types=limitsType;
                ok=class_addMethod(cls,s,method_getImplementation(methods[i]),types);}
            free(methods);if(!ok){objc_disposeClassPair(cls);return fail(@"Adapter method installation failed");}
            objc_registerClassPair(cls);adapterClass=cls;
        }
    }
    auto c=[Native540AdapterContext new];c->backend=backend;c->desktop=desktop;
#ifndef NATIVE540_DESKTOP_HOST_MODEL
    if(IOObjectRetain(service)!=KERN_SUCCESS)return fail(@"Accelerator retain failed");c->service=service;
    if(IORegistryEntryGetRegistryEntryID(service,&c->registryID)!=KERN_SUCCESS||!c->registryID)return fail(@"Accelerator registry identity failed");
#endif
    id allocated=[adapterClass alloc];if(!allocated)return fail(@"Metal subclass allocation failed");
    objc_setAssociatedObject(allocated,&contextKey,c,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    id device=[allocated init];if(!device)return fail(@"Metal superclass initialization failed");
    @try {[(Native540AdapterMethods*)device initLimits];[(Native540AdapterMethods*)device initFeatureQueries];}
    @catch(NSException *e){return fail([NSString stringWithFormat:@"Metal device initialization exception: %@",e.reason]);}
    if([backend respondsToSelector:@selector(setPublicDeviceIdentity:)])[backend setPublicDeviceIdentity:device];
    if(desktop){
        // Full protocol/signature audit gates desktop registration. Only on
        // the qualified target is a gap fatal; the host model records it.
        auto audit=NativeMetal540AuditDeviceProtocols(device);
#ifdef NATIVE540_TARGET_METAL
        if(audit.missing||audit.mismatched)return fail([NSString stringWithFormat:@"Protocol audit: %u missing %u mismatched %.48s",audit.missing,audit.mismatched,audit.first]);
#else
        (void)audit;
#endif
        // Adopt the framework's own registered protocols after the audit:
        // the same route sagit uses. No duplicate protocol is invented.
        Protocol *deviceProtocol=objc_getProtocol("MTLDevice"),*spi=objc_getProtocol("MTLDeviceSPI");
        Class cls=object_getClass(device);
        if(deviceProtocol&&!class_conformsToProtocol(cls,deviceProtocol))class_addProtocol(cls,deviceProtocol);
        if(spi&&!class_conformsToProtocol(cls,spi))class_addProtocol(cls,spi);
        if((deviceProtocol&&!class_conformsToProtocol(cls,deviceProtocol))||(spi&&!class_conformsToProtocol(cls,spi)))
            return fail(@"Framework protocol adoption failed after audit");
    }
    return device;
}
id NativeMetal540CreateInitializedDevice(Native540Device *backend,uint32_t service,NSError **error){
    return createInitializedBackend(backend,service,false,error);
}
id NativeMetal540CreateInitializedDesktopDevice(id backend,uint32_t service,NSError **error){
    if(![backend respondsToSelector:@selector(newCommandQueueWithMaxCommandBufferCount:)]){
        if(error)*error=[NSError errorWithDomain:@"Native540DeviceAdapter" code:-5503 userInfo:@{NSLocalizedDescriptionKey:@"Concrete desktop backend required"}];return nil;
    }
    return createInitializedBackend(backend,service,true,error);
}
