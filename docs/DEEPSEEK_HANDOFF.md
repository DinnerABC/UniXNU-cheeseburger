# UniXNU-cheeseburger: development handoff

Updated 2026-10-30. Project: `/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger`. Start by reading the root `AGENTS.md` and `HANDOFF.md`, then inspect the current branch, working tree, device and latest log. Preserve all existing candidates, VM overlays, local source inputs and the untracked `package/BoardRuntime/NativeLegacyDisplay.cpp.p23-pre-canvas`.

## Goal and current state

Continue the OnePlus 5 / MSM8998 port on branch `ipados-17.7.11`: get the original iPadOS 17.7.11 / 21H461 root to a usable interface, investigate the requested phone-style UI experiment, and bring up touch, keys, and remaining hardware. Compare the official UniXNU source and other device ports before each fix. The iOS 16.7.16 work is preserved separately in branch `ios-16.7.16`; do not transplant its OS inputs into the current iPadOS stack accidentally.

The device owner confirmed the original iPadOS interface using `out/p21/touch-window2g/uefi/boot-p21-up1.img` (SHA-256 `290291868afdf7c325438602b3ed77feabc4ebde8c922fe0839216aebc12f59f`). Its log `out/p21/persistent-logs/20261029T164350Z/` has boot ID `0x5c58198d`, `online=8`, and 39 transferred frames. Another retained snapshot of the phone-identity candidate shows `VM managed=2048MiB`. These facts do not prove stable touch, complete input support, acceleration, or long-term operation.

The last candidate sent once was `out/p21/phone-ui-d2/uefi/boot-p21-up1.img`, SHA-256 `1999371cccd22d6b5b36bea283cf596c0f04de63d91c527806da59cf88d25da3`; Fastboot Sending and Booting returned OKAY. The user subsequently reported that this candidate entered a system screen. Whether it uses the desired phone layout and whether touch or keys work are not yet confirmed; the new runtime log has not yet been collected. The host did not enumerate the target in ADB or Fastboot before that report. Ask the user for Recovery only when needed, and collect the current run before another boot.

## Immediate work

1. Recover the latest phone-ui-d2 log from root Recovery. Compare its boot ID and SHA-256 to the previous phone run (`out/p21/persistent-logs/20261029T165823Z/`, ID `0x4dd45577`). Check `DISPLAY SCALE status/calls/chosen/rotation`, display clients, `SWAP`, frame counters, UI process exits and bounded SpringBoard / backboardd stacks. The previous phone run had active SpringBoard and backboardd but `SWAP=0`, `frames=0` through 579 s. No captured fatal UI exits does not establish full initialization. Symbolicate relevant stacks against the matching iPadOS 21H461 cache before attributing a cause. A lack of a frame submission does not prove a scaling failure.
2. Phone identity is an explicitly requested experiment: the payload ADT uses iPhone10,3 / D22 / phone while the root remains iPadOS. The official `docs/guide/mobilegestalt.md` recommends matching the userland family and identity; the experiment is outside that recommendation. The D2 ADT adds `/buttons` hold/voldown/volup, `home-button-type=2`, and `thin-bezel=1`. Existing board power/volume handling and volume-key Home chord are retained. Confirm actual MobileGestalt results, display scale, gesture handling and caches before assuming these properties took effect. Do not claim a complete iOS port.
3. Touch remains nonfunctional. The last successful iPad UI run reported `TOUCH error=-54 reset=0 i2c=0/0`: GPIO89 was low before the QUP5 / Synaptics S3320 F01/F12 bus probe, so HID did not start. Setting GPIO89 input pull-up did not fix that run. Recovery device-tree evidence specifies QUP5, I²C address 0x20, GPIO87/88, reset 89 and IRQ 125. Recovery showed pm8998_l6 enabled at 1.808 V and pm8998_l28 enabled at 3.008 V; this is not proof of their iPadOS runtime state. `out/p21/phone-ui/evidence/recovery-touch-dmesg.txt` preserves Recovery initialization evidence. Study the actual OnePlus driver and power/reset sequence; do not blindly write PMIC registers or borrow another board's wiring. Diagnose reset, supplies, I²C and HID in that order.
4. Buttons are implemented in `package/BoardRuntime/CheeseburgerButtons.cpp` and `CheeseburgerKeys.h`; `NativeHidInput.cpp` and `HidInputProbe.cpp` deliver events. Physical key behavior, wake and Home mapping still need device validation. An iPhone X identity has no physical Home button; do not assume the device's physical Home sensor already works.
5. Boot speed and smoothness remain open. The proven baseline uses CPU display rendering; GPU / Metal is not qualified for Adreno 540. Compare actual first-frame timestamps, CPU online count, memory pressure and rendering/copy timings across runs. The upstream Adreno 740 implementation is not an MSM8998 driver.

## Build and artifacts

- Official framework: `.work/p21-official-framework`, pinned commit `16e592abee8a550dec8154ed1f38b5ebc4f16213`; read its `AGENTS.md` and porting docs. Reference repositories: https://github.com/UniXNU/UniXNU, https://github.com/UniXNU/UniXNU-sheng, https://github.com/MC-XiaoXiao/ios_sagit, https://github.com/edk2-porting/edk2-msm8998 and https://github.com/xVanTuring/UniXNU-qemu.
- Board runtime: `package/BoardRuntime/`, especially `CheeseburgerTouch.cpp`, `SynapticsRMI4.h`, `CheeseburgerButtons.cpp`, `NativeHidInput.cpp`, `LegacyDisplayProbe.cpp`, `NativeLegacyDisplay.cpp`, `NativeUfs.cpp` and `BootLog.c`.
- Preserved build sources, scripts and provenance: `rebuild/ipados-up1/`; local independent candidate source/build records: `out/p21/phone-ui-d2/source-tree/` and `out/p21/phone-ui-d2/`. The firmware tree uses the retained Linux VM overlay at `/home/dinnerabc.guest/unixnu-up1-phone-d2/source`; its lower layers are necessary and are not a self-contained Git checkout. Do not remove them or run historical scripts that overwrite `out/p21/upstream-a`.
- The root `board.toml` still has `example=true`; a clean one-command generic pipeline is not qualified. Build the right XNU/board archive, prelink with the matching donor, update kernel / ADT / trust / Data payload, regenerate `CompressedPayload.h` and `Expected.h`, rebuild affected EFI modules and firmware, and verify actual compiled module pins and the image roundtrip. Historical transport acceptance is not proof of a new build's runtime success.
- The 2 GiB window needs the board's captured high RAM bank and the P23-derived DarwinBoot / UEFI memory-window changes, saved in `rebuild/ipados-up1/window-fix/`. Prior attempts that changed only window size displayed `NO WINDOW`; never treat reserved RAM as conventional memory.
- User requests each change be committed. Do not add IPSWs, generated images, keys, signed inputs, private logs or the untracked other-agent file to Git. Do not start GitHub Actions automatically.

## Logs and device handling

The persistent log occupies a 32 MiB window at userdata-relative 10 GiB. Preserve it and compare boot ID and window hash before interpreting data; a repeated ID or unchanged window is old evidence, while an unchanged window alone does not prove the kernel never started.

Use the already-running single system ADB service; the host most recently had `tcp:localhost:5037`. With the target in root Recovery:

```sh
.work/p3-tools-35/platform-tools/adb devices -l
CHEESEBURGER_ADB_SERVER=tcp:localhost:5037 \
  /opt/homebrew/bin/python3.13 scripts/logging/read-cheeseburger-log.py
```

Adjust the endpoint only to the actual existing service. Results are under `out/p21/persistent-logs/<UTC>/`: retain `window.bin`, `retrieval.json`, `decoded/` and the overview. `scripts/unattended/monitor.py` is not inherently running; it can also command Fastboot back to Recovery, so inspect its behavior and avoid competing monitors. Do not interrupt a live experiment.

The user previously authorized temporary `fastboot boot`, userdata restoration, and a bounded misc Recovery marker for a specified test; the current task does not authorize arbitrary writes to boot, recovery, firmware or GPT. User allows Data cleanup when necessary: identify the exact RAM Data or cache target first, preserve the other data and log window, and avoid a blanket wipe. The last attempt to mount the copied Data seed read-only via `hdiutil` was denied; no cache was cleared. Keep identities and evidence distinct, avoid automatic retry loops, and update `HANDOFF.md` after each candidate.
