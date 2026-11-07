# UniXNU-cheeseburger

UniXNU device port for the OnePlus 5 (`cheeseburger`, Snapdragon 835 / MSM8998). The current target is iPadOS 17.7.11 (21H461); the iOS 16.7.16 work is retained separately.

The device has reached the iPadOS desktop with touch. A540 GPU command submission and controlled Metal/AIR rendering have been exercised on hardware. **System GPU composition is not yet working**; the known usable desktop uses CPU presentation.

## Device support

| Feature | State | Notes |
| --- | --- | --- |
| Boot and storage | Working | UEFI, UFS and APFS root boot; persistent Data uses a bounded overlay. |
| CPU and memory | Working | Eight CPUs and 2 GiB managed memory observed; frequency and thermal policy are not tuned. |
| Display | Partial | 1080×1920 panel; CPU presentation reaches the desktop. First-frame reliability and GPU presentation need work. |
| Touch | Working | Synaptics S3320 over QUP I2C; tapping, swiping and orientation confirmed by the device owner. |
| Buttons | Partial | Power and volume input with a Home chord; full interaction is not qualified. |
| Adreno 540 / Metal | Partial | User submissions, fences, 2D operations and controlled AIR-derived rendering have device results. System Metal publication triggers backboardd aborts; GPU UI composition and general Metal compatibility remain unfinished. |
| KeyStore | Partial | Development compatibility only; no SEP-backed security claim. |
| Logging and recovery | Partial | Persistent logs can be retrieved from Recovery; automatic return has worked with a pre-set recovery marker, not for every failure path. |
| Wi-Fi, Bluetooth, cellular, audio, camera, sensors | Not working / unverified | No complete device support claim. |
| Power management and XNU USB | Unverified | Deep suspend, charging policy and XNU USB are not qualified. |

Detailed subsystem scope and next steps are in [porting.toml](porting.toml); current candidate and evidence are in [HANDOFF.md](HANDOFF.md).

## Build and boot

The format-2 board profile remains guarded by `example = true`; a clean one-command build is not yet qualified. Build XNU on local disk and synchronize artifacts to the NAS as described in [AGENTS.md](AGENTS.md), [P21 build guide](docs/P21_BUILD_GUIDE.md) and [build and boot guide](docs/P21_BUILD_AND_BOOT_GUIDE.md). Use the candidate-specific kernel, ADT, payload and UEFI together.

Development uses a temporary `fastboot boot`; a separately prepared compatible OS/SystemOS installation is required. Apple firmware, system images, signing material, device logs and generated images are not distributed here.

## References

- [UniXNU](https://github.com/UniXNU/UniXNU) and its [device porting records](https://unixnu.com/porting/records)
- [UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng): framework and system integration
- [ios_sagit](https://github.com/MC-XiaoXiao/ios_sagit): MSM8998 / A540 reference
- [edk2-msm8998](https://github.com/edk2-porting/edk2-msm8998): UEFI base
