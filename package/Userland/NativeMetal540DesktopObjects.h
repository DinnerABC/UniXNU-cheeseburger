// SPDX-License-Identifier: MIT
#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurfaceRef.h>
#include "NativeMetal540DesktopSession.h"
#include "NativeMetal540DesktopLibrary.h"

// Concrete native selector implementation, not a full protocol claim. The
// system adapter may only publish after its21H461 semantic gate is complete.
@class CBMetalDevice,CBMetalBuffer,CBMetalTexture,CBMetalLibrary,CBMetalFunction;
@class CBMetalPipeline,CBMetalSampler,CBMetalQueue,CBMetalCommands,CBMetalRender;
NS_ASSUME_NONNULL_BEGIN
CBMetalDevice * _Nullable CBMetalCreateNativeDevice(uint32_t service,NSError **error);
@interface CBMetalDevice : NSObject
- (nullable instancetype)initWithTransport:(NativeMetal540DesktopSession::Transport)transport
    connectionOwner:(id)owner compiler:(NativeMetal540DesktopLibrary::Compile)compiler error:(NSError **)error;
- (nullable CBMetalBuffer *)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options;
- (nullable CBMetalBuffer *)newBufferWithBytes:(const void *)bytes length:(NSUInteger)length options:(MTLResourceOptions)options;
- (nullable CBMetalTexture *)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor;
- (nullable CBMetalTexture *)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor iosurface:(IOSurfaceRef _Nullable)surface plane:(NSUInteger)plane;
- (nullable CBMetalLibrary *)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error;
- (nullable CBMetalLibrary *)newLibraryWithURL:(NSURL *)url error:(NSError **)error;
- (nullable CBMetalPipeline *)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)descriptor error:(NSError **)error;
- (nullable CBMetalSampler *)newSamplerStateWithDescriptor:(MTLSamplerDescriptor *)descriptor;
- (nullable CBMetalQueue *)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)count;
- (nullable CBMetalQueue *)newCommandQueue;
- (NSUInteger)currentAllocatedSize;
- (void)setPublicDeviceIdentity:(id)device;
- (id)publicDeviceIdentity;
@end
@interface CBMetalBuffer : NSObject
@property(readonly) id device;
@property(readonly) NSUInteger length;
@property(readonly) NSUInteger allocatedSize;
@property(readonly) MTLStorageMode storageMode;
@property(readonly) MTLCPUCacheMode cpuCacheMode;
@property(readonly) MTLHazardTrackingMode hazardTrackingMode;
@property(nullable,copy) NSString *label;
- (nullable void *)contents;
@end
@interface CBMetalTexture : NSObject
@property(readonly) id device;
@property(readonly) NSUInteger width,height,depth,arrayLength,mipmapLevelCount,sampleCount;
@property(readonly) MTLPixelFormat pixelFormat;
@property(readonly) MTLTextureType textureType;
@property(readonly) MTLTextureUsage usage;
@property(readonly) MTLStorageMode storageMode;
@property(readonly) MTLHazardTrackingMode hazardTrackingMode;
@property(readonly) NSUInteger allocatedSize;
@property(readonly) IOSurfaceRef _Nullable iosurface;
@property(readonly) NSUInteger iosurfacePlane;
@property(nullable,copy) NSString *label;
- (void)replaceRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level withBytes:(const void *)bytes bytesPerRow:(NSUInteger)row;
- (void)getBytes:(void *)bytes bytesPerRow:(NSUInteger)row fromRegion:(MTLRegion)region mipmapLevel:(NSUInteger)level;
@end
@interface CBMetalLibrary : NSObject
@property(readonly) id device;
@property(readonly,copy) NSArray<NSString *> *functionNames;
@property(nullable,copy) NSString *label;
- (nullable CBMetalFunction *)newFunctionWithName:(NSString *)name;
- (nullable CBMetalFunction *)newFunctionWithName:(NSString *)name constantValues:(MTLFunctionConstantValues * _Nullable)values error:(NSError **)error;
- (void)newFunctionWithName:(NSString *)name constantValues:(MTLFunctionConstantValues * _Nullable)values completionHandler:(void (^)(CBMetalFunction * _Nullable,NSError * _Nullable))handler;
@end
@interface CBMetalFunction : NSObject
@property(readonly) id device;
@property(readonly,copy) NSString *name;
@property(readonly) MTLFunctionType functionType;
@end
@interface CBMetalPipeline : NSObject
@property(readonly) id device;
@property(readonly,copy,nullable) NSString *label;
@end
@interface CBMetalSampler : NSObject
@property(readonly) id device;
@end
@interface CBMetalQueue : NSObject
@property(readonly) id device;
@property(nullable,copy) NSString *label;
- (nullable CBMetalCommands *)commandBuffer;
@end
@interface CBMetalCommands : NSObject
@property(readonly) id device;
@property(readonly) MTLCommandBufferStatus status;
@property(readonly,nullable) NSError *error;
@property(nullable,copy) NSString *label;
- (nullable CBMetalRender *)renderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor *)descriptor;
- (void)addCompletedHandler:(void (^)(CBMetalCommands *))handler;
- (void)commit;
- (void)waitUntilCompleted;
@end
@interface CBMetalRender : NSObject
- (void)setRenderPipelineState:(CBMetalPipeline *)pipeline;
- (void)setVertexBuffer:(CBMetalBuffer *)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)setFragmentBuffer:(CBMetalBuffer *)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)setVertexBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index;
- (void)setFragmentBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index;
- (void)setVertexTexture:(CBMetalTexture *)texture atIndex:(NSUInteger)index;
- (void)setFragmentTexture:(CBMetalTexture *)texture atIndex:(NSUInteger)index;
- (void)setVertexSamplerState:(CBMetalSampler *)sampler atIndex:(NSUInteger)index;
- (void)setFragmentSamplerState:(CBMetalSampler *)sampler atIndex:(NSUInteger)index;
- (void)setViewport:(MTLViewport)viewport;
- (void)setScissorRect:(MTLScissorRect)rect;
- (void)drawPrimitives:(MTLPrimitiveType)primitive vertexStart:(NSUInteger)start vertexCount:(NSUInteger)count;
- (void)endEncoding;
@end
NS_ASSUME_NONNULL_END
