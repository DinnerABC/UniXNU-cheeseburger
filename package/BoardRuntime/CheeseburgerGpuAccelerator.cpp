// SPDX-License-Identifier: MIT
// Registry-visible accelerator for the original Metal discovery:
// IOServiceMatching("IOAcceleratorES") -> MetalPluginName ->
// /System/Library/Extensions/<name>.bundle. The class derives from the original
// IOGPUFamily base through the audited aliases in accelerator-original-bindings.S;
// its static initializer is staged out of __mod_init_func and runs only after the
// board probe has seen the IOAcceleratorES metaclass.
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSString.h>
#include <string.h>
#include "OriginalAcceleratorABI.h"

extern "C" IOReturn cheeseburger_gpu_open_client(IOService *, task_t, void *, UInt32, OSDictionary *,
    IOUserClient **);

// Observed reads by the original discovery: MetalPluginName, then the plugin
// class name, then the sandbox grant name. A zero row means the discovery never
// looked at this accelerator in that boot.
extern "C" { volatile uint64_t native_gpu_plugin_reads[3] = {0, 0, 0}; }

class AGXAccelerator : public IOAcceleratorES {
    OSDeclareDefaultStructors(AGXAccelerator)
    static void note(const char *key);
public:
    IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **) override;
    OSObject *copyProperty(const OSSymbol *key) const override;
    OSObject *copyProperty(const OSString *key) const override;
    OSObject *copyProperty(const char *key) const override;
};
OSDefineMetaClassAndStructors(AGXAccelerator, IOAcceleratorES)

void AGXAccelerator::note(const char *key) {
    if (!key) return;
    if (!strcmp(key, "MetalPluginName")) native_gpu_plugin_reads[0]++;
    else if (!strcmp(key, "MetalPluginClassName")) native_gpu_plugin_reads[1]++;
    else if (!strcmp(key, "NativeSandboxGrantProcess")) native_gpu_plugin_reads[2]++;
}
OSObject *AGXAccelerator::copyProperty(const OSSymbol *key) const {
    note(key ? key->getCStringNoCopy() : nullptr);
    return IORegistryEntry::copyProperty(key);
}
OSObject *AGXAccelerator::copyProperty(const OSString *key) const {
    note(key ? key->getCStringNoCopy() : nullptr);
    return IORegistryEntry::copyProperty(key);
}
OSObject *AGXAccelerator::copyProperty(const char *key) const {
    note(key);
    return IORegistryEntry::copyProperty(key);
}

IOReturn AGXAccelerator::newUserClient(task_t owningTask, void *securityID, UInt32 type,
    OSDictionary *properties, IOUserClient **handler) {
    return cheeseburger_gpu_open_client(this, owningTask, securityID, type, properties, handler);
}

// Called once, after IOGPUFamily has registered the base metaclass. The
// properties are what backboardd's discovery reads before it loads the bundle;
// NativeSandboxGrantProcess names the process the kernel's sandbox grant admits.
extern "C" IOService *cheeseburger_gpu_accelerator_allocate(void) {
    AGXAccelerator *accel = new AGXAccelerator;
    if (!accel) return nullptr;
    if (!accel->init()) {
        accel->release();
        return nullptr;
    }
    OSString *plugin = OSString::withCString("NativeAdrenoMetal");
    OSString *pluginClass = OSString::withCString("NativeAdrenoMetalDevice");
    // Every process that runs the original discovery loads its own copy of the
    // plugin; the observed hosts so far are the render server, the media server
    // and SpringBoard. The kernel grant accepts the comma-separated list.
    OSString *grant = OSString::withCString("backboardd,mediaserverd,SpringBoard");
    if (plugin) accel->setProperty("MetalPluginName", plugin);
    if (pluginClass) accel->setProperty("MetalPluginClassName", pluginClass);
    if (grant) accel->setProperty("NativeSandboxGrantProcess", grant);
    OSSafeReleaseNULL(plugin);
    OSSafeReleaseNULL(pluginClass);
    OSSafeReleaseNULL(grant);
    return accel;
}
