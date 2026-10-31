# UniXNU-cheeseburger

The user's latest instruction is authoritative: follow the pinned official
UniXNU documentation, adapt each requirement to actual OnePlus 5/MSM8998 facts,
complete offline checks, and only then begin a new device boot/debug cycle.

Read docs/P21_OFFICIAL_WORKFLOW_AUDIT.md and docs/P21_OFFICIAL_ADAPTATION.md.
Official code: .work/p21-official-framework at the revision in
out/p21/official-adaptation/source-inventory.json. Read its AGENTS.md and the
relevant docs/porting guide before changing a subsystem.

Preserve historical out/pNN candidates, raw device evidence and source pins.
The root board.toml build guard stays enabled until all required migration
inputs and recipes are qualified. A file format conversion is not a runtime fix.
No partition or userdata writes except the user-authorized bounded persistent-log
window documented and backed up in out/p21/r14/log-window.json (userdata relative
10 GiB, 32 MiB). All system regions and GPT remain protected. No automatic reboot
loops, GitHub Actions or push.
Do not interrupt an active device experiment. A later boot needs its own offline
gate and identity-pinned receipt. Host/static checks never prove hardware success.

After a persistent-log boot, retrieve scripts/logging/read-cheeseburger-log.py
in Recovery before another boot can overwrite that run.

Keep original/source pthread and corecrypto providers mutually exclusive.
Keep board matching independent of OS product identity. Do not borrow another
board's addresses, IDs, storage geometry, touch controller or GPU driver.

If delegated, at most two low-tier subagents simultaneously; bounded read-only
work by default. Main agent owns hardware, security and final integration.

## Commit discipline

Every source or documentation change must be committed before the round ends.
Check `git status` first, keep commits small, and record the build/device
evidence in the message or HANDOFF so any candidate can be rolled back,
compared or used as a reference. Never carry a round's changes uncommitted.
Images, private logs and signing material stay out of Git.

## Evidence and attribution discipline

- Every offline gate run writes `gate-<UTC>.json` into the candidate directory
  (`scripts/unattended/preboot-gate-p21.py`). The legacy `offline-gate.json`
  and `pins.json` at the candidate root are historical and must not be cited
  as proof for any later image.
- Freeze every round with `scripts/p21-freeze-round.py` under
  `out/p21/gpu-cp/rounds/<UTC>-<tag>/` (artifact copies + hashes, gate/pins,
  boot/monitor/retrieval references, git state). Never overwrite a frozen
  round; past rounds whose artifacts were replaced are recorded with
  `--reconstruct` and marked hash-only. Keep `out/p21/gpu-cp/rounds/INDEX.md`
  current. `out/` is gitignored: the evidence stays on the repo volume, the
  scripts and docs are committed.
- A log that stops is not proof that the kernel stopped. `CheeseburgerAutoReset`
  calls `IOLog`/`IOSleep` before the SMC, so a console/msgbuf lock can also
  prevent auto-reset; the last persisted timestamp is a lower bound, not a
  stop time. Separate "not observed" from "not executed".
- Software page-table/TLB success is not DMA validation. A successful configure
  read-back covers only the registers actually checked; do not call it
  "configure verified". Distinguish measured values from copied/snapshot values
  and record the sampling time.
- Small-sample correlations (long log line, PMRD window, one A/B pair such as
  the 257 MHz blackout) are hypotheses, not universal root causes. Re-run the
  same image before attributing, and change one variable at a time.
- Decode SMMU/CP register bits from the applicable implementation
  (`docs/upstream/reference/sagit/...`, `arm-smmu.h`) and the actual read-back.
  Do not assert fault/stall semantics that were not verified on this SoC.

## GPU/CP bring-up rules

- Every `gpuReset` failure path must end in a defined, observable state:
  keep CP/VBIF halted, mark the attempt abandoned, and never unhalt or submit
  after a failure. Validate failure branches with a host register model before
  touching hardware.
- Do not repeat CP reset/kick on a shared running system without an independent
  liveness criterion (console, the same UFS log writer and plain RAM counters
  are not independent). Prove the observation channel first.
- The CBHB block at the last block of the authorized log window is the current
  console/msgbuf-independent heartbeat (`cheeseburger_bootlog_heartbeat`, 10 s;
  auto-reset writes `mark`/`smc` before its IOLog and SMC). It still shares the
  UFS driver: prove its cadence in a healthy boot before reading a fault boot,
  and keep the console and heartbeat evidence separate.
- Do not bypass secure-mode/ZAP protections to manufacture a success; confirm
  the real firmware/ZAP premises for this SoC first.
- Keep the `gfx3d` rate at the proven baseline (rate=0) until CP/GPMU and a
  minimal DMA path are verified.
- ZAP/PAS is a required premise for CP execution on this secure-boot device
  (`qcom,zap-shader`/`ga540_zap` in the DTB; sagit uses PAS-13). Use the
  vendor ZAP and the PAS ABI, probe SCM read-only before any state change, and
  never bypass secure mode by writing `RBBM_SECVID_TRUST_CNTL`.

## ZAP loader rules (A540)

- Derive the MDT layout from the actual ELF program headers at build/test time.
  On the A540 ZAP only the relocatable `PT_LOAD` segment (0x5000, 0x1334 bytes)
  is loadable: metadata = header 0x94 + hash 0x1988 = 0x1A1C, region extent =
  0x2000 with 1 MiB alignment, code from `.b02`. The earlier 0x4000 estimate
  wrongly counted the hash segment's `p_memsz`; do not hard-code it.
- `PAS IS_SUPPORTED` (service 2 command 7) returns 0 on this GPU firmware while
  init/setup/auth availability is 1. Record it; never treat it as an
  authorization result or as proof of failure.
- Flow: `init(metadata_pa)` -> `setup(region_pa, extent)` -> copy image ->
  `dsb`/cache clean -> `authenticate()`. Fail closed before any CP reset/start.
  A fresh load does not call `BOOT_SET_REMOTE_STATE`; Android calls it only to
  re-initialize an image that is already loaded.
- After authentication the metadata and image pages stay owned and untouched:
  no CPU read, clear, free or cache operation, and no reuse for other buffers.
- `cheeseburger-gpu-start=1` always requires this ZAP chain; there is no bypass
  switch. Record the raw SMC `x0/x1` for every step, plus TrustControl before
  and after (read only).
- On this board PAS init/mem_setup accepted a 40-bit physical region above
  4 GiB (`0x111300000`) even though the MDT link addresses are 32-bit: the
  destination bound for `imageInto` is 40 bits, not the loader-era 32-bit one.
  The `physicalMask` passed to `inTaskWithPhysicalMask` is a placement request;
  verify the returned address against the reported width and fall back.
- A successful PAS `authenticate` does not by itself clear `RBBM_SECVID_TRUST_CNTL`
  on this SoC: sagit's submission allows `trust==1` only together with an
  authenticated ZAP, and Android writes the trust register only when no ZAP is
  available. Never conclude failure from `trust==1` alone and never write it.
- The RBCCU and SP core power domains (0xa887/0xa881 and status bit 20) are
  separate from the GPUCC clock vote. Android's `a5xx_pm_resume` enables them
  before any CP use; with them off every RBBM read still succeeds but the CP
  cannot execute. Read the status before writing and keep the step observable.
- Use the device vendor's ring configuration: `CP_RB_CNTL = 0x20C` (RPTR shadow
  enabled) with `CP_RB_RPTR_ADDR` pointing at a mapped 4 KiB page. Do not pair
  NO_UPDATE with an unset scratch on this board. After a kick timeout, read the
  live MEControl/VBIFHalted and the scratch RPTR; the reset snapshot is not the
  post-kick state.
- The SMMU IMPL_DEF1 micro-MMU control at `SMMU+0x6000` (DTB
  `qcom,micro-mmu-control`) can be left halted by the boot chain; a halted
  translation pipeline stalls GPU fetches without a context fault. Read
  `LOCAL_HALT_REQ` (bit 2) before CP start and clear it exactly as
  `arm_smmu_resume` does, recording both values.
- A micro-MMU resume that times out or reads 0xffffffff must abort that GPU
  initialization (`cb_smmu[0]=10`, `cb_smmu[19]<0`) and must never reach a CP
  kick; keep the host cases for intact/resumed/timeout/invalid.
- The CP scratch cursor is device-written DMA. Flush by VA (clean+invalidate)
  and barrier before reading it, and report register RPTR/WPTR, scratch value,
  the fence leaf PTE and the global/secure/CB0 fault syndromes together; a
  cached zero is not evidence that the CP did not write.
- A failed kick can leave the GPU AHB stalled: failure cleanup must use posted
  writes (no trailing DSB) and must not read GPU/SMMU registers afterwards. The
  Record already carries the live status/IRQ values; log those and return so
  the failure is observable instead of dying on the stalled bus.
- After a failed kick, capture only CPU-owned memory (the scratch cursor) and
  the SMMU register file; never read GPU MMIO there. This keeps the failure
  record complete without risking the bus that just failed.
- Same-SoC order for the GPU ZAP: take ownership (halt/SW reset), start the
  engine with CP_ME_INIT, then run the PAS init/setup/authenticate chain, and
  submit CP_SET_SECURE_MODE only before user work. The vendor driver and sagit
  both authenticate after engine start; running PAS before the reset left the
  CP unable to fetch. The auth result still gates every submission: halt and
  abandon the engine on failure.
- Before a CP kick, probe the installed SMMU tables with the hardware ATOS
  path (CB_ATS1PR at CB+0x800 = 0x8800, CB_ATSR at 0x88f0, PAR at 0x8050/54)
  and record the raw PAR plus the expected PA. A software PTE lookup does not
  prove the hardware walk; the ATOS result localizes a fetch stall to the
  context or away from it.
- Failure-path SMMU reads are unsafe on this board: a failed kick followed by
  reading the SMMU fault registers black-screened the device and stopped UFS
  and the heartbeat. Read only the CPU-owned scratch page on failure; record
  the SMMU faults on the healthy success path instead.
- Program the Qualcomm SMMU-v2 CB ACTLR (bits 28..30 set = 0x70000000,
  vendor ACTLR_QCOM_OSH/ISH/NSH) before SCTLR; the vendor context init does
  this and it affects TLB/page-walk coherency. A no-halt ATOS that times
  out falls back to the vendor hard path: set LOCAL_HALT_REQ, wait IDLE, write
  ATS1PR, read PAR, then clear the request and verify it; a halt/resume failure
  gates the CP start.
- Parse SMMU ID2 with the vendor fields: IAS bits 0..3, OAS bits 4..7 (UBS is
  bits 8..11 and must not size the input). This board reports IAS=OAS=36, so
  program T0SZ=28 and start the table walk at level 1 (3 levels). A 40-bit
  IA/4-level table on this 36-bit SMMU never completes a hardware walk.
