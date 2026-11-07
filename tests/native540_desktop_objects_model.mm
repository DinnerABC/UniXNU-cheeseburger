// SPDX-License-Identifier: MIT
#import "NativeMetal540DesktopObjects.h"
#import "NativeMetal540DeviceAdapter.h"
#include "NativeMetal540DesktopConstants.h"
#include <cassert>
#include <map>
#include <vector>
#include <atomic>
struct Model {uint64_t next=0,fence=0;std::atomic<unsigned> submits{0},waits{0},frees{0};bool failWait=false;__unsafe_unretained CBMetalBuffer *inflight=nil;std::map<uint64_t,std::vector<uint32_t>> data;};
@interface ModelOwner : NSObject { @public Model value; }
@end
@implementation ModelOwner
@end
static int call(void *p,uint32_t s,const void *in,size_t,void *out,size_t *n){auto &m=*static_cast<Model *>(p);
    if(s==NativeAdrenoMethodQuery){*static_cast<NativeAdrenoAbiParams *>(out)={1,36,36,1,1ull<<28};*n=sizeof(NativeAdrenoAbiParams);return 0;}
    if(s==NativeAdrenoMethodGetDeviceInfo){NativeAdrenoAbiDeviceInfo d{};d.chip_id=0x05040001;d.flags=1;d.va_start=0x40000000;d.va_size=1ull<<28;*static_cast<NativeAdrenoAbiDeviceInfo *>(out)=d;*n=sizeof d;return 0;}
    if(s==NativeAdrenoMethodBoAlloc){uint64_t h=++m.next;m.data[h].resize(static_cast<const NativeAdrenoAbiBoAlloc *>(in)->bytes/4);*static_cast<NativeAdrenoAbiBoAllocReply *>(out)={h};*n=sizeof(NativeAdrenoAbiBoAllocReply);return 0;}
    if(s==NativeAdrenoMethodBoMap){*static_cast<NativeAdrenoAbiBoMapReply *>(out)={static_cast<const NativeAdrenoAbiBoMap *>(in)->iova};*n=sizeof(NativeAdrenoAbiBoMapReply);return 0;}
    if(s==NativeAdrenoMethodBoCpuMap){auto h=static_cast<const NativeAdrenoAbiBoCpuMap *>(in)->handle;*static_cast<NativeAdrenoAbiBoCpuMapReply *>(out)={uint32_t(h),0,m.data[h].size()*4};*n=sizeof(NativeAdrenoAbiBoCpuMapReply);return 0;}
    if(s==NativeAdrenoMethodBoCpuUnmap){*n=0;return 0;}
    if(s==NativeAdrenoMethodBoFree){++m.frees;m.data.erase(static_cast<const NativeAdrenoAbiBoFree *>(in)->handle);*n=0;return 0;}
    if(s==NativeAdrenoMethodSubmit){++m.submits;*static_cast<NativeAdrenoAbiSubmitReply *>(out)={++m.fence};*n=sizeof(NativeAdrenoAbiSubmitReply);return 0;}
    if(s==NativeAdrenoMethodWaitFence){++m.waits;if(m.inflight)assert(!m.inflight.contents);*n=0;return m.failWait?-92:0;}assert(false);return -1;
}
static CBMetalDevice *device(ModelOwner *owner){Model &m=owner->value;NativeMetal540DesktopSession::Transport t{};t.context=&m;t.call=call;
    t.mapView=[](void *p,uint32_t h,void **cpu,size_t *n){auto &b=static_cast<Model *>(p)->data[h];*cpu=b.data();*n=b.size()*4;return 0;};
    t.unmapView=[](void *,uint32_t,void *,size_t){return 0;};t.flush=[](void *,void *,size_t,bool){return 0;};NSError *error=nil;
    auto d=[[CBMetalDevice alloc] initWithTransport:t connectionOwner:owner compiler:qgpu_compile_air_nir error:&error];assert(d&&!error);return d;}
int main(int argc,char **argv){@autoreleasepool {assert(argc==2);
    auto constants=[MTLFunctionConstantValues new];float vector[3]={1,2,3};bool flag=true;
    [constants setConstantValue:vector type:MTLDataTypeFloat3 atIndex:7];[constants setConstantValue:&flag type:MTLDataTypeBool atIndex:2];
    std::vector<GPU::Compiler::FunctionConstantValue> values;NSError *constantError=nil;assert(CBMetalSnapshotConstants(constants,values,&constantError)&&!constantError&&values.size()==2);
    assert(values[0].index==2&&values[0].bytes[0]==1&&values[1].index==7&&!memcmp(values[1].bytes.data(),vector,12));
    [constants reset];assert(values[1].index==7&&!memcmp(values[1].bytes.data(),vector,12));
    [constants setConstantValue:&flag type:MTLDataTypeBool withName:@"named"];
    // reset cleared both indexed entries, so only the named one remains.
    assert(CBMetalSnapshotConstants(constants,values,&constantError)&&values.size()==1&&values[0].name=="named"&&values[0].bytes[0]==1);
    [constants reset];values.clear();    for(unsigned mode=0;mode<2;++mode){auto owner=[ModelOwner new];auto &m=owner->value;m.failWait=mode==1;auto d=device(owner);NSError *error=nil;
        id identity=[NSObject new];[d setPublicDeviceIdentity:identity];
        auto library=[d newLibraryWithURL:[NSURL fileURLWithPath:@(argv[1])] error:&error];assert(library&&!error&&library.functionNames.count>100);
        auto vs=[library newFunctionWithName:@"image_rect_blit_vert_lpf"],fs=[library newFunctionWithName:@"image_rect_blit_frag_lpf"];assert(vs&&fs&&vs.device==identity&&fs.functionType==MTLFunctionTypeFragment);
        auto descriptor=[MTLRenderPipelineDescriptor new];descriptor.vertexFunction=(id<MTLFunction>)(id)vs;descriptor.fragmentFunction=(id<MTLFunction>)(id)fs;
        for(unsigned i=0;i<2;++i){auto a=descriptor.colorAttachments[i];a.pixelFormat=MTLPixelFormatBGRA8Unorm;a.blendingEnabled=YES;a.sourceRGBBlendFactor=a.sourceAlphaBlendFactor=MTLBlendFactorOne;a.destinationRGBBlendFactor=a.destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;}
        auto pipeline=[d newRenderPipelineStateWithDescriptor:descriptor error:&error];assert(pipeline&&!error);
        auto attributeVertex=[library newFunctionWithName:@"std_vert0_lpf"];MTLRenderPipelineDescriptor *attributeDescriptor=[descriptor copy];attributeDescriptor.vertexFunction=(id<MTLFunction>)(id)attributeVertex;attributeDescriptor.fragmentFunction=(id<MTLFunction>)(id)[library newFunctionWithName:@"path_clear_frag_lpf"];
        attributeDescriptor.vertexDescriptor=[MTLVertexDescriptor vertexDescriptor];attributeDescriptor.vertexDescriptor.attributes[0].format=MTLVertexFormatFloat4;attributeDescriptor.vertexDescriptor.attributes[0].bufferIndex=0;
        attributeDescriptor.vertexDescriptor.attributes[3].format=MTLVertexFormatFloat2;attributeDescriptor.vertexDescriptor.attributes[3].offset=16;attributeDescriptor.vertexDescriptor.attributes[3].bufferIndex=0;
        attributeDescriptor.vertexDescriptor.layouts[0].stride=24;attributeDescriptor.vertexDescriptor.layouts[0].stepFunction=MTLVertexStepFunctionPerVertex;attributeDescriptor.vertexDescriptor.layouts[0].stepRate=1;
        auto attributes=[d newRenderPipelineStateWithDescriptor:attributeDescriptor error:&error];if(!attributes)fprintf(stderr,"attribute pipeline %s\n",error.localizedDescription.UTF8String);assert(attributes&&!error);
        attributeDescriptor.vertexDescriptor.layouts[0].stride=3;assert(![d newRenderPipelineStateWithDescriptor:attributeDescriptor error:&error]&&error);
        auto tex=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:1080 height:1920 mipmapped:NO];tex.storageMode=MTLStorageModeShared;tex.usage=MTLTextureUsageRenderTarget;
        auto target=[d newTextureWithDescriptor:tex],other=[d newTextureWithDescriptor:tex];assert(target&&other&&target.allocatedSize>=4352ull*1920);
        tex.cpuCacheMode=MTLCPUCacheModeWriteCombined;assert(![d newTextureWithDescriptor:tex]);tex.cpuCacheMode=MTLCPUCacheModeDefaultCache;
        descriptor.rasterSampleCount=4;assert(![d newRenderPipelineStateWithDescriptor:descriptor error:&error]);descriptor.rasterSampleCount=1;
        auto rejected=[d newCommandQueueWithMaxCommandBufferCount:0];assert(!rejected);
        auto sampledDescriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:64 mipmapped:NO];sampledDescriptor.storageMode=MTLStorageModeShared;sampledDescriptor.usage=MTLTextureUsageShaderRead;
        auto sampled=[d newTextureWithDescriptor:sampledDescriptor];uint32_t pixels[4096]{};[sampled replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 withBytes:pixels bytesPerRow:256];
        auto buffer=[d newBufferWithLength:4096 options:0];auto samplerDescriptor=[MTLSamplerDescriptor new];samplerDescriptor.minFilter=samplerDescriptor.magFilter=MTLSamplerMinMagFilterLinear;auto sampler=[d newSamplerStateWithDescriptor:samplerDescriptor];assert(sampler);
        auto dsDescriptor=[MTLDepthStencilDescriptor new];dsDescriptor.depthCompareFunction=MTLCompareFunctionAlways;dsDescriptor.depthWriteEnabled=NO;
        auto ds=[d newDepthStencilStateWithDescriptor:dsDescriptor];assert(ds&&ds.device==identity&&ds.depthCompareFunction==MTLCompareFunctionAlways&&!ds.depthWriteEnabled);
        dsDescriptor.depthCompareFunction=MTLCompareFunction(99);assert(![d newDepthStencilStateWithDescriptor:dsDescriptor]);
        auto q=[d newCommandQueueWithMaxCommandBufferCount:1];auto commands=[q commandBuffer];assert(commands&&![q commandBuffer]);
        auto pass=[MTLRenderPassDescriptor renderPassDescriptor];for(unsigned i=0;i<2;++i){pass.colorAttachments[i].texture=(id<MTLTexture>)(id)(i?other:target);pass.colorAttachments[i].loadAction=MTLLoadActionClear;pass.colorAttachments[i].storeAction=MTLStoreActionStore;pass.colorAttachments[i].clearColor=MTLClearColorMake(0,0,0,1);}
        auto encoder=[commands renderCommandEncoderWithDescriptor:pass];assert(encoder);[encoder setRenderPipelineState:pipeline];
        for(unsigned i:{1u,2u,5u})[encoder setVertexBuffer:buffer offset:0 atIndex:i];[encoder setFragmentBuffer:buffer offset:0 atIndex:1];[encoder setFragmentTexture:sampled atIndex:3];[encoder setFragmentSamplerState:sampler atIndex:0];
        [encoder setDepthStencilState:ds];[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[encoder endEncoding];
        __block unsigned completions=0;[commands addCompletedHandler:^(CBMetalCommands *b){++completions;[b waitUntilCompleted];[b addCompletedHandler:^(CBMetalCommands *){++completions;}];
            if(!mode){auto nestedQueue=[d newCommandQueue];auto nested=[nestedQueue commandBuffer];[nested commit];[nested waitUntilCompleted];assert(nested.status==MTLCommandBufferStatusCompleted);}
        }];
        m.inflight=buffer;[commands commit];[commands waitUntilCompleted];m.inflight=nil;assert(completions==2);
        if(!mode){assert(commands.status==MTLCommandBufferStatusCompleted&&!commands.error&&m.submits==3&&m.waits==3);assert([q commandBuffer]);}
        else {assert(commands.status==MTLCommandBufferStatusError&&commands.error&&m.submits==1&&m.waits==1&&!m.frees);assert(!buffer.contents);}
        // Desktop adapter closure: the shared 21H461 adapter wraps this exact
        // backend. On host the full-target audit is recorded, not fatal; the
        // device build (NATIVE540_TARGET_METAL) fails closed on any gap.
        NSError *adapterError=nil;id wrapped=NativeMetal540CreateInitializedDesktopDevice(d,(uint32_t)(uintptr_t)&m,&adapterError);
        if(!wrapped)fprintf(stderr,"desktop adapter: %s\n",adapterError.localizedDescription.UTF8String);
        assert(wrapped&&NativeMetal540CreateInitializedDesktopDevice(d,0,&adapterError)==nil);
        auto adapterQueue=[wrapped newCommandQueueWithMaxCommandBufferCount:2];assert(adapterQueue&&adapterQueue.device==wrapped);
        auto adapterSampler=[wrapped newSamplerStateWithDescriptor:samplerDescriptor];assert(adapterSampler&&adapterSampler.device==wrapped);
        assert([wrapped maxBufferLength]==16u*1024*1024);
        assert(![wrapped newCommandQueueWithMaxCommandBufferCount:0]);
        id afterSecond=NativeMetal540CreateInitializedDesktopDevice(d,(uint32_t)(uintptr_t)&m,&adapterError);
        assert(afterSecond&&afterSecond!=wrapped);
        // Unknown-selector forwarding: no exception escapes, selector recorded.
        if(!mode){unsigned unknownBefore=CBMetalUnknownSelectorCount();
            @autoreleasepool {(void)[wrapped performSelector:@selector(unrecognizedDesktopSelector:) withObject:nil];}
            assert(CBMetalUnknownSelectorCount()==unknownBefore+1);assert(![wrapped respondsToSelector:@selector(unrecognizedDesktopSelector:)]);}
    }
    puts("PASS actual ObjC desktop library/pipeline/full-HD MRT/sampling/blend/queue/encoder/submission/fence and nested callbacks; modeled transport NOT GPU execution");
}}
