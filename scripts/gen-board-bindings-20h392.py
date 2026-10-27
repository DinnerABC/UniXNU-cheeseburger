#!/usr/bin/env python3
"""Generate the 20H392 (iOS 16.7.16) board-runtime original bindings.

Everything here is read from the 20H392 donor kernelcache, never inferred from
the 21H461 tables: the display slots are the donor vtable entries themselves,
and the PMGR helpers are located by disassembling the donor's own functions.

The emitted .S files bind the class name to the original implementation. The
absolute values are donor VAs plus SLIDE; SLIDE is the placement difference
between the donor image and the final prelinked image. It is provisional until
the Stage C prelink spec fixes the segment placement (see
out/p23/ios-16.7.16-20H392/STAGE_C_BOARDRUNTIME_STATUS.md 3.7); the build
report records the value used.

Run: python3 scripts/gen-board-bindings-20h392.py
"""
from pathlib import Path
import hashlib
import importlib.util
import sys

R = Path(__file__).resolve().parents[1]
STAGE = R / 'out/p23/ios-16.7.16-20H392'
DONOR = STAGE / 'analysis/root-trust-data/kernelcache/kernelcache.thin.macho'
TOOLS = STAGE / 'analysis/display-abi/tools/abi.py'
OUT = R / 'package/BoardRuntime'

SLIDE = 0xbc0000  # initial candidate, verify against final native Mach-O ranges before prelink
KERNEL_VA_BASE = 0xfffffff000000000

def slid(address):
    full = address if address >= 0x100000000 else KERNEL_VA_BASE + address
    return full - SLIDE

# Display: N vtable 338 entries (0..337). Entries below 168 belong to IOService.
DISPLAY_ZTv = 0x6def8d0
DISPLAY_SLOTS = 338
DISPLAY_FIRST = 168
# Pure slots are overridden by the board class (NativeLegacyDisplay LEAF set).
DISPLAY_PURE = {211, 212, 242, 243, 292, 293, 321, 322, 329}
DISPLAY_NAMED = {184, 188, 246, 274, 276, 281, 282, 287}
DISPLAY_C2 = 0x5c0bbbc
DISPLAY_D2 = 0x5c0bc08
DISPLAY_VSYNC = 0x5c1b838
DISPLAY_SET_REFRESH = 0x5c23610
# IOSurface accessors used by the board display path (dump of the donor table at
# 0x5a4e7d4..0x5a4e86c). The 20H392 surface has no 64-bit allocation accessor:
# +0x90 is a 32-bit dimension field, so allocation comes from the descriptor.
SURFACE = {
    'descriptor': 0x5a4e844,  # ldr x0,[x0,#0x30]
    'width':      0x5a4e854,  # ldr x0,[x0,#0x58]
    'height':     0x5a4e85c,  # ldr x0,[x0,#0x60]
    'stride':     0x5a4e864,  # ldr w0,[x0,#0x68] (32-bit in 20H392)
    'format':     0x5a4e7e4,  # ldr w0,[x0,#0xa4]; proven by the 'BGRA' compare
    'planes':     0x5a4e80c,  # ldr w0,[x0,#0x98]
}
# Base-class overrides the board display does not define itself.
DISPLAY_BASE = {
    21:  '__ZN25IOMobileFramebufferLegacy4initEP12OSDictionary',
    44:  '__ZN25IOMobileFramebufferLegacy13setPropertiesEP8OSObject',
    79:  '__ZN25IOMobileFramebufferLegacy18systemWillShutdownEj',
    85:  '__ZN25IOMobileFramebufferLegacy5probeEP9IOServicePi',
    111: '__ZNK25IOMobileFramebufferLegacy11getWorkLoopEv',
}
# Named slots referenced directly by NativeLegacyDisplay.cpp (qualified calls
# the compiler emits as direct references, per the undefined-symbol list).
DISPLAY_DIRECT = {
    246: '__ZN25IOMobileFramebufferLegacy20getDisplayDimensionsEPjS0_',
    281: '__ZN25IOMobileFramebufferLegacy19completeCurrentSwapEv',
}

# PMGR: N vtable 325 entries (0..324); readAon/writeAon are slots 302/303 and
# the 20H392 implementations are empty bodies (0x5681de0/0x5681de4: ret).
PMGR_ZTv = 0x6c9fae8
PMGR_SLOTS = 325
PMGR_READ_SLOT = 302
PMGR_WRITE_SLOT = 303
PMGR_C2 = 0x5657040  # calls the base ctor stub 0x568f9c4, stores __ZTV+16
PMGR_D2 = 0x5657164  # b 0x568f9d0 (base destructor stub)
PMGR_NUB_CREATE = 0x56832d4  # mov w1,#0x740 + alloc 0x568f76c + setName 0x568fa3c


def load_kc():
    spec = importlib.util.spec_from_file_location('abi', TOOLS)
    if not spec:
        raise SystemExit(f'missing analysis tool: {TOOLS}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.KC(str(DONOR))


def slot(kc, ztv, index):
    value = kc.q(ztv + 16 + 8 * index)
    if value is None:
        raise SystemExit(f'no vtable slot {index} at {ztv:#x}')
    return value & 0xffffffff


def vtable_count(kc, ztv, limit):
    n = 0
    while n < limit:
        value = kc.q(ztv + 16 + 8 * n)
        if value is None or not kc.inexec(value & 0xffffffff):
            break
        n += 1
    return n


def check(kc):
    if vtable_count(kc, DISPLAY_ZTv, 1024) != DISPLAY_SLOTS:
        raise SystemExit('20H392 display vtable slot count changed')
    if vtable_count(kc, PMGR_ZTv, 1024) != PMGR_SLOTS:
        raise SystemExit('20H392 PMGR vtable slot count changed')
    for index in DISPLAY_PURE:
        value = slot(kc, DISPLAY_ZTv, index)
        if value != 0x770ace4:
            raise SystemExit(f'display slot {index} is no longer the shared pure stub')
    for index in list(DISPLAY_BASE) + list(DISPLAY_DIRECT):
        if not kc.inexec(slot(kc, DISPLAY_ZTv, index)):
            raise SystemExit(f'display slot {index} target is not executable')
    if not kc.inexec(slot(kc, PMGR_ZTv, PMGR_READ_SLOT)) or \
       not kc.inexec(slot(kc, PMGR_ZTv, PMGR_WRITE_SLOT)):
        raise SystemExit('PMGR AON slots are not executable')


def emit(path, donor_hash, lines, note):
    text = (f'// SPDX-License-Identifier: BSD-2-Clause-Patent\n'
            f'// GENERATED by scripts/gen-board-bindings-20h392.py from the 20H392 donor\n'
            f'// {DONOR.name} (sha256 {donor_hash}).\n'
            f'// {note}\n'
            f'// Values are donor VAs - 0x{SLIDE:x}; re-run the generator, never edit by hand.\n')
    text += '\n'.join(lines) + '\n'
    path.write_text(text)
    print(f'wrote {path.relative_to(R)} ({len(lines)} symbols)')


def display_bindings(kc):
    lines = []
    for index in range(DISPLAY_FIRST, DISPLAY_SLOTS):
        if index in DISPLAY_PURE or index in DISPLAY_NAMED or index in DISPLAY_BASE or index in DISPLAY_DIRECT:
            continue
        name = f'__ZN25IOMobileFramebufferLegacy9opaque{index}Ev'
        lines.append(f'.globl {name}\n.set {name}, {slid(slot(kc, DISPLAY_ZTv, index)):#x}')
    for index, name in DISPLAY_BASE.items():
        lines.append(f'.globl {name}\n.set {name}, {slid(slot(kc, DISPLAY_ZTv, index)):#x}')
    for index, name in DISPLAY_DIRECT.items():
        lines.append(f'.globl {name}\n.set {name}, {slid(slot(kc, DISPLAY_ZTv, index)):#x}')
    meta = (kc.q(DISPLAY_ZTv - 16) or 0) & 0xffffffffffffffff
    if not (meta >> 63):
        raise SystemExit('display metaclass pointer is not a kernel pointer')
    lines.append(f'.globl __ZN25IOMobileFramebufferLegacyC2EPK11OSMetaClass\n'
                 f'.set __ZN25IOMobileFramebufferLegacyC2EPK11OSMetaClass, {slid(DISPLAY_C2):#x}')
    lines.append(f'.globl __ZN25IOMobileFramebufferLegacyD2Ev\n'
                 f'.set __ZN25IOMobileFramebufferLegacyD2Ev, {slid(DISPLAY_D2):#x}')
    lines.append(f'.globl __ZTV25IOMobileFramebufferLegacy\n'
                 f'.set __ZTV25IOMobileFramebufferLegacy, {slid(DISPLAY_ZTv):#x}')
    lines.append(f'.globl __ZN25IOMobileFramebufferLegacy10gMetaClassE\n'
                 f'.set __ZN25IOMobileFramebufferLegacy10gMetaClassE, {slid(meta):#x}')
    lines.append(f'.globl _native_legacy_set_refresh_properties\n'
                 f'.set _native_legacy_set_refresh_properties, {slid(DISPLAY_SET_REFRESH):#x}')
    lines.append(f'.globl _native_legacy_vsync_notify\n'
                 f'.set _native_legacy_vsync_notify, {slid(DISPLAY_VSYNC):#x}')
    for name, address in SURFACE.items():
        lines.append(f'.globl _native_legacy_surface_{name}\n'
                     f'.set _native_legacy_surface_{name}, {slid(address):#x}')
    return lines


def pmgr_bindings(kc):
    meta = (kc.q(PMGR_ZTv - 16) or 0) & 0xffffffffffffffff
    if not (meta >> 63):
        raise SystemExit('PMGR metaclass pointer is not a kernel pointer')
    lines = [
        f'.globl __ZTV9ApplePMGR\n.set __ZTV9ApplePMGR, {slid(PMGR_ZTv):#x}',
        f'.globl __ZN9ApplePMGRC2EPK11OSMetaClass\n'
        f'.set __ZN9ApplePMGRC2EPK11OSMetaClass, {slid(PMGR_C2):#x}',
        f'.globl __ZN9ApplePMGRD2Ev\n.set __ZN9ApplePMGRD2Ev, {slid(PMGR_D2):#x}',
        f'.globl __ZN9ApplePMGR10gMetaClassE\n.set __ZN9ApplePMGR10gMetaClassE, {slid(meta):#x}',
        f'.globl _sheng_original_pmgr_nub_create\n'
        f'.set _sheng_original_pmgr_nub_create, {slid(PMGR_NUB_CREATE):#x}',
    ]
    return lines


def pmgr_header():
    lines = ['// SPDX-License-Identifier: BSD-2-Clause-Patent',
             '// GENERATED by scripts/gen-board-bindings-20h392.py from the 20H392',
             '// ApplePMGR vtable (325 entries; readAon/writeAon are slots 302/303).',
             '#pragma once', '#include <IOKit/IOPMGR.h>', 'class ApplePMGR : public IOPMGR {',
             ' OSDeclareAbstractStructors(ApplePMGR)',
             ' unsigned char originalPrivate[0x36968-sizeof(IOPMGR)];', 'public:']
    for index in range(177, PMGR_SLOTS):
        if index == PMGR_READ_SLOT:
            lines.append(' virtual uint32_t readAon();')
        elif index == PMGR_WRITE_SLOT:
            lines.append(' virtual void writeAon(uint64_t);')
        else:
            lines.append(f' virtual void opaque{index}() = 0;')
    lines.append('};')
    lines.append('static_assert(sizeof(ApplePMGR)==0x36968,"Original provider storage");')
    return '\n'.join(lines) + '\n'


def pmgr_slots_inc():
    lines = ['// GENERATED by scripts/gen-board-bindings-20h392.py: every 20H392 ApplePMGR',
             '// slot that is not classified by the board runtime is a guard, never original code.',
             '// Slots 168..176 are written out in CheeseburgerPlatformPower.cpp.']
    for index in range(177, PMGR_SLOTS):
        if index in (PMGR_READ_SLOT, PMGR_WRITE_SLOT):
            continue
        lines.append(f' void opaque{index}() override {{ unavailable({index}); }}')
    return '\n'.join(lines) + '\n'


def main():
    kc = load_kc()
    check(kc)
    digest = hashlib.sha256(DONOR.read_bytes()).hexdigest()
    emit(OUT / 'display-original-bindings-20H392.S', digest, display_bindings(kc),
         'N display vtable slots 168..337 minus the board pure overrides and base overrides.')
    emit(OUT / 'pmgr-original-bindings-20H392.S', digest, pmgr_bindings(kc),
         'N ApplePMGR vtable, constructor/destructor and nub factory.')
    (OUT / 'OriginalPMGRABI_20H392.h').write_text(pmgr_header())
    (OUT / 'GuardedPMGRSlots_20H392.inc').write_text(pmgr_slots_inc())
    print('wrote OriginalPMGRABI_20H392.h and GuardedPMGRSlots_20H392.inc')
    print('slide', hex(SLIDE), 'donor', digest[:16])


if __name__ == '__main__':
    main()
