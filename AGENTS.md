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
