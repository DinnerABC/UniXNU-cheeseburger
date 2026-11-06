// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurfaceRef.h>
#include "NativeMetal540Client.h"
#include "NativeMetal540Retirement.h"

// Owned finite public-selector subset, deliberately NOT <MTLDevice>/<MTLBuffer>
// etc: claiming those entire protocols would advertise unimplemented contracts.
// Fixed one-shot padding_gradient compute workload: 3120B input, 1024B
// output, 48B params {0,260,3,1,256,1,1,2,2,1,1,0}, groups4/local64.
// Exclusive owned connection; no concurrent external submits/maps. CPU must not
// mutate/reuse bound buffers from commit until successful fence or real failed
// connection retirement (public Error is NOT permission to reuse DMA storage).
// Each mapView supplies independent new-BO storage, never imports/aliases.
// No _MTLDevice inheritance, SPI layouts, system registration or forwarding.
@class Native540Buffer, Native540Library, Native540Function, Native540ComputePipeline;
@class Native540CommandQueue, Native540CommandBuffer, Native540ComputeEncoder;
@class Native540Texture, Native540RenderPipeline, Native540RenderEncoder;

// Callback pointer nullability is checked explicitly by the controlled initializer.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
struct Native540ObjectsTransport {
    NativeMetal540Client::Call _Nullable call = nullptr;
    void *context = nullptr;
    // Connection and callback context must live through retirement.
    __unsafe_unretained id connectionOwner = nil;
    // Existing verified CPU-inhibit BO contract ONLY, not cached/coherent Metal.
    bool verifiedCPUInhibit = false;
    // Preparation callbacks finish synchronously before the completion deadline;
    // they must not execute GPU work or wait for a GPU fence inline.
    int (*mapView)(void *, uint64_t token, void **address, size_t *bytes,
                   id __autoreleasing *mappingOwner) = nullptr;
    int (*flush)(void *, void *address, size_t bytes, bool beforeGPU) = nullptr;
    int (*unmapView)(void *,uint32_t token,uint64_t address,uint64_t bytes)=nullptr;
    uint64_t session=0;
    // Called once on connection teardown, never BoFree. Must retain/transfer
    // ALL mapping owners until real fence or failed-connection kernel retirement
    // (quarantine is acceptable). A return is NOT a successful GPU completion.
    void (*retireConnection)(void *, bool failed, NSArray *mappingOwners) = nullptr;
};
#pragma clang diagnostic pop

NS_ASSUME_NONNULL_BEGIN
@interface Native540Device : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
- (nullable instancetype)initControlledWithTransport:(Native540ObjectsTransport)transport
                                               error:(NSError **)error;
- (nullable Native540Buffer *)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options;
- (nullable Native540Buffer *)newBufferWithBytes:(const void* _Nullable)bytes length:(NSUInteger)length options:(MTLResourceOptions)options;
- (nullable Native540Library *)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error;
- (nullable Native540Library *)newLibraryWithFile:(NSString *)path error:(NSError **)error;
- (nullable Native540Library *)newLibraryWithURL:(NSURL *)url error:(NSError **)error;
- (nullable Native540Library *)newDefaultLibrary;
- (nullable Native540Library *)newDefaultLibraryWithBundle:(NSBundle *)bundle error:(NSError **)error;
- (nullable Native540Library *)newLibraryWithSource:(NSString *)source options:(nullable MTLCompileOptions *)options error:(NSError **)error;
- (void)newLibraryWithSource:(NSString *)source options:(nullable MTLCompileOptions *)options completionHandler:(void (^ _Nullable)(Native540Library * _Nullable, NSError * _Nullable))handler;
- (nullable Native540ComputePipeline *)newComputePipelineStateWithFunction:(Native540Function *)function error:(NSError **)error;
- (nullable Native540ComputePipeline *)newComputePipelineStateWithFunction:(Native540Function *)function options:(MTLPipelineOption)options reflection:(MTLComputePipelineReflection * _Nullable * _Nullable)reflection error:(NSError **)error;
- (nullable Native540ComputePipeline *)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)descriptor options:(MTLPipelineOption)options reflection:(MTLComputePipelineReflection * _Nullable * _Nullable)reflection error:(NSError **)error;
- (void)newComputePipelineStateWithFunction:(Native540Function *)function completionHandler:(void (^ _Nullable)(Native540ComputePipeline * _Nullable, NSError * _Nullable))handler;
- (void)newComputePipelineStateWithFunction:(Native540Function *)function options:(MTLPipelineOption)options completionHandler:(void (^ _Nullable)(Native540ComputePipeline * _Nullable, MTLComputePipelineReflection * _Nullable, NSError * _Nullable))handler;
- (void)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)descriptor options:(MTLPipelineOption)options completionHandler:(void (^ _Nullable)(Native540ComputePipeline * _Nullable, MTLComputePipelineReflection * _Nullable, NSError * _Nullable))handler;
- (nullable Native540Texture *)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor;
- (nullable Native540Texture *)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor iosurface:(IOSurfaceRef)surface plane:(NSUInteger)plane;
- (nullable Native540RenderPipeline *)newRenderPipelineWithVertexFunction:(Native540Function *)vertex
    fragmentFunction:(Native540Function *)fragment error:(NSError **)error;
- (nullable Native540RenderPipeline *)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor error:(NSError **)error;
- (nullable Native540RenderPipeline *)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor options:(MTLPipelineOption)options reflection:(MTLRenderPipelineReflection * _Nullable * _Nullable)reflection error:(NSError **)error;
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor completionHandler:(void (^ _Nullable)(Native540RenderPipeline * _Nullable, NSError * _Nullable))handler;
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor options:(MTLPipelineOption)options completionHandler:(void (^ _Nullable)(Native540RenderPipeline * _Nullable, MTLRenderPipelineReflection * _Nullable, NSError * _Nullable))handler;
- (Native540CommandQueue *)newCommandQueue;
- (NSUInteger)currentAllocatedSize;
// Session stores a weak identity; created resources retain it independently.
- (void)setPublicDeviceIdentity:(id)device;
- (int)retireResources;
- (NativeMetal540SessionRetirement::Record)retirementResult;
@end

@interface Native540Buffer : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@property(readonly) NSUInteger length;
@property(readonly) id device;
@property(readonly) MTLStorageMode storageMode;
@property(readonly) MTLCPUCacheMode cpuCacheMode;
@property(readonly) MTLHazardTrackingMode hazardTrackingMode;
@property(readonly) MTLResourceOptions resourceOptions;
@property(readonly) NSUInteger allocatedSize;
@property(nullable,copy) NSString *label;
@property(readonly,nullable) id heap;
@property(readonly) NSUInteger heapOffset;
@property(readonly) NSUInteger protectionOptions;
- (BOOL)isAliasable;
- (void)makeAliasable;
- (MTLPurgeableState)setPurgeableState:(MTLPurgeableState)state;
- (void *)contents;
@end
@interface Native540Library : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@property(readonly, copy) NSArray<NSString *> *functionNames;
@property(readonly) id device;
@property(nullable,copy) NSString *label;
// Hashes include input, ISA, full 621-word reflection, constants metadata.
@property(readonly, copy) NSDictionary<NSString *, NSString *> *snapshotHashes;
- (nullable Native540Function *)newFunctionWithName:(NSString *)name;
@end
@interface Native540Function : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@property(readonly, copy) NSString *name;
@property(readonly) id device;
@property(readonly) MTLFunctionType functionType;
@property(nullable,copy) NSString *label;
@property(readonly,copy) NSArray *vertexAttributes;
@property(readonly,copy) NSArray *stageInputAttributes;
@property(readonly) MTLPatchType patchType;
@property(readonly) NSInteger patchControlPointCount;
@property(readonly) MTLFunctionOptions options;
@property(readonly, copy) NSDictionary *functionConstantsDictionary;
- (nullable id)newArgumentEncoderWithBufferIndex:(NSUInteger)index;
@end
@interface Native540ComputePipeline : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@property(readonly) NSUInteger threadExecutionWidth;
@property(readonly) id device;
@property(readonly) NSUInteger maxTotalThreadsPerThreadgroup;
@property(readonly) NSUInteger staticThreadgroupMemoryLength;
@property(readonly,copy) NSString *label;
- (NSUInteger)imageblockMemoryLengthForDimensions:(MTLSize)dimensions;
@end
@interface Native540CommandQueue : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
- (Native540CommandBuffer *)commandBuffer;
@property(readonly) id device;
@end
@interface Native540Texture : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@property(readonly) NSUInteger width;
@property(readonly) id device;
@property(readonly) NSUInteger height;
@property(readonly) MTLPixelFormat pixelFormat;
@property(readonly) MTLTextureType textureType;
@property(readonly) NSUInteger depth;
@property(readonly) NSUInteger arrayLength;
@property(readonly) NSUInteger mipmapLevelCount;
@property(readonly) NSUInteger sampleCount;
@property(readonly) MTLTextureUsage usage;
@property(readonly) MTLStorageMode storageMode;
@property(readonly) MTLCPUCacheMode cpuCacheMode;
@property(readonly) MTLHazardTrackingMode hazardTrackingMode;
@property(readonly) MTLResourceOptions resourceOptions;
@property(readonly) NSUInteger allocatedSize;
@property(readonly) IOSurfaceRef _Nullable iosurface;
@property(readonly) NSUInteger iosurfacePlane;
@property(nullable,copy) NSString *label;
@property(readonly,nullable) id heap;
@property(readonly) NSUInteger heapOffset;
@property(readonly) NSUInteger protectionOptions;
@property(readonly,nullable) id rootResource;
@property(readonly,nullable) Native540Texture *parentTexture;
@property(readonly) NSUInteger parentRelativeLevel;
@property(readonly) NSUInteger parentRelativeSlice;
@property(readonly,nullable) Native540Buffer *buffer;
@property(readonly) NSUInteger bufferOffset;
@property(readonly) NSUInteger bufferBytesPerRow;
@property(readonly) BOOL isFramebufferOnly;
@property(readonly) BOOL isShareable;
@property(readonly) BOOL isSparse;
@property(readonly) BOOL allowGPUOptimizedContents;
@property(readonly) MTLTextureCompressionType compressionType;
@property(readonly) MTLTextureSwizzleChannels swizzle;
- (BOOL)isAliasable;
- (void)makeAliasable;
- (MTLPurgeableState)setPurgeableState:(MTLPurgeableState)state;
- (nullable Native540Texture *)newTextureViewWithPixelFormat:(MTLPixelFormat)format;
- (nullable Native540Texture *)newTextureViewWithPixelFormat:(MTLPixelFormat)format textureType:(MTLTextureType)type levels:(NSRange)levels slices:(NSRange)slices;
- (nullable Native540Texture *)newTextureViewWithPixelFormat:(MTLPixelFormat)format textureType:(MTLTextureType)type levels:(NSRange)levels slices:(NSRange)slices swizzle:(MTLTextureSwizzleChannels)swizzle;
// One controlled full64x64 readback after the same command's genuine fence.
- (BOOL)getBytes:(void *)bytes bytesPerRow:(NSUInteger)pitch;
- (void)getBytes:(void *)bytes bytesPerRow:(NSUInteger)pitch fromRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level;
@end
@interface Native540RenderPipeline : NSObject
@property(readonly) id device;
@property(readonly,copy) NSString *label;
- (BOOL)supportsVertexAmplificationCount:(NSUInteger)count;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end
@interface Native540RenderEncoder : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
- (void)setRenderPipelineState:(Native540RenderPipeline *)pipeline;
- (void)setVertexBuffer:(Native540Buffer *)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)setFragmentBuffer:(Native540Buffer *)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)drawPrimitives:(MTLPrimitiveType)type vertexStart:(NSUInteger)start vertexCount:(NSUInteger)count;
- (void)endEncoding;
@end
@interface Native540CommandBuffer : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@property(readonly) MTLCommandBufferStatus status;
@property(readonly) id device;
@property(readonly, nullable) NSError *error;
@property(readonly) BOOL executionRetired;
- (void)addCompletedHandler:(void (^)(Native540CommandBuffer *))handler;
- (nullable Native540ComputeEncoder *)computeCommandEncoder;
- (nullable Native540RenderEncoder *)renderCommandEncoderWithTexture:(Native540Texture *)texture;
- (nullable Native540RenderEncoder *)renderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor *)descriptor;
- (void)commit;
// Finite host wait; a deadline failure is Error, never Completed. No retry.
- (void)waitUntilCompleted;
@end
@interface Native540ComputeEncoder : NSObject
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
- (void)setComputePipelineState:(Native540ComputePipeline *)state;
- (void)setBuffer:(nullable Native540Buffer *)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)setThreadgroupMemoryLength:(NSUInteger)length atIndex:(NSUInteger)index;
- (void)dispatchThreadgroups:(MTLSize)groups threadsPerThreadgroup:(MTLSize)local;
- (void)endEncoding;
@end
NS_ASSUME_NONNULL_END
