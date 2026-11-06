// SPDX-License-Identifier: GPL-2.0-only
// Actual ObjC implementation + modeled transport; NO hardware/AIR execution claim.
// clang++ -std=c++17 -fobjc-arc -fblocks -Wall -Wextra -Werror
// -Wno-deprecated-declarations -fsanitize=address,undefined
// native540_objects_model.mm NativeMetal540Objects.mm NativeMetal540Client.cpp
// -framework Foundation -framework Metal -o /tmp/native540-objects-model
#import "../package/Userland/NativeMetal540Objects.h"
static constexpr char trustedEntry[]="padding_gradient";
#include <cassert>
#include <atomic>
#include <thread>
#include <chrono>
struct Model {
    NSMutableDictionary *sizes=[NSMutableDictionary new],*views=[NSMutableDictionary new];
    std::atomic<int> submits{0},waits{0},flushes{0},closed{0};
    bool waitFailure=false,submitFailure=false,flushFailure=false,shortMap=false,closedFailed=false;
    bool slowPreparation=false,slowWait=false;
    std::atomic<bool> waitReturned{false};
    uint64_t next=0;size_t retiredOwners=0;std::thread::id host=std::this_thread::get_id();
};
static int call(void *ctx,uint32_t method,const void *in,size_t,void *out,size_t *n) {
    auto &m=*(Model *)ctx;
    switch(method) {
    case NativeAdrenoMethodQuery: {
        NativeAdrenoAbiParams p{};p.abi_version=NativeAdrenoAbiVersion;p.input_bits=36;p.output_bits=36;p.max_bo_bytes=1<<24;
        *(NativeAdrenoAbiParams *)out=p;*n=sizeof(p);return 0;}
    case NativeAdrenoMethodGetDeviceInfo: {
        NativeAdrenoAbiDeviceInfo p{};p.chip_id=0x05040001;p.flags=NativeAdrenoDeviceClientIova;p.va_start=0x4000000;p.va_size=0x1000000;
        *(NativeAdrenoAbiDeviceInfo *)out=p;*n=sizeof(p);return 0;}
    case NativeAdrenoMethodBoAlloc: {
        auto a=(const NativeAdrenoAbiBoAlloc *)in;uint64_t h=++m.next;m.sizes[@(h)]=@(a->bytes);
        *(NativeAdrenoAbiBoAllocReply *)out={h};*n=sizeof(NativeAdrenoAbiBoAllocReply);return 0;}
    case NativeAdrenoMethodBoMap: {
        auto a=(const NativeAdrenoAbiBoMap *)in;*(NativeAdrenoAbiBoMapReply *)out={a->iova};*n=sizeof(NativeAdrenoAbiBoMapReply);return 0;}
    case NativeAdrenoMethodBoCpuMap: {
        auto a=(const NativeAdrenoAbiBoCpuMap *)in;NativeAdrenoAbiBoCpuMapReply reply{};
        reply.token=(uint32_t)a->handle;reply.bytes=[m.sizes[@(a->handle)] unsignedLongLongValue];
        *(NativeAdrenoAbiBoCpuMapReply *)out=reply;
        *n=sizeof(NativeAdrenoAbiBoCpuMapReply);return 0;}
    case NativeAdrenoMethodSubmit: {
        assert(std::this_thread::get_id()!=m.host);auto a=(const NativeAdrenoAbiSubmit *)in;
        assert(a->buffer_count==1&&a->buffers[0].bytes>12);++m.submits;
        if(m.submitFailure)return -91;*(NativeAdrenoAbiSubmitReply *)out={uint64_t(m.submits.load())};*n=sizeof(NativeAdrenoAbiSubmitReply);return 0;}
    case NativeAdrenoMethodWaitFence: {
        assert(std::this_thread::get_id()!=m.host);auto a=(const NativeAdrenoAbiWaitFence *)in;
        assert(a->timeout_ns>0&&a->timeout_ns<=1000000000);++m.waits;
        if(m.slowWait){std::this_thread::sleep_for(std::chrono::milliseconds(2300));m.waitReturned=true;return -92;}
        if(m.waitFailure)return -92;
        // Deliberately NOT the AIR algorithm or CPU expected result. Modeled
        // transport writes a distinctive observed value only after wait success.
        NSMutableData *output=m.views[@2];((uint32_t *)output.mutableBytes)[0]=0xc0decafe;
        *n=0;return 0;}
    default:assert(false);return -99;
    }
}
static int mapView(void *ctx,uint64_t token,void **address,size_t *bytes,id __autoreleasing *owner) {
    auto &m=*(Model *)ctx;
    if(m.slowPreparation&&[m.sizes[@(token)] unsignedLongLongValue]==8192)
        std::this_thread::sleep_for(std::chrono::milliseconds(2300));
    NSMutableData *data=[NSMutableData dataWithLength:[m.sizes[@(token)] unsignedLongLongValue]];
    m.views[@(token)]=data;*owner=data;*address=data.mutableBytes;*bytes=data.length-(m.shortMap?4:0);return 0;
}
static int flush(void *ctx,void *,size_t,bool) {auto &m=*(Model *)ctx;++m.flushes;return m.flushFailure?-93:0;}
static void retire(void *ctx,bool failed,NSArray *owners) {
    auto &m=*(Model *)ctx;m.closedFailed=failed;m.retiredOwners=owners.count;++m.closed;
}
static Native540Device *device(Model &m) {
    Native540ObjectsTransport t;t.call=call;t.context=&m;t.connectionOwner=m.views;t.verifiedCPUInhibit=true;t.mapView=mapView;t.flush=flush;t.retireConnection=retire;
    NSError *error=nil;auto d=[[Native540Device alloc] initControlledWithTransport:t error:&error];assert(d&&!error);return d;
}
static Native540Library *library(Native540Device *d,NSData *bytes) {
    auto data=dispatch_data_create(bytes.bytes,bytes.length,nullptr,DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    NSError *error=nil;auto l=[d newLibraryWithData:data error:&error];assert(l&&!error);
    assert(l.snapshotHashes.count==4&&l.functionNames.count==1);
    assert(![l newFunctionWithName:@"Transform540"]);return l;
}
static Native540CommandBuffer *encode(Native540Device *d,Native540Library *lib,Native540Buffer *out,bool badOffset=false) {
    auto input=[d newBufferWithLength:3120 options:MTLResourceStorageModeShared];
    auto params=[d newBufferWithLength:48 options:MTLResourceStorageModeShared];
    const uint32_t values[]={0,260,3,1,256,1,1,2,2,1,1,0};if(params)memcpy(params.contents,values,sizeof(values));
    NSError *error=nil;auto f=[lib newFunctionWithName:@(trustedEntry)];
    assert(f.functionType==MTLFunctionTypeKernel&&f.functionConstantsDictionary.count==0);
    auto p=[d newComputePipelineStateWithFunction:f error:&error];assert(p&&!error);
    auto q=[d newCommandQueue];auto b=[q commandBuffer];auto e=[b computeCommandEncoder];
    [e setComputePipelineState:p];[e setBuffer:input offset:0 atIndex:0];[e setBuffer:out offset:badOffset?1:0 atIndex:1];
    [e setBuffer:params offset:0 atIndex:2];
    [e dispatchThreadgroups:MTLSizeMake(4,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];[e endEncoding];return b;
}
int main(int argc,char **argv) {
    @autoreleasepool {
        assert(argc==2);NSData *real=[NSData dataWithContentsOfFile:@(argv[1])];assert(real.length==653424);
        // No compile reruns: use only existing real trusted AIR producer export.
        Model m;
        @autoreleasepool {
            auto d=device(m);
            assert(![d newBufferWithLength:0 options:0]);assert(![d newBufferWithLength:4 options:MTLResourceStorageModePrivate]);
            auto input=[d newBufferWithLength:17 options:0];assert(input.length==17&&input.contents&&input.storageMode==MTLStorageModeShared);
            assert([d respondsToSelector:@selector(newTextureWithDescriptor:)]);
            auto badTexture=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:64 height:64 mipmapped:NO];
            assert(![d newTextureWithDescriptor:badTexture]);
            NSError *error=nil;const char *bad="{\"producer\":\"qgpu_compile_glsl\"}";
            auto data=dispatch_data_create(bad,strlen(bad),nullptr,DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            assert(![d newLibraryWithData:data error:&error]&&error);
            NSMutableData *corrupt=[real mutableCopy];((uint8_t *)corrupt.mutableBytes)[99]^=1;
            data=dispatch_data_create(corrupt.bytes,corrupt.length,nullptr,DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            assert(![d newLibraryWithData:data error:&error]&&error);
            auto lib=library(d,real);auto output=[d newBufferWithLength:1024 options:0];
            auto b=encode(d,lib,output);assert(b.status==MTLCommandBufferStatusNotEnqueued);
            // Read the distinct modeled transport value. This is an observed
            // HOST-model write, not proof of executing the AIR algorithm/GPU.
            [b commit];[b waitUntilCompleted];assert(b.status==MTLCommandBufferStatusCompleted&&!b.error);
            assert(((uint32_t *)output.contents)[0]==0xc0decafe);assert(m.submits==1&&m.waits==1&&m.flushes>=10);
            bool caught=false;@try {[b commit];}@catch(NSException *){caught=true;}assert(caught&&m.submits==1);
        }
        assert(m.closed==1&&!m.closedFailed&&m.retiredOwners>=7);
        for(int mode=0;mode<5;++mode) {
            Model f;f.waitFailure=mode==0;f.submitFailure=mode==1;f.flushFailure=mode==2;f.shortMap=mode==3;
            @autoreleasepool {
                auto d=device(f);auto lib=library(d,real);
                if(mode==3){assert(![d newBufferWithLength:1024 options:0]);}
                else {
                    auto in=[d newBufferWithLength:1024 options:0];(void)in;
                    auto out=[d newBufferWithLength:1024 options:0];auto b=encode(d,lib,out,mode==4);
                    [b commit];[b waitUntilCompleted];assert(b.status==MTLCommandBufferStatusError&&b.error);
                    if(mode<3)assert(out.contents==nullptr);else assert(((uint32_t *)out.contents)[0]==0);assert(f.submits==(mode<2?1:0));
                    assert(f.waits==(mode==0?1:0));
                    auto again=encode(d,lib,out);[again commit];[again waitUntilCompleted];
                    if(mode<3)assert(again.status==MTLCommandBufferStatusError);
                }
            }
            assert(f.closed==1);if(mode<4)assert(f.closedFailed);
        }
        // Additional public contract checks on the real object implementation.
        Model state;
        @autoreleasepool {
            auto d=device(state);auto q=[d newCommandQueue];auto b=[q commandBuffer];
            bool caught=false;@try {[b waitUntilCompleted];}@catch(NSException *){caught=true;}assert(caught);
            auto e=[b computeCommandEncoder];assert(e);assert(![b computeCommandEncoder]);
            [e endEncoding];[b commit];[b waitUntilCompleted];assert(b.status==MTLCommandBufferStatusError&&state.submits==0);
            auto empty=[q commandBuffer];[empty commit];[empty waitUntilCompleted];assert(empty.status==MTLCommandBufferStatusError);
            auto lib=library(d,real);Model foreign;
            @autoreleasepool {
                auto other=device(foreign);auto f=[lib newFunctionWithName:@(trustedEntry)];NSError *error=nil;
                assert(![other newComputePipelineStateWithFunction:f error:&error]&&error);
            }
            assert(foreign.closed==1);
        }
        assert(state.closed==1);
        Model slow;slow.slowPreparation=true;
        @autoreleasepool {
            auto d=device(slow);auto lib=library(d,real);
            auto reserve=[d newBufferWithLength:17 options:0];(void)reserve;
            auto out=[d newBufferWithLength:1024 options:0];auto b=encode(d,lib,out);
            auto start=std::chrono::steady_clock::now();[b commit];
            assert(std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(2200));
            [b waitUntilCompleted];assert(b.status==MTLCommandBufferStatusCompleted&&!b.error);
            assert(slow.submits==1&&slow.waits==1&&slow.next==7);
        }
        assert(slow.closed==1&&!slow.closedFailed);
        Model timeout;timeout.slowWait=true;
        @autoreleasepool {
            auto d=device(timeout);auto lib=library(d,real);
            auto reserve=[d newBufferWithLength:17 options:0];(void)reserve;
            auto out=[d newBufferWithLength:1024 options:0];auto b=encode(d,lib,out);
            [b commit];[b waitUntilCompleted];
            assert(b.status==MTLCommandBufferStatusError&&b.error.code==MTLCommandBufferErrorTimeout);
            assert(timeout.submits==1&&timeout.waits==1&&timeout.closed==0);
            assert(out.contents==nullptr);
            while(!timeout.waitReturned.load())std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        assert(timeout.closed==1&&timeout.closedFailed&&timeout.retiredOwners==7);
        puts("PASS: real ObjC objects, trusted metallib, async fence, failure/lifetime contracts, slow preparation before deadline, GPU timeout retains owners (HOST ONLY)");
    }
}
