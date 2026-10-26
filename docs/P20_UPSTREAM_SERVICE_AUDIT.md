# P20 R10 upstream service and runtime composition audit

2026-10-25, Asia/Shanghai. Two gpt-6-luna read-only agents reviewed the upstream service chain and libobjc/SystemOS path history. The main agent reviewed effective compiler macros, original filesystem contents, kernel authorization and patches, and performed the candidate implementation/tests. No agent changed a device or replaced a userland service.

## Principal finding

R9 user screen transcription records SpringBoard14 selected fatal exits, namespace4/code1, a successfully read but160-byte-limited reason starting `couldn't dlopen libobjc-trampolines.dylib`, and zero swap checks/transfers. Nonzero exits127–134 are selected raw wait/reason data; total397–417 includes normal exits and is not a crash count. The transcript has some OCR/typing ambiguities; the complete dynamic-loader error is not available.

The original target SystemOS has `/usr/lib/libobjc-trampolines.dylib` (68080 bytes, SHA256 `8089b4432232e56464ce724e1ef334e5b95dfc0eaa33eacc8eb2a76b669744d4`), matching upstream's exact target. R9 actually linked the frozen P19-R2 kern_exec object. Replaying its command's preprocessor proves UNION_SYSTEMOS is undefined and CRYPTEX_FILES=1: SystemOS is mounted at `/private/preboot`, and init receives dyld cache/fallback environment strings. A service may still fail to find original runtime-loaded libraries under this arrangement; it is not evidence the firmware omits them.

Official UniXNU scripts/check-native-ram-data-checkpoints.py:56–69 records the same missing libobjc abort in its earlier RAM-Data stage. Its later check-owner-union-checkpoints.py:18–57 documents native union composition and original libobjc HEADER-page validation. That script does not prove executable-page acceptance, UI or cheeseburger hardware. Upstream QEMU report fixtures needed to execute those whole checkpoint scripts are absent here; they were audited, not represented as rerun.

R10 adopts the existing owner-cache union branch, with XNU_OWNER_UNION_SYSTEMOS=1, CRYPTEX_FILES=0 and old cache-carrier exception compiled out. It also copies the two official union lower-directory/getattrlistbulk patches. Root and cache UUID/provider/readonly/authorization checks, root graph checks, root recheck, manifest publication, original code/signature checks and fatal refusal paths remain. This changes runtime composition rather than copying or patching libobjc or service executables.

## Service/dependency review

| Layer | Original services/components | R9 evidence | Action and boundary |
| --- | --- | --- | --- |
| Startup and code loading | launchd, authenticated OS, SystemOS/dyld, writable RAM Data | launchd alive; separate SystemOS path mode compiled | R10 native readonly union candidate; actual ROOT values still require screen evidence |
| User management | usermanagerd, keybagd, container services, Data skeleton | usermanagerd persona abort absent since R8; keybagd/container processes selected | Keep R8 directory-only Data seed exactly; do not fabricate personas/keys or claim complete DataProtection |
| GUI process lifecycle | runningboardd, lsd, securityd, mobileassetd, logd | lsd selected; other roles not all sampled | Original jobs/binaries already present; fix actual load/IPC failures before adding replacement services |
| SpringBoard | original SpringBoard and runtime-loaded Objective-C libraries | 14 selected fatal exits; libobjc dlopen reason | First repair candidate is native SystemOS runtime namespace; stable SpringBoard not yet demonstrated |
| Widget/activity startup | chronod -> liveactivitiesd; ActivityKit/keybag event capability | absent in selected process snapshots | Inspect after loader fix; historical QEMU MKBEventsRegister NULL is not a proved local failure |
| Display server and first frame | backboardd, QuartzCore/IOSurface/IOMobileGraphicsFamily, NativeLegacyDisplay CPU backend | opens4, frames0, SWAPchecks0 | Preserve original display code and stage counters; no evidence of a rejected/copying frame yet |
| Setup, interaction and other apps | Setup/lockdownd/profiled, input, app cryptex | not qualified by current snapshots | Separate later functionality; upstream's skip-setup/stand-in choices are not automatically copied |

The upstream `check-springboard-process.py:25–35` presence checklist is launchd/backboardd/SpringBoard/runningboardd/usermanagerd/lsd/securityd/mobileassetd/logd plus writable Data. It is a process checkpoint, not an exhaustive GUI dependency graph. `qemu/README.md:112–118` records the historical SpringBoard -> chronod -> liveactivitiesd wait/crash chain; that experiment used a KeyStore event stand-in. We did not sync that stand-in, fabricate key state, disable unrelated jobs or suppress their original failures.

Original-job inventory is in out/p20/r10/evidence/original-service-jobs.json, including labels, MachServices, KeepAlive configuration, program hashes and Mach-O dependencies. Static presence/configuration is not proof a job starts or handles its requests successfully. The second read-only pass also verifies program paths remain inside the mounted OS and build platforms are iOS; consult its recorded fields for completion.

## Upstream provenance and tests

Fresh upstream FETCH_HEAD: `c62bf12595f9a86c1b7282cf12d5af40b441fdac`. The union patches and relevant checkpoint scripts are identical to the pinned checkout. The baseline build script changed for Linux host support; its union configuration block is unchanged, saved diff in evidence/upstream-build-script.diff. We did not replace the working Mac build framework or whole checkout.

Actual-function ASan/UBSan tests: 11 union-startup cases and11 union-enumeration cases with mount/vnode/permission APIs explicitly mocked. They cover no fallback env, readonly union flags/root graph, missing/wrong media, permissions/MAC, copyout and manifest failures, descriptor ownership and EOF handling. They do not emulate original APFS on this board. Actual source objects compile; final kernel retains all30 original Apple executable component hashes, authenticated root/manifest/Data policy checks, R8 Data bytes and R9 two-page diagnostics. Old carrier symbols and fallback-path literals are absent from new compiled objects. Reports: tests/union-startup.json, tests/union-enumeration.json, qualification/upstream-union.json and offline-gate.json.

## Hardware validation

Only one new-candidate temporary boot is allowed when the fixed device is confirmed in Recovery. Package, bootloader transport, native union visibility, stable services, first successful pixel transfer, visible original UI and interaction are separate evidence levels.

R10 page1 ROOT should show union=1, cache=0, readonly/auth/recheck=1; mount flags must contain0x21 (readonly|union), not necessarily equal0x21. Then inspect SpringBoard's retained abort count/reason and SWAP/PIXELS/frames. A disappearing libobjc abort would validate that blocker only; other service failures must still be grouped by their actual common reasons rather than silently bypassed.
