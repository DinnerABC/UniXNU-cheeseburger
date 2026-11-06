// SPDX-License-Identifier: GPL-2.0-only
#include "NativeMetal540GraphicsOwnedRun.h"
#include "NativeMetal540DeviceAdapter.h"
#include "NativeMetal540ProtocolAudit.h"
#import "NativeMetal540Objects.h"
#include "NativeMetal540Render.h"
#import <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <atomic>
#include <stdio.h>
@interface GraphicsContext : NSObject { @public io_connect_t connection;std::atomic<bool> retired; }
@end
@implementation GraphicsContext
- (instancetype)init {if((self=[super init])){connection=0;retired=false;}return self;}
@end
static void record(GraphicsContext *c,const char *text){if(!c->connection)return;
    NativeAdrenoAbiMetadata m={};m.handle=1;m.bytes=(uint32_t)strnlen(text,63);memcpy(m.text,text,m.bytes);
    IOConnectCallStructMethod(c->connection,NativeAdrenoMethodSetMetadata,&m,sizeof m,nullptr,nullptr);}
int NativeMetal540RunOwnedGraphics(unsigned service){@autoreleasepool {
    GraphicsContext *c=[GraphicsContext new];int rc=int(IOServiceOpen(service,mach_task_self(),0x4147,&c->connection));if(rc)return rc;
    Native540ObjectsTransport t{};t.context=(__bridge void*)c;t.connectionOwner=c;t.verifiedCPUInhibit=true;
    t.call=[](void *p,uint32_t s,const void *in,size_t n,void *out,size_t *bytes)->int {
        auto *c=(__bridge GraphicsContext*)p;return int(IOConnectCallStructMethod(c->connection,s,in,n,out,bytes));};
    t.mapView=[](void *p,uint64_t token,void **address,size_t *bytes,id __autoreleasing *owner)->int {
        auto *c=(__bridge GraphicsContext*)p;mach_vm_address_t a=0;mach_vm_size_t n=0;
        int rc=int(IOConnectMapMemory64(c->connection,(uint32_t)token,mach_task_self(),&a,&n,kIOMapAnywhere));
        *address=(void*)(uintptr_t)a;*bytes=n;*owner=rc?nil:[NSObject new];return rc;};
    t.unmapView=[](void *p,uint32_t token,uint64_t address,uint64_t bytes)->int {
        auto *c=(__bridge GraphicsContext*)p;if(!token||!address||!bytes)return -5470;
        return int(IOConnectUnmapMemory64(c->connection,token,mach_task_self(),address));
    };
    static std::atomic<uint64_t> sessions{0};t.session=++sessions;
    t.flush=[](void*,void*,size_t,bool)->int{__asm__ volatile("dsb sy":::"memory");return 0;};
    t.retireConnection=[](void *p,bool,NSArray*){auto *c=(__bridge GraphicsContext*)p;
        if(!c->retired.exchange(true)&&c->connection){IOServiceClose(c->connection);c->connection=0;}};
    int result=-5490;
    @autoreleasepool {
        NSError *error=nil;auto backend=[[Native540Device alloc] initControlledWithTransport:t error:&error];
        record(c,"DI begin-real-device-init");
        id initialized=NativeMetal540CreateInitializedDevice(backend,service,&error);
        if(!initialized){char text[64]={};snprintf(text,sizeof text,"DI failed %s",error.localizedDescription.UTF8String);record(c,text);return -5501;}
        Native540Device *d=(Native540Device*)initialized;
        record(c,"DI real-MTLDevice features-ready no-system-register");
        uint64_t registry=0;IORegistryEntryGetRegistryEntryID(service,&registry);
        uint64_t actualRegistry=((uint64_t(*)(id,SEL))[initialized methodForSelector:@selector(registryID)])(initialized,@selector(registryID));
        char registryText[64]={};snprintf(registryText,sizeof registryText,"DR registry%llx expected%llx match%u",(unsigned long long)actualRegistry,(unsigned long long)registry,(unsigned)(registry&&actualRegistry==registry));record(c,registryText);
        if(!registry||actualRegistry!=registry)return -5502;
        auto protocolAudit=NativeMetal540AuditDeviceProtocols(initialized);
        char auditText[64]={};snprintf(auditText,sizeof auditText,"DP n%u missing%u type%u %.24s",protocolAudit.methods,
            protocolAudit.missing,protocolAudit.mismatched,protocolAudit.first);record(c,auditText);
        NSData *bytes=[NSData dataWithContentsOfFile:@"/System/Library/Frameworks/SpriteKit.framework/default.metallib"];
        if(!d||!bytes){record(c,"RG missing-device-library");return -5491;}
        auto data=dispatch_data_create(bytes.bytes,bytes.length,nullptr,DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        auto lib=[d newLibraryWithData:data error:&error];auto vs=[lib newFunctionWithName:@"SolidColor_VertexFunc"];
        auto fs=[lib newFunctionWithName:@"SolidColor_FragFunc"];
        auto pd=[MTLRenderPipelineDescriptor new];pd.vertexFunction=(id<MTLFunction>)(id)vs;pd.fragmentFunction=(id<MTLFunction>)(id)fs;
        pd.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
        auto p=[d newRenderPipelineStateWithDescriptor:pd error:&error];
        if(!p){record(c,"RG library-pipeline-error");return -5492;}
        auto desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:64 mipmapped:NO];
        desc.storageMode=MTLStorageModeShared;desc.usage=MTLTextureUsageRenderTarget;desc.hazardTrackingMode=MTLHazardTrackingModeTracked;
        const uint32_t v[]={0xbf400000,0xbf400000,0,0x3f800000,0x3f400000,0xbf400000,0,0x3f800000,0,0x3f400000,0,0x3f800000};
        const uint32_t rgba[]={0x3f800000,0,0,0x3f800000};
        NSDictionary *surfaceProperties=@{(__bridge id)kIOSurfaceWidth:@64,(__bridge id)kIOSurfaceHeight:@64,
            (__bridge id)kIOSurfaceBytesPerRow:@256,(__bridge id)kIOSurfaceBytesPerElement:@4,
            (__bridge id)kIOSurfacePixelFormat:@0x52474241,(__bridge id)kIOSurfaceAllocSize:@16384};
        id surfaceOwner=CFBridgingRelease(IOSurfaceCreate((__bridge CFDictionaryRef)surfaceProperties));
        auto target=[d newTextureWithDescriptor:desc iosurface:(__bridge IOSurfaceRef)surfaceOwner plane:0];auto vertex=[d newBufferWithBytes:v length:sizeof v options:MTLResourceHazardTrackingModeTracked];auto color=[d newBufferWithBytes:rgba length:sizeof rgba options:MTLResourceHazardTrackingModeTracked];
        if(!target||!vertex||!color){record(c,"RG allocation-error");return -5493;}
        auto pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=(id<MTLTexture>)(id)target;
        pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
        auto q=[d newCommandQueue];auto b=[q commandBuffer];auto e=[b renderCommandEncoderWithDescriptor:pass];
        [e setRenderPipelineState:p];[e setVertexBuffer:vertex offset:0 atIndex:0];[e setFragmentBuffer:color offset:0 atIndex:0];
        [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];
        __block unsigned completed=0;
        [b addCompletedHandler:^(Native540CommandBuffer *){++completed;}];
        [b commit];[b waitUntilCompleted];
        bool identity=lib.device==initialized&&vs.device==initialized&&fs.device==initialized&&p.device==initialized&&
            target.device==initialized&&vertex.device==initialized&&q.device==initialized&&b.device==initialized&&
            !target.heap&&!vertex.heap&&!target.isAliasable&&!vertex.isAliasable&&
            target.isShareable&&!target.isSparse&&!target.isFramebufferOnly&&!target.allowGPUOptimizedContents&&
            target.resourceOptions==MTLResourceHazardTrackingModeTracked&&vertex.resourceOptions==MTLResourceHazardTrackingModeTracked&&
            [target setPurgeableState:MTLPurgeableStateKeepCurrent]==MTLPurgeableStateNonVolatile&&
            [vertex setPurgeableState:MTLPurgeableStateKeepCurrent]==MTLPurgeableStateNonVolatile;
        char contract[64]={};snprintf(contract,sizeof contract,"DC identity%u callback%u allocated%lu",(unsigned)identity,completed,(unsigned long)[d currentAllocatedSize]);record(c,contract);
        uint32_t pixels[4096]={};Native540Render::Pixels observed{};
        bool read=identity&&completed==1&&b.status==MTLCommandBufferStatusCompleted&&!b.error;
        if(read){@try{[target getBytes:pixels bytesPerRow:256 fromRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0];}
            @catch(NSException*){read=false;}}
        bool valid=read&&Native540Render::verify(pixels,sizeof pixels,observed);
        char text[64]={};snprintf(text,sizeof text,"RG status%lu read%u px%x/%x bad%u",(unsigned long)b.status,(unsigned)read,observed.center,observed.corner,observed.mismatches);record(c,text);
        if(b.error){snprintf(text,sizeof text,"RE %s",b.error.localizedDescription.UTF8String);record(c,text);}
        result=valid?0:-5494;
        unsigned verifiedFrames=valid?1:0;bool reused=true;
        uint32_t frameSamples[4]={observed.center,0,0,0};
        NSUInteger fixedAllocation=[d currentAllocatedSize];
        const uint32_t colors[][4]={{0,0x3f800000,0,0x3f800000},{0,0,0x3f800000,0x3f800000},{0x3f800000,0,0,0x3f800000}};
        const uint32_t expected[]={0xff00ff00,0xffff0000,0xff0000ff};
        if(valid)for(unsigned frame=0;frame<3;++frame){
            memcpy(color.contents,colors[frame],16);auto next=[q commandBuffer];auto encoder=[next renderCommandEncoderWithDescriptor:pass];
            [encoder setRenderPipelineState:p];[encoder setVertexBuffer:vertex offset:0 atIndex:0];[encoder setFragmentBuffer:color offset:0 atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[encoder endEncoding];
            [next commit];[next waitUntilCompleted];bool frameRead=next.status==MTLCommandBufferStatusCompleted&&next.executionRetired&&!next.error;
            if(frameRead){@try{[target getBytes:pixels bytesPerRow:256 fromRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0];}
                @catch(NSException*){frameRead=false;}}
            Native540Render::Pixels measured{};bool frameValid=frameRead&&Native540Render::verifyColor(pixels,sizeof pixels,expected[frame],measured);
            reused=reused&&[d currentAllocatedSize]==fixedAllocation;
            frameSamples[frame+1]=measured.center;
            if(!frameValid||!reused){valid=false;result=-5497;break;}++verifiedFrames;
        }
        snprintf(text,sizeof text,"RX %x/%x/%x/%x",frameSamples[0],frameSamples[1],frameSamples[2],frameSamples[3]);record(c,text);
        snprintf(text,sizeof text,"RC frames%u reuse%u allocated%lu",verifiedFrames,(unsigned)reused,(unsigned long)[d currentAllocatedSize]);record(c,text);
        if(valid){
            int retired=[d retireResources];auto receipt=[d retirementResult];unsigned freed=0,views=0;
            for(unsigned i=0;i<receipt.count;++i){freed+=receipt.bos[i].freeAck;views+=receipt.bos[i].viewUnmapAttempted&&receipt.bos[i].viewUnmapped;}
            snprintf(text,sizeof text,"OR rc%x free%u view%u proof%u left%lu",(unsigned)retired,freed,views,
                (unsigned)receipt.proof,(unsigned long)[d currentAllocatedSize]);record(c,text);
            if(retired||!receipt.proof)result=-5496;
        }
    }
    if(!c->retired.load())return -5495;return result;
}}
