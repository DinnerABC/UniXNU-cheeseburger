// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <cstddef>
#include <cstdint>
namespace NativeMetal540Capabilities {
// Fixed implementation-wide policy, NOT general Metal conformance. Caller must
// verify admitted code AND pinned device probe before supplying these flags.
enum EvidenceFlag : uint32_t {
    LinearRGBA8RasterCode = 1u << 0,
    LinearRGBA8DevicePixels = 1u << 1,
    RestrictedDescriptorAdmission = 1u << 2,
};
struct Evidence {
    const uint32_t flags;
    explicit constexpr Evidence(uint32_t value) : flags(value) {}
};
enum class Status {
    Ok, UnknownSelector, InvalidPolicy, MissingEvidence, InvalidEvidence,
    InvalidClass, BaseSizeMismatch, BaseMethodMissing, TypeMismatch,
    SubclassMethodExists, AddMethodFailed
};
struct Entry {
    const char *selector, *type, *targetIMP, *targetIMPSHA256, *reason;
    bool supported;
    uint32_t requiredEvidenceFlags;
};
struct Report {
    Status status = Status::InvalidClass;
    size_t checked = 0, installed = 0, supported = 0;
    uint32_t missingEvidenceFlags = 0;
    const char *selector = nullptr;
    bool preflightComplete = false, discardSubclass = false;
};
constexpr size_t QueryCount = 195;
constexpr size_t TargetBaseInstanceSize = 536;
const Entry *entries();
size_t count();
uint32_t minimumEvidenceFlags();
// UnknownSelector is an error, NOT a classified NO; outputs remain unchanged.
Status answer(const char *selector, BOOL &value, const char *&reason);
}
// Fresh unregistered DIRECT NativeMetal540... subclass of actual _MTLDevice;
// caller owns allocation/registration, inherited size536, no additional ivars.
// Invoke before any init/base-init feature queries. Full195/evidence preflight
// precedes ANY class_addMethod. A mutation failure requires discarding the still
// unregistered subclass (ObjC offers no rollback). No framework-base patches,
// Apple-family/tier claims, per-instance reads, or system device publication.
bool NativeMetal540InstallQueries(Class subclass, Class base,
    const NativeMetal540Capabilities::Evidence &evidence,
    NativeMetal540Capabilities::Report &report);
