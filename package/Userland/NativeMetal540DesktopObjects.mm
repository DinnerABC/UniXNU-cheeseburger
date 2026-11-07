// SPDX-License-Identifier: MIT
// ObjC ownership/descriptor/recording follows pinned sagit QcomMTL; native
// execution uses the board's existing ABI, not sagit's Family selectors.
#import "NativeMetal540DesktopObjects.h"
#include "NativeMetal540DesktopDraw.h"
#include <atomic>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static NSError *cbFailure(NSString *text) {return [NSError errorWithDomain:MTLCommandBufferErrorDomain code:MTLCommandBufferErrorInvalidResource userInfo:@{NSLocalizedDescriptionKey:text}];}
@interface CBMetalContext : NSObject { @public std::unique_ptr<NativeMetal540DesktopSession> session;id owner;NSRecursiveLock *lock;dispatch_queue_t worker;std::atomic<unsigned> pending;std::atomic<bool> lost;NSMutableArray *held,*deferred; }
@end
@implementation CBMetalContext
- (instancetype)init {if((self=[super init])){lock=[NSRecursiveLock new];worker=dispatch_queue_create("cb.metal.desktop",DISPATCH_QUEUE_SERIAL);pending=0;lost=false;held=[NSMutableArray new];deferred=[NSMutableArray new];}return self;}
@end
@interface CBMetalDevice () { @public CBMetalContext *_context;NativeMetal540DesktopLibrary::Compile _compiler; }
@end
@interface CBMetalBuffer () { @public CBMetalDevice *_device;NativeMetal540DesktopSession::Resource _resource;NSUInteger _length;NSString *_label;id _externalOwner; }
@end
@interface CBMetalTexture () { @public CBMetalBuffer *_backing;MTLTextureDescriptor *_descriptor;uint32_t _pitch;IOSurfaceRef _surface;id _surfaceOwner;bool _ready; }
@end
@interface CBMetalLibrary () { @public CBMetalDevice *_device;std::shared_ptr<NativeMetal540DesktopLibrary> _library;NSString *_label; }
@end
@interface CBMetalFunction () { @public CBMetalLibrary *_library;NSString *_name;NativeMetal540DesktopLibrary::Image _image; }
@end
@interface CBMetalPipeline () { @public CBMetalFunction *_vertex,*_fragment;MTLRenderPipelineDescriptor *_descriptor;GPU::Shader::RenderState _state; }
@end
@interface CBMetalSampler () { @public CBMetalDevice *_device;GPU::Shader::Sampler _state; }
@end
@interface CBMetalQueue () { @public CBMetalDevice *_device;NSUInteger _capacity;std::atomic<unsigned> _live;NSString *_label; }
@end
@interface CBMetalOperation : NSObject { @public NativeMetal540Desktop::Draw draw;CBMetalPipeline *pipeline;NSArray *owners;bool clear;uint32_t color; }
@end
@implementation CBMetalOperation
@end
@interface CBMetalCommands () { @public CBMetalQueue *_queue;NSCondition *_condition;MTLCommandBufferStatus _status;NSError *_error;NSMutableArray<CBMetalOperation *> *_operations;NSMutableArray *_handlers;CBMetalBuffer *_ib,*_scratch;bool _active,_committed,_callbacksDone,_pendingReserved;NSThread *_callbackThread;NSString *_label; }
- (void)fail:(NSString *)text;
- (void)execute;
@end
@interface CBMetalRender () { @public CBMetalCommands *_commands;CBMetalPipeline *_pipeline;CBMetalTexture *_targets[8];CBMetalBuffer *_buffers[2][8];NSUInteger _offsets[2][8];CBMetalTexture *_textures[2][8];CBMetalSampler *_samplers[2][16];MTLViewport _viewport;MTLScissorRect _scissor;bool _ended; }
@end
static bool cbOptions(MTLResourceOptions options){return options==0||options==MTLResourceHazardTrackingModeTracked;}
static bool cbLinked(MTLLinkedFunctions *f){return !f||(!f.functions.count&&!f.binaryFunctions.count&&!f.groups.count&&!f.privateFunctions.count);}
static uint32_t cbFormat(MTLPixelFormat f){return f==MTLPixelFormatRGBA8Unorm?GPU::Shader::RGBA8:f==MTLPixelFormatBGRA8Unorm?GPU::Shader::BGRA8:0;}
static CBMetalBuffer *cbBuffer(CBMetalDevice *d,NSUInteger n,void *imported=nullptr,id owner=nil){
    auto c=d->_context;[c->lock lock];CBMetalBuffer *out=nil;
    if(!c->lost&&!c->pending){out=[CBMetalBuffer new];out->_device=d;out->_length=n;out->_externalOwner=owner;
        int rc=imported?c->session->import(imported,n,out->_resource):c->session->allocate(n,out->_resource);
        if(rc){if(c->session->lost())c->lost=true;if(owner)[c->held addObject:owner];out=nil;}}
    [c->lock unlock];return out;
}
@implementation CBMetalDevice
- (instancetype)initWithTransport:(NativeMetal540DesktopSession::Transport)t connectionOwner:(id)owner compiler:(NativeMetal540DesktopLibrary::Compile)compiler error:(NSError **)error {
    if(error)*error=nil;if(!owner||!compiler){if(error)*error=cbFailure(@"Connection owner and real AIR compiler required");return nil;}
    if(!(self=[super init]))return nil;_context=[CBMetalContext new];_context->owner=owner;_compiler=compiler;
    _context->session=std::make_unique<NativeMetal540DesktopSession>(t);int rc=_context->session->initialize();
    if(rc){if(error)*error=cbFailure(@"Native desktop session initialization failed");return nil;}return self;
}
- (NSUInteger)currentAllocatedSize {return _context->session->allocatedBytes();}
- (CBMetalBuffer *)newBufferWithLength:(NSUInteger)n options:(MTLResourceOptions)o {return cbOptions(o)&&n&&n<=16u*1024*1024?cbBuffer(self,n):nil;}
- (CBMetalBuffer *)newBufferWithBytes:(const void *)bytes length:(NSUInteger)n options:(MTLResourceOptions)o {
    if(!bytes)return nil;auto c=_context;[c->lock lock];auto b=[self newBufferWithLength:n options:o];if(b)memcpy(b->_resource.cpu,bytes,n);[c->lock unlock];return b;
}
- (CBMetalTexture *)newTextureWithDescriptor:(MTLTextureDescriptor *)d {return [self newTextureWithDescriptor:d iosurface:nullptr plane:0];}
- (CBMetalTexture *)newTextureWithDescriptor:(MTLTextureDescriptor *)d iosurface:(IOSurfaceRef)s plane:(NSUInteger)plane {
    if(![d isKindOfClass:MTLTextureDescriptor.class]||plane||!cbFormat(d.pixelFormat)||d.textureType!=MTLTextureType2D||
       !d.width||!d.height||d.width>16384||d.height>16384||d.depth!=1||d.arrayLength!=1||d.mipmapLevelCount!=1||d.sampleCount!=1||
       d.storageMode!=MTLStorageModeShared||d.cpuCacheMode!=MTLCPUCacheModeDefaultCache||!cbOptions(d.resourceOptions)||
       d.compressionType!=MTLTextureCompressionTypeLossless||d.swizzle.red!=MTLTextureSwizzleRed||d.swizzle.green!=MTLTextureSwizzleGreen||
       d.swizzle.blue!=MTLTextureSwizzleBlue||d.swizzle.alpha!=MTLTextureSwizzleAlpha||(d.usage&~(MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget)))return nil;
    uint64_t pitch=(d.width*4+63)&~63ull,bytes=pitch*d.height;id owner=nil;void *address=nullptr;
    if(s){
        if(CFGetTypeID(s)!=IOSurfaceGetTypeID()||IOSurfaceGetPlaneCount(s)||IOSurfaceGetWidth(s)!=d.width||IOSurfaceGetHeight(s)!=d.height||
           IOSurfaceGetPixelFormat(s)!=(d.pixelFormat==MTLPixelFormatBGRA8Unorm?0x42475241u:0x52474241u)||IOSurfaceGetBytesPerElement(s)!=4||
           IOSurfaceGetElementWidth(s)!=1||IOSurfaceGetElementHeight(s)!=1)return nil;
        pitch=IOSurfaceGetBytesPerRow(s);bytes=IOSurfaceGetAllocSize(s);address=IOSurfaceGetBaseAddress(s);
        if((pitch&63)||pitch<d.width*4||uint64_t(pitch)*d.height>bytes||!address||(uintptr_t(address)&4095)||(bytes&4095))return nil;
        owner=CFBridgingRelease(CFRetain(s));
    }
    if(bytes>16u*1024*1024)return nil;auto b=cbBuffer(self,bytes,address,owner);if(!b)return nil;
    auto texture=[CBMetalTexture new];texture->_backing=b;texture->_descriptor=[d copy];texture->_pitch=uint32_t(pitch);texture->_surface=s;texture->_surfaceOwner=owner;texture->_ready=s!=nullptr;return texture;
}
- (CBMetalLibrary *)newLibraryWithData:(dispatch_data_t)data error:(NSError **)error {
    if(error)*error=nil;size_t n=data?dispatch_data_get_size(data):0;if(!n||n>64u*1024*1024){if(error)*error=cbFailure(@"Bounded original AIR library required");return nil;}
    const void *bytes=nullptr;size_t mapped=0;auto map=dispatch_data_create_map(data,&bytes,&mapped);if(!map||mapped!=n)return nil;
    std::string diagnostic;auto parsed=NativeMetal540DesktopLibrary::load(bytes,n,_compiler,0,diagnostic);
    if(!parsed){if(error)*error=cbFailure(@(diagnostic.c_str()));return nil;}auto library=[CBMetalLibrary new];library->_device=self;library->_library=parsed;return library;
}
- (CBMetalLibrary *)newLibraryWithURL:(NSURL *)url error:(NSError **)error {
    if(error)*error=nil;if(!url.isFileURL){if(error)*error=cbFailure(@"Local original metallib URL required");return nil;}
    int fd=open(url.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);struct stat st{};
    if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>64*1024*1024){if(fd>=0)close(fd);if(error)*error=cbFailure(@"Invalid local library");return nil;}
    NSMutableData *bytes=[NSMutableData dataWithLength:NSUInteger(st.st_size)];size_t at=0;bool valid=true;
    while(at<bytes.length){ssize_t n=read(fd,(uint8_t*)bytes.mutableBytes+at,bytes.length-at);if(n<0&&errno==EINTR)continue;if(n<=0){valid=false;break;}at+=size_t(n);}close(fd);
    if(!valid){if(error)*error=cbFailure(@"Short original library read");return nil;}
    return [self newLibraryWithData:dispatch_data_create(bytes.bytes,bytes.length,nullptr,DISPATCH_DATA_DESTRUCTOR_DEFAULT) error:error];
}
- (CBMetalSampler *)newSamplerStateWithDescriptor:(MTLSamplerDescriptor *)d {
    if(![d isKindOfClass:MTLSamplerDescriptor.class]||d.minFilter>MTLSamplerMinMagFilterLinear||d.magFilter>MTLSamplerMinMagFilterLinear||
       d.mipFilter!=MTLSamplerMipFilterNotMipmapped||d.sAddressMode!=MTLSamplerAddressModeClampToEdge||d.tAddressMode!=MTLSamplerAddressModeClampToEdge||
       d.rAddressMode!=MTLSamplerAddressModeClampToEdge||d.maxAnisotropy!=1||!d.normalizedCoordinates||d.compareFunction!=MTLCompareFunctionNever)return nil;
    auto s=[CBMetalSampler new];s->_device=self;s->_state.filters=uint32_t(d.minFilter)|(uint32_t(d.magFilter)<<1);return s;
}
- (CBMetalPipeline *)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)d error:(NSError **)error {
    if([d isKindOfClass:MTLRenderPipelineDescriptor.class])d=[d copy];
    if(error)*error=nil;if(![d isKindOfClass:MTLRenderPipelineDescriptor.class]||![d.vertexFunction isKindOfClass:CBMetalFunction.class]||
       ![d.fragmentFunction isKindOfClass:CBMetalFunction.class]||d.vertexFunction.device!=self||d.fragmentFunction.device!=self||d.rasterSampleCount!=1||
       d.depthAttachmentPixelFormat||d.stencilAttachmentPixelFormat||!d.rasterizationEnabled||d.supportIndirectCommandBuffers||d.binaryArchives.count||
       !cbLinked(d.vertexLinkedFunctions)||!cbLinked(d.fragmentLinkedFunctions)||d.vertexPreloadedLibraries.count||d.fragmentPreloadedLibraries.count||
       d.alphaToCoverageEnabled||d.alphaToOneEnabled||d.maxVertexAmplificationCount!=1){if(error)*error=cbFailure(@"Unsupported native render pipeline");return nil;}
    auto v=(CBMetalFunction*)d.vertexFunction,f=(CBMetalFunction*)d.fragmentFunction;GPU::Shader::Program vs{},fs{};
    memcpy(&vs,&v->_image->info,sizeof(v->_image->info));memcpy(&fs,&f->_image->info,sizeof(f->_image->info));vs.code=v->_image->code;fs.code=f->_image->code;vs.immediates=v->_image->immediates;fs.immediates=f->_image->immediates;
    GPU::Shader::Linkage link{};if(!GPU::Shader::link(vs,fs,link)){if(error)*error=cbFailure(@"Original AIR stage linkage failed");return nil;}
    for(unsigned i=0;i<16;++i){auto a=d.vertexDescriptor.attributes[i];
        if(vs.attributeMask&(1u<<i)){auto format=GPU::Shader::vertexFormat(uint32_t(a.format));if(!d.vertexDescriptor||!format.bytes||format.type!=vs.attributes[i].type||a.bufferIndex>=8||a.offset>2047){if(error)*error=cbFailure(@"Invalid native vertex attribute");return nil;}}
        else if(d.vertexDescriptor&&a.format!=MTLVertexFormatInvalid){if(error)*error=cbFailure(@"Vertex descriptor has unused attributes");return nil;}}
    for(unsigned i=16;i<31;++i)if(d.vertexDescriptor&&d.vertexDescriptor.attributes[i].format!=MTLVertexFormatInvalid){if(error)*error=cbFailure(@"Vertex attribute exceeds native stage limit");return nil;}
    for(unsigned i=0;i<31;++i)if(d.vertexBuffers[i].mutability!=MTLMutabilityDefault||d.fragmentBuffers[i].mutability!=MTLMutabilityDefault){if(error)*error=cbFailure(@"Unsupported pipeline buffer layout");return nil;}
    GPU::Shader::Draw layout{};for(unsigned i=0;i<16;++i)if(vs.attributeMask&(1u<<i)){auto a=d.vertexDescriptor.attributes[i];auto l=d.vertexDescriptor.layouts[a.bufferIndex];
        if(l.stride>2048||l.stepRate>UINT32_MAX||
           (l.stepFunction!=MTLVertexStepFunctionConstant&&l.stepFunction!=MTLVertexStepFunctionPerVertex&&l.stepFunction!=MTLVertexStepFunctionPerInstance)){if(error)*error=cbFailure(@"Unsupported vertex step function");return nil;}
        layout.attributes[i]={uint32_t(a.format),uint32_t(a.offset),uint32_t(a.bufferIndex),0};layout.layouts[a.bufferIndex]={uint32_t(l.stride),uint32_t(l.stepFunction),uint32_t(l.stepRate),0};}
    uint32_t bindings=0;if(!GPU::Shader::vertexLayout(vs,layout,bindings)){if(error)*error=cbFailure(@"Invalid vertex layout");return nil;}
    for(unsigned i=0;i<8;++i)if(d.colorAttachments[i].pixelFormat&&!cbFormat(d.colorAttachments[i].pixelFormat)){if(error)*error=cbFailure(@"Unsupported render color format");return nil;}
    auto p=[CBMetalPipeline new];p->_vertex=v;p->_fragment=f;p->_descriptor=[d copy];return p;
}
- (CBMetalQueue *)newCommandQueue {return [self newCommandQueueWithMaxCommandBufferCount:16];}
- (CBMetalQueue *)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)n {if(!n||n>64)return nil;auto q=[CBMetalQueue new];q->_device=self;q->_capacity=n;q->_live=0;return q;}
@end
@implementation CBMetalBuffer
- (void)dealloc {auto c=_device->_context;[c->lock lock];if(_resource.id){
    if(c->pending)[c->deferred addObject:@{@"id":@(_resource.id),@"owner":_externalOwner?:NSNull.null}];
    else {int rc=c->session->release(_resource.id);if(rc&&_externalOwner)[c->held addObject:_externalOwner];}}
    [c->lock unlock];}
- (id)device {return _device;}
- (NSUInteger)length {return _length;}
- (NSUInteger)allocatedSize {return _resource.bytes;}
- (MTLStorageMode)storageMode {return MTLStorageModeShared;}
- (MTLCPUCacheMode)cpuCacheMode {return MTLCPUCacheModeDefaultCache;}
- (MTLHazardTrackingMode)hazardTrackingMode {return MTLHazardTrackingModeTracked;}
- (NSString *)label {return _label;}
- (void)setLabel:(NSString *)s {_label=[s copy];}
- (void *)contents {auto c=_device->_context;if(c->lost.load()||c->pending.load())return nullptr;[c->lock lock];NativeMetal540DesktopSession::Resource r{};void *p=!c->lost&&!c->pending&&c->session->resource(_resource.id,r)?r.cpu:nullptr;[c->lock unlock];return p;}
@end
@implementation CBMetalTexture
- (id)device {return _backing.device;}
- (NSUInteger)width {return _descriptor.width;}
- (NSUInteger)height {return _descriptor.height;}
- (NSUInteger)depth {return 1;}
- (NSUInteger)arrayLength {return 1;}
- (NSUInteger)mipmapLevelCount {return 1;}
- (NSUInteger)sampleCount {return 1;}
- (MTLPixelFormat)pixelFormat {return _descriptor.pixelFormat;}
- (MTLTextureType)textureType {return MTLTextureType2D;}
- (MTLTextureUsage)usage {return _descriptor.usage;}
- (MTLStorageMode)storageMode {return MTLStorageModeShared;}
- (MTLHazardTrackingMode)hazardTrackingMode {return MTLHazardTrackingModeTracked;}
- (NSUInteger)allocatedSize {return _backing.allocatedSize;}
- (IOSurfaceRef)iosurface {return _surface;}
- (NSUInteger)iosurfacePlane {return 0;}
- (NSString *)label {return _backing.label;}
- (void)setLabel:(NSString *)s {_backing.label=s;}
- (void)replaceRegion:(MTLRegion)r mipmapLevel:(NSUInteger)level withBytes:(const void *)bytes bytesPerRow:(NSUInteger)row {
    auto c=((CBMetalDevice*)self.device)->_context;[c->lock lock];void *cpu=_backing.contents;
    if(!cpu||!bytes||level||r.origin.z||r.size.depth!=1||r.origin.x>self.width||r.size.width>self.width-r.origin.x||r.origin.y>self.height||r.size.height>self.height-r.origin.y||row<r.size.width*4){[c->lock unlock];[NSException raise:NSInvalidArgumentException format:@"Invalid native texture upload"];return;}
    uint32_t seed=0;int rc=_surface?int(IOSurfaceLock(_surface,0,&seed)):0;
    if(!rc)for(NSUInteger y=0;y<r.size.height;++y)memcpy((uint8_t*)cpu+(r.origin.y+y)*_pitch+r.origin.x*4,(const uint8_t*)bytes+y*row,r.size.width*4);
    if(_surface&&!rc)rc=int(IOSurfaceUnlock(_surface,0,&seed));if(rc)c->lost=true;_ready=!rc;[c->lock unlock];
    if(rc)[NSException raise:NSInternalInconsistencyException format:@"Native IOSurface upload lock/unlock failed: %d",rc];
}
- (void)getBytes:(void *)bytes bytesPerRow:(NSUInteger)row fromRegion:(MTLRegion)r mipmapLevel:(NSUInteger)level {
    auto c=((CBMetalDevice*)self.device)->_context;[c->lock lock];void *cpu=_backing.contents;
    if(!cpu||!bytes||!_ready||level||r.origin.z||r.size.depth!=1||r.origin.x>self.width||r.size.width>self.width-r.origin.x||r.origin.y>self.height||r.size.height>self.height-r.origin.y||row<r.size.width*4){[c->lock unlock];[NSException raise:NSInvalidArgumentException format:@"Texture read needs proven native completion"];return;}
    uint32_t seed=0;int rc=_surface?int(IOSurfaceLock(_surface,kIOSurfaceLockReadOnly,&seed)):0;
    if(!rc)for(NSUInteger y=0;y<r.size.height;++y)memcpy((uint8_t*)bytes+y*row,(uint8_t*)cpu+(r.origin.y+y)*_pitch+r.origin.x*4,r.size.width*4);
    if(_surface&&!rc)rc=int(IOSurfaceUnlock(_surface,kIOSurfaceLockReadOnly,&seed));if(rc){c->lost=true;_ready=false;}[c->lock unlock];
    if(rc)[NSException raise:NSInternalInconsistencyException format:@"Native IOSurface read lock/unlock failed: %d",rc];
}
@end
@implementation CBMetalLibrary
- (id)device {return _device;}
- (NSArray *)functionNames {NSMutableArray *out=[NSMutableArray new];for(const auto &n:_library->names())[out addObject:@(n.c_str())];return out;}
- (NSString *)label {return _label;}
- (void)setLabel:(NSString *)s {_label=[s copy];}
- (CBMetalFunction *)newFunctionWithName:(NSString *)name {std::string error;auto image=_library->function(name.UTF8String,error);if(!image)return nil;auto f=[CBMetalFunction new];f->_library=self;f->_name=[name copy];f->_image=image;return f;}
@end
@implementation CBMetalFunction
- (id)device {return _library.device;}
- (NSString *)name {return _name;}
- (MTLFunctionType)functionType {return _image->info.stage==GPU::Shader::Vertex?MTLFunctionTypeVertex:_image->info.stage==GPU::Shader::Fragment?MTLFunctionTypeFragment:MTLFunctionTypeKernel;}
@end
@implementation CBMetalPipeline
- (id)device {return _vertex.device;}
- (NSString *)label {return _descriptor.label;}
@end
@implementation CBMetalSampler
- (id)device {return _device;}
@end
@implementation CBMetalQueue
- (id)device {return _device;}
- (NSString *)label {return _label;}
- (void)setLabel:(NSString *)s {_label=[s copy];}
- (CBMetalCommands *)commandBuffer {unsigned n=_live.load();do {if(n>=_capacity)return nil;}while(!_live.compare_exchange_weak(n,n+1));auto b=[CBMetalCommands new];b->_queue=self;b->_condition=[NSCondition new];b->_operations=[NSMutableArray new];b->_handlers=[NSMutableArray new];b->_status=MTLCommandBufferStatusNotEnqueued;return b;}
@end
static GPU::Shader::Texture cbPlane(CBMetalTexture *texture,uint32_t id){return {id,0,uint32_t(texture.width),uint32_t(texture.height),texture->_pitch,cbFormat(texture.pixelFormat)};}
@implementation CBMetalCommands
- (void)dealloc {if(!_callbacksDone)--_queue->_live;}
- (id)device {return _queue.device;}
- (MTLCommandBufferStatus)status {[_condition lock];auto s=_status;[_condition unlock];return s;}
- (NSError *)error {[_condition lock];auto e=_error;[_condition unlock];return e;}
- (NSString *)label {return _label;}
- (void)setLabel:(NSString *)s {_label=[s copy];}
- (void)fail:(NSString *)text {[_condition lock];if(!_error)_error=cbFailure(text);[_condition unlock];}
- (void)addCompletedHandler:(void (^)(CBMetalCommands *))handler {
    if(!handler)return;[_condition lock];bool done=_callbacksDone;if(!done)[_handlers addObject:[handler copy]];[_condition unlock];if(done)handler(self);
}
- (CBMetalRender *)renderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor *)d {
    if(!d||_committed||_active||_operations.count>248)return nil;d=[d copy];auto r=[CBMetalRender new];r->_commands=self;
    CBMetalDevice *device=(CBMetalDevice*)self.device;NSMutableArray *owners=[NSMutableArray new];
    for(unsigned i=0;i<8;++i){auto a=d.colorAttachments[i];if(!a.texture)continue;
        if(![a.texture isKindOfClass:CBMetalTexture.class]||a.texture.device!=device||a.level||a.slice||a.depthPlane||a.resolveTexture||a.storeAction!=MTLStoreActionStore||
           (a.loadAction!=MTLLoadActionLoad&&a.loadAction!=MTLLoadActionClear)){[self fail:@"Unsupported native render pass attachment"];return nil;}
        auto t=(CBMetalTexture*)a.texture;r->_targets[i]=t;[owners addObject:t];
        if(a.loadAction==MTLLoadActionClear){double rgba[]={a.clearColor.red,a.clearColor.green,a.clearColor.blue,a.clearColor.alpha};uint32_t values[4]{};
            for(unsigned j=0;j<4;++j){if(!std::isfinite(rgba[j])||rgba[j]<0||rgba[j]>1){[self fail:@"Invalid native clear color"];return nil;}values[j]=uint32_t(std::lround(rgba[j]*255));}
            auto op=[CBMetalOperation new];op->clear=true;op->draw.target=t->_backing->_resource.id;op->draw.state.target=cbPlane(t,i+1);op->owners=@[t];
            op->color=t.pixelFormat==MTLPixelFormatBGRA8Unorm?values[2]|(values[1]<<8)|(values[0]<<16)|(values[3]<<24):values[0]|(values[1]<<8)|(values[2]<<16)|(values[3]<<24);[_operations addObject:op];
        }
    }
    if(d.depthAttachment.texture||d.stencilAttachment.texture||!r->_targets[0]){[self fail:@"Only native color attachments are supported"];return nil;}
    r->_viewport={0,0,double(r->_targets[0].width),double(r->_targets[0].height),0,1};r->_scissor={0,0,r->_targets[0].width,r->_targets[0].height};_active=true;return r;
}
- (void)commit {
    [_condition lock];if(_committed){[_condition unlock];[NSException raise:NSInvalidArgumentException format:@"Native command buffer already committed"];return;}
    _committed=true;if(_active&&!_error)_error=cbFailure(@"Render encoder was not ended");_status=MTLCommandBufferStatusCommitted;[_condition unlock];
    auto c=((CBMetalDevice*)self.device)->_context;
    if(self.error||c->lost){[self fail:@"Native command preparation/admission failed"];dispatch_async(c->worker,^{@autoreleasepool {[self execute];}});return;}
    [c->lock lock];
    if(!self.error&&!c->lost&&!c->pending){_ib=cbBuffer((CBMetalDevice*)self.device,65536);_scratch=cbBuffer((CBMetalDevice*)self.device,4096);
        if(_ib&&_scratch){++c->pending;_pendingReserved=true;}}
    if(!_pendingReserved)[self fail:@"Native command preparation/admission failed"];[c->lock unlock];
    dispatch_async(c->worker,^{@autoreleasepool {[self execute];}});
}
- (void)execute {
    auto c=((CBMetalDevice*)self.device)->_context;[c->lock lock];int rc=0;CBMetalBuffer *ib=_ib,*scratch=_scratch;
    if(c->lost||self.error||!_pendingReserved||!ib||!scratch)rc=NativeMetal540DesktopSession::Invalid;
    if(!rc){[_condition lock];_status=MTLCommandBufferStatusScheduled;[_condition unlock];}
    for(CBMetalOperation *op in _operations){if(rc)break;uint32_t words=0;
        if(op->clear){auto &t=op->draw.state.target;rc=NativeMetal540Desktop::encodeClear(*c->session,ib->_resource.id,scratch->_resource.id,op->draw.target,t.width,t.height,t.pitch,op->color,words);}
        else rc=NativeMetal540Desktop::encodeDraw(*c->session,ib->_resource.id,scratch->_resource.id,*op->pipeline->_vertex->_image,*op->pipeline->_fragment->_image,op->draw,words);
        uint64_t ids[64]={ib->_resource.id,scratch->_resource.id};size_t n=2;
        for(id owner in op->owners){CBMetalBuffer *b=[owner isKindOfClass:CBMetalBuffer.class]?owner:[owner isKindOfClass:CBMetalTexture.class]?((CBMetalTexture*)owner)->_backing:nil;
            if(b){bool duplicate=false;for(size_t i=0;i<n;++i)if(ids[i]==b->_resource.id)duplicate=true;if(!duplicate){if(n==64){rc=NativeMetal540DesktopSession::Capacity;break;}ids[n++]=b->_resource.id;}}}
        uint64_t fence=0;if(!rc)for(id owner in op->owners)if([owner isKindOfClass:CBMetalTexture.class])((CBMetalTexture*)owner)->_ready=false;
        if(!rc)rc=c->session->submit(ib->_resource.id,words,ids,n,fence);
        if(!rc)rc=c->session->wait(fence,1000000000ull);
        if(!rc)for(id owner in op->owners)if([owner isKindOfClass:CBMetalTexture.class])((CBMetalTexture*)owner)->_ready=true;
    }
    if(_pendingReserved){--c->pending;_pendingReserved=false;}
    if(!c->pending&&!c->session->lost()){
        for(NSDictionary *pending in c->deferred){int released=c->session->release([pending[@"id"] unsignedLongLongValue]);
            if(released){rc=released;c->lost=true;if(pending[@"owner"]!=NSNull.null)[c->held addObject:pending[@"owner"]];}}
        [c->deferred removeAllObjects];
    }
    if(rc){if(c->session->lost())c->lost=true;[self fail:@"Native draw/submit/fence failed"];
        if(c->lost){[c->held addObjectsFromArray:_operations];[c->held addObject:ib?:[NSObject new]];[c->held addObject:scratch?:[NSObject new]];}}
    _ib=nil;_scratch=nil;ib=nil;scratch=nil;[c->lock unlock];
    [_condition lock];_status=_error?MTLCommandBufferStatusError:MTLCommandBufferStatusCompleted;[_condition unlock];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{@autoreleasepool {
        [_condition lock];_callbackThread=NSThread.currentThread;[_condition unlock];
        for(;;){[_condition lock];NSArray *handlers=[_handlers copy];[_handlers removeAllObjects];if(!handlers.count){_callbacksDone=true;_callbackThread=nil;--_queue->_live;[_condition broadcast];[_condition unlock];break;}[_condition unlock];for(void (^handler)(CBMetalCommands *) in handlers)handler(self);}
        [_operations removeAllObjects];
    }});
}
- (void)waitUntilCompleted {
    [_condition lock];if(!_committed){[_condition unlock];[NSException raise:NSInvalidArgumentException format:@"Commit native command buffer before waiting"];return;}
    if(_callbackThread==NSThread.currentThread){[_condition unlock];return;}
    while(!_callbacksDone)[_condition wait];[_condition unlock];
}
@end
@implementation CBMetalRender
- (void)check {if(_ended||_commands->_committed)[NSException raise:NSInvalidArgumentException format:@"Native render encoder is closed"];}
- (void)setRenderPipelineState:(CBMetalPipeline *)p {[self check];if(![p isKindOfClass:CBMetalPipeline.class]||p.device!=_commands.device){[_commands fail:@"Foreign render pipeline"];return;}_pipeline=p;}
- (void)bindBuffer:(CBMetalBuffer *)b offset:(NSUInteger)o index:(NSUInteger)i stage:(unsigned)s {
    [self check];if(i>=8||![b isKindOfClass:CBMetalBuffer.class]||b.device!=_commands.device||o>b.length||o>UINT32_MAX){[_commands fail:@"Invalid native buffer binding"];return;}_buffers[s][i]=b;_offsets[s][i]=o;
}
- (void)setVertexBuffer:(CBMetalBuffer *)b offset:(NSUInteger)o atIndex:(NSUInteger)i {[self bindBuffer:b offset:o index:i stage:0];}
- (void)setFragmentBuffer:(CBMetalBuffer *)b offset:(NSUInteger)o atIndex:(NSUInteger)i {[self bindBuffer:b offset:o index:i stage:1];}
- (void)setVertexBytes:(const void *)bytes length:(NSUInteger)n atIndex:(NSUInteger)i {auto b=[(CBMetalDevice*)_commands.device newBufferWithBytes:bytes length:n options:0];[self setVertexBuffer:b offset:0 atIndex:i];}
- (void)setFragmentBytes:(const void *)bytes length:(NSUInteger)n atIndex:(NSUInteger)i {auto b=[(CBMetalDevice*)_commands.device newBufferWithBytes:bytes length:n options:0];[self setFragmentBuffer:b offset:0 atIndex:i];}
- (void)bindTexture:(CBMetalTexture *)t index:(NSUInteger)i stage:(unsigned)s {[self check];if(i>=8||![t isKindOfClass:CBMetalTexture.class]||t.device!=_commands.device||!t->_ready||!(t.usage&MTLTextureUsageShaderRead)){[_commands fail:@"Invalid sampled texture"];return;}_textures[s][i]=t;}
- (void)setVertexTexture:(CBMetalTexture *)t atIndex:(NSUInteger)i {[self bindTexture:t index:i stage:0];}
- (void)setFragmentTexture:(CBMetalTexture *)t atIndex:(NSUInteger)i {[self bindTexture:t index:i stage:1];}
- (void)bindSampler:(CBMetalSampler *)s index:(NSUInteger)i stage:(unsigned)stage {[self check];if(i>=QGPU_STAGE_SAMPLER_COUNT||![s isKindOfClass:CBMetalSampler.class]||s.device!=_commands.device){[_commands fail:@"Invalid sampler"];return;}_samplers[stage][i]=s;}
- (void)setVertexSamplerState:(CBMetalSampler *)s atIndex:(NSUInteger)i {[self bindSampler:s index:i stage:0];}
- (void)setFragmentSamplerState:(CBMetalSampler *)s atIndex:(NSUInteger)i {[self bindSampler:s index:i stage:1];}
- (void)setViewport:(MTLViewport)v {[self check];if(!std::isfinite(v.originX)||!std::isfinite(v.originY)||!std::isfinite(v.width)||!std::isfinite(v.height)||v.width<=0||v.height<=0||v.width>16384||v.height>16384||v.znear!=0||v.zfar!=1){[_commands fail:@"Unsupported viewport"];return;}_viewport=v;}
- (void)setScissorRect:(MTLScissorRect)r {[self check];if(r.x>_targets[0].width||r.width>_targets[0].width-r.x||r.y>_targets[0].height||r.height>_targets[0].height-r.y){[_commands fail:@"Scissor outside native target"];return;}_scissor=r;}
- (void)drawPrimitives:(MTLPrimitiveType)primitive vertexStart:(NSUInteger)start vertexCount:(NSUInteger)count {
    [self check];if(_commands->_operations.count>=256){[_commands fail:@"Native command operation capacity reached"];return;}if(!_pipeline||!count||start>UINT32_MAX||count>GPU::Shader::MaxDrawVertices||start+count>UINT32_MAX||
        (primitive!=MTLPrimitiveTypeTriangle&&primitive!=MTLPrimitiveTypeTriangleStrip&&primitive!=MTLPrimitiveTypeLine)){[_commands fail:@"Invalid native draw"];return;}
    auto op=[CBMetalOperation new];op->pipeline=_pipeline;NSMutableArray *owners=[NSMutableArray arrayWithObject:_pipeline];auto &d=op->draw;auto &state=d.state;
    state.version=GPU::Shader::Version;state.registry=_pipeline->_vertex->_image->registry;state.primitive=uint32_t(primitive);state.start=uint32_t(start);state.count=uint32_t(count);state.instances=1;
    state.depthStencil=GPU::Shader::defaultDepthStencilState();state.state=GPU::Shader::defaultRenderState(uint32_t(_targets[0].width),uint32_t(_targets[0].height));
    float view[]={float(_viewport.originX+_viewport.width/2),float(_viewport.width/2),float(_viewport.originY+_viewport.height/2),float(-_viewport.height/2)};memcpy(state.state.viewport,view,sizeof view);
    state.scissor[0]=uint32_t(_scissor.x);state.scissor[1]=uint32_t(_scissor.y);state.scissor[2]=uint32_t(_scissor.width);state.scissor[3]=uint32_t(_scissor.height);
    for(unsigned i=0;i<8;++i){auto a=_pipeline->_descriptor.colorAttachments[i];if(!_targets[i]){if(a.pixelFormat){[_commands fail:@"Missing pipeline attachment"];return;}continue;}
        auto t=_targets[i];if(t.pixelFormat!=a.pixelFormat){[_commands fail:@"Render attachment format mismatch"];return;}[owners addObject:t];auto plane=cbPlane(t,i+1);
        auto color=GPU::Shader::copyColorState(state.state);color.blend=a.blendingEnabled;color.writeMask=uint32_t(a.writeMask);
        if(a.rgbBlendOperation!=MTLBlendOperationAdd||a.alphaBlendOperation!=MTLBlendOperationAdd||
           (a.blendingEnabled&&(a.sourceRGBBlendFactor!=MTLBlendFactorOne||a.sourceAlphaBlendFactor!=MTLBlendFactorOne||a.destinationRGBBlendFactor!=MTLBlendFactorOneMinusSourceAlpha||a.destinationAlphaBlendFactor!=MTLBlendFactorOneMinusSourceAlpha))){[_commands fail:@"Only premultiplied alpha blending is supported"];return;}
        color.destinationRGB=color.destinationAlpha=a.blendingEnabled?GPU::Shader::OneMinusSourceAlpha:GPU::Shader::Zero;
        if(!i){d.target=t->_backing->_resource.id;state.target=plane;state.state.blend=color.blend;state.state.writeMask=color.writeMask;state.state.destinationRGB=color.destinationRGB;state.state.destinationAlpha=color.destinationAlpha;}
        else {d.colors[i-1]=t->_backing->_resource.id;state.colorTargets[i-1]=plane;state.colorStates[i-1]=color;}
    }
    const QGPUProgramDescription *programs[]={&_pipeline->_vertex->_image->info,&_pipeline->_fragment->_image->info};
    uint32_t fetchMask=0;
    for(unsigned i=0;i<QGPU_MAX_ATTRIBUTES;++i)if(programs[0]->attributeMask&(1u<<i)){auto a=_pipeline->_descriptor.vertexDescriptor.attributes[i];auto l=_pipeline->_descriptor.vertexDescriptor.layouts[a.bufferIndex];
        state.attributes[i]={uint32_t(a.format),uint32_t(a.offset),uint32_t(a.bufferIndex),0};state.layouts[a.bufferIndex]={uint32_t(l.stride),uint32_t(l.stepFunction),uint32_t(l.stepRate),0};fetchMask|=1u<<a.bufferIndex;}
    for(unsigned stage=0;stage<2;++stage){const auto &p=*programs[stage];
        for(unsigned i=0;i<8;++i)if((p.bufferMask|(stage?0:fetchMask))&(1u<<i)){auto b=_buffers[stage][i];NSUInteger off=_offsets[stage][i];
            GPU::Shader::Program description{};memcpy(&description,&p,sizeof p);uint64_t required=0;
            bool footprint=GPU::Shader::bindingBytes(description,i,stage?0:uint64_t(start)+count,1,required);
            if(!b||off>=b.length||b.length-off>UINT32_MAX||!footprint||required>b.length-off){[_commands fail:@"Missing or undersized original AIR buffer"];return;}
            auto &binding=stage?state.fragmentBuffers[i]:state.buffers[i];binding={i+1,uint32_t(off),uint32_t(b.length-off),0};d.buffers[stage*8+i]={b->_resource.id,uint32_t(off),uint32_t(b.length-off)};[owners addObject:b];}
        auto *planes=stage?state.textures:state.vertexTextures;auto *samplers=stage?state.samplers:state.vertexSamplers;
        for(unsigned i=0;i<8;++i)if((p.sampleMask|p.fetchMask|p.queryMask)&(1u<<i)){unsigned logical=GPU::Shader::textureBinding(p,i);auto t=_textures[stage][logical];if(!t){[_commands fail:@"Missing original AIR texture"];return;}planes[i]=cbPlane(t,i+1);(stage?d.fragmentTextures[i]:d.vertexTextures[i])=t->_backing->_resource.id;[owners addObject:t];}
        for(unsigned i=0;i<QGPU_STAGE_SAMPLER_COUNT;++i)if(p.samplerMask&(1u<<i)){auto s=_samplers[stage][i];if(!s){[_commands fail:@"Missing original AIR sampler"];return;}samplers[i]=s->_state;[owners addObject:s];}
    }
    op->owners=[owners copy];[_commands->_operations addObject:op];
}
- (void)endEncoding {[self check];_ended=true;_commands->_active=false;}
@end
