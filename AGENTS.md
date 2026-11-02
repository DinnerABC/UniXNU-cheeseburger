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

## Local XNU builds and NAS evidence

- The NAS checkout is authoritative for source and retained candidates. Run
  XNU preparation, compilation, object generation and linking on the local
  disk in an isolated `/Users/dinnerabc/unixnu-p21/` build tree; do not run
  the full XNU build in a NAS source tree. Use `P21_LOCAL_ROOT` and
  `P21_CANDIDATE` with `scripts/p21-compile-atos-gate.py` for P21 candidates.
- Before each build, incrementally sync current source, patches and generated
  inputs, check their hashes and resolve board/archive symlinks. Never delete
  other agents' files or concurrently use the same build tree.
- Copy the actual compiled kernel and records back to the NAS candidate and
  compare hashes. Prelink, payload, UEFI, pins and image must use the same
  round's inputs. Keep build, packaging and device results distinct.
- Reuse verified official header caches; when a download is necessary use
  proxy `127.0.0.1:7897`. Watch process status and logs rather than fixed long
  sleeps. Preserve the established VM firmware overlay and previous candidates.

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
- Validate SMMU ID2 width codes before indexing: IAS/OAS are 4-bit fields
  (codes 0..6 only); reserved codes must fail the setup, never read past the
  7-entry width table.
- Write 64-bit SMMU registers (TTBR0, ATS1PR) as a single aligned 64-bit store
  as the vendor smmu_writeq does; two 32-bit halves are not atomic.
- SMMU page tables are allocated low-first through the restricted physical
  mask, validating the returned PA (<4 GiB) and recording low/high counts; a
  mask is a request, not a guarantee. Never present a high allocation as low.
- ATOS records FSR/FSYNR/FAR before clearing them, requires both the halt
  request bit and the IDLE acknowledgement, and gates the CP kick on
  halt/resume/timeout failures.

## GPU clock / CP confirmed facts (2026-11-01)

- `gcc_bimc_gfx_clk` (GCC 0x46040) must be enabled and read back bit0=1 before
  trusting the GPU SMMU. The UEFI ClockDxe name alone did not enable it; with
  it halted, ATOS stays ACTIVE and CP kicks time out. Enable it in the GPU
  probe and record the before/after value; do not assume the UEFI clock table
  covered every DTS clock.
- The DarwinBoot low SMMU pool is one `cheeseburger-smmu-pool=<pa>` argument
  for a fixed 64 KiB `AllocateMaxAddress + EfiReservedMemoryType` range. Map
  it with `IOMemoryDescriptor::withPhysicalAddress` and
  `kIOMapAnywhere | kIOMapCopybackCache`; `IODeviceMemory` defaults make
  `bzero`'s `dc zva` fault on this board.
- Keep the ATOS gate enabled by default. The explicit
  `cheeseburger-gpu-atos-gate=0` diagnostic was only to isolate the gate and
  must not be left in a candidate. ATOS now returns 0 with the BIMC clock on.
- A successful kick means RPTR reached WPTR and the reset idle mask
  (`idleStatus`, which allows 0x80000101/0x800001c1) passes, not only
  `!(Status & ~1)`. Status3 bit24 remains part of the check.
- Startup order is ME_INIT -> GPMU upload/ready (0xbabeface) -> ZAP/PAS auth.
  Only then submit CP_SET_SECURE_MODE(0) and a CACHE_FLUSH_TS fence. A fence
  is not proven until the CPU flush/read returns the sequence; RPTR or
  GPMUReady alone are not DMA/rendering evidence.
- Boot args must stay below roughly 700 bytes; the pool uses one argument and
  the current payload is 599 bytes. Do not append a second size argument.
- A persistent log that stops is not proof the kernel stopped. A secondary-core
  panic can leave the log/console path blocked while the rest of the system
  runs and the 400 s auto-reset still fires. Record the user-visible uptime and
  treat the screen panic text as separate evidence; the panic text is not
  persisted by the current panic screen.
- Remaining known issue: `CP_AHB_FAULT` (e.g. 900009c, ME write) can be set
  after GPMU/ZAP without blocking the fence. Decode it before concluding a
  user submission failure; do not read stalled GPU MMIO on failure paths.


## GPU 用户态提交（2026-11-01 已确认）

- 用户态 ABI 采用 sheng 的 `NativeAdrenoABI.h` 原样；服务名 `NativeAdrenoGpu`，客户端类
  `AGXDeviceUserClient`，服务在 CP fence 成功后才发布。用户态进程未打开服务前，不能宣称
  P1（用户态 batch+fence）完成。
- 板级新增 IOKit 类必须加入 `p21-build-board-runtime.py` 的 `late` 集合（元类初始化移出
  `__mod_init_func`，在 publish 时显式调用）；否则会在持久日志线程启动前 panic。
- owner trust：第 0 个模块被内核当作 static，加载失败会 `panic("failed to load static
  trust cache module")`；自建模块必须放在两个 Apple 模块之后（engineering，本机实测
  0/1/2 全部 loaded）。
- GPU addon：`native_gpu_addon_install` 在 Data 挂载内部写文件会卡死（无日志、约 2 分钟
  watchdog 复位）；`sgai/sgam` 可分别关闭安装/挂载。Data 种子任何改动都会改变 seed
  identity，触发 reseed 并使日志线程约 105 s 停止；不要用改种子来绕安装。
- `CONFIG_UNION_MOUNTS` 已编入（`-DCONFIG_UNION_MOUNTS`）；VFS 的 union 查找/readdir 在
  `vfs_lookup.c`/`vfs_attrlist.c`，bindfs 只保留 `MNT_UNION` 标志、不实现 union。挂载前
  必须记录 mount flags、`mnt_vnodecovered` 与 union lookup 结果；firmlink/cryptex 可能让
  路径绕过挂载点。不要为此实现完整 unionfs，也不要再覆盖 bluetoothd。
- launchd job 目录已确认是 `/System/Library/LaunchDaemons`（boot `0x3db45895` 的 `GJOBS`
  `ld=0 ldb=0`，`com.apple.backboardd.plist` 存在）；SystemOS cryptex 路径不存在
  （`cx=2 cxb=2`）。之前可见检查的 ENOENT 是**非 union 挂载盖住下层目录**造成的，不是
  路径选错。本机 bindfs 是普通 bind（无 `MNT_UNION`），文件挂载返回 ENOTDIR；launchd
  仍运行原 job，说明挂载未影响其 job 列表/缓存。下一步查官方 bindfs 的 union 支持与
  挂载时机，不要重复“换 cryptex 路径”或“文件影子挂载”。
- fence 等待的 `IODelay(50)` 代表 50000 ns；单位换算集中在 `CheeseburgerGpuWait.h`，
  宿主测试覆盖成功/超时。用户 BO 只允许单客户端；map 拒绝 IOVA 重叠；页表更新在客户端
  锁内串行；`BoFree` 必须先通过 `cheeseburger_gpu_engine_idle()` 且 SMMU unmap/TLB
  成功才释放，失败保留对象。
- CPU BO 视图由 `clientMemoryForType` 返回 `kIOMapInhibitCache`，GPU PTE 非缓存；
  `dsb` 只作顺序屏障，不是 DMA 一致性证明，cache 属性要单独记录（`GPSV2 mapopt`）。
- 快照 rounds 可能停止，CBHB 心跳继续；内核 fence 以 CBHB 的
  `zapdone/submit/submitdone/fencedone` 标记和心跳为准，不能因快照停止就判定内核停止。
- 官方 addon 挂载在本机已证明：`bindfs + MNT_RDONLY|MNT_UNION` 后 `GADDON2`
  `flags=0x4101029 orig=0 own=0`，`GADDON3` 显示挂载路径 0:0（`BINDM_FORCE_ROOT` 生效）
  而 Data 原文件仍是 501:501；不要为此在 Data 上 chown。launchd 仍 `GPROBE exec=0`，
  剩余是 launchd 的目录枚举/plist 接受/加载时机，不是所有权。
- P1 只能说“未观察到用户态 GPU 服务连接”；`markers=0` 不等于进程未启动。必须用同一 boot 的
  `GPROBE exec`（成功 exec 计数）、marker open、`GPSVC`/`GPSV2` 的 open/refused 和
  `IOServiceOpen` 返回值分别说明 launchd 加载/exec/找到服务/open；`/dev/console` 写入与
  日志捕获在探针真正运行前不算已验证。
- BO 关闭统一使用 `CheeseburgerGpuBoPolicy.h`：引擎运行中或 SMMU unmap/TLB 失败时保留对象；
  `ClientGate` 的 generation 保证旧连接 close/free 不会清掉新连接。宿主
  `gpu_bo_policy_model` 覆盖关闭交错/旧代释放/unmap 失败。

## GPU 用户态接入（2026-11-02 已确认）

- launchd 系统域只从签名服务缓存导入作业，搜索路径不含 `/Library/LaunchDaemons`；缓存中的
  `com.apple.factory.NFQRCoded` 从未被导入。`/System` 作业即使被 XPC activity 触发，owner
  二进制替换也会被 **AMFI launch constraint（enforcing，launch type 1，“Constraint not matched”）**
  拒绝；owner trust cache 的 cdhash 不能替代平台身份。不要再尝试新增/替换 launchd plist、
  路径或缓存，也不要启用 `sgac`/`launchd_unsecure_cache`（反汇编确认其直接跳过缓存
  SecStaticCode/AMFI 校验）。
- 用户态 GPU 按上游方式：内核发布 `IOAcceleratorES`（`MetalPluginName`、
  `NativeSandboxGrantProcess=backboardd`）→ backboardd 的 Metal 发现加载
  `/System/Library/Extensions/NativeAdrenoMetal.bundle` → 插件在 backboardd 内打开用户客户端；
  放行靠官方 `zzw-native-adreno-sandbox-grant.patch`（只对服务命名的进程跳过 MACF open 检查）。
  前置是 prelink/加载 `com.apple.iokit.IOGPUFamily`，且其基类 ABI 需按本机 21H392 kernelcache
  审计，不能套用 A740 地址。
- 我们的 `NativeAdrenoABI.h` 与官方 165 行版本一致；不要为接入再改 ABI，先改服务发布形态与插件。
- 用户客户端 open 允许 root 或 mobile(501)（`GPSVC … uid=` 记录实际 uid），其他 uid 拒绝；
  单客户端门与 BO 策略不变。探针一轮完成后 idle，避免 KeepAlive/XPC activity 反复提交。
- 取证：Data 是 seed + UFS overlay。存储区在 `/dev/block/sda` 绝对 LBA 11261615（4 KiB），
  布局 = 头 1 块 + 位图 80 块 + 页区；**页 i 在块 81+i**。用 `dev-seed.dbr` 展开后按位图写回，
  读写挂载（journal 重放）才能读 `launchd.log`。步骤与证据见
  `docs/P21_GPU_USERSPACE_INTEGRATION_20261102.md`。

## GPU 用户态插件迁移（2026-11-02 实机推进）

- `IOGPUFamily` 只被 prelink 不会注册 `IOAcceleratorES`；发布 accelerator 前必须
  `OSKextLoadKextWithIdentifier("com.apple.iokit.IOGPUFamily")`，再 `OSMetaClass::preModLoad`
  + 本类 `__GLOBAL__sub_I` + `postModLoad`，最后 attach 到 `IOService::getPlatform()` 并
  `registerService()`（与上游 `GpuPowerProbe` 相同）。
- 加速器类 `AGXAccelerator : IOAcceleratorES` 依赖三个原始地址：构造/析构/元类。用本机
  prelink 镜像静态审计（vtable 与 IOService 仅 [0,1,7] 不同；getter 返回元类）后写入
  `accelerator-original-bindings.S`；Apple 组件按 donor+slide 放置，重编不改地址，但改 roots/slide
  后必须重算。不要用 A740 的地址。
- 目标 OS 的 Metal 发现要求插件 principal class 是私有 `_MTLDevice` 子类；否则属性会读、
  accelerator 会 open/close，但 `initWithAcceleratorPort:` 从不执行。插件需链接 Metal 并从
  framework 解析 `_MTLDevice`。
- 发现会先用 type 1（`IOGPUDeviceCreateWithAPIProperty`，API 值在高 16 位）打开 accelerator，
  并可能长期保持该连接；它不是 ABI 客户端。单客户端门只约束 0x4147：其它类型作为 observer
  创建，`externalMethod`/`clientMemoryForType` 一律拒绝，环/SMMU/BO 仍只属于一个 ABI 客户端。
- 发现逐进程执行：每个查询 Metal 的进程各自加载插件并调用 `initWithAcceleratorPort:`（已见
  mediaserverd）。`NativeSandboxGrantProcess` 必须是包含这些宿主的逗号列表（backboardd/
  mediaserverd/SpringBoard）；未列出的宿主 open 被 sandbox 静默拒绝，内核看不到 marker。
- 400 s 自动复位另有独立守护线程（不 IOLog、不写 UFS），在 `requested+10 s` 直接发 PSCI，
  防止日志/console 锁阻止返回；主线程仍负责 mark/smc 记录。
- 插件以 0x4147 open 客户端，走与探针相同的 P1（Query→BO→Map→Submit→Fence→CPU 回读）；
  marker 用 0x4147xxxx 的 open（低半非零）即可在无连接时记录进度。boot 日志/快照可能提前
  停止而心跳与 400 s 自动复位继续，是否执行以 marker、`GPSVC`、`GOPEN`、插件标记文件和
  Data 重建为准。
