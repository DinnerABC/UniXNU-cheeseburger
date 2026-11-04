# A540 user-submission path (2026-11-04)

Status of the Adreno 540 bring-up after the P21-Linnet round. Every statement
below is backed by a device run; the round artifacts and raw logs are named so a
later reader can re-check them.

## What is confirmed on hardware

| Step | Evidence |
| --- | --- |
| Kernel GPU startup (reset, `CP_ME_INIT`, GPMU upload, PAS/ZAP) | `CB GPU reset rc=0`, `gpmu OK ready=babeface`, `CB GPU ZAP: ok`, fence `seq=1` |
| Accelerator published, client opens it | `CB GPU open: ok … name=mediaserverd`, `GPSVC open=2` |
| Two BOs allocated and mapped through the SMMU 4K context | `GPSVC bo=2 map=2`, `GBOA0/1 iova=4000000/4001000 rc=0` |
| Client command stream executed by the CP | `GBOD 0:703d8003/4001000/…`, gate `C rc=0 cp=703d8003 kview=703d8003` |
| GPU write into the client's target BO, CPU readback | `GBOD 1:adc0de1/feed0002/600d0003` — exactly the three values in the client IB |
| Fence completion and wait | `GPSVC sub=1 fence=1 wait=1 err=0`, client report `P21 plugin done s12 e0 v4` (4/4 checks) |
| BO unmap and release | `GPSVC unmap=2 free=2 err=0` |
| GPU-authored pixels on the panel | four 24-row bars written by the CP into the firmware scanout, middle row of each reads back the written colour (`CB GPU fill rc=0 bars=4 rows=24 ibs=24 px=ff00ff00/…`), visible on the panel (P21-Oriole) |

Round: `P21-Linnet`, boot `0x477aef4e`,
log `out/p21/persistent-logs/20261104T110602Z/`, image
`d496059936e5c6d33b27f55d97c0fbf82b328e2108659eac05422b610719bd21`, kernel
`1282e4a4f6677e61c3d4f271459a565450840018ec230e8891b745584d825276`.

## Scanout fill

`cheeseburger-gpu-fill=<argb>` paints bars into the buffer named by
`PE_current_console()` — the firmware console frame buffer the panel scans,
the same memory the boot text and the early stage bars use. The scanout pages
are mapped once, before the submission lock is taken, because
`cheeseburger_gpu_smmu_map` takes that same non-recursive lock through
`cheeseburger_gpu_mapping_begin` (mapping under the lock deadlocked a boot).
The rows are then written by `CP_MEM_WRITE` inside the lock, a few rows per IB,
each IB carrying its own fence. The bars last only while nothing repaints the
frame buffer; a periodic repaint is the next step.

## What is not implemented

- GPU rendering: no draw, no shader, no IOSurface, no Metal. The display path is
  still the CPU framebuffer presenter; `frames` counts presentation transfers,
  not GPU composition.
- The verified submission carries a probe IB written by `adrenoprobed`. Apple's
  graphics stack does not yet produce real command buffers for this driver.
- Preemption, multiple rings, the secure (SID 2) context and the SMMU fault IRQ
  are not exercised. `SIRQ count=0` in every round; the fault path is untested.
- Power management: the core domains are left up for the boot lifetime.

## Findings worth keeping

- The CP power-up command (`0x778000` to the RBCCU/SP registers) may only be
  issued while the CP is stopped. Re-issuing it after ZAP stops the CP within a
  millisecond and a following doorbell causes an unclassified kernel abort
  (P21-Quail). All later samples are read-only.
- A stream whose first indirect buffer contains no words is not executed by this
  CP and never reaches its timestamp packet. Such a submission is now completed
  on the CPU instead of being kicked.
- `VBIF_HALT` (0x3080) reads `0xf0000` when idle: bits 19:16 are per-client
  status, only bits 3:0 are the request. A whole-register comparison against
  zero is never true on this SoC.
- A low-pool BO is one page of a shared 32 KiB block, so the kernel view must be
  taken at the block base plus the page offset; `getBytesNoCopy()` alone points
  at the wrong page and used to hide every client write.

## Next steps

1. Minimal pixel fill: encode an A5xx draw or copy that writes known pixels into
   a BO, then read them back with the same `GBOD` mechanism.
2. Move from the probe daemon to the Metal frontend so the submitted streams
   carry render commands, and verify with the same records.
3. Re-test sheng `NativeAdrenoVm::invalidateTlb` (a bank TLBIALL before every
   submit) now that the submission path is usable.
4. Exercise the SMMU fault IRQ path and the secure stream before any workload
   that could fault.
