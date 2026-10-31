// SPDX-License-Identifier: GPL-2.0-only
// Qualcomm PAS-13 GPU ZAP loader. The ZAP archive is a private vendor input
// (generated header, never committed); this file parses it, loads the signed
// image into an owned physical region and performs the documented PAS chain:
// init(metadata) -> mem_setup(region) -> copy image -> authenticate.
//
// The same-SoC vendor driver and sagit authenticate after CP_ME_INIT/engine
// start and only then submit CP_SET_SECURE_MODE; the caller runs this chain
// after a successful engine start and halts on failure. Host tests cover the
// archive/MDT rules and the PAS state machine. Only the secure monitor can
// accept the signature; a successful SMC here is the only evidence that
// authentication happened, and it is still not proof that the CP fetches
// commands. Nothing in this file writes RBBM_SECVID_TRUST_CNTL.
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include "QcomPAS.h"
#include "CheeseburgerZap.h"
#if __has_include("CheeseburgerZapFirmware.h")
#include "CheeseburgerZapFirmware.h"
#define CB_ZAP_FW 1
#else
static const unsigned char zap_firmware_blob[1] = {0};
static const unsigned zap_firmware_size = 0;
#define CB_ZAP_FW 0
#endif

extern "C" {
volatile uint64_t cb_zap[32];
void flush_dcache(vm_offset_t, unsigned, boolean_t);
void cheeseburger_bootlog_step(const char *) __attribute__((weak_import));
}

namespace {
struct ScmBackend {
    void smc(QcomSecure::Registers &r) {
        register uint64_t x0 __asm__("x0") = r.x[0];
        register uint64_t x1 __asm__("x1") = r.x[1];
        register uint64_t x2 __asm__("x2") = r.x[2];
        register uint64_t x3 __asm__("x3") = r.x[3];
        register uint64_t x4 __asm__("x4") = r.x[4];
        register uint64_t x5 __asm__("x5") = r.x[5];
        register uint64_t x6 __asm__("x6") = r.x[6];
        register uint64_t x7 __asm__("x7") = r.x[7];
        __asm__ volatile("dsb sy\n\tsmc #0\n\tdsb sy\n\tisb"
                         : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
                           "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
                         :
                         : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
                           "x16", "x17", "cc", "memory");
        r = {{x0, x1, x2, x3, x4, x5, x6, x7}};
    }
    void sleep(unsigned ms) { IOSleep(ms); }
};

using Transport = QcomSecure::Transport<ScmBackend>;
using Pas = QcomSecure::PAS<Transport>;

struct ZapBuffer {
    IOBufferMemoryDescriptor *descriptor = nullptr;
    void *allocation = nullptr;      // first byte of the descriptor
    uint64_t allocation_pa = 0;
    uint32_t total = 0, bytes = 0;
    volatile uint8_t *va = nullptr;  // aligned start of the usable bytes
    uint64_t pa = 0;
    uint64_t mask = 0;
    unsigned width = 0;              // physical-address bits accepted by the mask
};

static bool allocate(ZapBuffer &b, uint32_t bytes, uint64_t alignment, uint64_t mask, unsigned width) {
    if (!bytes || !alignment || (alignment & (alignment - 1)) || alignment < 4096) return false;
    const uint64_t total = (uint64_t(bytes) + alignment + 4095) & ~4095ull;
    if (total > 8 * 1024 * 1024) return false;
    b.descriptor = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task,
        static_cast<IOOptionBits>(kIODirectionInOut) | static_cast<IOOptionBits>(kIOMemoryPhysicallyContiguous) |
            static_cast<IOOptionBits>(kIOMemoryMapperNone),
        total, mask);
    if (!b.descriptor) return false;
    if (b.descriptor->prepare() != kIOReturnSuccess) {
        b.descriptor->release();
        b.descriptor = nullptr;
        return false;
    }
    IOByteCount span = 0;
    const uint64_t pa0 = b.descriptor->getPhysicalSegment(0, &span, kIOMemoryMapperNone);
    void *const va0 = b.descriptor->getBytesNoCopy();
    const uint64_t aligned = (pa0 + alignment - 1) & ~(alignment - 1);
    const uint64_t limit = 1ULL << width;
    // The physical mask is a placement request, not a guarantee: reject a
    // descriptor that came back outside it and let the caller try the next
    // width instead of recording a misleading mask.
    if (!pa0 || (pa0 & 4095) || !va0 || span < total || b.descriptor->getLength() != total ||
        aligned < pa0 || aligned - pa0 > total || bytes > total - (aligned - pa0) ||
        aligned >= limit || bytes > limit - aligned) {
        (void)b.descriptor->complete();
        b.descriptor->release();
        b.descriptor = nullptr;
        return false;
    }
    b.allocation = va0; b.allocation_pa = pa0; b.total = uint32_t(total);
    b.va = static_cast<volatile uint8_t *>(va0) + (aligned - pa0);
    b.pa = aligned; b.bytes = bytes; b.mask = mask; b.width = width;
    return true;
}

static void step(const char *tag) {
    if (&cheeseburger_bootlog_step) cheeseburger_bootlog_step(tag);
}

static int fail(unsigned stage, int code) {
    cb_zap[0] = uint64_t(int32_t(code));
    cb_zap[29] = stage;
    step("zapfail");
    return code;
}

static void record(const QcomSecure::Reply &r, unsigned slot) {
    cb_zap[slot] = uint64_t(unsigned(r.status));
    cb_zap[slot + 1] = uint64_t(int32_t(r.raw));
    cb_zap[slot + 2] = r.value;
}
}  // namespace

// Returns 0 only after the secure monitor reported success for init, mem_setup
// and authenticate. On any failure the caller must halt/abandon the engine;
// authentication gates all user submissions, not the ME_INIT engine start.
extern "C" int cheeseburger_gpu_zap_load(volatile uint32_t *gpu) {
    for (unsigned i = 0; i < 32; ++i) cb_zap[i] = 0xdeadbeef;
    cb_zap[0] = 0;
    cb_zap[1] = zap_firmware_size;
    if (!CB_ZAP_FW || !zap_firmware_size) {
        IOLog("CB GPU ZAP: no private ZAP image compiled in\n");
        return fail(0, -100);
    }
    CheeseburgerZap::Archive archive;
    CheeseburgerZap::Image image;
    if (!archive.open(zap_firmware_blob, zap_firmware_size) || !image.parse(archive)) {
        IOLog("CB GPU ZAP: container/ELF rejected size=%u\n", zap_firmware_size);
        return fail(0, -101);
    }
    cb_zap[2] = archive.bytes();
    cb_zap[3] = image.imageBytes();
    cb_zap[4] = image.metadataBytes();
    cb_zap[5] = image.physicalBase();
    cb_zap[6] = image.imageAlignment();
    cb_zap[7] = image.relocatable() ? 1 : 0;
    cb_zap[8] = image.segmentCount();

    // Vendor firmware is mapped below 4 GiB first: the same constraint the
    // sagit loader applies so either SMCCC convention can address it. A 40-bit
    // fallback is recorded, never silently assumed.
    static ZapBuffer metadata, region;
    if (metadata.descriptor || region.descriptor) return fail(0, -107);  // one image per boot
    const uint64_t masks[] = {0x00000000fffff000ull, 0x000000fffffff000ull};
    bool allocated = false;
    for (unsigned i = 0; i < 2 && !allocated; ++i) {
        if (allocate(metadata, image.metadataBytes(), 4096, masks[i], i ? 40 : 32))
            allocated = true;
    }
    if (!allocated) {
        IOLog("CB GPU ZAP: metadata allocation failed bytes=%u\n", image.metadataBytes());
        return fail(0, -102);
    }
    allocated = false;
    for (unsigned i = 0; i < 2 && !allocated; ++i) {
        if (allocate(region, image.imageBytes(), image.imageAlignment(), masks[i], i ? 40 : 32))
            allocated = true;
    }
    cb_zap[9] = metadata.pa;
    cb_zap[10] = region.pa;
    cb_zap[11] = region.width;
    if (!allocated) {
        IOLog("CB GPU ZAP: image allocation failed bytes=%u align=%x\n",
              image.imageBytes(), image.imageAlignment());
        return fail(0, -103);
    }
    if (!image.metadataInto(const_cast<uint8_t *>(metadata.va), metadata.bytes)) {
        IOLog("CB GPU ZAP: metadata copy rejected %u/%u\n", metadata.bytes, image.metadataBytes());
        return fail(0, -104);
    }
    flush_dcache((vm_offset_t)metadata.va, metadata.bytes, FALSE);
    __asm__ volatile("dsb sy" ::: "memory");

    ScmBackend io;
    Transport transport(io);
    Pas pas(transport);
    const auto probe = transport.probe();
    cb_zap[12] = unsigned(probe);
    cb_zap[13] = transport.is64() ? 1 : 0;
    if (probe != QcomSecure::Status::OK) {
        IOLog("CB GPU ZAP: SCM probe failed st=%u\n", unsigned(probe));
        return fail(0, -105);
    }
    auto available = [&](unsigned command) {
        const auto r = transport.available(2, command);
        return r.status == QcomSecure::Status::OK && r.value == 1 ? 1 : 0;
    };
    cb_zap[14] = available(1);
    cb_zap[15] = available(2);
    cb_zap[16] = available(5);
    if (!cb_zap[14] || (image.relocatable() && !cb_zap[15]) || !cb_zap[16]) {
        IOLog("CB GPU ZAP: PAS commands unavailable init=%llu setup=%llu auth=%llu\n",
              cb_zap[14], cb_zap[15], cb_zap[16]);
        return fail(0, -106);
    }
    // The IS_SUPPORTED query (service 2 command 7) returns 0 on this GPU PAS
    // firmware even when every operation below is available; the sagit loader
    // records it without treating it as an authorization result.
    const auto supported = transport.supported();
    cb_zap[17] = supported.status == QcomSecure::Status::OK ? supported.value : 0xdeadbeef;
    cb_zap[18] = uint64_t(int32_t(supported.raw));

    cb_zap[30] = gpu ? gpu[(0xf400 * 4) / 4] : 0xffffffff;  // TrustControl, read only

    step("zapinit");
    auto status = pas.init(metadata.pa);
    auto reply = pas.last();
    record(reply, 19);
    if (status != QcomSecure::Status::OK) {
        IOLog("CB GPU ZAP: init rt=%d raw=%x val=%llx\n", int(status), uint32_t(reply.raw), reply.value);
        return fail(1, -110);
    }
    step("zapsetup");
    status = pas.setup(region.pa, image.imageBytes(), image.relocatable());
    reply = pas.last();
    record(reply, 22);
    if (status != QcomSecure::Status::OK) {
        IOLog("CB GPU ZAP: setup rt=%d raw=%x val=%llx\n", int(status), uint32_t(reply.raw), reply.value);
        return fail(2, -111);
    }
    if (!image.imageInto(const_cast<uint8_t *>(region.va), region.bytes, region.pa)) {
        IOLog("CB GPU ZAP: image copy rejected %u/%u pa=%llx\n", region.bytes, image.imageBytes(), region.pa);
        return fail(2, -112);
    }
    flush_dcache((vm_offset_t)region.va, region.bytes, FALSE);
    __asm__ volatile("dsb sy" ::: "memory");
    cb_zap[28] = 2;
    step("zapauth");
    status = pas.authenticate();
    reply = pas.last();
    record(reply, 25);
    if (status != QcomSecure::Status::OK) {
        IOLog("CB GPU ZAP: auth rt=%d raw=%x val=%llx\n", int(status), uint32_t(reply.raw), reply.value);
        return fail(3, -113);
    }
    step("zapdone");
    cb_zap[28] = 3;
    cb_zap[29] = 0;
    // Read-only state after authentication; the image and metadata pages must
    // not be read, cleared or freed from here on.
    cb_zap[31] = gpu ? gpu[(0xf400 * 4) / 4] : 0xffffffff;
    IOLog("CB GPU ZAP: ok md=%llx/%u img=%llx/%u al=%x src=%u mm=%u rm=%u "
          "init=%d/%x setup=%d/%x auth=%d/%x trust=%x->%x sup=%llx\n",
          metadata.pa, metadata.bytes, region.pa, region.bytes, image.imageAlignment(),
          image.physicalBase(), metadata.width, region.width,
          int(cb_zap[19]), uint32_t(cb_zap[20]), int(cb_zap[22]), uint32_t(cb_zap[23]),
          int(cb_zap[25]), uint32_t(cb_zap[26]), uint32_t(cb_zap[30]), uint32_t(cb_zap[31]),
          cb_zap[17]);
    return 0;
}
