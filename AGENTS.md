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
