# P22 touch boot result

## Completed

- Original UP1 log window retrieved in Recovery before the new boot:
  `out/p21/persistent-logs/20261026T130815Z`, 745 commits, zero write errors,
  maximum observed SWAP check75/reject0/transfer75.
- Complete native QUP5/RMI4/F12/HID candidate built with the official kernel and
  prelink pipeline, `-j2`; host protocol tests and packed input gate passed.
- One identity-pinned Recovery -> bootloader -> temporary Fastboot boot accepted.
  No flash, erase, GPT or system/userdata filesystem write was issued. The
  authorized runtime 32MiB persistent-log window remains enabled unchanged.

Image: `out/p22/uefi/boot-p22-synaptics-touch.img`

SHA256: `e9a41daed6a159457975d63f12eef88359c0ccfcb7306bb7085ac109cf042b56`

Kernel SHA256: `c45abaae372b56750fcc2dc26fefc94934e86480c42b14ab4cc52ef27204a7fa`

Receipt: `out/p22/boot-result.json`; transport state:
`TRANSPORT_ACCEPTED_SCREEN_PENDING`, attempts1/boot_commands1.

## Still unverified

P22 screen/UI retention, physical bus response, actual F12 layout, hardware finger
samples, interrupt routing, HID matching and real UI click/swipe/multitouch are
not yet confirmed. Transport acceptance is not runtime success. No automatic
reboot/retry follows this boot.

The default candidate uses 40Hz polling and transform3. Blue touch stage4 means
hardware/protocol discovery and digitizer creation completed; increasing frames
means successful reads/decoding, active indicates fingers, HID counts publication
attempts. These counters alone do not prove user-space delivery or UI interaction.
Stage-1 plus error records a bounded failure; the existing UI should continue.

Acceptance requires physical finger down/move/up, two contacts, and visible
response in the original UI. Preserve this run until the user reports the screen
and touch result; retrieve persistent logs next time Recovery is confirmed.

## R2: 8-core / 4GiB resource candidate

Previous P22 log was read in Recovery20261026T134118Z:230 commits, zero write
errors, SWAP transfer27/reject0, touch error-51/clock0 and no bus/HID activity.
The legal XO divider0 check is corrected in R2.

Eight-core capacity and packed inputs passed. Candidate requests4GiB contiguous
RAM, vnode66560, eight physical affinities and native SMP/self-IPI. It includes
the primary-controller startup-order correction and cross-core scanout exclusion.

One identity-pinned temporary boot was accepted by Fastboot, boot_commands1.
No flash/erase/GPT or OS filesystem write was issued. The existing bounded
persistent-log window remains the only authorized runtime storage writer.

Image: `out/p22/r2/uefi/boot-p22-r2-8cpu-4g.img`

SHA256: `4a557a88c081e88476fd3d0e4b2d2b3fa9cff2c810d5c0fea4373a4e84f7dcd5`

Kernel SHA256: `e68e543e948c871539c895e54aaae129b5a0e2ca9f2fd26c8f9270fb0f49daf4`

Runtime screen, actual4096MiB, CPU online8, per-core timers/IPIs and touch
response remain pending. No automatic repeat follows this boot.

User-requested repeat: prior log read20261026T162735Z retained the same boot_id
and230 commits as the earlier P22 run, so it does not establish new R2 runtime
progress. The identical R2 image was temporarily booted once again on explicit
user request, Fastboot accepted; receipt `out/p22/r2/restarts/20261026T162817Z/boot-result.json`. No automatic retry.

## R2 runtime failure and R3 repack

User reported `NO WINDOW 100000000 BIG RUN` followed by reboot. The official
DarwinBoot AllocateWindow prints that message after failing the4GiB contiguous
allocation, before ExitBootServices/kernel entry. The largest-run number was not
captured; do not infer a precise available maximum or an SMP/Touch failure.

At explicit user request, R3 directly repacks the identical R2 kernel with a
2GiB contiguous request and vnode33792. Eight CPUs, native SMP startup fix,
clock0 touch correction and all original OS/Data/storage inputs are retained.
No additional test suite, independent offline audit or kernel compilation is
performed for this metadata-only correction. Prior logs were read in Recovery
20261026T163205Z before the next temporary boot.

R3 one temporary boot: `TRANSPORT_ACCEPTED_SCREEN_PENDING`, image `out/p22/r3/uefi/boot-p22-r3-8cpu-2g.img`, SHA256 `494770e76dabb72e8040c6a0568eb05e11466c801810abb52e29339989de9175`. Device runtime memory/CPU/touch remain unconfirmed.

## R4 memory-only correction

User reported R3 also showed NO WINDOW and explicitly requested1GiB with
everything else unchanged, then immediate repack/boot without extra checks.
R4 changes only chosen/darwinboot-memory-size from2GiB to1GiB. The exact R3
kernel, boot arguments, CPU8 topology, vnode33792, touch code and Data remain.
The menu title is intentionally unchanged because the config bytes are frozen.
Previous logs were read in Recovery before the direct temporary-boot pipeline.

## R5 generic SMP candidate

Early ticket-lock report maps to pset0+0x100; retrieved window contains no new
R4 panic stack. Official ArmCpu remains9066b53bbc82e92a98ec44a11dd50fd13eaf1a1b.
R5 separates native16-bit reset affinity from Apple HAS_CLUSTER and uses one
generic SMP domain with capacity8, preserving1GiB/vnode33792/touch. Compile
and packed firmware completed; one temporary boot accepted, no flash or retry.
Image `out/p22/r5/uefi/boot-p22-r5-native-smp-1g.img`, SHA256 `b0657d32460b7961e3ed73fb4a86c690967855e6645263d727ef83dfb7ad083e`. Runtime lock fix and online
core count are unverified until device feedback.
