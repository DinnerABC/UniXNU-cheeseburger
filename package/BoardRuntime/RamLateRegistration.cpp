// P17 adapter: register the unchanged official RAM block class only after the
// original Apple IOStorageFamily has initialized its parent metaclass.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <libkern/c++/OSSymbol.h>
#include <mach/kmod.h>
extern "C" {
extern kmod_info_t g_kernel_kmod_info;
kern_return_t OSKextLoadKextWithIdentifier(const char *);
void p17_ram_static_init(void) __asm__("__GLOBAL__sub_I_NativeRamBlock.cpp");
// Read-only media inventory sampler: it only reads service-tree properties, so
// the panic screen can print values that must not be taken from a photograph.
void p17_media_inventory_start(void);
volatile int native_ram_block_registration_status = -1;
void native_ram_block_register(void) {
 static bool loaded=false;
 if(loaded)return;
 kern_return_t status=OSKextLoadKextWithIdentifier("com.apple.iokit.IOStorageFamily");
 native_ram_block_registration_status=status;
 if(status)panic("P17 RAM: original storage load failed: %d",status);
 const OSSymbol *key=OSSymbol::withCString("IOBlockStorageDevice");
 const OSMetaClass *base=key?OSMetaClass::getMetaClassWithName(key):nullptr;
 OSSafeReleaseNULL(key);
 if(!base||base!=&IOBlockStorageDevice::gMetaClass||base->getClassSize()!=sizeof(IOBlockStorageDevice)) {
  native_ram_block_registration_status=-2;
  panic("P17 RAM: original IOBlockStorageDevice ABI mismatch");
 }
 void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
 if(!handle){native_ram_block_registration_status=-3;panic("P17 RAM: preModLoad failed");}
 p17_ram_static_init();
 status=OSMetaClass::postModLoad(handle);
 native_ram_block_registration_status=status;
 if(status)panic("P17 RAM: class registration failed: %d",status);
 key=OSSymbol::withCString("NativeRamBlock");
 const OSMetaClass *ram=key?OSMetaClass::getMetaClassWithName(key):nullptr;
 OSSafeReleaseNULL(key);
 if(!ram){native_ram_block_registration_status=-4;panic("P17 RAM: class registration absent");}
 loaded=true;
 IOLog("P17 RAM: original storage ABI checked; NativeRamBlock registered\n");
 p17_media_inventory_start();
}
}
