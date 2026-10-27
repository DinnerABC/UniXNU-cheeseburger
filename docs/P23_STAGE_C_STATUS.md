# P23 Stage C: iOS 16.7.16 (20H392)

`main` tracks the experimental iPhone10,3 / D22AP user-space candidate for OnePlus 5. The iPadOS 17.7.11 baseline is preserved at branch `ipados-17.7.11`. This is a development port, not an Apple-supported device or OS combination.

The XNU 8796 candidate and board runtime linked, and the 20H392 components were prelinked and packaged. A 75,108,352-byte boot image was sent by `fastboot boot`; the transport returned OKAY. Actual kernel progress, graphics, input, and operating-system startup have not been confirmed for this candidate. Eight CPUs and 1 GiB are configured, not yet verified on this build.

The prepared OS/SystemOS userdata image was written to the first 8,380,219,392 bytes of userdata; a short final 1 MiB was repaired. The device-side SHA-256 matched `2c2aca3feb7585c91b80ec6b50cc1888ac9315f4c77d981566de5dd902eabefa`. The 32 MiB persistent log window at the 10 GiB offset was verified unchanged. No other partition was written. The user declined a userdata backup for this deployment.

The 20H392 AMFI early initializer is a candidate inferred from the component's policy registration call chain. It and the display, HID, PMGR and storage bindings require device validation. The Apple 8796 public source build has performance-counter diagnostics disabled because its ARM KPC implementation was not provided; this does not demonstrate CPU or graphics performance.

The version-specific scripts, patch series and prelink tool live under `out/p23/ios-16.7.16-20H392/`; these source files are tracked, while generated images, proprietary inputs, keys, and device logs remain local and ignored. The staged process depends on separately obtained upstream sources, Apple inputs and the local build environment; a clean one-command build is not yet qualified. `board.toml` retains the guarded iPadOS profile for the general framework and must not be read as a completed default-profile migration.

Before another boot, recover the current persistent log in Recovery using `scripts/logging/read-cheeseburger-log.py`. Keep the first failure and report actual runtime evidence separately from Fastboot transport success.
