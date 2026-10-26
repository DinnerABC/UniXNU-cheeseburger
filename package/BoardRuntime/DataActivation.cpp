// P17-R3 platform ordering adapter. The official NativeRamBlock driver and
// native_owner_data_prepare remain unchanged. Data is not a root dependency:
// release its IOResources personality only after authenticated SystemOS setup.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/types.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSString.h>

extern "C" {
int native_ram_block_activate(const char *);
int native_ram_block_volume_seed(const char *, dev_t *, char *, size_t,
    uint64_t *, uint64_t *, uint8_t *);
volatile int native_ram_block_activation_status = -1;
uint64_t native_ram_block_activation_started_ms;
uint64_t native_ram_block_activation_finished_ms;
uint64_t native_ram_block_activation_elapsed_ms;
}

static uint64_t activation_now_ns()
{
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns;
}

static int activation_finish(int status)
{
    native_ram_block_activation_finished_ms = activation_now_ns() / 1000000;
    native_ram_block_activation_elapsed_ms = native_ram_block_activation_finished_ms -
        native_ram_block_activation_started_ms;
    native_ram_block_activation_status = status;
    return status;
}

extern "C" int native_ram_block_activate(const char *uuid)
{
    const uint64_t timeout_ns = UINT64_C(30000000000);
    uint64_t started_ns = activation_now_ns();
    native_ram_block_activation_started_ms = started_ns / 1000000;
    native_ram_block_activation_finished_ms = 0;
    native_ram_block_activation_elapsed_ms = 0;
    native_ram_block_activation_status = EINPROGRESS;
    if (!uuid || strlen(uuid) != 36) return activation_finish(EINVAL);
    for (unsigned i = 0; i < 36; ++i) {
        bool hyphen = i == 8 || i == 13 || i == 18 || i == 23;
        bool hex = (uuid[i] >= '0' && uuid[i] <= '9') ||
            (uuid[i] >= 'A' && uuid[i] <= 'F') || (uuid[i] >= 'a' && uuid[i] <= 'f');
        if ((hyphen && uuid[i] != '-') || (!hyphen && !hex)) return activation_finish(EINVAL);
    }
    OSDictionary *matching = IOService::serviceMatching("IOMedia");
    OSDictionary *properties = OSDictionary::withCapacity(1);
    OSString *wanted = OSString::withCString(uuid);
    bool valid = matching && properties && wanted && properties->setObject("UUID", wanted) &&
        matching->setObject("IOPropertyMatch", properties);
    OSSafeReleaseNULL(wanted);
    OSSafeReleaseNULL(properties);
    if (!valid) {
        OSSafeReleaseNULL(matching);
        return activation_finish(ENOMEM);
    }

    IOLog("P17 Data activation: publishing UniXNUOwnerData after authenticated SystemOS\n");
    IOService::publishResource("UniXNUOwnerData");
    uint64_t elapsed = activation_now_ns() - started_ns;
    IOService *service = elapsed < timeout_ns ?
        IOService::waitForMatchingService(matching, timeout_ns - elapsed) : nullptr;
    if (!service) {
        matching->release();
        return activation_finish(ETIMEDOUT);
    }
    service->release();

    // Publication is only a scheduling event. Accept readiness exclusively via
    // the original typed selector (UUID, unique media, BSD fields, provider chain,
    // online seed identity). Allow BSD-client properties to finish appearing.
    int error = ETIMEDOUT;
    while (activation_now_ns() - started_ns < timeout_ns) {
        OSIterator *iterator = IOService::getMatchingServices(matching);
        if (!iterator) { error = ENOMEM; break; }
        unsigned count = 0;
        while (iterator->getNextObject() && count < 2) ++count;
        bool stable = iterator->isValid();
        iterator->release();
        if (stable && count > 1) { error = EEXIST; break; }
        if (stable && count == 1) {
            dev_t device = NODEV;
            char path[64] = {};
            uint64_t base = 0, bytes = 0;
            uint8_t digest[32] = {};
            if (native_ram_block_volume_seed(uuid, &device, path, sizeof(path),
                &base, &bytes, digest)) {
                if (device == NODEV || !path[0] || !bytes || (bytes & 4095) ||
                    bytes > UINT64_C(0x10000000000)) { error = EINVAL; break; }
                error = 0;
                break;
            }
        }
        IOSleep(100);
    }
    matching->release();
    IOLog("P17 Data activation: result=%d\n", error);
    return activation_finish(error);
}
