// Permissive stand-in for the "Sandbox" MAC policy (ported from the ios-sweet
// 0050 patch, same iOS 16.7.16 target). libsystem_sandbox reaches the kernel
// with __mac_syscall("Sandbox", op, arg); without a policy of that name every
// call fails with ENOPOLICY and launchd reports mach lookups as
// "error = 159: Sandbox restriction" (lsd cannot reach mobilegestalt, which
// leaves LaunchServices reseeding blocked and stalls SpringBoard/backboardd).
//
// Operations that only ask for permission succeed; the few that must return
// data (container paths, extension tokens) fail with ENOENT, exactly as the
// reference port does. The first use of every operation number is logged.
#include <mach/mach_types.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel_types.h>
#include <sys/errno.h>
#include <security/mac_policy.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>


/* The exported header declares mac_policy_register with C++ linkage in this
 * configuration, while the kernel exports the C symbol; bind it explicitly. */
extern "C" int native_mac_policy_register(struct mac_policy_conf *, mac_policy_handle_t *, void *)
    __asm__("_mac_policy_register");

static uint64_t native_sandbox_seen;

static int
native_sandbox_syscall(__unused struct proc *p, int call, __unused user_addr_t arg)
{
	if (call >= 0 && call < 64 && !(native_sandbox_seen & (1ULL << call))) {
		native_sandbox_seen |= 1ULL << call;
		IOLog ("P23 Sandbox: first op %d\n", call);
	}
	switch (call) {
	case 4:         /* container path */
	case 5:         /* extension issue */
	case 6:         /* extension consume */
	case 0x43:      /* container expected */
		return ENOENT;
	default:
		return 0;
	}
}

static const struct mac_policy_ops native_sandbox_ops = {
	.mpo_policy_syscall = native_sandbox_syscall,
};

static struct mac_policy_conf native_sandbox_conf = {
	.mpc_name = "Sandbox",
	.mpc_fullname = "P23 permissive sandbox stand-in",
	.mpc_ops = &native_sandbox_ops,
};

static mac_policy_handle_t native_sandbox_handle;

extern "C" void native_sandbox_policy_register(void)
{
	static bool done = false;
	if (done) return;
	unsigned enabled = 0;
	if (!PE_parse_boot_argn ("native-sandbox", &enabled, sizeof enabled) || enabled != 1) return;
	done = true;
	int error = native_mac_policy_register (&native_sandbox_conf, &native_sandbox_handle, nullptr);
	IOLog ("P23 Sandbox: permissive policy %s (%d)\n", error ? "NOT registered" : "registered", error);
}
