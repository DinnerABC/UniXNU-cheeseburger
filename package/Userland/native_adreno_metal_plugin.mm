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
#include <unistd.h>
#include "NativeAdrenoABI.h"

namespace {
constexpr uint32_t kOpenType = 0x4147;

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
@end

@implementation NativeAdrenoMetalDevice
- (id)initWithAcceleratorPort:(unsigned)port {
    line("P21PLUGIN stage=enter port=%u pid=%d", port, (int)getpid());
    note("enter pid=%d port=%u", (int)getpid(), port);
    marker(port, 1);
    Ctx c;
    // The accelerator may be discovered before the CP/fence path is ready, so
    // retry the open instead of concluding that the service is absent.
    for (unsigned i = 0; i < 240 && !c.conn; ++i) {
        kern_return_t kr = IOServiceOpen((io_service_t)port, mach_task_self(), kOpenType, &c.conn);
        if (c.conn) {
            line("P21PLUGIN stage=open rc=%d conn=%u try=%u", (int)kr, c.conn, i);
            note("open rc=%d try=%u", (int)kr, i);
            marker(port, 2);
            break;
        }
        c.error = (int)kr;
        marker(port, 3);
        usleep(500000);
    }
    if (!c.conn) {
        line("P21PLUGIN stage=noopen e%x", (unsigned)c.error);
        note("noopen e%x", (unsigned)c.error);
        return nil;
    }
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
    NativeAdrenoAbiBoAllocReply ib = {}, tgt = {};
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
    mach_vm_address_t ibAddr = 0, tgtAddr = 0;
    mach_vm_size_t ibLen = 0, tgtLen = 0;
    if (!checked(c, IOConnectMapMemory64(c.conn, crIb.token, mach_task_self(), &ibAddr, &ibLen, 1), "mapmem-ib")) return nil;
    if (!checked(c, IOConnectMapMemory64(c.conn, crTgt.token, mach_task_self(), &tgtAddr, &tgtLen, 1), "mapmem-tgt")) return nil;
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
    IOConnectUnmapMemory64(c.conn, crIb.token, mach_task_self(), ibAddr);
    IOConnectUnmapMemory64(c.conn, crTgt.token, mach_task_self(), tgtAddr);
    NativeAdrenoAbiBoCpuUnmap ui = {ib.handle, crIb.token, 0}, ut = {tgt.handle, crTgt.token, 0};
    call(c, NativeAdrenoMethodBoCpuUnmap, &ui, (NativeAdrenoAbiTimestamp *)nullptr);
    call(c, NativeAdrenoMethodBoCpuUnmap, &ut, (NativeAdrenoAbiTimestamp *)nullptr);
    NativeAdrenoAbiBoFree fi = {ib.handle}, ft = {tgt.handle};
    call(c, NativeAdrenoMethodBoFree, &fi, (NativeAdrenoAbiTimestamp *)nullptr);
    call(c, NativeAdrenoMethodBoFree, &ft, (NativeAdrenoAbiTimestamp *)nullptr);
    line("P21PLUGIN stage=idle verified=%d", verified);
    note("done verified=%d", verified);
    marker(port, 7);
    IOServiceClose(c.conn);
    // Stage 1: no device is returned, so backboardd keeps the software renderer.
    return nil;
}
@end
