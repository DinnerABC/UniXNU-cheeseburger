// SPDX-License-Identifier: MIT
#import "NativeMetal540DesktopObjects.h"
#import <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <atomic>

@interface CBMetalConnection : NSObject { @public io_connect_t port;io_service_t service; }
@end
@implementation CBMetalConnection
- (instancetype)init {if((self=[super init])){port=0;service=0;}return self;}
- (void)dealloc {if(port)IOServiceClose(port);if(service)IOObjectRelease(service);}
@end

CBMetalDevice *CBMetalCreateNativeDevice(uint32_t service,NSError **error) {
    if(error)*error=nil;
    if(!service||!IOObjectConformsTo(service,"IOAcceleratorES")){if(error)*error=[NSError errorWithDomain:@"CBMetalTransport" code:kIOReturnBadArgument userInfo:nil];return nil;}
    auto owner=[CBMetalConnection new];kern_return_t status=IOObjectRetain(service);
    if(!status){owner->service=service;status=IOServiceOpen(service,mach_task_self(),0x4147,&owner->port);}
    if(status){if(error)*error=[NSError errorWithDomain:@"CBMetalTransport" code:status userInfo:nil];return nil;}
    NativeMetal540DesktopSession::Transport t{};t.context=(__bridge void*)owner;
    t.call=[](void *p,uint32_t selector,const void *in,size_t n,void *out,size_t *bytes)->int {
        auto c=(__bridge CBMetalConnection*)p;return int(IOConnectCallStructMethod(c->port,selector,in,n,out,bytes));};
    t.mapView=[](void *p,uint32_t token,void **address,size_t *bytes)->int {
        auto c=(__bridge CBMetalConnection*)p;mach_vm_address_t va=0;mach_vm_size_t n=0;
        int rc=int(IOConnectMapMemory64(c->port,token,mach_task_self(),&va,&n,kIOMapAnywhere));*address=(void*)(uintptr_t)va;*bytes=n;return rc;};
    t.unmapView=[](void *p,uint32_t token,void *address,size_t)->int {
        auto c=(__bridge CBMetalConnection*)p;return int(IOConnectUnmapMemory64(c->port,token,mach_task_self(),uintptr_t(address)));};
    // Kernel clientMemoryForType supplies inhibit-cache BO mappings; imported
    // IOSurface maintenance is performed by the existing kernel dispatcher.
    t.flush=[](void *,void *,size_t,bool)->int {
#if defined(__aarch64__)
        __asm__ volatile("dsb sy":::"memory");
#else
        std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
        return 0;
    };
    return [[CBMetalDevice alloc] initWithTransport:t connectionOwner:owner compiler:qgpu_compile_air_nir error:error];
}
