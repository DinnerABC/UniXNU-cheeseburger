// SPDX-License-Identifier: GPL-2.0-only
#import "NativeMetal540Objects.h"
#include "NativeMetal540Render.h"
#include <cassert>
#include <atomic>
#include <vector>
struct Model {
    NSMutableDictionary *views=[NSMutableDictionary new],*sizes=[NSMutableDictionary new];
    std::atomic<unsigned> submits{0},waits{0},flushes{0},closed{0};unsigned next=0;
    bool submitFailed=false,waitFailed=false,noPixels=false;void *imported=nullptr;
    unsigned retirementOps=0,retirementFailAt=0,freeAck=0,viewAck=0;bool freed[8]={};
    __unsafe_unretained Native540Device *inflightDevice=nil;
    __unsafe_unretained Native540Buffer *inflightVertex=nil;
};
static int call(void *p,uint32_t s,const void *in,size_t,void *out,size_t *bytes){auto &m=*(Model*)p;
    if(s==NativeAdrenoMethodBoCpuUnmap||s==NativeAdrenoMethodBoFree){
        uint64_t h=((const NativeAdrenoAbiBoFree*)in)->handle;assert(h&&h<=7&&!m.freed[h]);
        if(++m.retirementOps==m.retirementFailAt)return -93;
        if(s==NativeAdrenoMethodBoFree){m.freed[h]=true;++m.freeAck;}*bytes=0;return 0;
    }
    if(s==NativeAdrenoMethodQuery){*(NativeAdrenoAbiParams*)out={1,36,36,1,1u<<28};*bytes=sizeof(NativeAdrenoAbiParams);return 0;}
    if(s==NativeAdrenoMethodGetDeviceInfo){NativeAdrenoAbiDeviceInfo d{};d.chip_id=0x05040001;d.flags=1;d.va_start=0x40000000;d.va_size=0x1000000;*(NativeAdrenoAbiDeviceInfo*)out=d;*bytes=sizeof d;return 0;}
    if(s==NativeAdrenoMethodBoImport){auto a=(const NativeAdrenoAbiBoImport*)in;m.imported=(void*)(uintptr_t)a->address;
        uint64_t h=++m.next;m.sizes[@(h)]=@(a->bytes);*(NativeAdrenoAbiBoAllocReply*)out={h};*bytes=sizeof(NativeAdrenoAbiBoAllocReply);return 0;}
    if(s==NativeAdrenoMethodBoAlloc){auto a=(const NativeAdrenoAbiBoAlloc*)in;uint64_t h=++m.next;m.sizes[@(h)]=@(a->bytes);*(NativeAdrenoAbiBoAllocReply*)out={h};*bytes=sizeof(NativeAdrenoAbiBoAllocReply);return 0;}
    if(s==NativeAdrenoMethodBoMap){auto a=(const NativeAdrenoAbiBoMap*)in;*(NativeAdrenoAbiBoMapReply*)out={a->iova};*bytes=sizeof(NativeAdrenoAbiBoMapReply);return 0;}
    if(s==NativeAdrenoMethodBoCpuMap){auto a=(const NativeAdrenoAbiBoCpuMap*)in;*(NativeAdrenoAbiBoCpuMapReply*)out={(uint32_t)a->handle,0,[m.sizes[@(a->handle)] unsignedLongLongValue]};*bytes=sizeof(NativeAdrenoAbiBoCpuMapReply);return 0;}
    if(s==NativeAdrenoMethodSubmit){auto a=(const NativeAdrenoAbiSubmit*)in;assert(a->buffer_count==1&&a->buffers[0].bytes>4096);++m.submits;if(m.submitFailed)return -91;
        NSMutableData *target=m.views[@1];const uint32_t *words=m.imported?(const uint32_t*)m.imported:(const uint32_t*)target.mutableBytes;
        for(unsigned i=0;i<4096;++i)assert(words[i]==Native540Render::Sentinel);
        *(NativeAdrenoAbiSubmitReply*)out={16+m.submits.load()};*bytes=sizeof(NativeAdrenoAbiSubmitReply);return 0;}
    if(s==NativeAdrenoMethodWaitFence){++m.waits;if(m.waitFailed)return -92;
        if(m.inflightDevice){unsigned allocations=m.next;uint32_t input[4]={};assert(!m.inflightVertex.contents);
            for(unsigned i=0;i<16;++i){assert(![m.inflightDevice newBufferWithLength:16 options:MTLResourceHazardTrackingModeTracked]);
                assert(![m.inflightDevice newBufferWithBytes:input length:sizeof input options:0]);}
            assert(m.next==allocations&&!m.retirementOps);
        }
        // HOST SOFTWARE DMA ONLY; this is not real AIR/GPU execution.
        NSMutableData *target=m.views[@1],*scratch=m.views[@7];
        uint32_t *words=m.imported?(uint32_t*)m.imported:(uint32_t*)target.mutableBytes;
        auto rgba=(const uint32_t*)((NSMutableData*)m.views[@3]).bytes;
        uint32_t color=0xff000000|(rgba[0]==0x3f800000?0xff:0)|(rgba[1]==0x3f800000?0xff00:0)|(rgba[2]==0x3f800000?0xff0000:0);
        if(!m.noPixels)for(int y=0;y<64;++y)for(int x=0;x<64;++x){int X=2*x+1,Y=2*y+1;words[y*64+x]=(Y<112&&2*X+Y>144&&112-2*X+Y>0)?color:Native540Render::Background;}
        auto stamp=(uint32_t*)scratch.mutableBytes;stamp[0]=1;stamp[2]=2;stamp[4]=3;*bytes=0;return 0;}
    assert(false);return -99;
}
static Native540Device *device(Model &m){Native540ObjectsTransport t{};t.context=&m;t.connectionOwner=m.views;t.verifiedCPUInhibit=true;t.call=call;
    t.mapView=[](void *p,uint64_t token,void **address,size_t *bytes,id __autoreleasing *owner)->int{auto &m=*(Model*)p;auto data=[NSMutableData dataWithLength:[m.sizes[@(token)] unsignedLongLongValue]];m.views[@(token)]=data;*address=data.mutableBytes;*bytes=data.length;*owner=data;return 0;};
    t.session=9;t.unmapView=[](void *p,uint32_t token,uint64_t address,uint64_t bytes)->int{
        auto &m=*(Model*)p;assert(token&&address&&bytes);if(++m.retirementOps==m.retirementFailAt)return -93;
        ++m.viewAck;return 0;
    };
    t.flush=[](void *p,void*,size_t,bool)->int{++((Model*)p)->flushes;return 0;};
    t.retireConnection=[](void *p,bool,NSArray*){++((Model*)p)->closed;};NSError *error=nil;auto d=[[Native540Device alloc] initControlledWithTransport:t error:&error];assert(d&&!error);return d;}
int main(int argc,char **argv){@autoreleasepool {assert(argc==2);NSData *real=[NSData dataWithContentsOfFile:@(argv[1])];assert(real.length==92788);
    for(unsigned mode=0;mode<27;++mode){Model m;bool retirementFailure=mode>=8;
        m.submitFailed=!retirementFailure&&mode%4==1;m.waitFailed=!retirementFailure&&mode%4==2;m.noPixels=!retirementFailure&&mode%4==3;
        if(retirementFailure)m.retirementFailAt=mode-7;
        @autoreleasepool {auto d=device(m);uint32_t copied[12]={1,2,3,4};
            assert(![d newBufferWithBytes:nullptr length:16 options:0]);
            auto copy=[d newBufferWithBytes:copied length:sizeof copied options:MTLResourceStorageModePrivate];assert(!copy);
            NSError *error=nil;auto data=dispatch_data_create(real.bytes,real.length,nullptr,DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            auto lib=[d newLibraryWithData:data error:&error];assert(lib&&!error&&lib.functionNames.count==2);
            auto v=[lib newFunctionWithName:@"SolidColor_VertexFunc"],f=[lib newFunctionWithName:@"SolidColor_FragFunc"];
            assert(v.functionType==MTLFunctionTypeVertex&&f.functionType==MTLFunctionTypeFragment);
            assert(![d newComputePipelineStateWithFunction:v error:&error]);
            auto pd=[MTLRenderPipelineDescriptor new];pd.vertexFunction=(id<MTLFunction>)(id)v;pd.fragmentFunction=(id<MTLFunction>)(id)f;
            pd.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
            pd.label=@"solid";MTLRenderPipelineReflection *reflection=nil;
            auto p=[d newRenderPipelineStateWithDescriptor:pd options:MTLPipelineOptionNone reflection:&reflection error:&error];
            assert(!reflection&&[p.label isEqualToString:@"solid"]&&[p supportsVertexAmplificationCount:1]&&![p supportsVertexAmplificationCount:2]);
            assert(![d newRenderPipelineStateWithDescriptor:pd options:MTLPipelineOptionArgumentInfo reflection:&reflection error:&error]&&error&&!reflection);
            __block unsigned callbacks=0;dispatch_semaphore_t ready=dispatch_semaphore_create(0);
            [d newRenderPipelineStateWithDescriptor:pd completionHandler:^(Native540RenderPipeline *async,NSError *failure){
                assert(async&&!failure&&[async.label isEqualToString:@"solid"]&&async.device==d);assert(![d currentAllocatedSize]);++callbacks;dispatch_semaphore_signal(ready);
            }];
            pd.rasterSampleCount=4;pd.label=@"changed";
            assert(dispatch_semaphore_wait(ready,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC))==0&&callbacks==1);
            [d newRenderPipelineStateWithDescriptor:pd options:MTLPipelineOptionArgumentInfo completionHandler:^(Native540RenderPipeline *async,MTLRenderPipelineReflection *r,NSError *failure){assert(!async&&!r&&failure);++callbacks;dispatch_semaphore_signal(ready);}];
            assert(dispatch_semaphore_wait(ready,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC))==0&&callbacks==2);
            [d newRenderPipelineStateWithDescriptor:pd completionHandler:nil];assert(!m.submits&&!m.next);
            pd.rasterSampleCount=1;pd.label=@"solid";error=nil;
            if(!p)fprintf(stderr,"pipeline %s sample%lu raster%d vd%p vl%p fl%p mask%lu/%lu\n",error.localizedDescription.UTF8String,(unsigned long)pd.rasterSampleCount,pd.rasterizationEnabled,(__bridge void*)pd.vertexDescriptor,(__bridge void*)pd.vertexLinkedFunctions,(__bridge void*)pd.fragmentLinkedFunctions,(unsigned long)pd.colorAttachments[0].writeMask,(unsigned long)pd.colorAttachments[1].writeMask);
            assert(p&&!error);
            pd.rasterSampleCount=4;assert(![d newRenderPipelineStateWithDescriptor:pd error:&error]);pd.rasterSampleCount=1;
            auto desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:64 mipmapped:NO];desc.storageMode=MTLStorageModeShared;desc.usage=MTLTextureUsageRenderTarget;
            desc.cpuCacheMode=MTLCPUCacheModeWriteCombined;assert(![d newTextureWithDescriptor:desc]);desc.cpuCacheMode=MTLCPUCacheModeDefaultCache;
            desc.hazardTrackingMode=MTLHazardTrackingModeUntracked;assert(![d newTextureWithDescriptor:desc]);desc.hazardTrackingMode=MTLHazardTrackingModeTracked;
            desc.compressionType=MTLTextureCompressionTypeLossy;assert(![d newTextureWithDescriptor:desc]);desc.compressionType=MTLTextureCompressionTypeLossless;
            desc.swizzle=MTLTextureSwizzleChannelsMake(MTLTextureSwizzleBlue,MTLTextureSwizzleGreen,MTLTextureSwizzleRed,MTLTextureSwizzleAlpha);
            assert(![d newTextureWithDescriptor:desc]);desc.swizzle=MTLTextureSwizzleChannelsDefault;assert(m.next==0);
            id surfaceOwner=nil;Native540Texture *target=nil;
            if(mode>=4){NSDictionary *props=@{(__bridge id)kIOSurfaceWidth:@64,(__bridge id)kIOSurfaceHeight:@64,
                (__bridge id)kIOSurfaceBytesPerRow:@256,(__bridge id)kIOSurfaceBytesPerElement:@4,
                (__bridge id)kIOSurfacePixelFormat:@0x52474241,(__bridge id)kIOSurfaceAllocSize:@16384};
                surfaceOwner=CFBridgingRelease(IOSurfaceCreate((__bridge CFDictionaryRef)props));
                target=[d newTextureWithDescriptor:desc iosurface:(__bridge IOSurfaceRef)surfaceOwner plane:0];assert(target&&m.imported);
            } else target=[d newTextureWithDescriptor:desc];
            auto vertex=[d newBufferWithBytes:copied length:48 options:0],color=[d newBufferWithLength:16 options:0];assert(target&&vertex&&color);assert(!memcmp(vertex.contents,copied,sizeof copied));
            const uint32_t red[]={0x3f800000,0,0,0x3f800000};memcpy(color.contents,red,16);
            assert(vertex.allocatedSize==4096&&vertex.cpuCacheMode==MTLCPUCacheModeDefaultCache&&vertex.hazardTrackingMode==MTLHazardTrackingModeTracked);
            assert(target.textureType==MTLTextureType2D&&target.depth==1&&target.sampleCount==1&&target.allocatedSize==16384);
            vertex.label=@"vertices";target.label=@"target";assert([vertex.label isEqualToString:@"vertices"]&&[target.label isEqualToString:@"target"]);
            assert(!vertex.heap&&!vertex.heapOffset&&!vertex.protectionOptions&&!vertex.isAliasable);
            assert(!target.heap&&!target.heapOffset&&!target.protectionOptions&&!target.isAliasable);
            assert(!target.rootResource&&!target.parentTexture&&!target.buffer&&!target.bufferOffset&&!target.bufferBytesPerRow);
            assert(!target.isSparse&&!target.isFramebufferOnly&&!target.allowGPUOptimizedContents&&target.isShareable==(mode>=4));
            assert(target.compressionType==MTLTextureCompressionTypeLossless&&target.swizzle.red==MTLTextureSwizzleRed);
            for(id resource in @[vertex,target]){
                assert([resource setPurgeableState:MTLPurgeableStateKeepCurrent]==MTLPurgeableStateNonVolatile);
                assert([resource setPurgeableState:MTLPurgeableStateNonVolatile]==MTLPurgeableStateNonVolatile);
                for(NSUInteger state: {0ul,3ul,4ul,NSUIntegerMax}){bool rejected=false;
                    @try{[resource setPurgeableState:MTLPurgeableState(state)];}@catch(NSException*){rejected=true;}assert(rejected);}
                bool rejected=false;@try{[resource makeAliasable];}@catch(NSException*){rejected=true;}assert(rejected);
            }
            unsigned resources=m.next;
            assert(![target newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm]);
            assert(![target newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:MTLTextureType2D levels:NSMakeRange(0,1) slices:NSMakeRange(0,1)]);
            assert(![target newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:MTLTextureType2D levels:NSMakeRange(0,1) slices:NSMakeRange(0,1) swizzle:MTLTextureSwizzleChannelsDefault]);
            assert(m.next==resources&&!m.retirementOps&&!m.submits&&vertex.contents);
            uint32_t pixels[4096]={};assert(![target getBytes:pixels bytesPerRow:256]);
            auto pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=(id<MTLTexture>)(id)target;
            pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
            pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
            auto q=[d newCommandQueue];auto b=[q commandBuffer];auto e=[b renderCommandEncoderWithDescriptor:pass];
            if(!e)fprintf(stderr,"renderpass %s arrays%lu level%lu slice%lu plane%lu\n",b.error.localizedDescription.UTF8String,(unsigned long)pass.renderTargetArrayLength,(unsigned long)pass.colorAttachments[0].level,(unsigned long)pass.colorAttachments[0].slice,(unsigned long)pass.colorAttachments[0].depthPlane);
            assert(e);
            assert(lib.device==d&&v.device==d&&p.device==d&&target.device==d&&vertex.device==d&&q.device==d&&b.device==d);
            __block unsigned completed=0;
            [b addCompletedHandler:^(Native540CommandBuffer *done){assert(done==b);[done waitUntilCompleted];++completed;}];
            auto later=[q commandBuffer];auto le=[later renderCommandEncoderWithTexture:target];
            [le setRenderPipelineState:p];[le setVertexBuffer:vertex offset:0 atIndex:0];[le setFragmentBuffer:color offset:0 atIndex:0];
            [le drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[le endEncoding];
            [e setRenderPipelineState:p];[e setVertexBuffer:vertex offset:0 atIndex:0];[e setFragmentBuffer:color offset:0 atIndex:0];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];
            if(mode==0){m.inflightDevice=d;m.inflightVertex=vertex;}
            [b commit];[b waitUntilCompleted];m.inflightDevice=nil;m.inflightVertex=nil;
            assert(completed==1);
            [b addCompletedHandler:^(Native540CommandBuffer *done){assert(done==b);[done waitUntilCompleted];++completed;}];assert(completed==2);
            bool read=[target getBytes:pixels bytesPerRow:256];Native540Render::Pixels r{};
            if(read)[target getBytes:pixels bytesPerRow:256 fromRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0];
            bool caught=false;@try{[target getBytes:pixels bytesPerRow:256 fromRegion:MTLRegionMake2D(0,0,32,32) mipmapLevel:0];}
                @catch(NSException*){caught=true;}assert(caught);
            if(retirementFailure||mode%4==0){assert(b.status==MTLCommandBufferStatusCompleted&&read&&Native540Render::verify(pixels,sizeof pixels,r));assert(r.mismatches==0);}
            if(!retirementFailure&&(mode%4==1||mode%4==2))assert(b.status==MTLCommandBufferStatusError&&!read&&b.error);
            if(!retirementFailure&&mode%4==3)assert(b.status==MTLCommandBufferStatusCompleted&&read&&!Native540Render::verify(pixels,sizeof pixels,r));
            if(!retirementFailure&&mode%4==0){
                unsigned allocations=m.next;
                const uint32_t colors[][4]={{0,0x3f800000,0,0x3f800000},{0,0,0x3f800000,0x3f800000},{0x3f800000,0,0,0x3f800000}};
                const uint32_t expected[]={0xff00ff00,0xffff0000,0xff0000ff};
                for(unsigned frame=0;frame<3;++frame){memcpy(color.contents,colors[frame],16);auto next=[q commandBuffer];auto encoder=[next renderCommandEncoderWithDescriptor:pass];assert(encoder);
                    [encoder setRenderPipelineState:p];[encoder setVertexBuffer:vertex offset:0 atIndex:0];[encoder setFragmentBuffer:color offset:0 atIndex:0];
                    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[encoder endEncoding];
                    [next commit];[next waitUntilCompleted];assert(next.status==MTLCommandBufferStatusCompleted&&next.executionRetired&&!next.error);
                    assert([target getBytes:pixels bytesPerRow:256]&&m.next==allocations);
                    assert(Native540Render::verifyColor(pixels,sizeof pixels,expected[frame],r));
                }
                assert(m.submits==4&&m.waits==4);
            }
            if(retirementFailure){
                assert([d retireResources]==-93&&!([d retirementResult].proof));unsigned calls=m.retirementOps;
                assert([d retireResources]==-93&&m.retirementOps==calls);assert(![target getBytes:pixels bytesPerRow:256]);
            } else if(mode%4==0){
                assert([d retireResources]==0);auto receipt=[d retirementResult];assert(receipt.proof&&receipt.count==7&&m.freeAck==7);
                assert(m.viewAck==(mode>=4?6u:7u)&&[d currentAllocatedSize]==0&&vertex.contents==nullptr);
                assert(![target getBytes:pixels bytesPerRow:256]);assert(![d newBufferWithLength:16 options:0]);
                assert([d retireResources]==NativeMetal540SessionRetirement::AlreadyAttempted);
            } else if(mode%4==1||mode%4==2){assert([d retireResources]==NativeMetal540SessionRetirement::Prerequisite&&!m.freeAck);}
            [later commit];[later waitUntilCompleted];assert(later.status==MTLCommandBufferStatusError);
            unsigned expectedSubmits=!retirementFailure&&mode%4==0?4u:1u;
            assert(m.submits==expectedSubmits&&m.waits==(!retirementFailure&&mode%4==1?0u:expectedSubmits));
            auto repeat=[q commandBuffer];
            if(!retirementFailure&&mode%4==3)assert([repeat renderCommandEncoderWithTexture:target]);
            else assert(![repeat renderCommandEncoderWithTexture:target]);
            desc.pixelFormat=MTLPixelFormatBGRA8Unorm;assert(![d newTextureWithDescriptor:desc]);
        }
        assert(m.closed==(retirementFailure?0u:1u));
    }
    puts("PASS actual owned graphics objects/failure/no-pixels; HOST model NOT GPU execution");
}}
