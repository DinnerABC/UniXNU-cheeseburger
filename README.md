# UniXNU-cheeseburger

UniXNU device repository for the **OnePlus 5** (cheeseburger, Qualcomm MSM8998 / Snapdragon 835).

**The original iPadOS interface now appears on hardware.** P21-UP1 reaches a screen showing the time with CPU rendering. Touch input is the next priority.

This is an independent device port built on [UniXNU](https://github.com/UniXNU/UniXNU). The current system is iPadOS 17.7.11 (21H461), with XNU 10063.141.1.

- [UniXNU documentation](https://unixnu.com)
- [Porting guide](https://unixnu.com/porting/)
- [Build guide](https://unixnu.com/building)
- [Current port status](porting.toml)

## Status

| Component | State |
| --- | --- |
| UEFI / DarwinBoot | Boots XNU through the device's UEFI firmware |
| CPU / memory | Single-CPU bring-up with a 1 GiB kernel memory window |
| Storage / root | UFS reads, authenticated owner root and SystemOS mounting |
| Data | Writable, volatile APFS RAM Data; resets to its seed on reboot |
| Display | Original iPadOS interface visible on the physical screen; CPU presentation |
| Touch | Not working yet; controller and transport adaptation pending |
| Buttons | Power / volume input and a volume-button Home chord; partial support |
| GPU | No Adreno 540 or Metal acceleration |
| KeyStore | Development-only non-key state/event compatibility; no SEP-backed protection |
| Persistent diagnostics | Bounded log capture and read-only retrieval in Recovery |
| Wi-Fi / cellular / audio / camera | Not supported or not validated |
| Power management | Partial; deep suspend and complete charging support are not validated |

The current UI milestone is based on the device owner's confirmation of the original screen showing the time. Full Setup/Home interaction and stability are still being brought up. Runtime details and image identities belong in [the boot comparison](docs/P21_UPSTREAM_BOOT_COMPARISON.md), rather than this page.

## Build

Use the [official UniXNU build environment](https://unixnu.com/building) and the fixed sources recorded in this repository. Board and SoC configuration live in `board.toml`, `configs/`, `dts/` and `soc/msm8998/`; integration code lives in `package/BoardRuntime/` and `patches/`.

The migration to the shared-SoC build layout is incomplete. `example = true` remains as a guard on the general image pipeline. Current hardware candidates use the staged local integration scripts (including `scripts/p21-up1/`); a clean, one-command end-to-end build is not yet qualified. See [the integration notes](docs/P21_OFFICIAL_ADAPTATION.md) for the current workflow and prerequisites.

Apple OS images, generated kernels, signed policies, private device captures and build outputs are not distributed in this repository. GitHub Actions workflows are manual-only.

## Device use

Development images use `fastboot boot`. A compatible, separately prepared OS/SystemOS installation is required; a boot image alone does not install iPadOS. Existing device preparation may replace Android userdata. There is no general-purpose installer or supported rollback procedure here.

Do not reuse another board's storage layout, MMIO addresses, device identity or signed policy. Follow the [porting guide](https://unixnu.com/porting/) and adapt them to the actual device.

## Sources

- [UniXNU](https://github.com/UniXNU/UniXNU): build framework, OS profiles and reusable components.
- [edk2-msm8998](https://github.com/edk2-porting/edk2-msm8998): device UEFI base.
- [UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng): integration and runtime component references.
- [UniXNU-qemu](https://github.com/xVanTuring/UniXNU-qemu): same-build userland and CPU display reference.
- [UniXNU-Pagani_SM8750](https://github.com/IssacL04/UniXNU-Pagani_SM8750): P21-UP1 development launch-policy reference.

Pinned revisions, local adaptations and test boundaries are recorded in [upstream integration](docs/P21_UPSTREAM_INTEGRATION.md) and [the UP1 experiment](docs/P21_UPSTREAM_PATCH_EXPERIMENT.md). Component and upstream license notices remain with their sources; see [upstream notices](evidence/UPSTREAM_LICENSE).
