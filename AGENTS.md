# UniXNU-cheeseburger

Workspace rules in `../AGENTS.md` cover the general order (upstream first, boot checks, log window, build/packaging chain, storage authorization, handoff). This file only records what is specific to this repository.

- The user's latest instruction is authoritative. Follow the pinned official docs
  `docs/P21_OFFICIAL_WORKFLOW_AUDIT.md` and `docs/P21_OFFICIAL_ADAPTATION.md`;
  official code lives in `.work/p21-official-framework` at the revision in
  `out/p21/official-adaptation/source-inventory.json`, or `.work/p24-official-framework`
  where a subsystem is pinned there. Read its AGENTS.md and the relevant porting
  guide before changing a subsystem.
- Preserve historical `out/pNN` candidates, raw device evidence and source pins.
  The root `board.toml` build guard stays enabled until all required migration
  inputs and recipes are qualified; a file format conversion is not a runtime fix.
- The only authorized userdata write is the bounded persistent-log window
  documented and backed up in `out/p21/r14/log-window.json` (userdata relative
  10 GiB, 32 MiB). Retrieve it in Recovery before any later boot can overwrite
  that run.
- No automatic reboot loops, GitHub Actions or pushes. Do not interrupt an active
  device experiment; every later boot needs its own offline gate and an
  identity-pinned receipt.
- Keep original/source pthread and corecrypto providers mutually exclusive. Keep
  board matching independent of OS product identity; do not borrow another
  board's addresses, IDs, storage geometry, touch controller or GPU driver.
- If delegated: at most two low-tier subagents at once, bounded read-only work by
  default. The main agent owns hardware, security and final integration.

## Unattended debug loop

Authorized for this Stage C bring-up only; not a general license for automatic
resets in normal use.

- Cross-check fixes against the pinned official framework first, then against
  reference device ports (sheng/Polaris/sailfish-class). Record the references in
  the fix receipt.
- Keep the post-boot wait dynamic: 180 s early, then 300 s and 600 s as failures
  move deeper. The value lives in `out/unattended/config.json` and in the boot
  argument `cheeseburger-unattended-reset`; change both together and rebuild the
  payload.
- `scripts/unattended/monitor.py` polls both adb servers and fastboot with
  sleeps, returns a device in fastboot to recovery, retrieves the authorized log
  window, and reports NEW_LOG / NO_NEW_LOG. Keep the session alive while it waits;
  never end a session merely because a boot is in progress.
- Early panics must be persisted to the authorized 32 MiB window before parking
  (`boot_log_panic_flush`); the normal writer starts only at `bsd_autoconf`. A run
  is only "logged" when the window hash changed.
- Stop the loop when the log cannot be retrieved or no new log appears, and
  report the exact missing evidence instead of guessing.
- The only non-log partition write authorized for this loop is the Android BCB
  `boot-recovery` command in `misc`, written by the unattended watchdog
  immediately before a PSCI reset so the next bootloader pass enters Recovery.
  GPT, boot, recovery, firmware and OS/SystemOS/Data regions stay read-only.

## Upstream cases already checked (Stage C SystemOS mount)

Record these before probing the device again; do not re-derive them.

- Official UniXNU docs describe the intended path: userdata windows OS +
  Cryptex1,SystemOS, kernel matches the root volume by UUID, then
  `offline-owner-systemos-mount` mounts SystemOS and composes the root with the
  owner cache (`upstream/UniXNU/docs/concepts/boot-chain.md`,
  `docs/concepts/userdata.md`, `docs/xnu/patch-reference.md`). The patch itself
  (`upstream/UniXNU/patches/xnu/offline-owner-systemos-mount.patch`) requires the
  root mount to be MNT_RDONLY and `native_verified_block_root_authorized(root)`;
  it returns EAUTH (80) without calling any volume query when that gate fails.
- Official upstream has NOT solved Apple root-hash authentication:
  `upstream/UniXNU/scripts/audit-target-root-hashes.py` records
  `root_authentication_solved=False`; SystemVolume/Cryptex1 root-hash inputs
  still need personalization. Do not try to synthesize `system-volume-auth-blob`
  from the IPSW.
- Same-device P21 evidence (iPadOS 17.7.11, booted to userland): the log shows
  the identical `apfs_extract_root_hash_arm: could not retrieve
  system-volume-auth-blob`, `failed to find named root snapshot: Need
  authenticator (81)` and `root hash authentication is required`
  (out/p21/persistent-logs/20261026T044653Z/.../msgbuf.txt). So those APFS
  messages are normal for this loader and are not the cause of the Stage C
  SystemOS EAUTH.
- In the same P21 log the owner mount succeeds silently (no `CacheAuth:` IOLog)
  and `apfs_log_op_with_proc ... mount-complete` is printed for the root. The
  failing 20H392 log (out/p21/persistent-logs/20261028T030018Z) never prints a
  `CacheAuth:` line either, which points at the pre-query short circuit
  (MNT_RDONLY / root_device) in native_owner_cache_prepare, not at the
  verified-block query. Confirm with a numeric panic (fail code + stage + root
  flags) before changing mount semantics.
- Reference ports (UniXNU-sheng, .work/tmp/sheng2, UniXNU-OnePlus-Ace5) have no
  separate reboot or root-mount workaround beyond PSCI/BCB; do not import one.

## Never park the device (mandatory)

A boot of ANY image while unattended must have a verified automatic return to
Recovery. A candidate that can stop on a panic screen, a parked kernel or a
userland with no automatic reset is not allowed, even as a diagnostic control.

- Operator decision (2026-10-29): a panic now **parks** its CPU after
  `cheeseburger-panic-hold` (60 s) so the screen and the persistent log survive;
  the `cheeseburger-unattended-reset` countdown is the only reset path. This is
  safe only with real SMP (`cpus=8 sheng-smp=1 armcpu-self-ipi=1`); never park a
  single-CPU image. The pre-boot gate must still assert `sheng-log=1`, the
  countdown argument inside the delivered window, and the reset symbols.
- Start `scripts/unattended/monitor.py --poll 2` for every boot, and read the
  persistent window as soon as the device returns; no sleep-based waiting.
- The monitor probes the already-listening ADB endpoints (5037 and 5038 by
  default) and never starts a second server; pass `--servers` if the host
  uses other ports.
- Cross-stage or cross-device control images (P21/P22/other boards) must be
  rebuilt with the same reset guarantees before they are booted here. If that
  is not possible, do not boot them; report the missing capability instead.
- When the device leaves adb and fastboot entirely (parked panic/userland), no
  host-side reset exists. Stop the loop, keep the USB watcher running, and ask
  the user to power-cycle (hold power ~10 s). Do not start another boot.
- After any boot that fails to return, the next run starts only after the device
  is back in root Recovery and the persistent window is read (or proven
  unchanged).

## Firmware/payload binding (mandatory)

The UEFI loader bakes its inputs at compile time: `CompressedPayload.h`
(expanded payload SHA-256 and length) under `P5Integration/RamPayload/`, and
`P9Expected.h` in both `P9Integration/Probe/` and `DarwinBootPkg/Driver/`
(kernel and ADT SHA-256). Any change to the payload, kernel, ADT, trust cache,
Data seed or `unixnu.toml` invalidates them.

- Rebuild the UEFI firmware for every payload change (`build-payload.py` → copy
  both headers into the VM tree via `prepare-uefi.py` → rebuild → export
  `MSM8998PKG_UEFI.fd` and the DTB). Never pair a new payload with an older
  firmware.
- A gate that only checks `OFFLINE_PACKAGING_PASS`/`exit_code==0` is not enough:
  require the exported firmware to differ from the previous candidate and record
  the build log and firmware SHA-256 in the receipt.
- Mismatch symptom: Fastboot OKAY, UEFI menu/shell, no kernel output, and the
  persistent window keeps the previous boot ID. Do not diagnose that as an XNU
  panic.

## Stage C owner volume UUID

The official kernel driver generates `build/xnu/owner-data-volume.h` and defaults
`XNU_OWNER_CACHE_VOLUME_UUID` to the sheng value
`8846FE78-2910-4297-AF97-D59BB6F8FC16` (iPadOS 17.7.11 SystemCryptex). The 20H392
candidate's SystemOS volume is `96096B28-B6DC-4BC7-9D3A-2632A0FE53F1`. Always
export `XNU_OWNER_CACHE_VOLUME_UUID=96096B28-B6DC-4BC7-9D3A-2632A0FE53F1` before
`stagec-build.py`, and check `strings kern_exec.o` for `8846FE78` (must be absent)
before packing. A stale object kept the P21 UUID and produced `CB owner fail=104`
with `vm=0` on every boot.
