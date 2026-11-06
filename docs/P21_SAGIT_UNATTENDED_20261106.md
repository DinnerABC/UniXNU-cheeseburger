# Sagit A540 unattended migration — 2026-11-06

Historical no-write batch below is superseded by the real SSBO WIDTH fix and
successful64/256 shader writes, including default power-collapse regression.
See `docs/P21_A540_SSBO_WIDTH_20261106.md` and HANDOFF's latest entry. Preserve
these failed rounds as evidence; their final blocker is no longer current.

## Implemented source migrations

The chosen runtime/compiler route remains native sagit A540, not Turnip.
Sources are pinned to `ios_sagit` `673f85070fe9f7872793015eb230a5c3987d4a8c`
and its Mesa24.3.4 `769e51468b49b2a42f0a0eaf71cf9eed5ff4e5de` backend.

- `AdrenoStartup::hardwareClockGating`: all96 entries adapted to byte register
  offsets in CheeseburgerGpuClock540.h; host comparison is exact. Startup
  configure now applies it. Only selected real register readbacks were sampled.
- `QcomGPUHardware::dispatchNextJob`: CPU domain resume is engineering opt-in,
  not a prerequisite for every healthy GPMU-managed submission. The local
  default now matches this policy; failed/quarantined engines remain refused.
- `AdrenoRecovery::recover`: after the first cold reset and mapping qualification,
  enable core blocks before ME_INIT. A failed core readback prevents CP start.
  This is not a second runtime reset or power write on a live CP.
- Vendor `a5xx_enable_pc`: the SPTP power-collapse configuration is optional.
  An explicit diagnostic argument skips it at cold GPMU initialization, with
  all firmware/authentication/fence guards unchanged. Default remains enabled.
- `AdrenoPipeline::loadShader`: direct and indirect ISA load paths retain mapped
  shader backing. `Encoder::regs` supplies the count6 TYPE4 shader object packet.
- Mesa `fd5_emit.c::emit_ssbos`: SSBO NUM_UNIT is the highest enabled slot,
  rather than an unconditional16. The typed frontend now follows that count.

## Corrected actual plugin publication

Building/trusting a bundle does not cause the target to load it. The earlier
payload omitted the addon's actual bytes and the build addon omitted the plugin.
With sgai0, a persisted older bundle continued to be used.

The owner addon now contains the signed bundle executable, Info.plist and
CodeResources alongside existing owner files. The candidate payload carries
kernel.macho.gpuaddon and its ADT has the loader's zero16-byte GpuAddon range.
DarwinBoot fills that range using its existing addon loader protocol.

`0011-cheeseburger-addon-after-data-mount.patch` supports sgai2: the existing
validated install runs after Data mount has returned but before launchd, not
inside the Data mount operation that previously hung this board. It preserves
the same Data seed identity and existing overlay; no launchd cache, AMFI,
platform identity or trust enforcement is bypassed. It updates only owner
ngpu files through the filesystem, not raw device partition writes.

Actual device records confirm files5/written1/same4/failed0, and later same5.
The new plugin reaches one ABI connection, preserves the original CP_MEM_WRITE
P1 regression and runs a single shader dispatch with four additional BOs.
There is no automatic retry, free or reconnect after unproven DMA. The existing
kernel lifetime/quarantine policy owns connection-close retirement.

## Actual shader/compiler fixes

The historical handwritten store `stib.b` belongs to the A6xx ISA family. An
assembler accepting gpu540 did not prove it was a valid A540 instruction. Its
failure records remain archived; the active candidates disable that kernel
probe and no longer treat it as a shader-success criterion.

A separate pinned GLSL compiler entry invokes sagit's real A540 backend.
The standalone frontend accepts GLSL430, not450; the450 failure is retained.
Only two exact admitted constant-store sources are permitted: globalID and
localID, both one group of64 threads. Their source SSBO declarations are
composed with backend reflection as fixed Compiler::compileEntries does;
allocation, ISA, constants and sysvals are not hand-edited.

- Global shader:64 words, instrlen2,constlen28, full2/half1, branch1, localID0,
  groupID192; ISA SHA e836dfd6496b140abbaf969400d3dfd63f1e5f4defc5436e66cc5391dd8863f2.
- Local shader:64 words, instrlen1,constlen28, full2/half1, branch1, localID0,
  groupID252; ISA SHA 9eefeefa2b94619209fad900a6a21f8bd21ae91d30be0c70aef76465f17e8b05.

Actual disassembly uses A5xx stgb. Local variant has no workgroup r48 read.
Both use c24.x for exact byte bounds. These are GLSL/NIR/IR3 probes, not a
metallib/AIR frontend or a complete system Metal device.

## Healthy CP readback, not fabricated shader success

The same completed user IB uses CP_REG_TO_MEM (opcode3e from the fixed PM4 XML)
to place eight selected register values in its own scratch. CPU reads only
owned mapped memory after fence completion. The values survive in GPSTATE.

Readback confirms CS enabled, actual footprint/instruction units, shader object
IOVA, local/group ID mapping, grid64 and address mode1. It does not prove all
HLSQ internal state, constants, code fetches or descriptor contents. CP state
readback is not shader execution. No CPU writes expected output values.

## Nine device rounds and outcomes

Each unique image was booted once only after its predecessor was retrieved.
Each has a new gate, hash-pinned kernel/payload/FD, real EFI pin scan, boot
receipt and independent preserved raw32MiB window. All returned Recovery in
roughly244–246 seconds; no host reboot assistance was used. BCB was preseeded,
so this does not prove which automatic reset path ran.

| Candidate | Image prefix | Boot | Log UTC | Finding |
|---|---|---|---|---|
| sagit-hwcg-20261106T0030 | e4d498f9b89d | 3dc789cd | 20261105T170038Z | kernel CS fenced but no stores; old plugin still persisted |
| sagit-direct-addon-20261106T0120 | 566f844ad58e | 3ddb1b5e | 20261105T172948Z | actual new addon/plugin installed; user P1+probe completed, v0/64 guard960 |
| sagit-cold-core-20261106T0135 | 2c74b929c715 | 3dd05dff | 20261105T174533Z | after-reset cores on, already off before ZAP; cannot blame PAS |
| sagit-no-pc-20261106T0150 | c0ba1c542031 | 3dd1722a | 20261105T175833Z | cores remain on before/after ZAP, old probe still no stores |
| sagit-glsl-a5xx-20261106T0215 | 7047e41882e6 | 3dcd5756 | 20261105T182854Z | actual A5xx compiled globalID shader still v0/64 guard960 |
| sagit-state-20261106T0235 | 7e3d587561d9 | 3dcc5538 | 20261105T184616Z | CP reads expected CS control/object/grid/address values |
| sagit-local-id-20261106T0300 | 3615bd842157 | 3dd72953 | 20261105T190321Z | localID shader removes groupID factor; still no stores |
| sagit-real-direct-20261106T0310 | ab453e3b323b | 3dd8bd3b | 20261105T191617Z | identical real local ISA direct load still no stores |
| sagit-ssbo-count-20261106T0320 | f0a8cd55a911 | 3ddd47c0 | 20261105T193115Z | actual descriptor count1 still no stores |

The table records nine rounds, including the final descriptor-count follow-up.
All new logs contain35 rounds and0 recorded writer errors. CPU framebuffer
presentation remains2 transfers in these samples. User pipeline retains P1
values and real fences, but shader verification remains0/64 and guard960/960.
Do not report GPU rendering or Metal completion.

## Verification and remaining limitation

Host models cover packet parity/count/bounds, all-or-nothing encoding,
compiler/provenance hashes, reflected constants, mapped-resource lifetime,
20 failure injections and output/guard simulation. ASan/UBSan and arm64 iOS17.7
compilation pass. Simulation is not a device proof.

The remaining failure is shader activation or internally consumed state, not
plugin publication, ABI allocation, CP submission, instant power-domain off,
workgroup ID or the tested direct/indirect load distinction. A safe next step
requires a valid A540 internal HLSQ/constant observation channel. Sagit's CP
crashdumper example requires a healthy fully idle kernel context and retained
resources; it must not be invoked after a failed kick, against unknown address
spaces or with weakened idle masks. Nominal CPU SP/HLSQ reads are unsafe here.
No unsupported crashdumper execution or mask bypass was added in this batch.
