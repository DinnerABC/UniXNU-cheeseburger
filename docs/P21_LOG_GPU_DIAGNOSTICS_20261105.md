# P21 logging and GPU drain diagnostics — 2026-11-05

## Storage and safety

The log stays within the authorized userdata-relative 10 GiB, 32 MiB window.
The current 4096-byte block layout is header 0, msgbuf 1–256, 247 snapshot
slots of 32 blocks starting at 257, and 31 residual heartbeat blocks. The
heartbeat subrings remain tick22, step6 and auto-reset3. No export region,
partition expansion or GPT change is introduced by this work.

Normal workers write and synchronize the bounded UFS window. Panic paths must
not allocate, sleep, take UFS locks or write it directly. An early panic, broken
storage or a worker unable to run can leave no new persistent evidence. A last
record is a lower bound on execution, not a kernel stop time.

## Time and identity

`LOGTIME start/freq/sample` uses CNTPCT_EL0 and CNTFRQ_EL0. Start is the writer's
successful window-open time, not power-on. `sec` is integer writer-relative
seconds; raw sample ticks provide the precise common time origin. Tick records
also carry ticks/start/freq.

Wagtail's writer started near kernel54 s. Snapshot171 s therefore corresponds
to kernel225 s; heartbeat168 s corresponds to roughly222 s, with a10 s cadence.
These records do not establish that a log channel stopped unusually early.
Older records without LOGTIME use the first-commit line only as an approximate
origin upper bound; the first write/sync duration is unknown.

Only validated committed-header boot IDs are merged. Foreign boot slots,
invalid heartbeat blocks and uncommitted records are retained as raw evidence
but excluded from the effective view. Modulo overwrite makes the unobserved
prefix and tail unknowable; they are not claimed as lost writes.

## CORE2 and DRAIN

CORE2 uses separate signed rc/fence_rc and an independent fence_stage. Its
valid flags are before1, after2, result4 and fence8. Zero in a field whose flag
is absent means unexecuted/unsampled, not success. The legacy CORE record was
misleading because resume[0] was never assigned, and packed fence rc/stage bits
overlapped. New output no longer uses that representation.

DRAIN retains only values already read by the drain/resume state machine. No
extra GPU/SMMU read is issued after a failed kick. The first nonzero reason is
latched and repeated in snapshots and the independent-console heartbeat.

Stages: initial1, ack2, final3, drained4, resuming5, resumed6, preidle7.
Reasons: none0, initialInvalid1, ringBusy2, controlMismatch3, ackInvalid4,
ackTimeout5, finalInvalid6, cursorChanged7, notIdle8, status3Busy9,
resumeInvalid10, resumeControl11, resumeCursor12, PreIdleInvalid13,
PreIdleTimeout14.

DRAIN valid bits0–10 are rptr,wptr,me,vbif,mask,ackme,ackvb,status,status3,endr,
endw. A set bit means sampled; ffffffff is still invalid MMIO, not a normal
register value. Snapshot fields are live/non-atomic and repeated diagnostic
records are not fresh samples.

The text slot reserves256 bytes for LOGCOMMIT, which reports sample time,
composed length and truncated rows. LOGTIME reports cumulative snapshot and
heartbeat failures. The heartbeat remains usable after snapshot write failure,
but stops on the GPT-change guard; it still shares the UFS driver.

## Actual device findings

Logging candidate `p21-native540-log-20261105T153209Z`, image36a5e92668f4…,
boot3dda2caa, log20261105T154220Z, returned Recovery at245.4 s.35 rounds,
0 snapshot errors and0 heartbeat errors. DRAIN final stage3/reason8 recorded
rptr=wptr=endr=endw=b5, halt ACK f0f/f000f, status=c00001c1, status3=0.
The only rejected status bit was0x40000000. CORE2 valid5/rc=-4 correctly states
that no post-domain or fence sample exists. CPU presentation recorded2 frames.

The pinned Linux reference `adc218676eef25575469234709c2d87185ca223a`
`a5xx_gpu.c::_a5xx_check_idle/a5xx_idle` waits for GPU idle after CP ring drain.
The A5xx XML names bit30 GPU_BUSY_IGN_AHB_CP: it is busy, not a permission to
ignore the bit. Reference hashes match their saved sources.json.

The functional follow-up adds a bounded100 ms pre-halt idle wait, leaving the
mask unchanged. Tests cover transient busy clearing, pre-idle invalid/timeout
with no halt writes, and busy reappearing after halt remaining rejected.

Follow-up `p21-native540-preidle-20261105T1550`, image12ae023a1e8a…,
boot3ddafcd5, log20261105T160041Z, returned at245.5 s. The pre-idle gate passed,
but the same final stage3/reason8/status=c00001c1 appeared after halt. This rules
out simply adding the current pre-halt wait as a sufficient fix. Domain writes,
post-transition fence and shader dispatch were not reached. CPU presentation
recorded2 frames; GPU client opens/submits remain0. No third retry was made.

Next runtime work must investigate the halt/power-transition interaction using
applicable A540/vendor evidence. Do not widen the mask, reset a live CP or write
core domains merely to get past the gate. The native Metal compiler can be
advanced offline while this prerequisite is unresolved.

## Effective evidence and verification

The reader preserves window.bin, retrieval.json and official decoded files;
additional EFFECTIVE_EVIDENCE.json/md retain provenance and normalized times.
An early new-report warning about slots.txt differing was caused by read_text
normalizing479 CRLF pairs; original bytes exactly match the raw slots. The
analyzer now compares decoded bytes. The old report is preserved and the
corrected analysis is separately saved in the first candidate's logs.

Host validation: six GPU state/resource models PASS;12 heartbeat and17 evidence
tests PASS; real native540 CS/VS/FS compilation and malformed-input tests PASS.
Host results do not establish device shader execution or Metal rendering.
