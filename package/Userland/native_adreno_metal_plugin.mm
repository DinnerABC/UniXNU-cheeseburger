// SPDX-License-Identifier: GPL-2.0-only
// Owner Metal plugin, reached by the original discovery inside backboardd:
// IOAcceleratorES -> MetalPluginName -> NSBundle -> principal class ->
// initWithAcceleratorPort:. Stage 1 runs the user-space submission test through
// that accelerator port and returns nil, so no device is registered and the
// original software renderer keeps running. The test itself is the same native
// Adreno ABI sequence as the probe daemon (query, two BOs, map, submit, fence,
// CPU readback), only the process hosting it is different.
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <dispatch/dispatch.h>
#include <atomic>
#include "NativeAdrenoABI.h"
#include "NativeMetal540Probe.h"
#include "NativeMetal540RuntimeABI.h"
#include "NativeMetal540Transform.h"
#include "NativeMetal540AIRProbe.h"
#include "NativeMetal540OwnedRun.h"
#include "NativeMetal540GraphicsOwnedRun.h"
#include "NativeMetal540Render.h"
#include "NativeMetal540RenderAIRProbe.h"
#include "NativeMetal540Surface.h"
#include "NativeMetal540DesktopObjects.h"
#include "NativeMetal540DeviceAdapter.h"

namespace {
constexpr uint32_t kOpenType = 0x4147;
// The discovery is per-process. Only this host runs the hardware test; every
// other host (backboardd included) returns nil at once and keeps the original
// software renderer, instead of competing for the single ABI client.
constexpr const char *kTargetHost = "mediaserverd";

void line(const char *fmt, ...) {
    char text[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (fd >= 0) {
        write(fd, text, strlen(text));
        write(fd, "\n", 1);
        close(fd);
    }
}

// Independent channel: /private/var/tmp is writable for the discovery host,
// and the recovered Data image keeps this file even when the console is not
// captured. One line per stage, appended.
void note(const char *fmt, ...) {
    char text[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    int fd = open("/private/var/tmp/p21-plugin.txt", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd >= 0) {
        write(fd, text, strlen(text));
        write(fd, "\n", 1);
        close(fd);
    }
}

// Progress markers are refused opens of type 0x4147<value>: they need no
// connection, and the driver counts them even when the sandbox denies the real
// open. A zero count means the open path never reached the driver.
void marker(unsigned port, unsigned value) {
    io_connect_t connection = 0;
    if (IOServiceOpen((io_service_t)port, mach_task_self(), 0x41470000u | (value & 0xffff),
            &connection) == KERN_SUCCESS && connection) {
        IOServiceClose(connection);
    }
}

uint32_t parity(uint32_t v) { v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return (~v) & 1; }
uint32_t packet7(uint32_t op, uint32_t n) {
    return 0x70000000u | n | (parity(n) << 15) | (op << 16) | (parity(op) << 23);
}

struct Ctx {
    io_connect_t conn = 0;
    int step = 0;
    int error = 0;
    uint64_t ticks = 0;
};

void report(Ctx &c, const char *why, int rc) {
    if (!c.conn) return;
    NativeAdrenoAbiMetadata meta = {};
    meta.handle = 1;
    char text[64] = {};
    snprintf(text, sizeof text, "P21 plugin %s s%d e%x t%llx", why, c.step, c.error,
        (unsigned long long)c.ticks);
    meta.bytes = (uint32_t)strlen(text);
    memcpy(meta.text, text, meta.bytes);
    IOConnectCallMethod(c.conn, NativeAdrenoMethodSetMetadata, nullptr, 0, &meta, sizeof meta,
        nullptr, nullptr, nullptr, nullptr);
}

template <class In, class Out>
kern_return_t call(Ctx &c, uint32_t method, const In *in, Out *out) {
    size_t outSize = out ? sizeof(Out) : 0;
    return IOConnectCallMethod(c.conn, method, nullptr, 0, in, sizeof(In), nullptr, nullptr, out,
        out ? &outSize : nullptr);
}
template <class Out>
kern_return_t callOut(Ctx &c, uint32_t method, Out *out) {
    size_t outSize = out ? sizeof(Out) : 0;
    return IOConnectCallMethod(c.conn, method, nullptr, 0, nullptr, 0, nullptr, nullptr, out,
        out ? &outSize : nullptr);
}

bool checked(Ctx &c, kern_return_t kr, const char *why) {
    if (kr == KERN_SUCCESS) {
        ++c.step;
        return true;
    }
    if (!c.error) c.error = (int)kr;
    report(c, why, (int)kr);
    return false;
}
}  // namespace

// The original discovery accepts a plugin class only when it is a subclass of
// the private _MTLDevice (isSubclassOfClass:), so declare that ancestry here and
// let the Metal framework provide the class at load time.
@interface _MTLDevice : NSObject
@end

@interface NativeAdrenoMetalDevice : _MTLDevice
+ (id)runProbe:(unsigned)port;
@end

@implementation NativeAdrenoMetalDevice
#ifdef NATIVE540_DESKTOP_DEVICE
// Desktop mode: backboardd hosts the real system device. The discovery owns
// the port only until this initializer returns, so the device keeps its own
// retain through the concrete transport. Registration happens only when the
// qualified21H461 audit and protocol adoption both pass.
- (id)createDesktopDevice:(unsigned)port {
    static std::atomic<bool> attempted{false};
    if (attempted.exchange(true)) return nil;
    marker(port, 0x100);
    NSError *error = nil;
    CBMetalDevice *backend = CBMetalCreateNativeDevice(port, &error);
    if (!backend) {
        line("P21PLUGIN stage=desktop-backend e%x", error ? (unsigned)error.code : 0u);
        note("desktop backend failed: %@",
            error.localizedDescription ?: @"unknown");
        return nil;
    }
    marker(port, 0x101);
    id device = NativeMetal540CreateInitializedDesktopDevice(backend, port, &error);
    if (!device) {
        line("P21PLUGIN stage=desktop-adapter e%x", error ? (unsigned)error.code : 0u);
        note("desktop adapter failed: %@",
            error.localizedDescription ?: @"unknown");
        return nil;
    }
    marker(port, 0x102);
    line("P21PLUGIN stage=desktop-device pid=%d", (int)getpid());
    note("desktop device created; framework registration pending");
    return device;
}
#endif
- (id)initWithAcceleratorPort:(unsigned)port {
    // Match the official plugin: a nil return releases self under ARC, so its
    // Metal superclass must have been initialized before any exit path.
    self = [super init];
    if (!self || !port) return nil;
    const char *host = getprogname();
#ifdef NATIVE540_DESKTOP_DEVICE
    // Desktop mode runs only in backboardd; other hosts keep software rendering.
    if (host && !strcmp(host, "backboardd")) return [self createDesktopDevice:port];
#endif
    if (!host || strcmp(host, kTargetHost)) return nil;
    static std::atomic<bool> attempted{false};
    if (attempted.exchange(true)) return nil;
    // Metal discovery owns the port only until this initializer returns. Keep
    // a reference for the worker, and do not block the discovery/UI thread on
    // a potentially stalled kernel call (as in the official sheng plugin).
    if (IOObjectRetain((io_object_t)port) != KERN_SUCCESS) return nil;
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        [NativeAdrenoMetalDevice runProbe:port];
        IOObjectRelease((io_object_t)port);
    });
    return nil;
}

+ (id)runProbe:(unsigned)port {
    line("P21PLUGIN stage=enter port=%u pid=%d", port, (int)getpid());
    note("enter pid=%d port=%u", (int)getpid(), port);
    marker(port, 1);
    Ctx c;
    // Bounded readiness wait only: the service may come up just after the
    // accelerator. Five seconds, then this host keeps the software renderer
    // rather than blocking device discovery.
    for (unsigned i = 0; i < 20 && !c.conn; ++i) {
        kern_return_t kr = IOServiceOpen((io_service_t)port, mach_task_self(), kOpenType, &c.conn);
        if (c.conn) {
            line("P21PLUGIN stage=open rc=%d conn=%u try=%u", (int)kr, c.conn, i);
            note("open rc=%d try=%u", (int)kr, i);
            marker(port, 2);
            break;
        }
        c.error = (int)kr;
        // Another instance owns the single ABI client: do not spin.
        if (kr == kIOReturnBusy) { note("busy"); marker(port, 3); return nil; }
        marker(port, 3);
        usleep(250000);
    }
    if (!c.conn) {
        line("P21PLUGIN stage=noopen e%x", (unsigned)c.error);
        note("noopen e%x", (unsigned)c.error);
        return nil;
    }
    // All mapped resources survive every failure through connection close.
    // Retirement/quarantine is owned by the existing engine, not this worker.
    NativeAdrenoAbiBoAllocReply ib = {}, tgt = {};
    uint32_t ibTok = 0, tgtTok = 0;
    mach_vm_address_t ibAddr = 0, tgtAddr = 0;
    struct Guard {
        Ctx &c;
        ~Guard() { if (c.conn) IOServiceClose(c.conn); }
    } guard{c};
    NativeAdrenoAbiParams params = {};
    if (!checked(c, callOut(c, NativeAdrenoMethodQuery, &params), "query")) return nil;
    marker(port, 4);
    line("P21PLUGIN stage=query abi=%u in=%u out=%u max=%llu", params.abi_version, params.input_bits,
        params.output_bits, (unsigned long long)params.max_bo_bytes);
    NativeAdrenoAbiDeviceInfo info = {};
    if (!checked(c, callOut(c, NativeAdrenoMethodGetDeviceInfo, &info), "devinfo")) return nil;
    line("P21PLUGIN stage=devinfo chip=%x va=%llx size=%llu", info.chip_id,
        (unsigned long long)info.va_start, (unsigned long long)info.va_size);

    NativeAdrenoAbiBoAlloc alloc = {4096, 12, 1}, alloc2 = {4096, 12, 1};
    if (!checked(c, call(c, NativeAdrenoMethodBoAlloc, &alloc, &ib), "alloc-ib")) return nil;
    if (!checked(c, call(c, NativeAdrenoMethodBoAlloc, &alloc2, &tgt), "alloc-tgt")) return nil;
    const uint64_t ibIova = info.va_start, tgtIova = info.va_start + 0x1000;
    NativeAdrenoAbiBoMap mapIb = {ib.handle, ibIova, 1, 0, 2, 0}, mapTgt = {tgt.handle, tgtIova, 3, 0, 2, 0};
    NativeAdrenoAbiBoMapReply mr = {};
    if (!checked(c, call(c, NativeAdrenoMethodBoMap, &mapIb, &mr), "map-ib")) return nil;
    if (!checked(c, call(c, NativeAdrenoMethodBoMap, &mapTgt, &mr), "map-tgt")) return nil;
    NativeAdrenoAbiBoCpuMap cmIb = {ib.handle}, cmTgt = {tgt.handle};
    NativeAdrenoAbiBoCpuMapReply crIb = {}, crTgt = {};
    if (!checked(c, call(c, NativeAdrenoMethodBoCpuMap, &cmIb, &crIb), "cpumap-ib")) return nil;
    if (!checked(c, call(c, NativeAdrenoMethodBoCpuMap, &cmTgt, &crTgt), "cpumap-tgt")) return nil;
    ibTok = crIb.token; tgtTok = crTgt.token;
    mach_vm_size_t ibLen = 0, tgtLen = 0;
    if (!checked(c, IOConnectMapMemory64(c.conn, ibTok, mach_task_self(), &ibAddr, &ibLen, 1), "mapmem-ib")) return nil;
    if (!checked(c, IOConnectMapMemory64(c.conn, tgtTok, mach_task_self(), &tgtAddr, &tgtLen, 1), "mapmem-tgt")) return nil;
    if (ibLen < 4096 || tgtLen < 4096) {
        c.error = 0xbad1;
        report(c, "maplen", 0);
        return nil;
    }
    volatile uint32_t *iw = reinterpret_cast<volatile uint32_t *>(ibAddr);
    volatile uint32_t *tw = reinterpret_cast<volatile uint32_t *>(tgtAddr);
    for (unsigned i = 0; i < 1024; ++i) tw[i] = 0xdeadbeef;
    const uint32_t values[3] = {0x0adc0de1u, 0xfeed0002u, 0x600d0003u};
    uint32_t words[12];
    for (unsigned i = 0; i < 3; ++i) {
        words[i * 4] = packet7(0x3d, 3);  // CP_MEM_WRITE
        words[i * 4 + 1] = uint32_t(tgtIova + i * 4);
        words[i * 4 + 2] = uint32_t(tgtIova >> 32);
        words[i * 4 + 3] = values[i];
    }
    for (unsigned i = 0; i < 12; ++i) iw[i] = words[i];
    __asm__ volatile("dsb sy" ::: "memory");
    NativeAdrenoAbiSubmit submit = {};
    submit.buffer_count = 1;
    submit.buffers[0] = {ib.handle, ibIova, sizeof words};
    NativeAdrenoAbiSubmitReply sr = {};
    if (!checked(c, call(c, NativeAdrenoMethodSubmit, &submit, &sr), "submit")) return nil;
    line("P21PLUGIN stage=submit fence=%llu", (unsigned long long)sr.fence);
    marker(port, 5);
    NativeAdrenoAbiWaitFence wait = {sr.fence, 200000000ull};
    if (!checked(c, call(c, NativeAdrenoMethodWaitFence, &wait, (NativeAdrenoAbiTimestamp *)nullptr), "wait")) return nil;
    marker(port, 6);
    __asm__ volatile("dsb sy" ::: "memory");
    const int verified = (tw[0] == values[0]) + (tw[1] == values[1]) + (tw[2] == values[2]) +
        (tw[3] == 0xdeadbeef);
    line("P21PLUGIN stage=values %08x/%08x/%08x/%08x verified=%d", (unsigned)tw[0], (unsigned)tw[1],
        (unsigned)tw[2], (unsigned)tw[3], verified);
    NativeAdrenoAbiTimestamp ts = {};
    callOut(c, NativeAdrenoMethodGetTimestamp, &ts);
    c.ticks = ts.ticks;
    if (verified != 4) c.error = 0xbadd;
    {
        NativeAdrenoAbiMetadata meta = {};
        char text[64] = {};
        snprintf(text, sizeof text, "P21 plugin done s%d e%x v%d t%llx", c.step, c.error, verified,
            (unsigned long long)c.ticks);
        meta.bytes = (uint32_t)strlen(text);
        memcpy(meta.text, text, meta.bytes);
        IOConnectCallMethod(c.conn, NativeAdrenoMethodSetMetadata, nullptr, 0, &meta, sizeof meta,
            nullptr, nullptr, nullptr, nullptr);
    }
    line("P21PLUGIN stage=idle verified=%d", verified);
    note("done verified=%d", verified);
    marker(port, 7);
    if (verified != 4) return nil; // Preserve P1 as the prerequisite regression.
    char abiRows[16][64]={};unsigned abiCount=NativeMetal540CaptureABI(abiRows,16);
    for(unsigned i=0;i<abiCount;++i){
        note("%s",abiRows[i]);
        NativeAdrenoAbiMetadata meta={};meta.handle=i+1;meta.bytes=(uint32_t)strlen(abiRows[i]);
        memcpy(meta.text,abiRows[i],meta.bytes);
        IOConnectCallStructMethod(c.conn,NativeAdrenoMethodSetMetadata,&meta,sizeof meta,nullptr,nullptr);
    }
    struct ProbeContext { Ctx *client; unsigned port; } pc{&c, port};
    NativeMetal540Probe::Transport transport;
    transport.context = &pc;
    transport.call = [](void *opaque, uint32_t method, const void *in, size_t bytes,
                        void *out, size_t *size) -> int {
        auto &p = *static_cast<ProbeContext *>(opaque);
        return (int)IOConnectCallStructMethod(p.client->conn, method, in, bytes, out, size);
    };
    transport.mapView = [](void *opaque, uint32_t token, uint64_t &address, uint64_t &bytes) -> int {
        auto &p = *static_cast<ProbeContext *>(opaque);
        mach_vm_address_t va = 0; mach_vm_size_t length = 0;
        kern_return_t kr = IOConnectMapMemory64(p.client->conn, token, mach_task_self(),
            &va, &length, kIOMapAnywhere);
        address = va; bytes = length;
        return (int)kr;
    };
    transport.order = [](void *, bool) { __asm__ volatile("dsb sy" ::: "memory"); };
    transport.record = [](void *opaque, const NativeMetal540Probe::Record &r) {
        auto &p = *static_cast<ProbeContext *>(opaque);
        marker(p.port, 0x5400u + (unsigned)r.stage);
        line("P540 GLSL430/A5xx s%u r%u e%x f%llu v%u g%u", (unsigned)r.stage,
            r.resource, (unsigned)r.error, (unsigned long long)r.fence, r.verified, r.guardVerified);
        note("P540 GLSL430/A5xx NOT-Metalcompiler s%u r%u e%x h%llx va%llx f%llu v%u g%u",
            (unsigned)r.stage, r.resource, (unsigned)r.error, (unsigned long long)r.handle,
            (unsigned long long)r.iova, (unsigned long long)r.fence, r.verified, r.guardVerified);
        note("P540 cpu=inhibit gpu=noncache ownership=connection bytes=%llu isa=%u ib=%u out=%x scratch=%x",
            (unsigned long long)r.cpuBytes, r.codeWords, r.ibWords, r.observed, r.scratch);
        if(r.stage==NativeMetal540Probe::Readback){
            line("P540STATE ctrl=%x cfg=%x hcfg=%x hcnt=%x obj=%x ids=%x grid=%x addr=%x",
                r.state[0],r.state[1],r.state[2],r.state[3],r.state[4],r.state[5],r.state[6],r.state[7]);
            NativeAdrenoAbiMetadata sampleMeta={};sampleMeta.handle=r.handle?r.handle:1;
            char sampleText[64]={};snprintf(sampleText,sizeof sampleText,"PX %x/%x/%x/%x",r.samples[0],r.samples[1],r.samples[2],r.samples[3]);
            sampleMeta.bytes=(uint32_t)strlen(sampleText);memcpy(sampleMeta.text,sampleText,sampleMeta.bytes);
            IOConnectCallStructMethod(p.client->conn,NativeAdrenoMethodSetMetadata,&sampleMeta,sizeof sampleMeta,nullptr,nullptr);
            NativeAdrenoAbiMetadata stateMeta={};stateMeta.handle=r.handle?r.handle:1;
            char stateText[64]={};snprintf(stateText,sizeof stateText,"PS %x/%x/%x/%x/%x/%x/%x/%x",
                r.state[0],r.state[1],r.state[2],r.state[3],r.state[4],r.state[5],r.state[6],r.state[7]);
            stateMeta.bytes=(uint32_t)strlen(stateText);memcpy(stateMeta.text,stateText,stateMeta.bytes);
            IOConnectCallStructMethod(p.client->conn,NativeAdrenoMethodSetMetadata,&stateMeta,sizeof stateMeta,nullptr,nullptr);
        }
        NativeAdrenoAbiMetadata meta = {};
        meta.handle = r.handle ? r.handle : 1;
        char text[64] = {};
        snprintf(text, sizeof text, "P540 GLSL430/A5xx s%u r%u e%x f%llx v%u g%u",
            (unsigned)r.stage, r.resource, (unsigned)r.error,
            (unsigned long long)r.fence, r.verified, r.guardVerified);
        meta.bytes = (uint32_t)strlen(text); memcpy(meta.text, text, meta.bytes);
        IOConnectCallStructMethod(p.client->conn, NativeAdrenoMethodSetMetadata,
            &meta, sizeof meta, nullptr, nullptr);
    };
    NativeMetal540Probe probe(transport);
    int shaderRC = probe.run();
    note("P540 done rc=%x producer=GLSL430/A5xx no-system-Metal-device", (unsigned)shaderRC);
    if(shaderRC)return nil;
    NativeMetal540Transform::Transport transformTransport;
    transformTransport.call=transport.call;transformTransport.mapView=transport.mapView;
    transformTransport.order=transport.order;transformTransport.context=&pc;
    transformTransport.record=[](void *opaque,const NativeMetal540Transform::Record &r){
        auto &p=*static_cast<ProbeContext *>(opaque);
        marker(p.port,0x5500u+(unsigned)r.stage);
        note("T540 s%u r%u e%x f%llx in%u ig%u out%u og%u",(unsigned)r.stage,r.resource,
            (unsigned)r.error,(unsigned long long)r.fence,r.inputVerified,r.inputGuardVerified,r.verified,r.guardVerified);
        if(r.stage==NativeMetal540Transform::Readback){
            const char *prefixes[]={"TX ","TI "};const uint32_t *samples[]={r.samples,r.inputSamples};
            for(unsigned i=0;i<2;++i){
                NativeAdrenoAbiMetadata m={};m.handle=r.handle?r.handle:1;char text[64]={};
                snprintf(text,sizeof text,"%s%x/%x/%x/%x",prefixes[i],samples[i][0],samples[i][1],samples[i][2],samples[i][3]);
                m.bytes=(uint32_t)strlen(text);memcpy(m.text,text,m.bytes);
                IOConnectCallStructMethod(p.client->conn,NativeAdrenoMethodSetMetadata,&m,sizeof m,nullptr,nullptr);
            }
        }
        NativeAdrenoAbiMetadata m={};m.handle=r.handle?r.handle:1;char text[64]={};
        snprintf(text,sizeof text,"T540 s%u e%x f%llx in%u/%u v%u g%u",(unsigned)r.stage,(unsigned)r.error,
            (unsigned long long)r.fence,r.inputVerified,r.inputGuardVerified,r.verified,r.guardVerified);
        m.bytes=(uint32_t)strlen(text);memcpy(m.text,text,m.bytes);
        IOConnectCallStructMethod(p.client->conn,NativeAdrenoMethodSetMetadata,&m,sizeof m,nullptr,nullptr);
    };
    NativeMetal540Transform transform(transformTransport);
    int transformRC=transform.run();
    note("T540 done rc=%x producer=GLSL430 dual-SSBO",(unsigned)transformRC);
    if(transformRC)return nil;
    NativeMetal540AIRProbe::Transport airTransport;
    airTransport.call=transport.call;airTransport.mapView=transport.mapView;airTransport.order=transport.order;airTransport.context=&pc;
    airTransport.record=[](void *opaque,const NativeMetal540AIRProbe::Record &r){
        auto &p=*static_cast<ProbeContext *>(opaque);marker(p.port,0x5600u+(unsigned)r.stage);
        note("A540 AIR padding s%u e%x f%llx src%u/%u params%u/%u out%u/%u",(unsigned)r.stage,(unsigned)r.error,
            (unsigned long long)r.fence,r.inputVerified,r.inputGuardVerified,r.paramVerified,r.paramGuardVerified,r.verified,r.guardVerified);
        if(r.stage==NativeMetal540AIRProbe::Readback){
            const char *prefixes[]={"AX ","AI "};const uint32_t *samples[]={r.samples,r.inputSamples};
            for(unsigned i=0;i<2;++i){
                NativeAdrenoAbiMetadata m={};m.handle=r.handle?r.handle:1;char text[64]={};
                snprintf(text,sizeof text,"%s%x/%x/%x/%x",prefixes[i],samples[i][0],samples[i][1],samples[i][2],samples[i][3]);
                m.bytes=(uint32_t)strlen(text);memcpy(m.text,text,m.bytes);
                IOConnectCallStructMethod(p.client->conn,NativeAdrenoMethodSetMetadata,&m,sizeof m,nullptr,nullptr);
            }
        }
        NativeAdrenoAbiMetadata m={};m.handle=r.handle?r.handle:1;char text[64]={};
        snprintf(text,sizeof text,"A540 s%u e%x f%llx i%u/%u p%u/%u v%u g%u",(unsigned)r.stage,(unsigned)r.error,
            (unsigned long long)r.fence,r.inputVerified,r.inputGuardVerified,r.paramVerified,r.paramGuardVerified,r.verified,r.guardVerified);
        m.bytes=(uint32_t)strlen(text);memcpy(m.text,text,m.bytes);
        IOConnectCallStructMethod(p.client->conn,NativeAdrenoMethodSetMetadata,&m,sizeof m,nullptr,nullptr);
    };
    NativeMetal540AIRProbe air(airTransport);int airRC=air.run();
    note("A540 done rc=%x entry=padding_gradient AIR-NIR-A540",(unsigned)airRC);
    if(airRC)return nil;
    NativeMetal540Render::Transport renderTransport;
    renderTransport.call=transport.call;renderTransport.mapView=transport.mapView;renderTransport.order=transport.order;renderTransport.context=&pc;
    renderTransport.record=[](void *opaque,const NativeMetal540Render::Record &r){
        auto &p=*static_cast<ProbeContext *>(opaque);marker(p.port,0x5700u+(unsigned)r.stage);
        note("R540 s%u e%x f%llx vs%u fs%u ib%u center%x corner%x bad%u",(unsigned)r.stage,(unsigned)r.error,
            (unsigned long long)r.fence,r.vsWords,r.fsWords,r.ibWords,r.pixels.center,r.pixels.corner,r.pixels.mismatches);
        NativeAdrenoAbiMetadata m={};m.handle=r.handle?r.handle:1;char text[64]={};
        snprintf(text,sizeof text,"R540 s%u e%x f%llx px%x/%x bad%u",(unsigned)r.stage,(unsigned)r.error,
            (unsigned long long)r.fence,r.pixels.center,r.pixels.corner,r.pixels.mismatches);
        m.bytes=(uint32_t)strlen(text);memcpy(m.text,text,m.bytes);
        IOConnectCallStructMethod(p.client->conn,NativeAdrenoMethodSetMetadata,&m,sizeof m,nullptr,nullptr);
        if(r.stage==NativeMetal540Render::Readback||r.stage==NativeMetal540Render::Complete){
            snprintf(text,sizeof text,"RP in%u out%u edge%u ts%u/%u/%u",r.pixels.inside,r.pixels.outside,r.pixels.uncheckedEdge,
                r.clearTimestamp,r.colorTimestamp,r.ucheTimestamp);m.bytes=(uint32_t)strlen(text);memcpy(m.text,text,m.bytes);
            IOConnectCallStructMethod(p.client->conn,NativeAdrenoMethodSetMetadata,&m,sizeof m,nullptr,nullptr);
        }
    };
    NativeMetal540Render render(renderTransport);int renderRC=render.run(true);
    note("R540 triangle rc=%x no-system-Metal-device",(unsigned)renderRC);
    if(renderRC)return nil;
    NativeMetal540RenderAIRProbe::Transport graphics{};
    graphics.call=transport.call;graphics.mapView=transport.mapView;graphics.order=transport.order;graphics.context=&pc;
    graphics.record=[](void *opaque,const NativeMetal540RenderAIRProbe::Record &r){
        auto &p=*static_cast<ProbeContext *>(opaque);NativeAdrenoAbiMetadata m={};m.handle=1;
        char text[64]={};snprintf(text,sizeof text,"RA s%u e%x f%llx px%x/%x bad%u",(unsigned)r.stage,(unsigned)r.error,
            (unsigned long long)r.fence,r.pixels.center,r.pixels.corner,r.pixels.mismatches);
        m.bytes=(uint32_t)strlen(text);memcpy(m.text,text,m.bytes);
        IOConnectCallStructMethod(p.client->conn,NativeAdrenoMethodSetMetadata,&m,sizeof m,nullptr,nullptr);
        snprintf(text,sizeof text,"RI v%u/%u c%u/%u ts%u/%u/%u",r.inputVerified,r.inputGuardVerified,
            r.colorVerified,r.colorGuardVerified,r.clearTimestamp,r.colorTimestamp,r.ucheTimestamp);
        m.bytes=(uint32_t)strlen(text);memcpy(m.text,text,m.bytes);
        IOConnectCallStructMethod(p.client->conn,NativeAdrenoMethodSetMetadata,&m,sizeof m,nullptr,nullptr);
    };
    NativeMetal540RenderAIRProbe graphicsProbe(graphics);int graphicsRC=graphicsProbe.run(true);
    note("RA real SpriteKit AIR graphics rc=%x no-system-device",(unsigned)graphicsRC);
    if(graphicsRC)return nil;
    IOServiceClose(c.conn);c.conn=0;
    int ownedRC=NativeMetal540RunOwnedObjects(port);
    note("M540 owned public-selector chain rc=%x no-system-device",(unsigned)ownedRC);
    if(ownedRC)return nil;
    int graphicsOwnedRC=NativeMetal540RunOwnedGraphics(port);
    note("RG owned graphics object chain rc=%x no-system-device",(unsigned)graphicsOwnedRC);
    if(graphicsOwnedRC)return nil;
    int surfaceInit=0;
    auto sink=[](void *,const NativeMetal540Surface::Record &r){
        io_connect_t metadata=r.connection;
        if(!metadata)return;
        auto send=[&](const char *text){NativeAdrenoAbiMetadata m={};m.handle=1;m.bytes=(uint32_t)strnlen(text,63);memcpy(m.text,text,m.bytes);
            IOConnectCallStructMethod(metadata,NativeAdrenoMethodSetMetadata,&m,sizeof m,nullptr,nullptr);};
        char text[64]={};
        snprintf(text,sizeof text,"SF540 s%u e%x f%llx px%x/%x bad%u",(unsigned)r.stage,(unsigned)r.error,
            (unsigned long long)r.fence,r.pixels.center,r.pixels.corner,r.pixels.mismatches);send(text);
        snprintf(text,sizeof text,"SP in%u out%u edge%u ts%u/%u/%u c%u",r.pixels.inside,r.pixels.outside,r.pixels.uncheckedEdge,
            r.clearTimestamp,r.colorTimestamp,r.ucheTimestamp,(unsigned)r.cachephase);send(text);
        snprintf(text,sizeof text,"SI id%u b%llx n%llu p%llu hold%u",r.surface.surfaceID,(unsigned long long)r.surface.base,
            (unsigned long long)r.surface.allocation,(unsigned long long)r.surface.pitch,(unsigned)r.surfaceHeld);send(text);
        if(r.retirement.attempted){
            unsigned freed=0,unmapped=0;
            for(unsigned i=0;i<r.retirement.count;++i){freed+=r.retirement.bos[i].freeAck;unmapped+=r.retirement.bos[i].viewUnmapped;}
            snprintf(text,sizeof text,"SR s%u e%x raw%x free%u map%u proof%u",(unsigned)r.retirement.stage,
                (unsigned)r.retirement.error,(unsigned)r.retirement.rawrc,freed,unmapped,(unsigned)r.retirement.proof);send(text);
        }
        if(r.error){snprintf(text,sizeof text,"SE s%u e%x raw%x lock%u",(unsigned)r.stage,(unsigned)r.error,(unsigned)r.rawrc,(unsigned)r.lock);send(text);}
    };
    NSObject *surfaceOwner=[NSObject new];
    Native540SurfaceSession *surface=[[Native540SurfaceSession alloc] initWithService:port record:sink
        context:(void *)(uintptr_t)port owner:surfaceOwner error:&surfaceInit];
    if(!surface){note("SF540 init rc=%x",(unsigned)surfaceInit);return nil;}
    int surfaceRC=[surface runWithPrerequisites:YES];
    note("SF540 private IOSurface rc=%x",(unsigned)surfaceRC);
    int retirement=[surface retire];
    note("SF540 explicit retirement=%x confirmed=%u no-system-presentation",(unsigned)retirement,
        (unsigned)[surface result].retirementConfirmed);
    // These controlled workloads do not publish a system Metal device.
    return nil;
}
@end
