# UniXNU-cheeseburger

Device repository for the OnePlus 5 (cheeseburger, Qualcomm MSM8998 / Snapdragon 835), built on [UniXNU](https://github.com/UniXNU/UniXNU). The `main` branch targets iPadOS 17.7.11 (21H461) and XNU 10063.141.1. The experimental iOS 16.7.16 port is retained on `ios-16.7.16`.

The device owner has confirmed the original iPadOS interface on hardware, and touch input now works on the panel: the Synaptics S3320 is driven over BLSP1 QUP5 with the I2C v2.2.1 programming, F01/F12 are discovered, 80-byte multi-touch packets are decoded and published through the HID keyboard+digitizer device. A later run recorded eight CPUs online, 2 GiB managed memory and display transfers. The Adreno 540 kernel submission path is now device-verified end to end: a user client allocates and maps two buffers, the CP executes its command stream, the GPU writes the requested values into the client's target buffer and the client waits on a completed fence and reads them back (see [A540 user-submission path](docs/P21_GPU_SUBMISSION_20261104.md)). A540 also executes a real 2D graphics path: the kernel builds sagit-derived RB/GRAS state and a CP_BLIT solid fill into an indirect buffer, the CP clears a 256x64 offscreen target into four colours and the CPU reads back all four plus an edge sample (P21-Bittern, `match=4/4`). That is an engine-level clear, not a Metal or user-space render; IOSurface, shaders, Metal and system composition are still missing, the display remains a CPU framebuffer presenter and the first frame remains intermittent.

- [UniXNU documentation](https://unixnu.com)
- [Porting guide](https://unixnu.com/porting/)
- [Build guide](https://unixnu.com/building)
- **Porting record: [`porting.toml`](porting.toml)** — one `[[area]]` per subsystem with `status` in the official vocabulary (`planned`, `implemented`, `host-verified`, `device-verified`, `not-applicable`), plus inputs, findings, limits and next actions
- [Development handoff](HANDOFF.md)
- [Next-agent handoff](docs/DEEPSEEK_HANDOFF.md)
- [Baked boot arguments](docs/P21_BAKED_BOOT_ARGS.md)
- [A540 user-submission path](docs/P21_GPU_SUBMISSION_20261104.md) — what is verified on hardware and what is not
- [A540 2D graphics path](docs/P21_A540_GRAPHICS_20261104.md) — sagit CP_BLIT port, device result and remaining gaps

## Responsibilities

This repository owns everything specific to the OnePlus 5 and nothing that
belongs to the reusable framework:

- `package/BoardRuntime/` — built-in XNU drivers linked into the kernel: ArmCpu
  SMP glue, UFS, legacy display presentation, S6/S3320 touch, buttons, offline
  HID input, keystore compatibility, persistent log and the board probes.
- `configs/`, `dts/`, `soc/msm8998/`, `board.toml` — board and SoC
  configuration, captured device tree facts and the build guard.
- `patches/` — board-local XNU patches; generic changes stay upstream.
- UEFI board work recorded under `rebuild/ipados-up1/` (touch rail vote, NPA
  client ABI notes) and applied to the pinned firmware overlay.
- `scripts/p21-*` — the reproducible P21 build, prelink, payload, packaging and
  gate drivers for this candidate.

## Dependencies

- Pinned [UniXNU](https://github.com/UniXNU/UniXNU) framework at the revision in
  `out/p21/official-adaptation/source-inventory.json`, plus the Apple XNU source
  it checks out (10063.141.1 for this branch).
- The donor iPadOS 17.7.11 (21H461) kernelcache used for prelinking the original
  kexts, and the original trust cache.
- A prepared OS / SystemOS installation and Data seed; neither is distributed here.
- The retained Linux VM overlay layers that build the UEFI firmware.

## Device support

| Feature | State | Notes |
| --- | --- | --- |
| UEFI / DarwinBoot | Working | Temporary Fastboot boot; the firmware chain validates the kernel, ADT and trust cache and exits boot services |
| CPU / memory | Working | Eight CPUs online (`online=8`, PSCI SMC, SGI/timer running) and 2 GiB managed memory; frequency and thermal policy are not tuned |
| Storage / root | Working | Native UFS reads, owner-authorized OS / SystemOS root mounting and `launchd`; writes are limited to the diagnostic-log and Data-store windows, no general write path |
| Data | Working | APFS Data from a 1 GiB seed with a persistent overlay in userdata (LBA 13620983, 262153 blocks): changes survive reboot, GPT unchanged, `err=0` |
| Display | Partial | iPadOS UI renders on the panel through CPU framebuffer presentation; iOS renders 1242x2208 and the driver scales it to the 1080x1920 panel, now through a cacheable scanout mapping with one cache clean per frame. The first frame is intermittent across boots, brightness is unverified and the hardware MDP scanout is not used yet |
| Phone-style UI | Not working | Phone identity with an iPadOS root reached a system screen but still showed a tablet layout |
| Touch | Working | Synaptics S3320 over BLSP1 QUP5 (`qcom,i2c-qup-v2.2.1`): v2 byte tags, repeated-start register reads, F01/F12 discovery, 10-finger 80-byte F12 packets read through the IN block handshake, HID digitizer on the combined keyboard+digitizer device, 25 ms polling (attention IRQ not enabled). Confirmed by the device owner |
| Buttons | Partial | Power and volume adapter, with a volume-key Home chord; full physical interaction is not qualified |
| GPU / Metal | Partial | A540 CP started through reset/`CP_ME_INIT`/GPMU/PAS-ZAP, SMMU 4K context with `gfx3d_user` SID 0 routed, and the full user-submission path verified on hardware: BO allocate/map, client IB executed by the CP, GPU write into the client's target BO, fence completion and wait, CPU readback of the expected values, clean unmap/free (P21-Linnet, boot `0x477aef4e`). Since P21-Bittern the kernel also runs sagit-derived A5xx 2D state and a CP_BLIT solid fill: four 128x32 quadrants of an offscreen linear target come back exactly as written (`ff102030/ff405060/ff8090a0/ffb0c0d0`, `match=4/4`, boot `0x409f33da`), and P21-Starling copied that image GPU-to-GPU with sagit snapshot2D into two targets (pitch 1024 and 1088, both `match=4/4`); service-side BO import, job holds and quarantine are in place. Display handoff is constrained: the firmware GOP scanout uses a 4320-byte pitch, which the A5xx 2D engine cannot encode (64-byte units), and an 8 MiB GPU frame allocated on this board landed above 4 GiB while the MDP direct-scanout address is 32-bit. P21-Veery retried that allocation early and eight times and still found no low contiguous 8 MiB block, so the MDP flip path is safely skipped. Since P21-Serin the kernel also uses sagit-style normal scattered BO pages (no low pool): a 32 MiB buffer landed entirely at 5.27–5.33 GiB and the 2D engine still wrote and read it back 4/4; P21-Redpoll then placed the IB itself at 5.33 GiB and the CP fetched it and read a high page with CP_MEM_TO_MEM. The earlier "CP cannot reach above 5 GB" conclusion was a port artifact. The client submission chain is restored (GPSVC err=0, GBOD). The client window is now 128 MiB with a 256 MiB SMMU aperture (P21-Siskin, low pool removed), and the client chain, 2D clear/copy and the 5.1-5.3 GiB high-address CP tests all pass; P21-Twite mapped an 8 MiB 1088x1920 surface as 2040 scattered runs with the kit batch API and the 2D engine filled it 4/4; P21-Wheatear moved the service BO path to that batch API, added sagit's 16 MiB/BO and 128 MiB/client quotas, and moved the kernel test surface outside the client window. P21-Wryneck then wrote a one-shot GPU pattern into the actual compositor IOSurface (stride 5120, 80 scattered runs, 4.3–5.98 GiB) before presentation and read it back 4/4, with the client chain intact. P21-Xenops made that write per-frame but panicked reading a boot-global copy of the client IB's kernel view after the client freed that BO; P21-Yellowlegs replaced it with a per-call view passed under the job's holds, then ran the per-frame handoff on hardware (`SURF f=1 ... runs=80 stride=5120 match=4/4`, six painted swaps, frame counter 27, no panic, automatic return at 427 s). P21-Yellowhammer tried copying a window of the live UI inside the same surface but flooded the persistent log with per-run mapping lines, so its readback was unconfirmed. P21-Zenaida instead copies a 512x64 window of the live surface's own pixels into the separate 8 MiB GPU surface and reads it back 4/4 (`src=dst`, flat-colour sample); the mapper's success logs are gone, the failed-fence path retains its mapping, and the device returned at 432 s. The next target is sampling UI detail and composing behind those surfaces; shader/Metal rendering is still blocked on the SP-domain and sagit's Metal compiler path. P21-Shelduck began the sheng-style display-bank SMMU hand-off: the MMSS SMMU (0xcd00000) already has SID 0 routed to context bank 0 in the bootloader but translation is disabled (`SCTLR=0x200626`, `SMR0=0x80000000`, `S2CR0=0`), so the guarded identity takeover made no writes and the GOP stays physical; next is a free context bank with S2CR0 repointed. P21-Godwit implemented the full path behind an explicit `cheeseburger-dpu-scanout=1` (free bank, GOP identity, two 16 MiB IOVA windows, GPU-fence-ordered flips, CPU-copy fallback); the default remains the read-only probe. Two boots did not validate it: a wrong 64-byte GOP-stride check (MDP needs 8-byte alignment) was fixed, and the corrected boot black-screened (log stopped near 192 s, `frames=0`). The takeover was rolled back; the default is again the read-only probe, and the full code stays in history for a staged retry. The IR3 shader toolchain now assembles a5xx compute shaders and the kernel issues the sagit compute dispatch. All seven CS state variants fence but write nothing (P21-Lapwing); the boot log shows the RBCCU/SP core domains fall back off after ZAP (bit 20 clear), which the 2D engine does not need but a CS does. A sagit-style halt-and-resume of the core domains wedged the system (P21-Plover, log stops before the first resume line); that path was removed and P21-Sandpiper restored the bootable baseline (2D clear match=4/4, user chain err=0, automatic return). Shader writes remain unverified; the current tree does not touch the live CP power domains. P21-Wagtail (built, not yet booted) restores them in a CP-quiescent window built from the already-verified SMMU drain/resume path — the runtime PAS authenticate is what resets RBCCU/SP, while sagit only resumes a loader-authenticated ZAP — and adds an isolated constant-shader probe plus a `CORE`/`SHDR` log record; its device result is pending. Still missing: working shaders/triangles, IOSurface, Metal and system GPU composition; the display remains CPU-composed. See [A540 2D graphics path](docs/P21_A540_GRAPHICS_20261104.md) |
| KeyStore | Partial | Development compatibility; no SEP-backed security claim |
| Logs | Working | Bounded persistent log window, 2 s host monitor and Recovery retrieval; there is no live channel yet (USB CDC-ACM is only a read-only stage-0 probe) |
| Wi-Fi / Bluetooth / cellular / audio / camera / sensors | Not tested | No hardware support claim |
| Power management / USB | Not tested | Deep suspend, charging and XNU USB are not established |

Fastboot acceptance, kernel execution, user-space startup and usable hardware are separate milestones. The phone-style identity experiment uses an iPadOS root and differs from the matching OS/identity configuration recommended by upstream.

## Build

Entry point for the current candidate: `P21_CANDIDATE=<candidate>
scripts/p21-compile-atos-gate.py`. It runs the official framework
(`feeds.resolve` → `patches.prepare` → `kernel.build`) against the candidate tree
and writes the kernel, then the remaining steps run in order: the candidate's
`scripts/prelink.py` → panic-screen patch → `check-kernel.py` →
`build-payload.py` → `prepare-uefi.py` and `scripts/p2-linux-build.sh` in the
build VM → firmware export → `scan-pins.py` → `pack.py` →
`scripts/unattended/preboot-gate-p21.py` → one `fastboot boot`.

The XNU build tree lives on local disk (`/Users/dinnerabc/unixnu-p21/...`);
candidate artifacts stay under `out/p21/p21-<name>/` with the round's kernel,
payload, firmware and gate records. A full round is about twenty minutes
including the firmware build. Use the [official UniXNU build environment](https://unixnu.com/building),
the board configuration in `board.toml`, and the pinned source inputs.

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
