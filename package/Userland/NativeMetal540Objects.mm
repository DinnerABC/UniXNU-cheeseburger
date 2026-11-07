// SPDX-License-Identifier: GPL-2.0-only
// Owned-object semantics: ios_sagit 673f850... QcomMTL.mm newLibraryWithData,
// schedule/retirement, setBuffer, dispatchThreadgroups. Do not reuse iOS18 ABI.
#import "NativeMetal540Objects.h"
#include "NativeMetal540Compute.h"
#include "NativeMetal540Render.h"
#include "../../tools/gpu-native540/render_air_fixture.h"
#include "../../tools/gpu-native540/native540_objects_registry.h"
#import <CommonCrypto/CommonDigest.h>
#include <atomic>
#include <memory>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

static NSError *failure(NSString *text, NSInteger code=MTLCommandBufferErrorInvalidResource) {
    return [NSError errorWithDomain:MTLCommandBufferErrorDomain code:code
                          userInfo:@{NSLocalizedDescriptionKey:text}];
}
static NSError *creationFailure(NSString *text) {
    return [NSError errorWithDomain:MTLLibraryErrorDomain code:MTLLibraryErrorUnsupported
                          userInfo:@{NSLocalizedDescriptionKey:text}];
}
static bool emptyLinkedFunctions(MTLLinkedFunctions *f) {
    return !f||(!f.functions.count&&!f.binaryFunctions.count&&!f.groups.count&&!f.privateFunctions.count);
}
static bool pipelineOptions(MTLPipelineOption options, NSError **error) {
    if(options==MTLPipelineOptionNone)return true;
    if(error)*error=creationFailure(@"Pipeline reflection, archives and validation options are not provided");
    return false;
}
static bool textureDescriptor(MTLTextureDescriptor *d) {
    if(![d isKindOfClass:MTLTextureDescriptor.class])return false;
    auto swizzle=d.swizzle;
    return d.textureType==MTLTextureType2D&&d.pixelFormat==MTLPixelFormatRGBA8Unorm&&d.width==64&&d.height==64&&
        d.depth==1&&d.mipmapLevelCount==1&&d.sampleCount==1&&d.arrayLength==1&&d.storageMode==MTLStorageModeShared&&
        d.cpuCacheMode==MTLCPUCacheModeDefaultCache&&d.usage==MTLTextureUsageRenderTarget&&
        (d.resourceOptions==0||d.resourceOptions==MTLResourceHazardTrackingModeTracked)&&
        d.compressionType==MTLTextureCompressionTypeLossless&&swizzle.red==MTLTextureSwizzleRed&&
        swizzle.green==MTLTextureSwizzleGreen&&swizzle.blue==MTLTextureSwizzleBlue&&swizzle.alpha==MTLTextureSwizzleAlpha;
}
static NSString *digest(NSData *data) {
    if(data.length>UINT32_MAX)return nil;
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.bytes,(CC_LONG)data.length,hash);
    NSMutableString *s=[NSMutableString new];
    for(unsigned char c:hash)[s appendFormat:@"%02x",c];return s;
}
@interface Native540Session : NSObject {
@public
    Native540ObjectsTransport transport;
    id connection;
    __weak id publicDevice;
    std::unique_ptr<NativeMetal540Client> client;
    NSRecursiveLock *lock;
    dispatch_queue_t worker;
    NSMutableArray *owners, *records;
    uint64_t cursor, end;
    std::atomic<bool> lost;
    int allocationError;unsigned importedCount;
    unsigned pendingCommands;
    bool lastCompletion,retirementStarted,retirementProof;
    NativeMetal540SessionRetirement::Record retirement;
    NSMutableSet *retiredHandles,*invalidViews;
    NSArray *renderWorkspace;
    void *failedRetirementHold;
}
- (int)flushBuffer:(Native540Buffer *)buffer before:(bool)before;
@end
@interface Native540Device () { @public Native540Session *_session; }
@end
@interface Native540Buffer () {
@public Native540Session *_session; id _deviceIdentity;uint64_t _handle, _iova, _token;
    NSUInteger _length, _allocation; void *_cpu; id _mapping;NSString *_label;bool _imported;
}
- (instancetype)initOwned;
@end
@interface Native540Library () {
@public Native540Session *_session;id _deviceIdentity;NSData *_input, *_code, *_reflection, *_constants;
    NSDictionary *_hashes;NSString *_label;bool _graphics;
}
- (instancetype)initOwned;
@end
@interface Native540Function () { @public Native540Library *_library;NSString *_name,*_label;MTLFunctionType _type; }
- (instancetype)initOwned;
@end
@interface Native540ComputePipeline () { @public Native540Function *_function;NSString *_label; }
- (instancetype)initOwned;
@end
@interface Native540Texture () { @public Native540Buffer *_backing;id _surfaceOwner;IOSurfaceRef _surface;bool _completed,_submitted;uint64_t _generation; }
- (instancetype)initOwned;
@end
@interface Native540RenderPipeline () { @public Native540Function *_vertex,*_fragment;NSString *_label; }
- (instancetype)initOwned;
@end
@interface Native540CommandQueue () { @public Native540Session *_session;id _deviceIdentity; }
- (instancetype)initOwned;
@end
@interface Native540CommandBuffer () {
@public Native540Session *_session;id _deviceIdentity;NSCondition *_condition;
    MTLCommandBufferStatus _status; NSError *_error;
    NSMutableArray *_completionHandlers;
    bool _completionNotified,_completionDispatching,_executionRetired;
    NSThread *_callbackThread;
    bool _active, _ended, _dispatched, _committed;
    Native540ComputePipeline *_pipeline;
    NSArray<Native540Buffer *> *_buffers;
    NativeMetal540::Dispatch _dispatch;
    NativeMetal540::Resources _resources;
    Native540Buffer *_ib, *_shader, *_scratch;
    Native540Buffer *_fragmentShader;
    Native540Texture *_target;Native540RenderPipeline *_renderPipeline;
    Native540Render::Resources _renderResources;
    bool _graphics,_pendingReserved;
    uint64_t _targetGeneration;
    uint32_t _ibWords;
    const char *_preparationPhase;
}
- (instancetype)initOwned;
- (int)prepare;
- (void)execute;
- (void)reject:(NSString *)message;
- (void)notifyCompletion;
@end
@interface Native540RenderEncoder () {
@public Native540CommandBuffer *_command;Native540RenderPipeline *_pipeline;
    Native540Buffer *_vertex,*_fragment;bool _ended;
}
- (instancetype)initOwned;
- (BOOL)editable;
@end
@interface Native540ComputeEncoder () {
@public Native540CommandBuffer *_command; Native540ComputePipeline *_pipeline;
    Native540Buffer *_bindings[8]; NSUInteger _offsets[8];
    uint32_t _shared[8]; bool _ended;
}
- (instancetype)initOwned;
- (BOOL)editable;
@end

@implementation Native540Session
- (instancetype)init {
    if((self=[super init])){lock=[NSRecursiveLock new];owners=[NSMutableArray new];
        records=[NSMutableArray new];retiredHandles=[NSMutableSet new];invalidViews=[NSMutableSet new];
        pendingCommands=0;lastCompletion=retirementStarted=retirementProof=false;retirement={};worker=dispatch_queue_create("native540.objects",DISPATCH_QUEUE_SERIAL);lost=false;}
    return self;
}
- (void)dealloc {
    // No BoFree/unmap: kernel connection-retirement owns DMA safety, including
    // allocated BOs whose mapping callback failed before returning an owner.
    if(transport.retireConnection)transport.retireConnection(transport.context,lost.load(),[owners copy]);
}
- (int)flushBuffer:(Native540Buffer *)buffer before:(bool)before {
    if(buffer->_imported){
#if defined(__aarch64__)
        __asm__ volatile("dsb sy":::"memory");
#else
        std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
        return 0;
    }
    int rc=transport.flush(transport.context,buffer->_cpu,buffer->_allocation,before);
    if(rc)lost=true;return rc;
}
@end

// All BOs remain session-owned until connection retirement, bounded to 64 BOs.
// No imported memory or alias claims: callback supplies independent mappings
// of newly allocated kernel BOs, never borrowed/imported CPU storage.
static Native540Buffer *allocate(Native540Session *s,NSUInteger length,uint32_t permissions) {
    if(!length||length>NativeMetal540::MaxBuffer)return nil;
    [s->lock lock];
    Native540Buffer *b=nil;s->allocationError=NativeMetal540Client::Invalid;
    const uint64_t bytes=(uint64_t(length)+4095)&~4095ull;
    if(!s->lost.load()&&!s->pendingCommands&&!s->retirementStarted&&s->records.count<64&&bytes<=s->end-s->cursor) {
        b=[[Native540Buffer alloc] initOwned];b->_session=s;b->_deviceIdentity=s->publicDevice;b->_length=length;b->_allocation=bytes;
        int rc=s->client->allocate(bytes,b->_handle);
        if(!rc)for(NSDictionary *record in s->records)if([record[@"handle"] unsignedLongLongValue]==b->_handle)rc=NativeMetal540Client::Invalid;
        if(!rc)rc=s->client->map(b->_handle,s->cursor,permissions,b->_iova);
        if(!rc) {
            NativeAdrenoAbiBoCpuMap in{b->_handle};NativeAdrenoAbiBoCpuMapReply out{};size_t n=sizeof(out);
            rc=s->transport.call(s->transport.context,NativeAdrenoMethodBoCpuMap,&in,sizeof(in),&out,&n);
            if(!rc&&(n!=sizeof(out)||!out.token||out.reserved||out.bytes!=bytes))rc=NativeMetal540Client::Invalid;
            b->_token=out.token;
        }
        if(!rc)for(NSDictionary *record in s->records)if([record[@"token"] unsignedLongLongValue]==b->_token)rc=NativeMetal540Client::Invalid;
        if(!rc) {
            size_t mapped=0;id owner=nil;
            rc=s->transport.mapView(s->transport.context,b->_token,&b->_cpu,&mapped,&owner);
            if(owner)[s->owners addObject:owner];b->_mapping=owner;
            uintptr_t address=(uintptr_t)b->_cpu;
            if(!rc&&(!owner||!address||(address&3)||mapped!=bytes||bytes>UINTPTR_MAX-address))rc=NativeMetal540Client::Invalid;
            if(!rc)for(NSDictionary *record in s->records) {
                uintptr_t start=[record[@"cpu"] unsignedLongLongValue];uint64_t extent=[record[@"bytes"] unsignedLongLongValue];
                if(address<start+extent&&start<address+bytes)rc=NativeMetal540Client::Invalid;
            }
        }
        if(rc){s->allocationError=rc;s->lost=true;b=nil;}
        else {
            s->allocationError=0;
            [s->records addObject:@{@"handle":@(b->_handle),@"token":@(b->_token),@"cpu":@((uintptr_t)b->_cpu),@"bytes":@(bytes),@"iova":@(b->_iova),@"imported":@NO}];
            s->cursor+=bytes;memset(b->_cpu,0,bytes);
        }
    }
    [s->lock unlock];return b;
}
@implementation Native540Device
- (instancetype)initControlledWithTransport:(Native540ObjectsTransport)t error:(NSError **)error {
    if(error)*error=nil;
    if(!t.call||!t.mapView||!t.flush||!t.retireConnection||!t.connectionOwner||!t.verifiedCPUInhibit) {
        if(error)*error=failure(@"Verified CPU-inhibit mapping/flush and connection retirement required");return nil;
    }
    if(!(self=[super init]))return nil;
    _session=[Native540Session new];auto s=_session;s->transport=t;s->connection=t.connectionOwner;s->publicDevice=self;
    s->client=std::make_unique<NativeMetal540Client>(t.call,t.context);
    int rc=s->client->query();NativeAdrenoAbiDeviceInfo info{};size_t n=sizeof(info);
    if(!rc)rc=t.call(t.context,NativeAdrenoMethodGetDeviceInfo,nullptr,0,&info,&n);
    if(!rc&&(n!=sizeof(info)||info.chip_id!=0x05040001||info.reserved||info.flags!=NativeAdrenoDeviceClientIova||
        !info.va_start||(info.va_start&4095)||info.va_start>=NativeMetal540::AddressLimit||info.va_size<=0x40000||
        info.va_size>NativeMetal540::AddressLimit-info.va_start))rc=NativeMetal540Client::Invalid;
    if(rc){s->lost=true;if(error)*error=failure(@"A540 connection identity/query rejected");return nil;}
    // Exclusive connection: preserve the established P1/probe low reservations.
    s->cursor=info.va_start+0x40000;s->end=info.va_start+info.va_size;return self;
}
- (Native540Buffer *)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options {
    // storageModeShared stays Shared; physical CPU-inhibit is not Metal caching.
    return options==MTLResourceStorageModeShared||options==MTLResourceHazardTrackingModeTracked?allocate(_session,length,3):nil;
}
- (Native540Buffer *)newBufferWithBytes:(const void*)bytes length:(NSUInteger)n options:(MTLResourceOptions)o {
    if(!bytes||!n)return nil;auto s=_session;[s->lock lock];Native540Buffer *b=[self newBufferWithLength:n options:o];
    if(b)memcpy(b->_cpu,bytes,n);[s->lock unlock];return b;
}
- (Native540Library *)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error {
    if(error)*error=nil;
    const size_t size=data?dispatch_data_get_size(data):0;
    if(size!=Native540ObjectsRegistry::InputBytes&&size!=92788) {
        if(error)*error=failure(@"Only the fixed trusted 21H461 metallib snapshots are admitted");return nil;
    }
    const void *bytes=nullptr;size_t length=0;
    dispatch_data_t mapping=dispatch_data_create_map(data,&bytes,&length);
    if(!mapping||length!=size||!bytes)return nil;
    NSData *input=[NSData dataWithBytes:bytes length:length];
    if(size==92788){
        using namespace Native540RenderAIRFixture;
        NSData *v=[NSData dataWithBytes:vsCode length:sizeof(vsCode)],*f=[NSData dataWithBytes:fsCode length:sizeof(fsCode)];
        NSData *vr=[NSData dataWithBytes:vsReflection length:sizeof(vsReflection)],*fr=[NSData dataWithBytes:fsReflection length:sizeof(fsReflection)];
        if(memcmp(input.bytes,"MTLB",4)||![digest(input) isEqualToString:@(librarySHA256)]||
           ![digest(v) isEqualToString:@(vsCodeSHA256)]||![digest(f) isEqualToString:@(fsCodeSHA256)]||
           ![digest(vr) isEqualToString:@(vsReflectionSHA256)]||![digest(fr) isEqualToString:@(fsReflectionSHA256)]){
            if(error)*error=failure(@"Unknown/corrupt SpriteKit AIR registry");return nil;
        }
        Native540Library *lib=[[Native540Library alloc] initOwned];lib->_session=_session;lib->_deviceIdentity=_session->publicDevice;lib->_input=input;lib->_graphics=true;
        lib->_hashes=@{@"input":digest(input),@"vertex":digest(v),@"fragment":digest(f),@"vertexReflection":digest(vr),@"fragmentReflection":digest(fr)};
        return lib;
    }
    using namespace Native540ObjectsRegistry;
    NSData *shaderBytes=[NSData dataWithBytes:Code length:sizeof(Code)];
    NSData *reflection=[NSData dataWithBytes:Reflection length:sizeof(Reflection)];
    NSData *constants=[NSData dataWithBytes:Constants length:sizeof(Constants)];
    if(input.length<4||memcmp(input.bytes,"MTLB",4)||![digest(input) isEqualToString:@"fa9101b5c07bd91720de2cbbcb170b3acd061d8eae4bf13bc6f9f7fc07794e9b"]||
       ![digest(shaderBytes) isEqualToString:@(CodeHash)]||![digest(reflection) isEqualToString:@(ReflectionHash)]||
       ![digest(constants) isEqualToString:@(ConstantsHash)]||!NativeMetal540::validInfo(Info)) {
        if(error)*error=failure(@"Unknown/corrupt metallib or trusted AIR registry snapshot; no GLSL/manifest inputs");return nil;
    }
    Native540Library *lib=[[Native540Library alloc] initOwned];lib->_session=_session;lib->_deviceIdentity=_session->publicDevice;
    lib->_input=input;lib->_code=shaderBytes;lib->_reflection=reflection;lib->_constants=constants;
    lib->_hashes=@{@"input":digest(input),@"code":digest(shaderBytes),@"reflection":digest(reflection),@"constantMetadata":digest(constants)};
    return lib;
}
- (Native540Library *)newLibraryWithFile:(NSString *)path error:(NSError **)error {
    if(error)*error=nil;
    if(![path isKindOfClass:NSString.class]||!path.length){if(error)*error=creationFailure(@"A local metallib path is required");return nil;}
    return [self newLibraryWithURL:[NSURL fileURLWithPath:path] error:error];
}
- (Native540Library *)newLibraryWithURL:(NSURL *)url error:(NSError **)error {
    if(error)*error=nil;
    if(![url isKindOfClass:NSURL.class]||!url.isFileURL){if(error)*error=creationFailure(@"A local metallib URL is required");return nil;}
    int fd=open(url.fileSystemRepresentation,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);struct stat before{},after{};
    if(fd<0){if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:nil];return nil;}
    bool valid=fstat(fd,&before)==0&&S_ISREG(before.st_mode)&&
        (before.st_size==Native540ObjectsRegistry::InputBytes||before.st_size==92788);
    NSMutableData *bytes=valid?[NSMutableData dataWithLength:(NSUInteger)before.st_size]:nil;size_t readBytes=0;
    while(valid&&readBytes<bytes.length){ssize_t n=read(fd,(uint8_t*)bytes.mutableBytes+readBytes,bytes.length-readBytes);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){valid=false;break;}readBytes+=size_t(n);
    }
    valid=valid&&fstat(fd,&after)==0&&after.st_size==before.st_size&&after.st_ino==before.st_ino&&after.st_dev==before.st_dev;
    if(close(fd))valid=false;
    if(!valid){if(error)*error=creationFailure(@"A stable regular registered metallib file is required");return nil;}
    auto data=dispatch_data_create(bytes.bytes,bytes.length,nullptr,DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    return [self newLibraryWithData:data error:error];
}
- (Native540Library *)newDefaultLibrary {return [self newDefaultLibraryWithBundle:NSBundle.mainBundle error:nullptr];}
- (Native540Library *)newDefaultLibraryWithBundle:(NSBundle *)bundle error:(NSError **)error {
    if(error)*error=nil;
    NSString *path=[bundle isKindOfClass:NSBundle.class]?[bundle pathForResource:@"default" ofType:@"metallib"]:nil;
    if(!path){
        if(error)*error=[NSError errorWithDomain:MTLLibraryErrorDomain code:MTLLibraryErrorFileNotFound
            userInfo:@{NSLocalizedDescriptionKey:@"No default metallib in the requested bundle"}];
        return nil;
    }
    return [self newLibraryWithFile:path error:error];
}
- (Native540Library *)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options error:(NSError **)error {
    (void)source;(void)options;
    if(error)*error=creationFailure(@"Runtime source compilation is not provided; use a registered metallib");return nil;
}
- (void)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options completionHandler:(void (^)(Native540Library *,NSError *))handler {
    if(!handler)return;source=[source copy];options=[options copy];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool {
        NSError *error=nil;Native540Library *library=[self newLibraryWithSource:source options:options error:&error];handler(library,error);
    }});
}
- (Native540ComputePipeline *)newComputePipelineStateWithFunction:(Native540Function *)function error:(NSError **)error {
    if(error)*error=nil;
    if(![function isKindOfClass:Native540Function.class]||function->_library->_session!=_session||function->_type!=MTLFunctionTypeKernel) {
        if(error)*error=failure(@"Foreign or missing trusted compute function");return nil;
    }
    Native540ComputePipeline *p=[[Native540ComputePipeline alloc] initOwned];p->_function=function;return p;
}
- (Native540ComputePipeline *)newComputePipelineStateWithFunction:(Native540Function *)function options:(MTLPipelineOption)options reflection:(MTLComputePipelineReflection **)reflection error:(NSError **)error {
    if(error)*error=nil;if(reflection)*reflection=nil;
    if(!pipelineOptions(options,error))return nil;
    return [self newComputePipelineStateWithFunction:function error:error];
}
- (Native540ComputePipeline *)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d options:(MTLPipelineOption)options reflection:(MTLComputePipelineReflection **)reflection error:(NSError **)error {
    if(error)*error=nil;if(reflection)*reflection=nil;
    if(!pipelineOptions(options,error))return nil;
    if(![d isKindOfClass:MTLComputePipelineDescriptor.class]){if(error)*error=creationFailure(@"A native compute descriptor is required");return nil;}
    d=[d copy];auto stage=d.stageInputDescriptor;
    bool valid=(!stage||[stage isEqual:[MTLStageInputOutputDescriptor stageInputOutputDescriptor]])&&
        !d.supportIndirectCommandBuffers&&!d.binaryArchives.count&&!d.preloadedLibraries.count&&
        emptyLinkedFunctions(d.linkedFunctions)&&(d.maxTotalThreadsPerThreadgroup==0||d.maxTotalThreadsPerThreadgroup==64);
    for(unsigned i=0;i<31&&valid;++i)valid=d.buffers[i].mutability==MTLMutabilityDefault;
    if(!valid){if(error)*error=creationFailure(@"Unsupported compute descriptor features");return nil;}
    auto pipeline=[self newComputePipelineStateWithFunction:(Native540Function*)d.computeFunction error:error];
    if(pipeline)pipeline->_label=[d.label copy];return pipeline;
}
- (void)newComputePipelineStateWithFunction:(Native540Function *)function completionHandler:(void (^)(Native540ComputePipeline *,NSError *))handler {
    if(!handler)return;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool {
        NSError *error=nil;auto pipeline=[self newComputePipelineStateWithFunction:function error:&error];handler(pipeline,error);
    }});
}
- (void)newComputePipelineStateWithFunction:(Native540Function *)function options:(MTLPipelineOption)options completionHandler:(void (^)(Native540ComputePipeline *,MTLComputePipelineReflection *,NSError *))handler {
    if(!handler)return;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool {
        NSError *error=nil;MTLComputePipelineReflection *reflection=nil;
        auto pipeline=[self newComputePipelineStateWithFunction:function options:options reflection:&reflection error:&error];handler(pipeline,reflection,error);
    }});
}
- (void)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d options:(MTLPipelineOption)options completionHandler:(void (^)(Native540ComputePipeline *,MTLComputePipelineReflection *,NSError *))handler {
    if(!handler)return;d=[d copy];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool {
        NSError *error=nil;MTLComputePipelineReflection *reflection=nil;
        auto pipeline=[self newComputePipelineStateWithDescriptor:d options:options reflection:&reflection error:&error];handler(pipeline,reflection,error);
    }});
}
- (Native540Texture *)newTextureWithDescriptor:(MTLTextureDescriptor *)d {
    if(!textureDescriptor(d))return nil;
    Native540Buffer *b=allocate(_session,16384,3);if(!b)return nil;
    Native540Texture *t=[[Native540Texture alloc] initOwned];t->_backing=b;return t;
}
- (Native540Texture *)newTextureWithDescriptor:(MTLTextureDescriptor *)d iosurface:(IOSurfaceRef)surface plane:(NSUInteger)plane {
    if(!surface||plane||CFGetTypeID(surface)!=IOSurfaceGetTypeID()||!textureDescriptor(d))return nil;
    uint64_t address=(uintptr_t)IOSurfaceGetBaseAddress(surface);
    if(IOSurfaceGetPlaneCount(surface)||IOSurfaceGetWidth(surface)!=64||IOSurfaceGetHeight(surface)!=64||
       IOSurfaceGetBytesPerRow(surface)!=256||IOSurfaceGetAllocSize(surface)!=16384||IOSurfaceGetPixelFormat(surface)!=0x52474241||
       IOSurfaceGetBytesPerElement(surface)!=4||IOSurfaceGetElementWidth(surface)!=1||IOSurfaceGetElementHeight(surface)!=1||
       !address||(address&4095)||address>UINTPTR_MAX-16384||IOSurfaceGetBaseAddressOfPlane(surface,0)!=(void*)(uintptr_t)address||
       IOSurfaceGetWidthOfPlane(surface,0)!=64||IOSurfaceGetHeightOfPlane(surface,0)!=64||
       IOSurfaceGetBytesPerRowOfPlane(surface,0)!=256||IOSurfaceGetBytesPerElementOfPlane(surface,0)!=4)return nil;
    auto s=_session;[s->lock lock];
    if(s->lost.load()||s->pendingCommands||s->retirementStarted||s->importedCount||s->records.count>=64||16384>s->end-s->cursor){[s->lock unlock];return nil;}
    id owner=CFBridgingRelease(CFRetain(surface));[s->owners addObject:owner];
    Native540Buffer *b=[[Native540Buffer alloc] initOwned];b->_session=s;b->_deviceIdentity=s->publicDevice;
    b->_imported=true;b->_cpu=(void*)(uintptr_t)address;b->_length=b->_allocation=16384;b->_mapping=owner;
    NativeAdrenoAbiBoImport in{address,16384};NativeAdrenoAbiBoAllocReply reply{};size_t n=sizeof reply;
    int rc=s->transport.call(s->transport.context,NativeAdrenoMethodBoImport,&in,sizeof in,&reply,&n);
    if(!rc&&(n!=sizeof reply||!reply.handle))rc=NativeMetal540Client::Invalid;b->_handle=reply.handle;
    for(NSDictionary *r in s->records)if(!rc&&([r[@"handle"] unsignedLongLongValue]==b->_handle||
        (address<[r[@"cpu"] unsignedLongLongValue]+[r[@"bytes"] unsignedLongLongValue]&&[r[@"cpu"] unsignedLongLongValue]<address+16384)))rc=NativeMetal540Client::Invalid;
    if(!rc)rc=s->client->map(b->_handle,s->cursor,3,b->_iova);
    if(rc){s->lost=true;[s->lock unlock];return nil;}
    [s->records addObject:@{@"handle":@(b->_handle),@"token":@0,@"cpu":@(address),@"bytes":@16384,@"iova":@(b->_iova),@"imported":@YES}];s->cursor+=16384;++s->importedCount;
    Native540Texture *t=[[Native540Texture alloc] initOwned];t->_backing=b;t->_surface=surface;t->_surfaceOwner=owner;
    [s->lock unlock];return t;
}
- (Native540RenderPipeline *)newRenderPipelineWithVertexFunction:(Native540Function *)v
    fragmentFunction:(Native540Function *)f error:(NSError **)error {
    if(error)*error=nil;
    if(![v isKindOfClass:Native540Function.class]||![f isKindOfClass:Native540Function.class]||
       v->_library->_session!=_session||f->_library!=v->_library||!v->_library->_graphics||
       v->_type!=MTLFunctionTypeVertex||f->_type!=MTLFunctionTypeFragment){
        if(error)*error=failure(@"Foreign or unsupported graphics function pair");return nil;
    }
    Native540RenderPipeline *p=[[Native540RenderPipeline alloc] initOwned];p->_vertex=v;p->_fragment=f;return p;
}
- (Native540RenderPipeline *)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)d error:(NSError **)error {
    if(error)*error=nil;
    if(![d isKindOfClass:MTLRenderPipelineDescriptor.class]){if(error)*error=creationFailure(@"A native render descriptor is required");return nil;}
    d=[d copy];
    bool valid=d.rasterSampleCount==1&&d.depthAttachmentPixelFormat==MTLPixelFormatInvalid&&
        d.stencilAttachmentPixelFormat==MTLPixelFormatInvalid&&d.rasterizationEnabled&&
        !d.alphaToCoverageEnabled&&!d.alphaToOneEnabled&&emptyLinkedFunctions(d.vertexLinkedFunctions)&&emptyLinkedFunctions(d.fragmentLinkedFunctions)&&
        !d.supportIndirectCommandBuffers&&!d.binaryArchives.count&&!d.vertexPreloadedLibraries.count&&!d.fragmentPreloadedLibraries.count&&
        d.maxVertexAmplificationCount==1;
    for(unsigned i=0;i<31&&valid;++i)valid=d.vertexBuffers[i].mutability==MTLMutabilityDefault&&d.fragmentBuffers[i].mutability==MTLMutabilityDefault;
    for(unsigned i=0;i<31&&valid;++i)valid=!d.vertexDescriptor||d.vertexDescriptor.attributes[i].format==MTLVertexFormatInvalid;
    for(unsigned i=0;i<31&&valid;++i)valid=!d.vertexDescriptor||d.vertexDescriptor.layouts[i].stride==0;
    for(unsigned i=0;i<8&&valid;++i){auto a=d.colorAttachments[i];
        valid=a.pixelFormat==(i?MTLPixelFormatInvalid:MTLPixelFormatRGBA8Unorm)&&!a.blendingEnabled&&a.writeMask==MTLColorWriteMaskAll;}
    if(!valid){if(error)*error=failure(@"Unsupported graphics descriptor");return nil;}
    auto pipeline=[self newRenderPipelineWithVertexFunction:(Native540Function*)d.vertexFunction fragmentFunction:(Native540Function*)d.fragmentFunction error:error];
    if(pipeline)pipeline->_label=[d.label copy];return pipeline;
}
- (Native540RenderPipeline *)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)d options:(MTLPipelineOption)options reflection:(MTLRenderPipelineReflection **)reflection error:(NSError **)error {
    if(error)*error=nil;if(reflection)*reflection=nil;
    if(!pipelineOptions(options,error))return nil;
    return [self newRenderPipelineStateWithDescriptor:d error:error];
}
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)d completionHandler:(void (^)(Native540RenderPipeline *,NSError *))handler {
    if(!handler)return;d=[d copy];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool {
        NSError *error=nil;auto pipeline=[self newRenderPipelineStateWithDescriptor:d error:&error];handler(pipeline,error);
    }});
}
- (void)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)d options:(MTLPipelineOption)options completionHandler:(void (^)(Native540RenderPipeline *,MTLRenderPipelineReflection *,NSError *))handler {
    if(!handler)return;d=[d copy];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool {
        NSError *error=nil;MTLRenderPipelineReflection *reflection=nil;
        auto pipeline=[self newRenderPipelineStateWithDescriptor:d options:options reflection:&reflection error:&error];handler(pipeline,reflection,error);
    }});
}
- (void)setPublicDeviceIdentity:(id)device {_session->publicDevice=device;}
- (int)retireResources {
    auto s=_session;[s->lock lock];
    if(s->retirementStarted){int rc=s->retirement.error?s->retirement.error:NativeMetal540SessionRetirement::AlreadyAttempted;[s->lock unlock];return rc;}
    if(s->pendingCommands||!s->lastCompletion||s->lost.load()){[s->lock unlock];return NativeMetal540SessionRetirement::Prerequisite;}
    s->retirementStarted=true;
    NativeMetal540SessionRetirement::Transport t{};t.call=s->transport.call;t.unmapView=s->transport.unmapView;
    t.context=s->transport.context;t.session=s->transport.session;
    auto ledger=std::make_unique<NativeMetal540SessionRetirement>(t);int rc=0;
    for(NSDictionary *r in s->records){NativeMetal540SessionRetirement::Resource b{};
        b.handle=[r[@"handle"] unsignedLongLongValue];b.iova=[r[@"iova"] unsignedLongLongValue];
        b.address=[r[@"cpu"] unsignedLongLongValue];b.bytes=[r[@"bytes"] unsignedLongLongValue];
        b.token=[r[@"token"] unsignedIntValue];b.imported=[r[@"imported"] boolValue];b.cpuMapped=!b.imported;b.session=t.session;
        rc=ledger->add(b);if(rc)break;
    }
    if(!rc)rc=ledger->retire(true,true,true);s->retirement=ledger->result();
    for(unsigned i=0;i<s->retirement.count;++i){const auto &b=s->retirement.bos[i];
        if(b.viewUnmapped&&!b.resource.imported)[s->invalidViews addObject:@(b.resource.handle)];
        if(b.freeAck)[s->retiredHandles addObject:@(b.resource.handle)];
    }
    if(rc){s->lost=true;if(!s->failedRetirementHold)s->failedRetirementHold=(void*)CFBridgingRetain(s);}
    else {s->retirementProof=true;s->renderWorkspace=nil;[s->records removeAllObjects];[s->owners removeAllObjects];}
    [s->lock unlock];return rc;
}
- (NativeMetal540SessionRetirement::Record)retirementResult {auto s=_session;[s->lock lock];auto r=s->retirement;[s->lock unlock];return r;}
- (NSUInteger)currentAllocatedSize {
    auto s=_session;[s->lock lock];NSUInteger total=0;
    for(NSDictionary *r in s->records)total+=[r[@"bytes"] unsignedLongLongValue];
    [s->lock unlock];return total;
}
- (Native540CommandQueue *)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)count {
    // The legacy fixed-workload backend has no bounded retained-command pool.
    (void)count;return nil;
}
- (Native540CommandQueue *)newCommandQueue {
    Native540CommandQueue *q=[[Native540CommandQueue alloc] initOwned];q->_session=_session;q->_deviceIdentity=_session->publicDevice;return q;
}
@end
@implementation Native540Buffer
- (instancetype)initOwned {return [super init];}
- (NSUInteger)length {return _length;}
- (id)device {return _deviceIdentity;}
- (MTLStorageMode)storageMode {return MTLStorageModeShared;}
- (MTLCPUCacheMode)cpuCacheMode {return MTLCPUCacheModeDefaultCache;}
- (MTLHazardTrackingMode)hazardTrackingMode {return MTLHazardTrackingModeTracked;}
- (MTLResourceOptions)resourceOptions {return MTLResourceStorageModeShared|MTLResourceHazardTrackingModeTracked;}
- (NSUInteger)allocatedSize {return _allocation;}
- (NSUInteger)protectionOptions {return 0;}
- (id)heap {return nil;}
- (NSUInteger)heapOffset {return 0;}
- (BOOL)isAliasable {return NO;}
- (void)makeAliasable {[NSException raise:NSInvalidArgumentException format:@"Only heap resources may become aliasable"];}
- (MTLPurgeableState)setPurgeableState:(MTLPurgeableState)state {
    if(state!=MTLPurgeableStateKeepCurrent&&state!=MTLPurgeableStateNonVolatile)
        [NSException raise:NSInvalidArgumentException format:@"This backend retains nonvolatile DMA storage until proven retirement"];
    return MTLPurgeableStateNonVolatile;
}
- (NSString*)label {return _label;}
- (void)setLabel:(NSString*)value {_label=[value copy];}
- (void *)contents {auto s=_session;[s->lock lock];void *p=s->lost.load()||s->pendingCommands||s->retirementStarted||[s->invalidViews containsObject:@(_handle)]||[s->retiredHandles containsObject:@(_handle)]?nullptr:_cpu;[s->lock unlock];return p;}
@end
@implementation Native540Library
- (instancetype)initOwned {return [super init];}
- (NSArray *)functionNames {return _graphics?@[@(Native540RenderAIRFixture::vsEntry),@(Native540RenderAIRFixture::fsEntry)]:@[@(Native540ObjectsRegistry::Entry)];}
- (id)device {return _deviceIdentity;}
- (NSDictionary *)snapshotHashes {return _hashes;}
- (NSString *)label {return _label;}
- (void)setLabel:(NSString *)label {_label=[label copy];}
- (Native540Function *)newFunctionWithName:(NSString *)name {
    MTLFunctionType type=MTLFunctionTypeKernel;
    if(_graphics){
        if([name isEqualToString:@(Native540RenderAIRFixture::vsEntry)])type=MTLFunctionTypeVertex;
        else if([name isEqualToString:@(Native540RenderAIRFixture::fsEntry)])type=MTLFunctionTypeFragment;
        else return nil;
    } else if(![name isEqualToString:@(Native540ObjectsRegistry::Entry)])return nil;
    Native540Function *f=[[Native540Function alloc] initOwned];f->_library=self;f->_name=[name copy];f->_type=type;return f;
}
@end
@implementation Native540Function
- (instancetype)initOwned {return [super init];}
- (NSString *)name {return _name;}
- (id)device {return _library.device;}
- (MTLFunctionType)functionType {return _type;}
- (NSString *)label {return _label;}
- (void)setLabel:(NSString *)label {_label=[label copy];}
- (NSArray *)vertexAttributes {return @[];}
- (NSArray *)stageInputAttributes {return @[];}
- (MTLPatchType)patchType {return MTLPatchTypeNone;}
- (NSInteger)patchControlPointCount {return 0;}
- (MTLFunctionOptions)options {return MTLFunctionOptionNone;}
- (id)newArgumentEncoderWithBufferIndex:(NSUInteger)index {(void)index;return nil;}
// Fixed export has no unsatisfied function constants. Specialization not exposed.
- (NSDictionary *)functionConstantsDictionary {return @{};}
@end
@implementation Native540ComputePipeline
- (instancetype)initOwned {return [super init];}
- (NSUInteger)threadExecutionWidth {return Native540ObjectsRegistry::Info.subgroupSize;}
- (id)device {return _function.device;}
- (NSUInteger)maxTotalThreadsPerThreadgroup {return 64;}
- (NSUInteger)staticThreadgroupMemoryLength {return Native540ObjectsRegistry::Info.sharedBytes;}
- (NSString *)label {return _label;}
- (NSUInteger)imageblockMemoryLengthForDimensions:(MTLSize)dimensions {
    (void)dimensions;[NSException raise:NSInvalidArgumentException format:@"Imageblocks are not provided by this backend"];return 0;
}
@end
@implementation Native540Texture
- (instancetype)initOwned {return [super init];}
- (NSUInteger)width {return 64;}
- (id)device {return _backing.device;}
- (NSUInteger)height {return 64;}
- (MTLPixelFormat)pixelFormat {return MTLPixelFormatRGBA8Unorm;}
- (MTLTextureType)textureType {return MTLTextureType2D;}
- (NSUInteger)depth {return 1;}
- (NSUInteger)arrayLength {return 1;}
- (NSUInteger)mipmapLevelCount {return 1;}
- (NSUInteger)sampleCount {return 1;}
- (MTLTextureUsage)usage {return MTLTextureUsageRenderTarget;}
- (MTLStorageMode)storageMode {return _backing.storageMode;}
- (MTLCPUCacheMode)cpuCacheMode {return _backing.cpuCacheMode;}
- (MTLHazardTrackingMode)hazardTrackingMode {return _backing.hazardTrackingMode;}
- (MTLResourceOptions)resourceOptions {return _backing.resourceOptions;}
- (NSUInteger)allocatedSize {return _backing.allocatedSize;}
- (IOSurfaceRef)iosurface {return _surface;}
- (NSUInteger)iosurfacePlane {return 0;}
- (NSUInteger)protectionOptions {return _backing.protectionOptions;}
- (id)heap {return nil;}
- (NSUInteger)heapOffset {return 0;}
- (BOOL)isAliasable {return NO;}
- (void)makeAliasable {[_backing makeAliasable];}
- (MTLPurgeableState)setPurgeableState:(MTLPurgeableState)state {return [_backing setPurgeableState:state];}
- (id)rootResource {return nil;}
- (Native540Texture*)parentTexture {return nil;}
- (NSUInteger)parentRelativeLevel {return 0;}
- (NSUInteger)parentRelativeSlice {return 0;}
- (Native540Buffer*)buffer {return nil;}
- (NSUInteger)bufferOffset {return 0;}
- (NSUInteger)bufferBytesPerRow {return 0;}
- (BOOL)isFramebufferOnly {return NO;}
- (BOOL)isShareable {return _surface!=nullptr;}
- (BOOL)isSparse {return NO;}
- (BOOL)allowGPUOptimizedContents {return NO;}
- (MTLTextureCompressionType)compressionType {return MTLTextureCompressionTypeLossless;}
- (MTLTextureSwizzleChannels)swizzle {return MTLTextureSwizzleChannelsDefault;}
- (Native540Texture*)newTextureViewWithPixelFormat:(MTLPixelFormat)format {(void)format;return nil;}
- (Native540Texture*)newTextureViewWithPixelFormat:(MTLPixelFormat)format textureType:(MTLTextureType)type levels:(NSRange)levels slices:(NSRange)slices {
    (void)format;(void)type;(void)levels;(void)slices;return nil;
}
- (Native540Texture*)newTextureViewWithPixelFormat:(MTLPixelFormat)format textureType:(MTLTextureType)type levels:(NSRange)levels slices:(NSRange)slices swizzle:(MTLTextureSwizzleChannels)swizzle {
    (void)format;(void)type;(void)levels;(void)slices;(void)swizzle;return nil;
}
- (NSString*)label {return _backing.label;}
- (void)setLabel:(NSString*)value {_backing.label=value;}
- (BOOL)getBytes:(void *)bytes bytesPerRow:(NSUInteger)pitch {
    auto s=_backing->_session;[s->lock lock];bool valid=bytes&&pitch==256&&_completed&&!_submitted&&!s->lost.load()&&!s->retirementStarted;
    uint32_t seed=0;bool locked=valid&&_surface;
    int lockRC=locked?int(IOSurfaceLock(_surface,kIOSurfaceLockReadOnly,&seed)):0;
    valid=valid&&!lockRC;
    if(valid)memcpy(bytes,_backing->_cpu,16384);
    if(locked&&!lockRC){int unlocked=int(IOSurfaceUnlock(_surface,kIOSurfaceLockReadOnly,&seed));valid=valid&&!unlocked;}
    [s->lock unlock];return valid;
}
- (void)getBytes:(void *)bytes bytesPerRow:(NSUInteger)pitch fromRegion:(MTLRegion)r mipmapLevel:(NSUInteger)level {
    if(level||r.origin.x||r.origin.y||r.origin.z||r.size.width!=64||r.size.height!=64||r.size.depth!=1||
       ![self getBytes:bytes bytesPerRow:pitch])
        [NSException raise:NSInvalidArgumentException format:@"Unsupported or unfinished texture readback"];
}
@end
@implementation Native540RenderPipeline
- (instancetype)initOwned {return [super init];}
- (id)device {return _vertex.device;}
- (NSString *)label {return _label;}
- (BOOL)supportsVertexAmplificationCount:(NSUInteger)count {return count==1;}
@end
@implementation Native540CommandQueue
- (instancetype)initOwned {return [super init];}
- (id)device {return _deviceIdentity;}
- (Native540CommandBuffer *)commandBuffer {
    Native540CommandBuffer *b=[[Native540CommandBuffer alloc] initOwned];b->_session=_session;b->_deviceIdentity=_deviceIdentity;return b;
}
@end
@implementation Native540CommandBuffer
- (instancetype)initOwned {
    if((self=[super init])){_condition=[NSCondition new];_completionHandlers=[NSMutableArray new];_status=MTLCommandBufferStatusNotEnqueued;}
    return self;
}
- (id)device {return _deviceIdentity;}
- (void)addCompletedHandler:(void (^)(Native540CommandBuffer *))handler {
    if(!handler)return;
    [_condition lock];bool done=_completionNotified;
    if(!done)[_completionHandlers addObject:[handler copy]];[_condition unlock];
    if(done)handler(self);
}
- (void)notifyCompletion {
    [_condition lock];
    if(_completionNotified||(_status!=MTLCommandBufferStatusCompleted&&_status!=MTLCommandBufferStatusError)){[_condition unlock];return;}
    _completionNotified=true;_completionDispatching=true;_callbackThread=NSThread.currentThread;
    NSArray *handlers=[_completionHandlers copy];[_completionHandlers removeAllObjects];[_condition unlock];
    @try {for(void (^handler)(Native540CommandBuffer *) in handlers)handler(self);}
    @finally {[_condition lock];_completionDispatching=false;_callbackThread=nil;[_condition broadcast];[_condition unlock];}
}
- (MTLCommandBufferStatus)status {[_condition lock];auto s=_status;[_condition unlock];return s;}
- (BOOL)executionRetired {[_condition lock];bool retired=_executionRetired;[_condition unlock];return retired;}
- (NSError *)error {[_condition lock];NSError *e=_error;[_condition unlock];return e;}
- (void)reject:(NSString *)message {if(!_error)_error=failure(message);}
- (Native540ComputeEncoder *)computeCommandEncoder {
    [_condition lock];Native540ComputeEncoder *e=nil;
    if(_active||_ended||_committed||_error)[self reject:@"Only one uncommitted compute encoder is supported"];
    else {e=[[Native540ComputeEncoder alloc] initOwned];e->_command=self;_active=true;}
    [_condition unlock];return e;
}
- (Native540RenderEncoder *)renderCommandEncoderWithTexture:(Native540Texture *)texture {
    auto s=_session;[s->lock lock];[_condition lock];Native540RenderEncoder *e=nil;
    if(_active||_ended||_committed||_error||s->retirementStarted||s->lost.load()||![texture isKindOfClass:Native540Texture.class]||
       texture->_backing->_session!=s||texture->_submitted)[self reject:@"Foreign/reused render texture or encoder"];
    else {e=[[Native540RenderEncoder alloc] initOwned];e->_command=self;_target=texture;_targetGeneration=texture->_generation;_graphics=true;_active=true;}
    [_condition unlock];[s->lock unlock];return e;
}
- (Native540RenderEncoder *)renderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor *)d {
    auto a=d.colorAttachments[0];bool valid=d&&a.texture&&a.level==0&&a.slice==0&&a.depthPlane==0&&!a.resolveTexture&&
        a.loadAction==MTLLoadActionClear&&a.storeAction==MTLStoreActionStore&&a.clearColor.red==0&&a.clearColor.green==0&&
        a.clearColor.blue==0&&a.clearColor.alpha==1&&!d.depthAttachment.texture&&!d.stencilAttachment.texture&&
        !d.visibilityResultBuffer&&d.renderTargetArrayLength<=1;
    for(unsigned i=1;i<8&&valid;++i)valid=!d.colorAttachments[i].texture&&!d.colorAttachments[i].resolveTexture;
    if(!valid){[_condition lock];[self reject:@"Unsupported render pass descriptor"];[_condition unlock];return nil;}
    return [self renderCommandEncoderWithTexture:(Native540Texture*)a.texture];
}
- (void)commit {
    [_condition lock];
    if(_committed){[_condition unlock];[NSException raise:NSInvalidArgumentException format:@"Command already committed"];return;}
    _committed=true;
    if(_active||!_ended||!_dispatched)[self reject:@"End exactly one typed dispatch before commit"];
    if(_error){_status=MTLCommandBufferStatusError;[_condition broadcast];[_condition unlock];[self notifyCompletion];return;}
    [_condition unlock];
    int rc=[self prepare];
    [_condition lock];
    if(rc){_error=failure([NSString stringWithFormat:@"Prepare %s rc=%d; resources retained",_preparationPhase,rc]);_status=MTLCommandBufferStatusError;[_condition broadcast];[_condition unlock];[self notifyCompletion];return;}
    _status=MTLCommandBufferStatusCommitted;[_condition unlock];
    dispatch_async(_session->worker,^{@autoreleasepool {[self execute];}});
}
- (void)waitUntilCompleted {
    [_condition lock];
    if(!_committed){[_condition unlock];[NSException raise:NSInvalidArgumentException format:@"Commit before waiting"];return;}
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:2.0];
    while(_status!=MTLCommandBufferStatusCompleted&&_status!=MTLCommandBufferStatusError) {
        if(![_condition waitUntilDate:deadline]) {
            _error=failure(@"Bounded host completion deadline exceeded",MTLCommandBufferErrorTimeout);
            _status=MTLCommandBufferStatusError;_session->lost=true;[_condition broadcast];break;
        }
    }
    [_condition unlock];[self notifyCompletion];
    [_condition lock];
    while(_completionDispatching&&_callbackThread!=NSThread.currentThread)
        if(![_condition waitUntilDate:deadline])break;
    [_condition unlock];
}
- (int)prepare {
    auto s=_session;[s->lock lock];
    _preparationPhase="session";int rc=(s->lost.load()||s->retirementStarted)?NativeMetal540Client::Lost:0;
    if(_graphics){
        if(_targetGeneration!=_target->_generation||_target->_submitted||s->pendingCommands){
            [s->lock unlock];return NativeMetal540Client::Invalid;
        }
        using namespace Native540RenderAIRFixture;
        if(!rc&&s->renderWorkspace){
            Native540Buffer *__strong *dest[]={&_ib,&_shader,&_fragmentShader,&_scratch};
            for(unsigned i=0;i<4;++i){NSDictionary *r=s->renderWorkspace[i];Native540Buffer *b=[[Native540Buffer alloc] initOwned];
                b->_session=s;b->_deviceIdentity=s->publicDevice;b->_handle=[r[@"handle"] unsignedLongLongValue];
                b->_token=[r[@"token"] unsignedLongLongValue];b->_iova=[r[@"iova"] unsignedLongLongValue];
                b->_cpu=(void*)(uintptr_t)[r[@"cpu"] unsignedLongLongValue];b->_length=b->_allocation=[r[@"bytes"] unsignedLongLongValue];*dest[i]=b;
            }
            memset(_ib->_cpu,0,_ib->_allocation);memset(_scratch->_cpu,0,_scratch->_allocation);
        } else {
            _preparationPhase="render-ib";if(!rc){_ib=allocate(s,8192,1);if(!_ib)rc=s->allocationError;}
            if(!rc){_preparationPhase="vertex-shader";_shader=allocate(s,4096,1);if(!_shader)rc=s->allocationError;}
            if(!rc){_preparationPhase="fragment-shader";_fragmentShader=allocate(s,4096,1);if(!_fragmentShader)rc=s->allocationError;}
            if(!rc){_preparationPhase="scratch";_scratch=allocate(s,4096,3);if(!_scratch)rc=s->allocationError;}
            if(!rc){NSMutableArray *workspace=[NSMutableArray new];
                for(Native540Buffer *b in @[_ib,_shader,_fragmentShader,_scratch])
                    [workspace addObject:@{@"handle":@(b->_handle),@"token":@(b->_token),@"iova":@(b->_iova),@"cpu":@((uintptr_t)b->_cpu),@"bytes":@(b->_allocation)}];
                s->renderWorkspace=[workspace copy];
            }
        }
        uint32_t written=0;
        if(!rc){
            _renderResources.ib={_ib->_iova,_ib->_allocation};_renderResources.vs={_shader->_iova,_shader->_allocation};
            _renderResources.fs={_fragmentShader->_iova,_fragmentShader->_allocation};
            _renderResources.target={_target->_backing->_iova,_target->_backing->_allocation};
            _renderResources.scratch={_scratch->_iova,_scratch->_allocation};
            _preparationPhase="render-encode";
            if(!Native540Render::encodeAIR(vs,fs,_renderResources,(uint32_t*)_ib->_cpu,2048,written))rc=NativeMetal540Client::Invalid;
            memcpy(_shader->_cpu,vsCode,sizeof(vsCode));memcpy(_fragmentShader->_cpu,fsCode,sizeof(fsCode));
            auto *pixels=(uint32_t*)_target->_backing->_cpu;
            uint32_t seed=0;int locked=_target->_surface?int(IOSurfaceLock(_target->_surface,0,&seed)):0;
            if(locked)rc=locked;
            if(!rc)for(unsigned i=0;i<4096;++i)pixels[i]=Native540Render::Sentinel;
            if(_target->_surface&&!locked){int unlocked=int(IOSurfaceUnlock(_target->_surface,0,&seed));if(!rc)rc=unlocked;}
            NSArray *all=@[_ib,_shader,_fragmentShader,_scratch,_target->_backing];
            for(Native540Buffer *b in all)if(!rc)rc=[s flushBuffer:b before:true];
            for(Native540Buffer *b in _buffers)if(!rc)rc=[s flushBuffer:b before:true];
            if(!rc&&(memcmp(_shader->_cpu,vsCode,sizeof(vsCode))||memcmp(_fragmentShader->_cpu,fsCode,sizeof(fsCode))))rc=NativeMetal540Client::Invalid;
        }
        if(rc)s->lost=true;else {_ibWords=written;_target->_submitted=true;_target->_completed=false;++_target->_generation;++s->pendingCommands;_pendingReserved=true;s->lastCompletion=false;}
        [s->lock unlock];return rc;
    }
    if(!rc) {
        _preparationPhase="ib";_ib=allocate(s,8192,1);if(!_ib)rc=s->allocationError;
        if(!rc){_preparationPhase="shader";_shader=allocate(s,Native540ObjectsRegistry::Info.codeWords*4,1);if(!_shader)rc=s->allocationError;}
        if(!rc){_preparationPhase="scratch";_scratch=allocate(s,4096,3);if(!_scratch)rc=s->allocationError;}
    }
    uint32_t written=0;
    if(!rc) {
        _preparationPhase="encode";
        _resources.shader={_shader->_iova,_shader->_allocation};_resources.scratch={_scratch->_iova,_scratch->_allocation};
        auto lib=_pipeline->_function->_library;
        if(_buffers.count!=3||memcmp(_buffers[2]->_cpu,Native540ObjectsRegistry::PaddingParams,48))rc=NativeMetal540Client::Invalid;
        NativeMetal540::Program program{Native540ObjectsRegistry::Info,(const uint32_t *)lib->_code.bytes,
            (const uint32_t *)lib->_constants.bytes};
        memcpy(_shader->_cpu,lib->_code.bytes,lib->_code.length);
        if(!rc&&!NativeMetal540::encode(program,_dispatch,_resources,(uint32_t *)_ib->_cpu,2048,written))rc=NativeMetal540Client::Invalid;
        if(!rc)rc=[s flushBuffer:_shader before:true];
        if(!rc)rc=[s flushBuffer:_scratch before:true];
        for(Native540Buffer *b in _buffers)if(!rc)rc=[s flushBuffer:b before:true];
        if(!rc)rc=[s flushBuffer:_ib before:true];
        // Upload attestation observes mapped bytes, never an expected output.
        if(!rc&&memcmp(_shader->_cpu,lib->_code.bytes,lib->_code.length))rc=NativeMetal540Client::Invalid;
    }
    if(s->lost.load()&&!rc)rc=NativeMetal540Client::Lost;
    if(rc)s->lost=true;else {_ibWords=written;++s->pendingCommands;_pendingReserved=true;s->lastCompletion=false;}
    [s->lock unlock];return rc;
}
- (void)execute {
    auto s=_session;[s->lock lock];
    [_condition lock];bool canceled=_status==MTLCommandBufferStatusError;[_condition unlock];
    int rc=(canceled||s->lost.load())?NativeMetal540Client::Lost:0;
    uint64_t fence=0;
    if(!rc&&!s->lost.load()) {
        NativeAdrenoAbiSubmitBuffer ib{_ib->_handle,_ib->_iova,uint64_t(_ibWords)*4};
        rc=s->client->submit(&ib,1,fence);
        if(!rc){[_condition lock];if(_status!=MTLCommandBufferStatusError)_status=MTLCommandBufferStatusScheduled;[_condition unlock];}
        if(!rc)rc=s->client->wait(fence,200000000ull);
        if(!rc)rc=[s flushBuffer:_scratch before:false];
        for(Native540Buffer *b in _buffers)if(!rc)rc=[s flushBuffer:b before:false];
        if(_graphics&&!rc){
            rc=[s flushBuffer:_target->_backing before:false];
            const auto *stamp=(const volatile uint32_t*)_scratch->_cpu;
            if(!rc&&(stamp[0]!=1||stamp[2]!=2||stamp[4]!=3))rc=NativeMetal540Client::Invalid;
        }
    } else if(!rc)rc=NativeMetal540Client::Lost;
    if(rc)s->lost=true;
    if(_pendingReserved){if(s->pendingCommands)--s->pendingCommands;_pendingReserved=false;}
    s->lastCompletion=!rc&&!s->lost.load();
    [_condition lock];
    // A fence proves command completion, NOT algorithm correctness. Consumers
    // must read buffer contents and compare measured results themselves.
    if(_status!=MTLCommandBufferStatusError) {
        if(rc){_error=failure(@"Transport/encoding/fence/flush failure; retained for connection retirement");_status=MTLCommandBufferStatusError;}
        else {_status=MTLCommandBufferStatusCompleted;_executionRetired=true;if(_graphics){_target->_completed=true;_target->_submitted=false;}}
    }
    [_condition broadcast];[_condition unlock];[s->lock unlock];[self notifyCompletion];
}
@end
@implementation Native540RenderEncoder
- (instancetype)initOwned {return [super init];}
- (BOOL)editable {
    if(_ended||_command->_committed||_command->_dispatched||_command->_error){[_command reject:@"Render encoder not editable"];return NO;}
    return YES;
}
- (void)setRenderPipelineState:(Native540RenderPipeline *)p {
    [_command->_condition lock];
    if([self editable]){
        if(![p isKindOfClass:Native540RenderPipeline.class]||p->_vertex->_library->_session!=_command->_session)[_command reject:@"Foreign graphics pipeline"];
        else _pipeline=p;
    }
    [_command->_condition unlock];
}
- (void)setVertexBuffer:(Native540Buffer *)b offset:(NSUInteger)o atIndex:(NSUInteger)i {
    [_command->_condition lock];
    if([self editable]){
        if(i||o||![b isKindOfClass:Native540Buffer.class]||b->_session!=_command->_session||b.length!=48)[_command reject:@"Unsupported vertex binding"];
        else _vertex=b;
    }
    [_command->_condition unlock];
}
- (void)setFragmentBuffer:(Native540Buffer *)b offset:(NSUInteger)o atIndex:(NSUInteger)i {
    [_command->_condition lock];
    if([self editable]){
        if(i||o||![b isKindOfClass:Native540Buffer.class]||b->_session!=_command->_session||b.length!=16)[_command reject:@"Unsupported fragment binding"];
        else _fragment=b;
    }
    [_command->_condition unlock];
}
- (void)drawPrimitives:(MTLPrimitiveType)t vertexStart:(NSUInteger)s vertexCount:(NSUInteger)n {
    [_command->_condition lock];
    if([self editable]){
        if(t!=MTLPrimitiveTypeTriangle||s||n!=3||!_pipeline||!_vertex||!_fragment)[_command reject:@"Only one bounded SolidColor triangle is supported"];
        else {
            _command->_renderPipeline=_pipeline;_command->_buffers=@[_vertex,_fragment];
            _command->_renderResources.vertexBindings[0]={{_vertex->_iova,_vertex->_allocation},0,48,true};
            _command->_renderResources.fragmentBindings[0]={{_fragment->_iova,_fragment->_allocation},0,16,true};
            _command->_dispatched=true;
        }
    }
    [_command->_condition unlock];
}
- (void)endEncoding {
    [_command->_condition lock];
    if(_ended||_command->_committed)[_command reject:@"Render encoder ended or committed"];
    else {_ended=true;_command->_active=false;_command->_ended=true;}
    [_command->_condition unlock];
}
@end
@implementation Native540ComputeEncoder
- (instancetype)initOwned {return [super init];}
- (BOOL)editable {
    if(_ended||_command->_committed||_command->_dispatched||_command->_error){[_command reject:@"Encoder ended, dispatched or committed"];return NO;}return YES;
}
- (void)setComputePipelineState:(Native540ComputePipeline *)state {
    [_command->_condition lock];
    if([self editable]) {
        if(![state isKindOfClass:Native540ComputePipeline.class]||state->_function->_library->_session!=_command->_session)[_command reject:@"Foreign pipeline"];
        else _pipeline=state;
    }
    [_command->_condition unlock];
}
- (void)setBuffer:(Native540Buffer *)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index {
    [_command->_condition lock];
    if([self editable]) {
        if(index>=8||(buffer&&(![buffer isKindOfClass:Native540Buffer.class]||buffer->_session!=_command->_session||offset>=buffer.length))||(!buffer&&offset))[_command reject:@"Invalid buffer/index/offset"];
        else {_bindings[index]=buffer;_offsets[index]=offset;}
    }
    [_command->_condition unlock];
}
- (void)setThreadgroupMemoryLength:(NSUInteger)length atIndex:(NSUInteger)index {
    [_command->_condition lock];
    if([self editable]) {
        if(index>=8||!length||(length&15)||length>NativeMetal540::MaxShared)[_command reject:@"Invalid shared-memory index/length"];
        else _shared[index]=(uint32_t)length;
    }
    [_command->_condition unlock];
}
- (void)dispatchThreadgroups:(MTLSize)groups threadsPerThreadgroup:(MTLSize)local {
    [_command->_condition lock];
    if([self editable]) {
        const NSUInteger g[]={groups.width,groups.height,groups.depth},l[]={local.width,local.height,local.depth};
        bool valid=_pipeline!=nil;NativeMetal540::Dispatch d{};NativeMetal540::Resources r{};
        for(unsigned i=0;i<3;++i){if(!g[i]||g[i]>65535||!l[i]||l[i]>1024)valid=false;d.groups[i]=(uint32_t)g[i];d.local[i]=(uint32_t)l[i];}
        NSMutableArray *retained=[NSMutableArray new];
        for(unsigned i=0;i<8;++i) {
            d.threadgroupLengths[i]=_shared[i];auto b=_bindings[i];
            if(b){r.buffers[i]={{b->_iova,b->_allocation},_offsets[i],(uint32_t)(b.length-_offsets[i]),bool(Native540ObjectsRegistry::Info.writeMask&(1u<<i))};[retained addObject:b];}
        }
        // Fixed AIR entry is admitted only for its audited copy workload.
        // No arbitrary parameter-driven accesses; user owns CPU writes and must
        // not mutate any resource from commit until terminal fence/retirement.
        valid=valid&&g[0]==4&&g[1]==1&&g[2]==1&&l[0]==64&&l[1]==1&&l[2]==1&&
            _bindings[0]&&_bindings[1]&&_bindings[2]&&_offsets[0]==0&&_offsets[1]==0&&_offsets[2]==0&&
            _bindings[0].length==3120&&_bindings[1].length==1024&&_bindings[2].length==48;
        if(valid&&memcmp(_bindings[2]->_cpu,Native540ObjectsRegistry::PaddingParams,48))valid=false;
        // Dummy disjoint CPU-only regions for plan validation; actual retained
        // shader/scratch are allocated/revalidated by execute before any submit.
        r.shader={0x1000,16384};r.scratch={0x8000,4096};
        NativeMetal540::Program program{Native540ObjectsRegistry::Info,Native540ObjectsRegistry::Code,Native540ObjectsRegistry::Constants};
        NativeMetal540::Plan plan{};valid=valid&&NativeMetal540::plan(program,d,r,plan);
        if(!valid)[_command reject:@"Typed dispatch/reflection/binding admission rejected"];
        else {_command->_pipeline=_pipeline;_command->_buffers=[retained copy];_command->_dispatch=d;_command->_resources=r;_command->_dispatched=true;}
    }
    [_command->_condition unlock];
}
- (void)endEncoding {
    [_command->_condition lock];
    if(_ended||_command->_committed)[_command reject:@"Encoder already ended or committed"];
    else {_ended=true;_command->_active=false;_command->_ended=true;}
    [_command->_condition unlock];
}
@end
