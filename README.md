# UniXNU-cheeseburger

Device repository for the OnePlus 5 (cheeseburger, Qualcomm MSM8998 / Snapdragon 835), built on [UniXNU](https://github.com/UniXNU/UniXNU). The `main` branch targets iPadOS 17.7.11 (21H461) and XNU 10063.141.1. The experimental iOS 16.7.16 port is retained on `ios-16.7.16`.

The device owner has confirmed the original iPadOS interface on hardware, and touch input now works on the panel: the Synaptics S3320 is driven over BLSP1 QUP5 with the I2C v2.2.1 programming, F01/F12 are discovered, 80-byte multi-touch packets are decoded and published through the HID keyboard+digitizer device. A later run recorded eight CPUs online, 2 GiB managed memory and display transfers. The phone-style UI and GPU acceleration remain in development; the first frame is intermittent (a known pre-first-frame stall that varies between boots of the same image).

- [UniXNU documentation](https://unixnu.com)
- [Porting guide](https://unixnu.com/porting/)
- [Build guide](https://unixnu.com/building)
- [Development handoff](HANDOFF.md)
- [Next-agent handoff](docs/DEEPSEEK_HANDOFF.md)

## Device support

| Feature | State | Notes |
| --- | --- | --- |
| UEFI / DarwinBoot | Working | Temporary Fastboot boot; high-memory window requires board adaptation |
| CPU / memory | Partial | Eight CPUs online and 2 GiB managed memory recorded; long-term stability remains under test |
| Storage / root | Partial | Native UFS reads and owner-authorized OS / SystemOS root mounting |
| Data | Partial | Writable APFS RAM Data from a seed; persistent user Data is not established |
| Display | Partial | Original iPadOS UI appears on hardware using CPU framebuffer presentation; the first frame is intermittent across boots of the same image |
| Phone-style UI | Not working | Phone identity with an iPadOS root reached a system screen but still showed a tablet layout |
| Touch | Working | Synaptics S3320 over BLSP1 QUP5 (`qcom,i2c-qup-v2.2.1`): v2 byte tags, repeated-start register reads, F01/F12 discovery, 10-finger 80-byte F12 packets read through the IN block handshake, HID digitizer on the combined keyboard+digitizer device, 25 ms polling (attention IRQ not enabled). Confirmed by the device owner |
| Buttons | Partial | Power and volume adapter, with a volume-key Home chord; full physical interaction is not qualified |
| GPU / Metal | Not working | No qualified Adreno 540 acceleration |
| KeyStore | Partial | Development compatibility; no SEP-backed security claim |
| Logs | Partial | Bounded persistent log window and Recovery retrieval; automatic host collection requires a running monitor |
| Wi-Fi / Bluetooth / cellular / audio / camera / sensors | Not tested | No hardware support claim |
| Power management / USB | Not tested | Deep suspend, charging and XNU USB are not established |

Fastboot acceptance, kernel execution, user-space startup and usable hardware are separate milestones. The phone-style identity experiment uses an iPadOS root and differs from the matching OS/identity configuration recommended by upstream.

## Build

Use the [official UniXNU build environment](https://unixnu.com/building), the board configuration in `board.toml`, and the pinned source inputs. The device port contains board runtime sources in `package/BoardRuntime/`, board and SoC settings in `configs/`, `dts/` and `soc/msm8998/`, and local integration patches in `patches/`.

The general image pipeline remains guarded by `example = true`; a clean one-command build has not been qualified. The preserved UP1 reconstruction, input inventory, phone-identity experiment and window adaptation are under [`rebuild/ipados-up1/`](rebuild/ipados-up1/). Existing local firmware builds also depend on retained Linux VM overlay layers. See the [handoff](docs/DEEPSEEK_HANDOFF.md) and [official adaptation notes](docs/P21_OFFICIAL_ADAPTATION.md) for the current inputs and build boundaries.

Apple firmware, system images, signed policy material, private device logs and generated boot images are not distributed in this repository. GitHub Actions are manual-only.

## Device use

Development uses `fastboot boot` for temporary boots. A compatible, separately prepared OS / SystemOS installation is required; the boot image alone is not an installer. Follow the [porting guide](https://unixnu.com/porting/) and verify device-specific storage geometry, firmware, memory, MMIO and driver inputs before any device write.

## Sources

- [UniXNU](https://github.com/UniXNU/UniXNU): build framework, OS profiles and reusable components.
- [UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng): reference integration and device workflows.
- [edk2-msm8998](https://github.com/edk2-porting/edk2-msm8998): MSM8998 UEFI base.
- [ios_sagit](https://github.com/MC-XiaoXiao/ios_sagit): Xiaomi 6 MSM8998 reference; board and OS differences require verification.
- [UniXNU-qemu](https://github.com/xVanTuring/UniXNU-qemu): userland and CPU display reference.

Component and upstream notices are preserved with their sources; see [upstream notices](evidence/UPSTREAM_LICENSE).
