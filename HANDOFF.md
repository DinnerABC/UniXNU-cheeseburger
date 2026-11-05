## 2026-11-05：P21-Wagtail（构建完成、**未启动**；SP 域恢复 + A540 shader 执行路径）

> 用户在本轮启动前要求停止设备操作并交接给下一位 agent。**本轮没有 fastboot，
> 也没有写 misc。设备仍在 Recovery（`3b25cc7c`）。** 下一位按下面第 5 节执行即可。

### 一、根因与 sagit 对照（本轮新增结论）

- sagit（`MC-XiaoXiao/ios_sagit` @ `673f85070fe9f7872793015eb230a5c3987d4a8c`，2026-11-05 重新 fetch 仍为最新）
  的 `QcomGPUSecure.cpp` 只对**引导器已认证**的 ZAP 调用 PAS **resume**；内核从不执行
  `authenticate`，所以 GPU 核心域不会被重置。
- 一加 5 的固件不做预认证，我们在运行时执行完整 PAS（`init → mem_setup → authenticate`）。
  authenticate 会重置 GPU 核心域；P21-Lapwing 的 `rbccu=778801/1 sp=778001/1` 正是该步之后。
  这解释了两件事：2D（RB/RAC）一直正常，而 CS/VS/FS 只 fence、不写。
- 恢复方式与 Android `a5xx_pm_resume` / sagit `enableCoreBlocks` 相同：**CP 停止时**写
  `0xa887=0x778000`、`0xa881=0x778000`。活动 CP 上写会停 CP（P21-Quail）。
- 本轮复用本板已实机验证的 `drainForTables/resumeAfterTables`（每次 SMMU 表更新都在用）
  作为停机/恢复窗口，避免 P21-Plover 自写 halt 路径。

### 二、本轮代码（已提交）

- `package/BoardRuntime/CheeseburgerGpuCp.cpp`
  - 新增 `resumeCoreDomains()`：取 `gSubmitLock` → 采样域状态 → `drainForTables`（停 CP/VBIF、
    空环、idle）→ **posted** 写 RBCCU/SP 电源命令（各 3 ms）→ `resumeAfterTables` → 解锁后
    才读状态。halt 期间不读任何 GPMU/GPU 寄存器。失败即 quarantine，不再提交。
  - compute 测试改为 opt-in：必须 `cheeseburger-gpu-compute=1`；测试内先做上述转换，再用
    `keepAliveInline()` 发一个 fence 证明 CP 存活（失败即 quarantine）。
  - 私有 SSBO 第 7 页新增独立常量探针（`Probe540` 写 `0xdeadbeef`），与扫描带分开取证。
  - 探针新增 `CB GPU args: len=` 行，记录内核命令行真实长度（本轮 args 697 B + 运行期池参数）。
  - `native_adreno_compute[16]`（新增 [12..15] 常量探针）与 `native_adreno_resume[6]`。
- `package/BoardRuntime/BootLog.c`：新增持久行 `CORE rbccu=a>b sp=c>d rc=… fence=…` 与
  `SHDR rc/w/px`；`CMPT`/`CPRB` 不变。
- `package/BoardRuntime/CheeseburgerGpuService.cpp`（生命周期复核后的具体修正）：
  - `clientMemoryForType` / `BoCpuUnmap` 拒绝 imported BO（ABI 明确 imported BO 无 CPU token）。
  - `BoImport` 拒绝无 `task_` 的客户端。
  - `BoMap` 拒绝与“unmap 失败而保留的 IOVA”重叠的请求，避免覆盖仍可能被 GPU 使用的 leaf。
  - `clientClose()`/`free()` 与 `run()` 用同一把客户端锁串行 BO 表。
  - 未改：job holds、失败保留、fence 后退休、每次提交 TLBIALL。
- 文档：`docs/P21_A540_SHADER_20261105.md`（sagit 对照表、状态机、取证字段）。
- 未改动：启动/显示/触摸/UEFI/固件池/Data seed/日志路径；固件 DarwinBoot 仍为 16 页池。

### 三、构建（全部完成，离线门禁 PASS）

| 项 | 值 |
| --- | --- |
| 候选 | `out/p21/p21-wagtail/` |
| 内核（入口补丁后） | `f8ab89ca72dcb46d8062bce3e9d6deb16f6938434a05a04b3fec6c2af8634548`（126,070,784 B） |
| native（unstripped） | `f77b7ddd998ea4be2b5f935a419260e357f462f22c41e600b1cefb9f73c22613` |
| prelink | `24b7935167bbc128cf44abaece88ceadcae2814a36de66c0331cbc86f24ba0c9` |
| payload 展开 / 容器 | `b5795f8f58f57bdc7c92e841dde93c7255335bb628e417ada41282de932dd134` / `8a095b7f01edeb31b993c55bd2a280aa6bc8e152ce8ddf9b3c564dc0df947bbb` |
| 固件 FD | `a8a146a8a70bd35cd5ab588fb06690f76d0c3081ff03bc9052257266263fe3c7`（6,291,456 B，overlay `unixnu-up1-gpu-smmu-irq-drain`） |
| 镜像 | `out/p21/p21-wagtail/uefi/boot-p21-up1.img` `c7387f79732fd49c6af7848fca83f86d207cf8e15f033f3dbc4b84c7b3073126`（82,800,640 B） |
| gate | `20261105T135206Z` PASS，`args_bytes=697`，kernel/payload/ADT 三项 pin 命中（`FVMAIN.Fv` 等） |
| 构建冻结 | `out/p21/gpu-cp/rounds/20261105T135645Z-20261105T135206Z-p21-wagtail-unbooted/`（无 boot，boot/retrieval 为空） |

唯一相对基线的启动参数变化：`cheeseburger-gpu-compute=1`（加在 args 串第二位）。默认不设该参数
时，本轮内核与基线的运行行为一致（compute 不再默认执行）。

### 四、下一位启动前必须核对

1. 设备 `3b25cc7c` 处于 Recovery；上一轮日志 `out/p21/persistent-logs/20261105T130600Z/`
   已回收（控制启动 `0x42238bf5`），启动前无需再读。
2. 先跑 `scripts/unattended/preboot-gate-p21.py out/p21/p21-wagtail`（已 PASS；重跑需新 receipt）。
3. misc BCB（`/dev/block/sda4` 首 4 KiB 的 `boot-recovery\0`，保留其余字节）需要用户逐次授权；
   本轮**未写**。写入后必须 push→dd→回读，确认回读非零且首字节为 `boot-recovery`，否则不启动
   （P21-Skua 的教训）。
4. `adb reboot bootloader` → 确认 `fastboot devices` 看到 `3b25cc7c` → 只执行一次
   `fastboot boot out/p21/p21-wagtail/uefi/boot-p21-up1.img`，保存 `Sending/Booting OKAY` 与时间。
5. 观察窗口 230 s（约 262 s 自动回 Recovery）；若日志到 ~110 s 就停且无 USB，按第 6 节手动恢复。

### 五、启动后要看的字段（按顺序）

- `CB GPU args: len=…`（新）：确认真实命令行长度与是否截断。
- `CB GPU power: before … after …` / `after-zap rbccu=778801/1 sp=778001/1`：确认转换前域是关的。
- `CB GPU core: resume rc=<0|1> rb=a>b sp=c>d`：`b`/`d` 的 bit20（`0x100000`）都应置位；
  `rc=1` 表示本轮写了电源命令，`rc=0` 表示本来就开着。
- `CB GPU core: fence rc=0 stage=6 fr=…`：转换后 CP 仍执行；非 0 表示引擎已 quarantine（系统仍可用）。
- `CB GPU compute: var 0 sagit rc=0 … late=deadbeef` → 常量探针 `late` 应为 `deadbeef`；
  `CB GPU compute: probe rc=0 … px=deadbeef/deadbeef` 是独立 SSBO 证据。
- `CB GPU compute: band rc=0 … match=4/4`（采样值 = `id<<8`）：CS 写进 4320 pitch 扫描带。
- 持久快照应出现 `CORE`、`SHDR`、`CMPT`、`CPRB`；回归看 `CLR/CPY/BIG/SURF` 仍 `match=4/4`、
  `GPSVC … err=0`、`GBOD 1:adc0de1/feed0002/600d0003`、`DISPLAY frames>=2`。
- 启动后立即用 `scripts/logging/read-cheeseburger-log.py` 回收，比较 boot ID 与窗口哈希，
  再跑 `scripts/p21-freeze-round.py <UTC>-p21-wagtail --candidate out/p21/p21-wagtail --boot-log … --retrieval …`。

### 六、失败签名与回退

- 日志停在 `CB GPU core: resume` 之前/之后且无 USB：转换窗口不适用于本板（最可能的新风险）。
  手动长按电源回 Recovery，不重复启动同一镜像；保留日志，回退源码到 `p21-bootable`，
  下一轮把 `cheeseburger-gpu-core-resume=0` 作为对照（compute 仍会 fence、不写）。
- `resume rc=-4`：drain 前置条件失败（环不空/状态非 idle/ME 或 VBIF 未停）→ 引擎已 quarantine，
  系统应继续运行；查该时刻的 `pre pwr` 行。
- `rc=-5`：resume 失败 → 引擎 quarantine；说明 halt 后 VBIF/CP 没有恢复，不可再提交。
- `rc=-6`：写了命令但状态 bit20 仍为 0 → SP 未上电；CS 不会写，但 2D/用户链不受影响。
- 显示/客户端回归失败但 compute 正常：整体回退到本轮的 2D 基线（源码 HEAD 的 `p21-bootable` +
  `cheeseburger-gpu-compute=0`），不要只删 compute 的一半代码。
- 已知可启动控制镜像：`out/p21/p21-zorzal/logs/control-cde78c8c.img`。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | **P21-Wagtail** | `c7387f79…` | `f8ab89ca…` | SP 域恢复 + shader 执行路径（opt-in）；构建/gate PASS；**未启动**，待实机验证 |

## 2026-11-05：停止修改，回滚到可启动分支 p21-bootable（已登记）

- 两次共享固件低位池的镜像（`07a20f5c…`、`f798d565…`）均在 230 s 后无 USB/无 fastboot；用户确认第二次仍无蓝/品红条。
- 失败启动之后回收的持久窗口仍是**控制启动** `0x42238bf5`（`out/p21/persistent-logs/20261105T130600Z/`），说明失败镜像的内核日志 writer 从未运行——故障在 panic-screen/日志启动之前（kext 加载期的池映射/占用），不是 GPU/DPU 探针。
- **停止后续功能修改**。可启动基线：
  - 源码分支 `p21-bootable`（与已验证控制镜像同一份源码；只比 `c832bc6` 多本 HANDOFF 记录）。
  - 已验证镜像 `out/p21/p21-zorzal/logs/control-cde78c8c.img`（固件 DarwinBoot 16 页池、内核 HEAD/halt-only、无池共享、无 SMMU 写入）；该镜像控制启动 clients=23、frames=2、230 s 自动回。
- 固件 DarwinBoot 保持 `PoolPages = 16`（64 KiB）；不要扩大、不要在 kext 构造期触碰该池。
- 设备当前在 Recovery。

## 2026-11-05：低位池共享两次尝试均黑屏/无 USB（已回退；停止早期取池）

- 第一次：固件池 16→64 页（256 KiB）+ DPU kext 构造期取 8 页 → 230 s 后无 USB、蓝/品红条未出现，故障在 panic-screen 入口之前（固件/极早期）。
- 第二次：固件池保持 16 页（`0xfff40000/65536`，已由 GPU SMMU 正常使用），仅 DPU 在 kext 构造期从同一池取 8 页 → 同样在 230 s 后无 USB/无 fastboot。
- 结论：**在 kext 构造期映射/占用固件低位池会破坏启动**，与池大小无关；两条路都已回退。内核源码回到 `0496374`（halt-only、无池共享、无 SMMU 写入）；固件 DarwinBoot 保持 16 页。
- 已验证的控制镜像：`out/p21/p21-zorzal/logs/control-cde78c8c.img`（固件 16 页、内核 HEAD）；控制启动显示正常、clients=23、230 s 自动回。
- 设备两次均需手动切回 Recovery。
- 下一步（如果继续）：不要在 boot/构造期碰池；把“取池页”放到 GPU 探针之后（约 85 s，GPU SMMU 已映射并记录 gPoolUsed），或由 GPU SMMU 在完成自身表分配后显式转交剩余页；每次只做单变量并先控制启动。

## 2026-11-05：固件 64 页共享低位池尝试失败（黑屏/无 USB，已回退）

- 固件 DarwinBoot 原本已在 ExitBootServices 前预留 **16×4 KiB（64 KiB）** `EfiReservedMemoryType` 低位池，并通过 `cheeseburger-smmu-pool=<pa>` 交给内核（GPU SMMU 消费）。
- 本轮把固件池扩到 **64 页（256 KiB）**，并让 DPU 在 kext 构造期先从池中取 8 页；内核 GPU SMMU 的池大小同步改为 262144。
- 实机（镜像 `07a20f5c…`，12:23:34Z 启动）在 230 s 后**没有回到 Recovery，也没有 fastboot/ADB，USB 上完全不可见**（黑屏/挂死）。无法取日志；判定该共享池改动不安全。
- 已回退：固件 DarwinBoot `PoolPages` 恢复 16；内核 `CheeseburgerGpuSmmu`/`CheeseburgerDpuScanout` 恢复 HEAD（halt-only、无池共享、无 SMMU 写入）。源码回到 `0496374` 的安全状态。
- 设备需要手动切回 Recovery；下一轮先确认设备可用，再考虑更保守的低位池方案（例如固件池只扩到 32 页并让 DPU 只取 2 页，或完全由 GPU SMMU 统一分配后转交），且必须先做控制启动。

## 2026-11-05：P21-Zitting（XIN halt/DMAHalt 移植成功；handoff 仅剩低 4 GiB 表页分配阻塞）

- **XIN halt/DMAHalt 已验证**：按 sagit 的 `solePipeline`/CTL/LM 校验、`0xc9b0200/0xc9b0204` halt/ack、`0x12ac` clock；多次启动均为 `resumed=1`、`halt=0`，显示正常（clients=19–25、frames=2–3、GPSVC err=0、230 s 自动回）。
- **handoff 校验器已定位并修正**：`why=7` 对应“表 PA 超出输出位宽”；MMSS SMMU 的 ID2 OAS=32，所有页表必须低于 4 GiB。日志里的 `table=a90f000` 只是低 32 位，原表池在高位。
- **当前阻塞**：低 4 GiB 的 32 KiB 连续池、以及逐页 4 KiB `inTaskWithPhysicalMask(...,0xffffffff)` 在 ~85 s 全部返回失败（`tables=0`），说明此时低位内存已耗尽/不可分配。halt 与 resume 仍安全，未写任何 SMMU 寄存器。
- 下一步：在更早的时间点（显示驱动 start 或早期内核线程，约 20–40 s）预留 6 页低位表页；或在固件/引导阶段预留。拿到低位表页后，即可在已验证的 halt 窗口内写 CB0 身份映射并 resume。

### 旧记录
- 首版 handoff 的 `ok=0` 已由 `why` 位掩码定位为表 PA > 4 GiB（OAS=32）。


- 按 `upstream/ios_sagit/drivers/display/QcomDisplay/DisplayDMAHalt.h` 逐字移植 MSM8998 版本：MDP 版本 `0x30000000`、VIG0 `0x5000`、CTL0 `0x2000`，**XIN halt `0xb0200/0xb0204`**、Clock `0x12ac`，并执行 `solePipeline`/CTL/LM 全量校验；不再使用上一轮的 AXI halt 变体。
- 同时在内核中实现 sagit 同属性的 handoff 契约（禁用 CB、已发布根表、32 MiB 预留窗口 <4 GiB、framebuffer PA/DA、WB 表、上下文格式校验），本轮只构建/校验，不写 SMMU。
- 实机（boot `0x412ee552`，日志 `out/p21/persistent-logs/20261105T112718Z/`）：
  `CB GPU dpu halt handoff rc=-10 st=3 pipe=4000 size=7800438 stride=10e0 xin=0 ok=0 tables=6 bytes=8294400 resumed=1 sctlr=30000001 clock=c0000ccc halt=0 first=9f400000/a1400000 pa=9d400000 da=9d400000 table=a88a000 ctx=2020/1027/802020`。
  **XIN halt 与 resume 成功**，显示正常：`DISPLAY clients=25 frames=2`、SWAP transfer=2、`GPSVC err=0`、`SURF f=2`；230 s 自动回。
  handoff 的软件校验 `ok=0`（所有字段值已记录，未写任何 SMMU 寄存器，因此无黑屏风险）。
- 结论：sagit 的 halt/resume 顺序在本机可用；下一步给 handoff 校验加逐项位掩码，定位 `ok=0` 的具体条件，修正后即可在 halt 窗口内写 CB0。

## 2026-11-05：P21-Zosterops（halt + 自建 4K 表身份映射仍失败；连续两次 halt 后 clients=0，停止该路径）

- 在 Yellowthroat 的基础上，保留 VBIF AXI halt，改用本机已验证的 4K 表构造（6dad936 的 mapPage/表池）替代 kit live 映射。
- 实机（boot `0x4226c8db`，日志 `out/p21/persistent-logs/20261105T110531Z/`）：
  `CB GPU dpu identity halted rc=-10 st=3 pipe=5000 size=7800438 stride=10e0 ack=0 bank=0 fsr=400 restore=0x20000 released=1 sctlr=200626/200626 smr=80000000/80000000 s2cr=0/0`。
  AXI halt 与恢复读回正常、FSR 无 fault，但测试仍以失败退出（映射/编程链路未全通）；该次启动仍 `DISPLAY clients=0 frames=0`、GPSVC open=0。
- 与 Yellowthroat 相同的现象连续出现两次：只要对活动显示通路执行 halt/resume，后续用户态显示客户端就不再起来。即使寄存器回读恢复，显示路径的实际状态不安全。
- **决定停止 display-bank SMMU 直扫尝试**：源码回到 Tattler/HEAD（只读捕获 + 230 s）；halt 补丁保存在 `out/p21/p21-zosterops/logs/halt-legacy-identity.patch`，不再默认执行。
- 渲染下一步改走不触碰显示通路的路径：GPU 2D 内容继续写入合成器 surface（现有 CPU 呈现已验证可见），或先提升 CPU 呈现/合成；display 零拷贝需要更完整的 MDP5 halt/恢复模型（含 CTL/frame 重启）后再评估。

## 2026-11-05：P21-Yellowthroat（首次按 sagit 顺序 halt + CB0 身份映射；halt/编程成功、映射失败，已回退）

- 在 GPU reset 前按 sagit `DisplayDMAHalt` 的顺序执行：VBIF AXI halt（`0xc9b0208/0xc9b020c`）→ 编程 CB0（GOP 同址身份映射）→ 2 s → 关闭 CB0/恢复全局/恢复 VBIF。
- 实机（boot `0x442d94d8`，日志 `out/p21/persistent-logs/20261105T104406Z/`）：
  `CB GPU dpu identity halted rc=-10 st=3 pipe=5000 size=7800438 stride=10e0 ack=0 prog=1 map=0 fault=400 restore=1 sctlr=200626/200626 smr=80000000/80000000 s2cr=0/0`。
  即 AXI halt 已 ack、CB0 编程成功、恢复完成；但 **kit 的 LiveUpdateContextBatch 映射失败（map=0）**，测试按失败路径退出并恢复。
  该次启动 `DISPLAY clients=0 frames=0`、GPSVC open=0，用户态显示未起来，不能判定 halt/resume 安全。
- 源码已回退到 Tattler/HEAD（只读捕获 + 230 s）；halt 实现补丁保存在 `out/p21/p21-yellowthroat/logs/halt-identity.patch`。
- 下一步：保留 AXI halt，改用已验证的 4K 页表构造（6dad936 的 `mapPage`/表池）替换 kit live 映射；先验证 halt→映射→恢复不改画面，再让身份映射跨越 resume，最后才 flip 到窗口。

## 2026-11-05：P21-Scaup（只读捕获：固件未编程 MDP 上下文；自动重启改 230 s）

### 一、只读捕获结果（boot `0x43ca5a9a`，日志 `out/p21/persistent-logs/20261105T100257Z/`）
- 87.3 s：`CB GPU dpu capture rc=-7 st=3 bank=0 cb=20000 sctlr=0 smr=80000000 s2cr=0 tcr=0 tcr2=0 ttbr=0 mair=0 fsr=400 addr=9d400000 -> pa=0 walk=0`。
- 即：SID0 已登记（`SMR0=0x80000000`）并指向 CB0（`S2CR0=0`），但 **CB0 SCTLR/TCR/TCR2/TTBR0/MAIR 全为 0**——固件把显示 SMMU 上下文留成未编程/禁用；GOP 地址 `0x9d400000` 是物理地址，当前无翻译。
- 这正是黑屏根因：sagit 的 `DisplaySMMU.h` 明确要求“禁用的 stage-1 bank 只能在真实 DPU DMA halt 之后初始化”；我们之前是在管道持续取数时使能翻译/改 S2CR0，等于在活动显示通路上换页表。
- 本轮显示正常：frames=5、SWAP transfer=5、GPSVC err=0；400 s 自动回。

### 二、自动重启时间调整
- `cheeseburger-auto-reset` 从 400 改为 **230 s**（用户确认约 150 s 已进入系统界面）。实现无硬编码，主线程 230 s、guard 240 s。
- **P21-Tattler 实测**（boot `0x432b2f8b`，日志 `out/p21/persistent-logs/20261105T101239Z/`）：`CB auto reset: requested=230 s main=1 guard=1`（56.7 s），启动后约 262 s 回 Recovery；显示 frames=2、SWAP transfer=2、GPSVC err=0、捕获正常。
- 时间分析：GPU 探针/捕获约 84–87 s、首次合成器 swap 约 194–211 s；230 s 对“系统界面/GPU 探针”有约 80 s 余量，但对“首个 surface paint 证据”只剩约 19–36 s 余量。若某轮专门观察首个 GPU 画面，建议临时用 260–300 s。

### 三、下一步（安全顺序）
1. 按 sagit `DisplayDMAHalt.h` 先 halt VIG/CTL 取数，再初始化 CB0/表（GOP 同址身份映射 + 预留窗口），恢复管道；绝不在活动管道上改 SMMU。
2. 身份映射成功后再 flip 到窗口；失败立即恢复物理取数。

## 2026-11-05：display-bank SMMU 直扫失败与回退（Godwit / Willet；已退回 P21-Shelduck 只读探测）

### 一、黑屏与回退结论
- 用户确认 Godwit 第二次启动的镜像（`f210a4ee…`，全量直扫默认开启）**黑屏**；持久日志在 ~192 s 停止、`DISPLAY frames=0`。黑屏路径是“首个 swap 时接管 SMMU 并把管道 flip 到窗口 IOVA”，已在源码中整体回退，不再默认启用。
- 回退后重新修改为“早期 + 仅 GOP 身份映射 + 2 s 后完整恢复”的非侵入式验证（Willet）：
  - 第一次（`cd523e57…`）：110 s `CB GPU dpu identity rc=-6 stage=3`——表池分配失败（`NativeAdrenoMemory` 搜索预算 64 KiB < 512 KiB 请求；GPU SMMU 用 1 MiB）。显示正常（frames=13、SWAP transfer=13、GPSVC err=0），432 s 自动回。
  - 第二次（`173a5da1…`，表池预算改 1 MiB）：GPU reset 后 ~100 s 日志停止，`DISPLAY clients=0 frames=0`、无身份映射行；设备约 432 s 自动回，用户可见黑屏。该次未到测试代码（与预算改动无直接时序关系，但结果不可用）。
- **最终源码退回 `850a4ec`（P21-Shelduck 只读探测，零 SMMU 写入）**；全量实现保留在 git 历史 `6dad936`，Willet 源码与镜像在 `out/p21/p21-willet/`，不再作为默认。

### 二、Godwit 实现（历史记录，已回退；按 sheng `sheng-dpu-scanout`）
- `CheeseburgerDpuScanout.cpp` 重写为完整路径：读取 MMSS SMMU ID/流表 → 选择 ≥ID1.S2 且 `CB_SCTLR=0` 的空闲 context bank（不动 bootloader 的 CB0）→ 用自己的物理连续表池建 4K 页表 → GOP 同 PA 身份映射 → 写 CB（TCR/TCR2 SEP 7、TTBR0、MAIR 0x44、ACTLR 0x70000000、CB SCTLR 0x1027）→ `S2CR0.CBNDX=bank`、保留 `SMR0` → latch。
- 逐帧：同尺寸 BGRA surface 物理页映射到两个固定 16 MiB IOVA 窗口之一（base+32/48 MiB），`native_legacy_mdp_commit` 把管道 flip 到窗口并等 latch；失败或缩放帧回退 GOP（再走 CPU 拷贝）。
- `NativeLegacyDisplay.cpp` 增加 `native_legacy_mdp_commit()`；kickOnce 在 GPU paint 之后先尝试直扫，成功即 retire，不再 CPU 拷贝。
- **默认门控**：未设置 `cheeseburger-dpu-scanout=1` 时只读 SCTLR/SMR0/S2CR0 并返回（与已验证的 Shelduck 探测相同），不写任何寄存器。

### 二、实机（两次，均未验证直扫成功）
- 第一次（boot `0x40a57ca8`，镜像 `8b8d0585…`）：`CB DISP dpu setup rc=-1`——GOP stride 4320 被代码误按 64B 对齐拒绝；MDP 只需 8 字节对齐，64B 是 GPU 2D 限制。已修。
- 第二次（boot `0x409f4449`，镜像 `f210a4ee…`，日志 `out/p21/persistent-logs/20261105T090049Z/`）：约 433 s 自动回 Recovery，但持久日志停在 ~192 s，`DISPLAY clients=11 frames=0`、无 `CB DISP surface paint`/`DPU` 行——首个被验证的 swap 从未出现，takeover 未执行。不能据此宣称直扫可用，也不能归因于本实现（日志在首帧前停止）。
- 两次都未观察到 panic；Shelduck（只读探测）仍是最后已验证基线。

### 三、下一步
1. 用 `cheeseburger-dpu-scanout=1` 启动同源镜像，确认首个 swap 出现，并把 takeover 提前到 GPU 探针期（~110 s）而不是首个 swap，避开用户态时序与日志停止窗口。
2. 若接管成功：验证 `S2CR0=bank`、`SMR0`、CB SCTLR、GOP latch、无 CB/GFSR fault；再验证窗口 flip 与回退。
3. 失败时保留 Shelduck 探测行为；不把未验证路径设为默认。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | P21-Willet | `173a5da1…` | `f92645f8…` | 早期身份映射测试；表池预算修正后启动在 GPU reset 后 ~100 s 停止、clients=0（黑屏），未到测试；自动回 |
  | 2026-11-05 | P21-Willet | `cd523e57…` | `f92645f8…` | 早期身份映射测试首跑：表池分配失败 rc=-6；显示正常（frames=13）、432 s 自动回 |
  | 2026-11-05 | P21-Godwit | `f210a4ee…` | `80e668ee…` | display-bank SMMU 全量实现；用户确认第二次启动黑屏（日志 192 s 停止、frames=0）；源码已回退 |
  | 2026-11-05 | **P21-Shelduck** | `c4f1e253…` | `5fe3dc19…` | 迁移 display-bank SMMU：读明固件已预置 SID0 路由（M=0），守卫拒写；其余正常，434 s 自动回 |

## 2026-11-05：P21-Shelduck（迁移 sheng 的 display-bank SMMU 直扫：第一步探明固件已预置 MDP 流路由）

### 一、对照结论（sheng vs 我们）
- sheng 的正式显示配置走 `sheng-dpu-scanout=2`：显示管道通过 display bank 的 stage-1 SMMU 窗口取合成器 surface（TCR2 SEP 7 让 bit31 IOVA 可翻译），flip 前等待该帧 GPU fence；映射/latch 失败回退 CPU 拷贝。其显示 bank 窗口由 `ShengUfs` 持有。
- 我们此前只有 `cheeseburger-fb-flip=1` 的物理地址直扫：要求单段连续且 32 位 PA，80-run/4.3–6.0 GiB 的合成器 surface 无法直扫，只能 CPU 逐行缩放/拷贝。
- 本机 DTB 确认同一机制可用：`arm,smmu-mmss@cd00000` 下有 `qcom,smmu_mdp_unsec_cb`（SID 0）与 `qcom,smmu_mdp_sec_cb`（SID 1）；GPU 用的是独立的 `arm,smmu-kgsl@5040000`。

### 二、本轮迁移（第一步：只做同页身份接管，画面不变）
- 新增 `CheeseburgerDpuScanout.cpp`：复用已 vendor 的 ArmSmmu4K kit，规划为“预路由 SID0→自建 context、GOP 同 PA 身份映射、保持管道地址不变、失败完整回滚”。
- `NativeLegacyDisplay.cpp` 增加 `native_legacy_mdp_flush_latch()`（复提交当前管道并测 latch）与 kickOnce 的一次性调用；同时修掉 `flippath::present` 未检查 `pa>>32` 的截断风险。
- **实机（boot `0x3b6fa866`，日志 `out/p21/persistent-logs/20261105T082512Z/`）**：191.6 s 读出
  `CB DISP dpu smmu rc=-4 stage=1 sctlr=200626/0 smr=80000000/0 s2cr=0/0 latch=0us restore=0`。
  即 MMSS SMMU：`SCTLR=0x200626`（M=0，未启用）、`SMR0=0x80000000`（SID0 已登记）、`S2CR0=0`（指向 CB0 translation）。守卫判定“已有路由”并**拒绝写入**，未改任何 SMMU/管道寄存器。
- 其余正常：`CB GPU surf f=1 rc=0 ... match=4/4`（真实 UI 像素→独立 GPU 画布）、`GPSVC sub=1 fence=1 err=0`、帧计数 10、无 panic；434 s 自动回 Recovery。

### 三、结论与下一步
1. 固件已把 MDP 的 SID0 流表预置好但**全局未使能**；当前 GOP 取数是物理地址。不能覆盖 CB0，应改用空闲 context bank（≥ID1.S2 且 CB_SCTLR=0），保留 SMR0，只改 `S2CR0.CBNDX` 指向自建 bank。
2. 自建 bank 先身份映射 GOP（保持画面不变），再映射一块 1080x1920 GPU surface 并 flip；flip 前等 GPU fence，latch/映射失败立即恢复 `S2CR0.CBNDX=0`、`SCTLR` 与管道。
3. 成功后再把合成器 surface 接入；1242x2208 缩放帧仍需 VIG 缩放或 CPU 回退。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | **P21-Shelduck** | `c4f1e253…` | `5fe3dc19…` | 迁移 display-bank SMMU：读明固件已预置 SID0 路由（M=0），守卫拒写；其余正常，434 s 自动回 |
  | 2026-11-05 | P21-Zenaida | `9c5e601c…` | `f56ffe15…` | GPU 拷贝真实 UI 像素到独立 GPU 画布，四点 4/4；432 s 自动回 |

## 2026-11-05：P21-Zenaida（GPU 读取并重绘真实 UI 像素到独立 GPU 画布；日志洪泛修复）

### 一、P21-Yellowhammer 的失败尝试（已被 Zenaida 取代）
- 把逐帧绘制改成“在同一 surface 内把 512x64 的真实 UI 像素拷到另一位置”。设备正常启动、无 panic、427 s 自动回，但：
  - 每帧映射 256 行产生 319 个单页 run，旧版 `cheeseburger_gpu_smmu_map` 每个 run 打 3 行日志，25 帧就把持久日志窗口从 ~200 s 压缩到 ~233 s，f=1 的回读证据被冲掉；
  - 同 buffer 内 2D 拷贝的 4/4 无法确认（BootLog 快照为 0/4，且 `match` 每帧重置为 0，快照值不可作为当帧结论）。
- 结论：该轮不作为基线，不回退安全修复，只改单变量：拷贝目标换成独立 GPU BO，并删除高频映射日志。

### 二、改动
- `cheeseburger_gpu_paint_surface`：映射 surface 前 64 行（80 个 run，0xD000000），2D 引擎把 `(64,0)` 起的 512x64 真实像素拷到 bigbo 的 8 MiB GPU 面（0xC000000，GOP 之外、客户端窗口之外），随后在 surface 上画 64x8 颜色标记；fence 后从 GPU 面回读 4 点与源像素比较。
- fail-closed：只有 fence 成功才 `unmap_batch`；失败保留映射并置 `gSurfaceStuck` 停止后续帧。
- 删除 `cheeseburger_gpu_smmu_map` 每次成功的 3 行日志（仅保留 reject/fail），避免高频映射冲掉持久日志。
- 服务提交：IB head 拷贝与全 BO `flush_dcache` 移到 job holds 建立之后，与上一轮的按次传入 IB 视图一致。

### 三、构建（同轮）
- 内核 `fdc85273…`（本地树）→ prelink `4738453f…` → panic-screen 后 `f56ffe15…`；payload 展开 `12d9d501…`；固件 `8edaa65d…`（同一 overlay）；镜像 `out/p21/p21-zenaida/uefi/boot-p21-up1.img` `9c5e601c…`（82,492,672 B）；preboot-gate PASS；BCB 回读 `boot-recovery`（`8698abf4…`）。

### 四、实机（boot `0x40972449`，日志 `out/p21/persistent-logs/20261105T073955Z/`，07:32:03Z 启动）
- **GPU 读取真实 UI 像素并重绘**：202.3 s `CB GPU surf: f=1 rc=0 seq=23 w=353 runs=80 stride=5120 src=ff010000/ff010000/ff010000/ff010000 dst=ff010000/ff010000/ff010000/ff010000 match=4/4 min=0x114c8a000 max=0x14438a000`——源是当前合成器 surface 的像素，目标是独立 8 MiB GPU 面，四点回读一致（采样区为纯色 ff010000，只证明拷贝通路与几何，不证明任意内容）。
- `CB DISP surface paint rc=0` 共 6 次（202.3–345.4 s），`SURF f=11`（338 s 快照），逐帧继续。
- 日志窗口恢复到 54 s 起完整保留；无 panic、无 SMMU fault；`GPSVC open=2 sub=1 fence=1 err=0`；`CLR/CPY/HPA/BIG` 全绿（BIG 仍是 2040 run、4/4）；`CMPT` 仍 0/4。
- 设备在 07:39:15Z（约 432 s）自动回 Recovery。

### 五、结论与下一步
1. **2D 引擎的 GPU 渲染通路已打通**：读取实时 surface → 2D 拷贝 → 独立 GPU 画布 → CPU 回读一致；下一步把采样点移到 UI 有内容变化的区域（状态栏/文字），并把结果画回可见 surface 或作为呈现源。
2. **着色器/3D/Metal 仍不可用**：compute 至今 fence 完成但无写入；sagit 的 3D/Metal 需要其自有 Metal→NIR→A5xx 编译链，且 SP 域在 ZAP 后仍为未解前提。近期“GPU 渲染”仍以 2D 合成路径为主。
3. 保持 P21-Zenaida 为基线；回归整体回退。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | **P21-Zenaida** | `9c5e601c…` | `f56ffe15…` | GPU 拷贝真实 UI 像素到独立 GPU 画布，四点 4/4；日志洪泛修复；432 s 自动回 |
  | 2026-11-05 | P21-Yellowhammer | `71f68af6…` | `651116f6…` | 同 surface 内 UI 拷贝；映射日志冲掉证据、回读未确认；不作为基线 |

## 2026-11-05：P21-Yellowlegs（逐帧 GPU 写入合成器 surface；修复 Xenops 空 IB 悬空全局导致的 data abort）

### 一、P21-Xenops 失败与根因（已回退）
- Xenops 把 surface 交接改成逐帧后，出现内核 data abort：`pc 0xfffffff00bfa4fd8`、`lr 0xfffffff00bfa4f80`、`far 0xfffffff82e7be000`、`esr 0x96000007`（level-3 读翻译错误）。
- 用 Xenops `native/kernel.macho` 符号化：pc 落在 `cheeseburger_gpu_engine_submit`（日志第 1803 行）的空 IB 扫描，故障指令 `ldr w11,[x8]`；`x8=cb_inline_ib=0xfffffff82e7be000`，是客户端 IB BO 释放后失效的内核堆视图。
- 触发链：服务提交把 `cb_inline_ib` 写成客户端的 IB 内核视图（启动期全局）；客户端随后释放该 BO；之后每次 `clearKick` 进入同一个空 IB 扫描就读取悬空指针。逐帧 surface paint 让这条路径在客户端释放后仍被频繁调用，所以崩溃。Xenops 未提交，源码回退到 Wryneck（`f09f137`）后重新修复。
- 证据：`out/p21/p21-xenops/logs/xenops-panic-symbolized.txt`、`xenops-uncommitted-delta.patch`。

### 二、改动（在 Wryneck 基线上）
- `cheeseburger_gpu_engine_submit` 增加 `(const volatile uint32_t *ibView, unsigned ibWords)`：空 IB 扫描只读本次调用方提供的视图；板内 `clearKick`/`highcp` 传 `nullptr,0`；删除全局 `cb_inline_ib/cb_inline_dwords` 及 gate C 的 kview 读数。
- 服务 `submit`：等 job holds 建立后，才从首个 IB BO 取内核视图并传入同一次调用，避免客户端并发释放。
- 逐帧 surface 交接（Xenops 的功能保留）：每帧把 surface 前 64 行映射到 `0xD000000`，256x64 块水平移动、颜色轮换，fence 后 `unmap_batch` 退休本帧；每 30 帧做一次 CPU 回读。

### 三、构建（全部同轮）
- 内核 `beb000da…`（本地树，69.3 s）→ prelink `bb67b298…` → panic-screen 后 `1d0b0f64…`；payload 展开 `2de6e36f…`；固件 `MSM8998PKG_UEFI.fd` `3b285b06…`（同一 overlay 重编，pins 三项命中）；镜像 `out/p21/p21-yellowlegs/uefi/boot-p21-up1.img` `a7151d7a…`（82,493,440 B），preboot-gate PASS；misc BCB 已 push→dd→回读 `boot-recovery`（哈希 `8698abf4…`）。
- 启动前确认无旧 fastboot/监视进程，设备 `3b25cc7c`；`fastboot boot` 于 06:48:42Z（`Sending/Booting OKAY`）。

### 四、实机（boot `0x447ff06e`，日志 `out/p21/persistent-logs/20261105T065703Z/`）
- **逐帧 surface**：206.6 s `CB GPU surf: f=1 rc=0 seq=23 w=491 runs=80 stride=5120 x=16 px=ff405060/ff8090a0/ffb0c0d0/ff102030 match=4/4 min=0x113542000 max=0x180006000`；`CB DISP surface paint rc=0 stride=5120 w=1080 h=1920` 连续 6 次（206.6–297.3 s）；338 s 快照 `SURF f=27`，帧计数持续增长。
- 其余无回归：`CLR/CPY/HPA/HCP/BIG` 全部 rc=0、match=4/4（或 sent/val 正确）；`CMPT` 仍 rc=0 但 match=0/4（CS 写入未解决）；`GPSVC ready=1 open=2 sub=1 fence=1 err=0`；`GBOD 1:adc0de1/feed0002/600d0003`。
- **未 panic**：心跳到 338 s，日志无 data abort；设备在 06:55:57Z（启动后约 427 s）自动回 Recovery，证明 400 s 内核路径完整执行。
- 说明：这仍是 GPU 写入每个合成器 surface；呈现由现有 CPU 拷贝/MDP 完成，尚未整体 GPU 合成，也未接 Metal。

### 五、已知遗留
1. 逐帧交接在 fence 失败（rc!=0）时仍执行 `unmap_batch`；下一轮改为 fail-closed：未证明 fence 就保留映射并停止后续帧。
2. 服务提交前的诊断读取（`native_adreno_ib_head`、全 BO `flush_dcache`）仍在 job holds 之前，理论上可与并发 `BoFree` 竞争；一并移到 holds 之后。
3. compute 仍无效（CMPT match=0/4），等待安全的 SP 域方案；MDP 直扫仍受 GOP pitch/32 位 SRC0 约束。

### 六、下一步
1. 修掉上面的 fail-closed 与 holds 顺序，保持 P21-Yellowlegs 为基线。
2. 继续 Metal 前端（插件→metal2vulkan→Turnip 自有 A5xx 后端）与 IOSurface/合成路径。
3. 回归整体回退 P21-Yellowlegs。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | **P21-Yellowlegs** | `a7151d7a…` | `1d0b0f64…` | 逐帧 GPU 写合成器 surface（80 run，f=27，回读 4/4）；修复空 IB 悬空全局；客户端 sub=1/fence=1；427 s 自动回 |
  | 2026-11-05 | P21-Xenops | `fe23d459…` | `ecd9e37d…` | 逐帧 surface 首次尝试；空 IB 扫描读悬空全局 → data abort；未提交，已回退 |

## 2026-11-05：P21-Wryneck（GPU 写入合成器 IOSurface：stride 5120、80 个 run、2D 四象限回读 4/4）

### 一、改动
- 新增 `cheeseburger_gpu_paint_surface(memory,stride,w,h,format)`：在显示驱动 `kickOnce` 校验完合成器 IOSurface 后、任何缩放/拷贝/呈现之前，一次性把该 surface 的前 64 行映射到 `0xD000000`（窗口与内核测试区之外），用 2D 引擎画 256x64 四象限；要求 stride 为 64B 整数倍、BGRA 格式，失败只跳过、不改显示路径。
- 支持 8 个 attempt（首个 surface swap 可能早于 GPU 引擎就绪），成功后停止；对 surface 的 CPU 回读作为证据，随后由现有 CPU 拷贝/MDP 呈现同一 surface。
- BootLog 增加 `SURF` 行。

### 二、实机（boot `0x40a41cbb`，日志 `out/p21/persistent-logs/20261105T061947Z/`，镜像 `88de3944…`，内核 `458cc731…`，固件 `b2d0ba9a…`）
- **合成器 surface**：`CB DISP surface paint rc=0 stride=5120 w=1080 h=1920 fmt=42475241`（205.0 s，第一个晚到的 surface swap）。
- `CB GPU surf: rc=0 seq=23 w=491 runs=80 ... px=ff102030/ff405060/ff8090a0/ffb0c0d0 match=4/4 min=0x112d44000 max=0x17eba9000`——surface 的 80 个物理 run 混合在 4.3–5.98 GiB，GPU 写入并由 CPU 回读一致。
- 其余无回归：2D 清屏/拷贝/高地址 CP/8 MiB batch 表面全部通过；客户端 `GPSVC open=2 sub=1 fence=1 err=0`；`GBOD adc0de1/feed0002/600d0003`；400 s 自动回 Recovery。
- 说明：这是 **GPU→合成器 surface** 的写入与回读证据；呈现仍由现有 CPU 拷贝/MDP 完成，尚未由 GPU 合成整套 UI，也尚未接 Metal。

### 三、下一步
1. 把这套 surface 交接从一次性测试扩展为按帧写入（GPU 渲染结果进入每个合成器 surface），并保持 fence/资源生命周期。
2. Metal 前端：把插件/命令提交接到现有 ABI；compute 仍等待安全的 SP 域方案。
3. 保持 P21-Wryneck 为基线，回归整体回退。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | **P21-Wryneck** | `88de3944…` | `458cc731…` | GPU 写入合成器 IOSurface（stride 5120/80 run）回读 4/4；客户端链正常 |

## 2026-11-05：P21-Wheatear（服务端 BO 切 batch 映射；sagit 配额 16 MiB/BO、128 MiB/client；大 BO 测试移出客户端窗口）

### 一、改动
- 服务端 `boMap`/`freeBo` 改为收集 BO 的全部物理 run，调用 `cheeseburger_gpu_smmu_map_batch/unmap_batch`（kit 每 256 run 一次事务）；失败回滚用同一列表并退休 IOVA。
- 加入 sagit 配额：单 BO **16 MiB**（`boAlloc`/`boImport` 拒绝更大对象）、每 client **128 MiB**（`allocBytes_` 统计）；`Query.max_bo_bytes` 报告 16 MiB，窗口仍 128 MiB。单 ABI client 门限使全局 192 MiB 自然成立。
- 内核 `bigbo` 测试基址从客户端窗口内 `0x5000000` 移到窗口外 **`0xC000000`**（aperture 上限 0x10000000 内），避免未来客户端映射冲突。

### 二、实机（boot `0x40a3df1a`，日志 `out/p21/persistent-logs/20261105T060406Z/`，镜像 `3cce703a…`，内核 `ffeb6f04…`，固件 `37859308…`）
- `BIG rc=0 seq=13 w=491 runs=2040 min=0x15102b000 max=0x153d56000 match=4/4`（5.26–5.31 GiB，窗口外 0xC000000）。
- 其余无回归：`CLR/CPY/HPA match=4/4`、`HCP ib/src=0 val=1234abcd`。
- 客户端：`GPSVC open=2 bo=2 map=2 unmap=2 free=2 sub=1 fence=1 wait=1 err=0`；`GBOD 1:adc0de1/feed0002/600d0003`；服务 ~136 s 打开，400 s 自动回 Recovery。
- 说明：客户端仍只分配 2×4 KiB，配额与 batch 映射在客户端路径上被实际执行；大 BO 的 ABI 分配尚未由客户端覆盖（客户端二进制固定）。

### 三、下一步
1. 继续 IOSurface/合成器 surface 交接（GPU 写入显示驱动收到的 surface）与 Metal 前端；compute 仍等待安全 SP 域方案。
2. 可选：把全局 192 MiB 配额与多客户端语义写全（当前单客户端门限使全局上限自动成立）。
3. 保持 P21-Wheatear 为基线，回归整体回退。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | **P21-Wheatear** | `3cce703a…` | `ffeb6f04…` | 服务 batch 映射 + sagit 配额；8 MiB 表面窗口外 4/4；客户端链正常 |

## 2026-11-05：P21-Twite（sagit batch 映射：8 MiB/1080p 表面 2040 个分散 run 一次事务，2D 填充 4/4）

### 一、改动
- 接入 4K kit 的 `ArmSmmu4KLiveUpdateContextBatch`：新增 `CheeseburgerGpu::MapRange` 与 `cheeseburger_gpu_smmu_map_batch/unmap_batch`；4K 路径按 256 个 run 分块、每块一次 publish/ASID 同步/审计，ledger 仍保留完整 range 列表；旧表路径回退为逐页循环。
- 新增内核 `bigbo` 测试：普通 8 MiB 分配（1088x1920 BGRA，pitch 4352），按物理 run 收集后在 `0x5000000` 批量映射，2D 引擎填四条 1088x480 色带并回读。

### 二、实机（boot `0x409fdf8d`，日志 `out/p21/persistent-logs/20261105T054735Z/`，镜像 `f76febba…`，内核 `dd344991…`，固件 `171cca3b…`）
- `BIG rc=0 seq=13 w=491 runs=2040 min=0x1507a8000 max=0x1536f5000 px=ff102030/ff405060/ff8090a0/ffb0c0d0 match=4/4`（5.26–5.31 GiB，2040 个 run 全部经 batch 映射）。
- 其余测试无回归：`CLR match=4/4`、`CPY am/bm=4/4`、`HPA high=8192/8192 match=4/4`、`HCP ib/src=0 val=1234abcd`。
- 客户端：`GPSVC open=2 bo=2 map=2 unmap=2 free=2 sub=1 fence=1 wait=1 err=0`；`GBOD 1:adc0de1/feed0002/600d0003`；400 s 自动回 Recovery。
- 说明：服务端的 `boMap/freeBo` 仍逐 run 调用，batch API 目前由内核测试使用；客户端 BO 很小，不受影响。

### 三、下一步
1. 把服务端 `boMap/freeBo` 切换到 batch API（每次提交所有 run，减少 live-update 次数），并加 sagit 配额：单 BO 16 MiB、每 client 128 MiB、全局 192 MiB。
2. 把内核测试 IOVA 从客户端窗口内（0x5000000）移到窗口外（如 0xC000000..0xC7F7FFF），避免未来客户端映射冲突。
3. 继续 IOSurface/合成器 surface 交接与 Metal 前端；compute 仍等待安全 SP 域方案。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | **P21-Twite** | `f76febba…` | `dd344991…` | batch 映射 8 MiB/1080p 表面（2040 run）2D 填充 4/4；其余测试与客户端链正常 |

## 2026-11-05：P21-Siskin（删除低池；客户端窗口 128 MiB / aperture 256 MiB，sagit 量级）

### 一、改动
- 删除低池与全部低地址分支：`gLowPage*`、探针期 8 页预留、`cheeseburger_gpu_low_take()`、`native_adreno_low_pool`/`native_adreno_bo_low` 统计与 BootLog 行。
- 客户端 IOVA 窗口从 16 MiB 扩到 **128 MiB @0x4000000**（`cheeseburger_gpu_engine_window`）。
- SMMU kit aperture 从 128 MiB 扩到 **256 MiB**（sagit 的 `iova_bytes` 量级），leaf 预算 65536，range ledger 128→4096；表页仍 128 页（当前实际映射的点缀 <128 个 L3 表）。
- 服务 BO 仍是普通分散页 + 逐 run 映射；descriptor 使用 buffer 本身。

### 二、实机（boot `0x4224fdfd`，日志 `out/p21/persistent-logs/20261105T052337Z/`，镜像 `8d1bfaf1…`，内核 `c83cca58…`，固件 `87e419ef…`）
- 2D 清屏 `match=4/4`；GPU→GPU 拷贝 `am=4/4 bm=4/4`。
- 高地址：32 MiB 全部位于 5.14–5.32 GiB（`high=8192/8192`），2D 写入 `match=4/4`；CP 从高页取 IB（`sent=5a5a0001/2`）、高页 MEM_TO_MEM（`val=1234abcd`）均通过。
- 客户端：`GPSVC open=2 bo=2 map=2 unmap=2 free=2 sub=1 fence=1 wait=1 err=0`；`GBOD 1:adc0de1/feed0002/600d0003`；服务在 ~137 s 打开，观察窗口内正常。
- 400 s 自动回 Recovery。

### 三、下一步
1. 用一个接近真实表面的 **大 BO（如 8 MiB/1080p BGRA）** 走 ABI（BoAlloc/BoMap/Submit）验证 128 MiB 窗口与 4096 range ledger 的实际容量；必要时把映射调用合并为每 BO 一次多 range 更新。
2. IOSurface/合成器 surface 交接（GPU 写入显示驱动每帧收到的 surface），以及 Metal 前端；compute 仍等待安全 SP 域方案。
3. 保持低池删除后的基线；任何回归整体回退 P21-Siskin。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | **P21-Siskin** | `8d1bfaf1…` | `c83cca58…` | 删除低池；128 MiB 窗口/256 MiB aperture；2D/高地址/客户端链全部正常 |

## 2026-11-05：P21-Redpoll（CP 从 5.33 GiB 高页取 IB / MEM_TO_MEM 读高页全部通过；内存模型结论收口）

### 一、实机（boot `0x425fbcb5`，日志 `out/p21/persistent-logs/20261105T050543Z/`，镜像 `bc233ef6…`，内核 `68a31c1c…`，固件 `3f6c3405…`）
- 普通分散页 32 MiB 分配：`min=0x1518bc000 max=0x153d49000`（5.27–5.33 GiB），`high=8192/8192`；2D 引擎在其尾部 64 KiB 画四象限 `match=4/4`。
- **CP 取指**：把 IB 放在高页 `pa=0x153d49000`（5.33 GiB），`ib rc=0 seq=11 sent=5a5a0001/5a5a0002`。
- **CP 读高页**：低地址 IB 用 `CP_MEM_TO_MEM` 读高页，`src rc=0 seq=12 val=1234abcd`。
- 用户链：`GPSVC open=2 bo=2 map=2 unmap=2 free=2 sub=1 fence=1 wait=1 err=0`；`GBOD 1:adc0de1/feed0002/600d0003`。
- 2D 清屏/GPU→GPU 拷贝仍 `match=4/4`。

### 二、结论（内存模型收口）
- **GPU 不依赖低物理内存**：RB 写、CP 取 IB、CP_MEM_TO_MEM 读在 5.27–5.33 GiB 页上全部成功；旧的“CP 不能访问 >5 GB”是移植期错误归因（copy 越界 fault + 单段映射假设），低池/`<4 GiB` mask 不是硬件要求。
- 服务 BO 已使用 sagit 风格普通分散页 + 逐 run 映射；低池仅剩探针期预留，不再服务任何 BO，可在下一轮删除。
- 唯一真实的低地址约束仍是 **MDP 直扫的 32 位 SRC0**（显示路径），与 GPU BO 无关。

### 三、下一步
1. 删除低池与所有低地址分支（`cheeseburger_gpu_low_take`、`native_adreno_bo_low`、相关启动预留），保持一次构建一次启动。
2. 把客户端窗口从 16 MiB 扩到 sagit 量级（128 MiB/client、256 MiB aperture），并相应扩 leaf 预算/表页/gRanges；为 1080p IOSurface（8.29 MiB/张）和 Metal 前端留空间。
3. 继续 IOSurface/合成器 surface 交接；compute 仍等待安全的 SP 域方案。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | **P21-Redpoll** | `bc233ef6…` | `68a31c1c…` | CP 高页取 IB + 高页 MEM_TO_MEM 均通过；普通分散页策略与客户端链正常 |

## 2026-11-05：P21-Serin（修正 copy 越界；普通分散页策略；高物理地址实机通过——推翻“CP 不能访问 >5 GB”）

### 一、关键修正：copy 测试越界导致的 SMMU fault
- dstB 使用 pitch 1088、64 行，需要 `1088*64 = 69632` 字节，但旧测试只分配/映射了 65536 字节，越界写了 5120 字节，触发 SMMU stalled fault（Wigeon 日志 `smfsr=80000402`）。
- 该 fault 使 `cheeseburger_gpu_engine_failed()` 置位：后续 flip/compute/highpa 全部被 fail-closed 拒绝（`exit rc=-1`），客户端也无法打开服务（Starling/Veery/Wigeon 的 `GPSVC open=0` 由此解释，不是客户端本身问题）。
- 修正：按 `pitch*height` 分配/映射每个复制目标（dstA 65536、dstB 69632），copy 与后续测试恢复正常。

### 二、新分配策略（按 sagit 模型）
- `boAlloc` 不再使用低池/`<4 GiB` mask；`contiguous=0` 时分配普通 `IOBufferMemoryDescriptor`，不要求物理连续。
- `boMap` 改为遍历 BO 描述符的每个物理 run、逐段映射到客户端 IOVA；`freeBo`/quarantine 也按 run 重新遍历解映射和恢复页属性；自有 BO 的 descriptor 直接使用 buffer 本身（分散页没有单一物理范围）。
- 低池仍保留为探针期的预留，但不再用于服务 BO。

### 三、P21-Serin 实机（boot `0x422fb451`，日志 `out/p21/persistent-logs/20261105T044906Z/`，镜像 `2888b992…`，内核 `59565b96…`，固件 `bdf5a5cb…`）
- `CB GPU clear ... match=4/4`；`CB GPU copy ... am=4/4 bm=4/4`（无 fault）。
- **高地址自检**：32 MiB 普通分配，`min=0x151f3e000`、`max=0x1545e3000`（5.27–5.32 GiB），`high=8192/8192` 页全部 >4 GiB；在其尾部 64 KiB 用 2D 引擎画四象限，`px=ff102030/ff405060/ff8090a0/ffb0c0d0 match=4/4`。**GPU（CP+RB/SMMU）可以在 >5 GiB 物理页上正确写入/回读**，旧的低地址依赖是移植期 workaround，不是硬件上限。
- 客户端链路完全恢复：`GPSVC open=2 bo=2 map=2 unmap=2 free=2 sub=1 fence=1 wait=1 err=0`；`GBOD 1:adc0de1/feed0002/600d0003`。这也证明新的“普通分散页 + 逐段映射”BO 策略在 ABI 路径上可用。
- 说明：本轮高地址测试使用的是低地址内核 IB 去指挥 RB 写高地址目标；客户端 IB 所在的 4 KiB BO 是否落在高地址未单独记录，CP 从高地址取 IB 仍需一次专门验证。

### 四、下一步
1. 把客户端窗口从 16 MiB 扩到 sagit 量级（128 MiB/client、256 MiB aperture），否则真实 Metal/IOSurface 负载（1080p BGRA 8.29 MiB/张）仍会不够。
2. 专门验证 CP 从高物理地址 BO 取 IB（把 IB 放在普通分配的高页），以及 CP_MEM_TO_MEM 读高页；如果都通过，可删除低池与所有低地址分支。
3. 继续 IOSurface/合成器 surface 交接；MDP 直扫仍受 32 位 SRC0 限制，不是 GPU 内存模型问题。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | **P21-Serin** | `2888b992…` | `59565b96…` | 修正 copy 越界；普通分散页 BO；高地址 5.27–5.32 GiB 2D 写入 match=4/4；客户端链恢复 |

## 2026-11-05：P21-Veery（提前+重试仍拿不到低 4 GiB 连续全帧；MDP flip 安全跳过）

- 把 8 MiB 全帧（1088x1920，pitch 4352）的分配提前到探针早期（63.9 s）并最多重试 8 次；实机（boot `0x42402acc`，日志 `out/p21/persistent-logs/20261105T041045Z/`，镜像 `17975381…`，内核 `a0829194…`，固件 `044c8742…`）仍然 `CB GPU flip: no low contiguous 8 MiB frame`，flip 未执行（`CB GPU flip: exit rc=-1`），设备 400 s 自动回 Recovery。
- 同轮 2D 路径无回归：`CB GPU clear ... match=4/4`；`CB GPU copy ... am=4/4 bm=4/4 pa=1024 pb=1088`。
- 客户端本轮仍未打开服务（`GPSVC open=0 ... err=0`），ABI 用户链未执行，无失败信号。
- 结论：在这台 2 GiB 设备上，低 4 GiB 的 8 MiB 连续块在 63 s 与 108 s 都不可得，MDP 直扫（32 位 SRC0）路线不能依赖该分配。可见 GPU 输出的下一选择：
  1. 合成器的 IOSurface（显示驱动每帧收到并 CPU 拷贝/校验）——把 GPU 绘制结果写进该 surface，现有显示路径会呈现；需要 stride 64B 对齐，并记录 surface PA/stride；
  2. 或等 shader 路线（compute 写 GOP 字节地址），但它被 ZAP 后 SP 域回落阻塞，且不能在实时 CP 上做停机/上电。

### 下一步
- 在 `NativeLegacyDisplay.cpp` 的 swap 校验之后、CPU 拷贝之前加一次性的 “GPU 绘制到合成器 surface” 测试：记录 surface 的 width/height/stride/format/首 PA，stride 为 64B 整数倍时用 2D 引擎在左上角画 256x64 四象限，fence 后由现有 CPU 拷贝呈现；CPU 回读同一 surface 像素作为证据；失败或无对齐则跳过，不改显示路径。
- 保持一次构建一次启动，失败回退 P21-Starling/Veery 基线。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | **P21-Veery** | `17975381…` | `a0829194…` | 提前+重试仍无低 4GiB 连续全帧；MDP flip 跳过；2D 清屏/拷贝正常 |

## 2026-11-05：P21-Starling（sagit snapshot2D 的 GPU→GPU 拷贝实机通过：同 pitch 4/4、1088 pitch 4/4）

### 一、验证内容
- 在 P21-Bittern 的 2D 清屏之后，把已渲染的 256x64 四象限图像用 sagit `snapshot2D` 的 sourced `CP_BLIT` 拷到两块自有线性目标：
  - dstA `0x2070000`，pitch 1024（与源相同）；
  - dstB `0x2080000`，pitch **1088**（与源不同，仍为 64B 的整数倍）。
- 实机（boot `0x40a85c75`，日志 `out/p21/persistent-logs/20261105T035408Z/`，镜像 `8662ced6…`，内核 `ad6543ff…`，固件 `56d4166b…`）：
  - `CB GPU clear: rc=0 seq=8 words=491 ... match=4/4`；
  - `CB GPU copy: rc=0 seq=9 words=331 a=ff102030/ff405060/ff8090a0/ffb0c0d0 am=4/4 b=ff102030/ff405060/ff8090a0/ffb0c0d0 bm=4/4`；持久行 `CPY ... pa=1024 pb=1088`。
  - 设备 400 s 自动回 Recovery；本轮 **客户端没有打开 GPU 服务**（`GPSVC open=0 ... err=0`），因此 ABI 用户链本轮未被执行，也没有失败信号；内核侧 2D 路径正常。
- 结论：sagit `snapshot2D`（独立源/目标 pitch、R32_UINT、sourced `CP_BLIT`）在 A540 上可用，是后续“渲染结果 → 对齐显示/合成表面”的拷贝原语；GOP 的 4320 pitch 仍不能用 2D 直接写。

### 二、当前状态与下一步
1. 2D 能力：清屏（Bittern）与 GPU→GPU 拷贝（Starling）均已实机验证；可以在此基础上接 IOSurface/显示表面导入（BoImport → BoMap → copyTile），但 GOP 扫描线仍需 MDP/对齐缓冲或 shader 字节寻址方案。
2. compute：仍因 ZAP 后 SP/RBCCU 域回落而不执行（Lapwing 的 7 个变体全 0）；不要在实时 CP 上做 halt+域写入+寄存器读回（Plover 挂死，日志停在 100.5 s）。如要重试，必须 posted writes、停机后不读 GPU 寄存器、以下一次 fence 判活，并单独候选默认关闭。
3. 建议顺序：先用 copy 原语完成“自有 GPU 表面 → 对齐显示表面”的交接验证，再考虑 IOSurface 与 Metal；每次仍一次构建一次启动，失败回退 P21-Starling。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-05 | **P21-Starling** | `8662ced6…` | `ad6543ff…` | snapshot2D GPU→GPU 拷贝实机通过（1024/1088 pitch 各 4/4）；2D 清屏正常 |

## 2026-11-04：Plover 日志回收 + P21-Sandpiper 回退控制（移除实时 CP 停机/上电路径，恢复可开机基线）

### 一、Plover 日志（已回收）
- boot `0x40a73020`，日志 `out/p21/persistent-logs/20261104T221102Z/`；msgbuf 停在 100.5 s（`reset rc=0` 之后），没有 `CB GPU core` 行，11 个心跳轮次；也就是说挂点在 `gpuResumeCoreDomains` 的停机/读回路径上，且发生在它第一条日志之前。**今后不要在运行中的 CP 上执行 halt + 电源域写入后再读 GPU 寄存器**；这类读在 AXI 停住时会直接卡死内核。

### 二、回退（源码）
- 删除 `gpuResumeCoreDomains()`、`native_adreno_resume` 与 pre-kick 的调用/`cheeseburger-gpu-core-resume` 开关；**保留**已实机可启动的 CS 变体矩阵与 compute 下发（它们只 fence、不写，不影响开机）。
- 回退后不触碰 ZAP 后的电源域；pre-kick 恢复为只读采样，P21-Bittern 的 2D 清屏、用户提交链保持原样。

### 三、控制启动（P21-Sandpiper）
- 镜像 `99e1db64…`、内核 `2c0992ec…`、固件 `a3c5af5e…`、payload `5ef8b732…`、gate `20261104T221759Z` PASS、misc 回读 `8698abf4…`；`22:18:17Z` 单次 `fastboot boot`，400 s 自动回 Recovery（日志 `20261104T222557Z`，boot `0x40a1c23d`）。
- `CB GPU clear: rc=0 seq=8 words=491 ... match=4/4`；`CMPT rc=0 seq=16 ... match=0/4`（compute 仍不写，符合预期）；`GPSVC ... sub=1 fence=1 wait=1 err=0`；`GBOD 1:adc0de1/feed0002/600d0003`；无挂死。
- 结论：回退基线可开机、2D 与用户链正常；compute 的“无写入”问题仍在，但当前候选不再包含会挂住设备的代码。

### 四、下一步
1. 不再在实时 CP 上做 halt + 域写入 + 寄存器读回；如要重试电源域，必须改成 posted writes、停机后不读 GPU 寄存器、只用下一次 fence 判活，并单独候选、默认关闭。
2. 先继续安全的 2D 路线：验证 sagit `snapshot2D` 的 GPU→GPU 拷贝（自有 64B 对齐目标），再考虑 IOSurface 导入。
3. 若要继续 compute：先确认 SP 域是否真的为 CS 所必需（例如在启动阶段保留域上电的对照实验），再决定是否重试 resume；不要用 P21-Plover 的方式。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Plover** | `1d6e6958…` | `3208aede…` | 停机/上电 resume 挂死；日志停在 100.5 s，无 core 行 |
  | 2026-11-04 | **P21-Sandpiper** | `99e1db64…` | `2c0992ec…` | 回退后的控制启动：2D match=4/4、用户链 err=0、自动回 Recovery |

## 2026-11-04：P21-Lapwing / P21-Petrel / P21-Plover（CS 变体矩阵全部为 0；发现 ZAP 后 SP/RBCCU 域回落；核心域 resume 尝试把 Plover 挂住）

### 一、根因线索（已确认）
- 启动日志：pre-reset 上电后 `after=140000/140001`（RBCCU/SP status bit20 置位），而 ZAP 之后 `rbccu=778001/1 sp=778001/1`（bit20 清零）。**2D 引擎不依赖 SP 所以一直正常；CS 需要 SP，域回落时 CS 无法执行。** 这与 sagit `QcomGPUHardware` 的 `fDomainResumeEnabled` + `idlePowerTransition` 设计一致（他们也必须在 CP 静止窗口里重新上电）。
- Lapwing（boot `0x4229114e`，日志 `20261104T184708Z`）跑了 7 个状态变体：sagit 原始、WGID/UNK=0、THREADSIZE=0、去 0x2/BUFFER、单线程 local=1、SSBO block=14、NUM_UNIT=1；每个变体 dispatch `rc=0`、fence 完成，自有 4 KiB SSBO 的 immediate 与 20 ms 后读回**全部为 0**。因此不是几何/绑定字段或完成顺序，且在 SP 域关闭时 CS 不会写。
- Petrel（boot `0x409f2d15`，日志 `20261104T190451Z`）：首次实现 `gpuResumeCoreDomains`（sagit idlePowerTransition 式：空 ring + idle 检查 → halt CP/VBIF → 0xa887/0xa881=0x778000 等 bit20 → 恢复），但空闲判定误用 `st&~1`，拒绝正常 idle 状态 `0x80000101/0x800001c1`，返回 `rc=-3`，引擎按 fail-closed 停止；计算/清屏本轮未执行，设备仍自动回 Recovery。
- Plover（镜像 `1d6e6958…`，内核 `3208aede…`，固件 `f9e9bf1c…`）：修正空闲判定后再次运行 resume；`19:11:30Z` 单次 `fastboot boot`，到 `19:19` 仍无 ADB/Fastboot，系统在 halt/域写入路径上挂住。**Plover 的持久日志尚未回收。**
- 处置：resume 路径已改为仅 `cheeseburger-gpu-core-resume=1` 时执行（默认关闭），普通启动保持已验证的只读 pre-kick；设备需要手动重启回 Recovery 后才能读取 Plover 日志并继续。

### 二、下一步
1. 设备回 Recovery 后先回收 Plover 日志（boot/窗口哈希），看 halt、域写入、resume 各走到哪一步。
2. 若确认 halt/域写入是挂因：改成 sagit 的完整前置健康检查与 `platformReady` 语义，或只 halt ME、不 halt VBIF；在确认显示/UFS/AXI 不受影响前不再默认启用。
3. 若 SP 域无法安全拉起，CS 路线暂停，继续用 2D 引擎推进（GPU→GPU copy、IOSurface 导入）；compute 写入自有缓冲成功后再回 GOP 扫描带。
4. 所有新实验仍保持一次构建一次启动，失败整体回退到 Bittern 的已验证 2D 基线。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Lapwing** | `20af38df…` | `bfa444f7…` | 7 个 CS 状态变体全部 `rc=0` 但 SSBO 无写入；确认 ZAP 后域回落 |
  | 2026-11-04 | **P21-Petrel** | `4fd9c807…` | `0fb0e4c3…` | 首次核心域 resume，空闲判定过严 `rc=-3`；引擎 fail-closed |
  | 2026-11-04 | **P21-Plover** | `1d6e6958…` | `3208aede…` | 修正后 resume 把系统挂住；日志待回收；resume 已默认关闭 |

## 2026-11-04：P21-Egret / P21-Gannet（IR3 compute 通路：着色器可汇编并下发，SSBO 写入尚未观察到）

### 一、IR3 工具链（本机可用）
- 用本机 Mesa 26.2 的 `ir3_parse_asm` + `libfreedreno_ir3` 构建了一个宿主汇编器（`fd_dev_id{gpu_id=540}`、`disable_cache=true`）；命令与头文件路径记录在 `tools/gpu-ir3/README.md`。产物是 a5xx IR3 机器码 + 元数据（instrlen/constlen/max_reg/localid/threadsize）。
- 两个 shader（源码在 `tools/gpu-ir3/`）：
  - `scan_band.asm`：`shl.b r1.x, r0.x, 8` → `stib` 写 SSBO0 的第 id 个 dword（id<<8）。
  - 常量探针：写 `0xdeadbeef`。
  两者的 32 字 ISA、`instrlen=1`、`constlen=0`、`max_reg=1`、`double_threadsize=1`、`localid=0`、`wgid=252` 均写入 `package/BoardRuntime/CheeseburgerGpuCompute540.h`。

### 二、compute 下发实现（sagit `dispatch` 移植）
- `ScanBand540::encode()`：`restore()` → SP/HLSQ CS 配置（`SP_CS_CTRL_REG0` THREADSIZE/FULLREGFOOTPRINT、`HLSQ_CS_CNTL` instrlen+SSBO、`HLSQ_CS_CONSTLEN/INSTRLEN`）→ `SP_CS_OBJ_START` 六寄存器 → `HLSQ_UPDATE_CNTL=0x1f00000` → `HLSQ_CS_CNTL_0/1` → CP_LOAD_STATE4 0x30 加载 1 个指令单元 → 两条 SSBO 描述符（state block 15，16 项，尺寸/地址）→ NDRANGE/`KERNEL_GROUP` → CP 0x33 `{0,groups,1,1}` → idle → flush(4)+wait，最后由既有 Engine540 fence 收尾。
- 内核在 PE 控制台可用的前提下把 GOP 前 64 行（1080×64、pitch 4320，正好线性 276480 B）映射到 GPU VA `0x2100000`，把 ISA 页映射到 `0x2300000`，另把 4 KiB 自有 SSBO 映射到 `0x2400000` 作为“执行与否”的判据。
- 宿主模型 `tests/gpu_blit540_model.cpp` 覆盖 compute 流：CS/SSBO 寄存器、shader object、NDRANGE、0x33 组数、flush(4)。

### 三、实机结果
- P21-Egret（boot `0x40a7d465`，日志 `20261104T180726Z`）：compute `rc=0 seq=9 words=360 local=64 groups=1080`，fence 完成，但扫描带采样仍是旧内容 `0xff000000`。
- P21-Gannet（boot `0x40a6afd2`，日志 `20261104T182302Z`）：加入隔离矩阵后：
  - 自有 SSBO 常量探针 `rc=0`，采样 `0/0/0/0`（应为 `deadbeef`）；
  - 自有 SSBO shift 探针 `rc=0`，采样 `0/0/0/0`；
  - 扫描带常量/shift `rc=0`，采样仍 `ff000000`。
  - `CB GPU engine: pre pwr ... st=80000101` 在首次 dispatch 后变为 `st=c0000101`（bit30 置位，非 idle），随后用户提交仍成功（`GPSVC err=0`、`GBOD adc0de1/feed0002/600d0003`）。
- **结论（已确认）**：CP 接受并完成了包含 0x33 的流，但没有观察到任何 SSBO 写入，连自有 4 KiB 缓冲也没有；因此不是 GOP 地址/pitch/映射问题，问题在 CS 启动/状态或完成顺序。GPU 在 dispatch 后处于 busy 状态，不能排除 CS 仍在运行或未真正启动。

### 四、下一步（具体）
1. 在探针 dispatch 后加 20–100 ms 延时并二次读取（当前在同一毫秒内读取；fence 只证明 CP 消费了 0x33，不证明 CS 完成）。
2. 变体矩阵（一次启动内）：`HLSQ_CS_CNTL_0` 的 WGID/UNK 由 252 改 0；`THREADSIZE` 0/1；去掉 `SP_CS_CTRL_REG0` 的 0x2/BUFFER 位；local=1、groups=1 的单线程 dispatch。每种写不同常量到自有 SSBO 的不同 dword，读回即可判定哪组状态能启动 CS。
3. 若仍无写入，按 sagit `dispatch` 补齐 `constantSamplers`/常量上传（即使 constlen=0 也上传一组驱动常量）与 `HLSQ_CS_CNTL_1` 细节；仍保持一次构建一次启动。
4. compute 写入自有缓冲成功后再回到 GOP 扫描带，最后接 2D/3D 渲染结果。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Egret** | `6c273d9e…` | `afa931aa…` | IR3 compute 首次下发：rc=0/fence，但 SSBO 无写入 |
  | 2026-11-04 | **P21-Gannet** | `5456b660…` | `e59cbe3c…` | compute 隔离矩阵：自有 SSBO 探针也为 0；dispatch 后 GPU bit30 busy；用户链无回归 |

## 2026-11-04：P21-Curlew / P21-Dunlin（显示扫描线约束定位：GOP pitch=4320 非 64B 对齐；MDP 帧落在 4 GiB 以上）

### 一、Curlew：把 P21-Bittern 的静默跳过变成可诊断结果
- `clearPrepareScanout()` 现在记录 `PE_current_console` 的 base/width/height/rowBytes/depth、失败码与页数；`CLR` 持久行增加 `prep/pe` 字段。
- 实机（boot `0x40a26751`，日志 `out/p21/persistent-logs/20261104T172200Z/`）：离屏清屏仍为 `rc=0 seq=8 words=491 ... match=4/4`；扫描线准备 `prep=-5`，`pe=1080/1920/4320/32`。
- **根因（已确认）**：固件 GOP 扫描缓冲为 1080 像素、pitch **4320 字节**；A5xx 2D 的 `RB_2D_*_SIZE.PITCH` 以 64 字节为单位（`pitch>>6`），4320 不是 64 的整数倍，无法编码。因此 2D 引擎不能直接写这块扫描线；这不是 SMMU、映射或权限问题。
- 同轮实现了 sagit `snapshot2D` 的 2D 拷贝（`Blit540::copySetup/copyTile/copyEnd`：`RB_2D_BLIT_CNTL=0x86000000`、`UNKNOWN_2184=9`、独立源/目标 pitch、R32_UINT、sourced `CP_BLIT`），宿主模型覆盖编码、边界和失败关闭；因该 pitch 约束，它尚未在扫描线上执行。

### 二、Dunlin：GPU 全帧 + MDP 直接扫描尝试（安全跳过）
- 新增显示驱动测试钩子 `native_legacy_scanout_flip(pa,stride,w,h,hold_ms,out)`：复用 `flippath` 的 `prepare/commit/restore`，把 MDP 管道的 SRC0 指向一块 64 字节对齐的 GPU 帧，扫描 5 s 后恢复 GOP 缓冲。
- GPU 侧新增 `cheeseburger_gpu_scanout_flip_test()`：分配 1088x1920（pitch 4352）全帧，用 2D 引擎画四条 480 行色带并回读，然后请求显示管道扫描它。
- 实机（boot `0x40a433bd`，日志 `out/p21/persistent-logs/20261104T174146Z/`）：
  - 离屏清屏与用户提交链无回归：`match=4/4`、`GPSVC ... err=0`、`GBOD 1:adc0de1/feed0002/600d0003`。
  - 全帧分配成功（`pa=0x111876000`、8,355,840 B、连续），但 **PA=4.58 GiB > 4 GiB**；MDP5 `PIPE_SRC0_ADDR` 是 32 位（显示驱动的 `present()` 也把 PA 截成 32 位），因此钩子未执行（`CB GPU flip: exit rc=-1`），未改显示状态、无黑屏。
- 结论：可见 GPU 像素当前受两件事阻挡——2D/3D 引擎的 64B 目标 pitch，以及 MDP 直接扫描的 32 位物理地址。两条绕行路线：
  1. **compute shader 的字节寻址 global store**（sagit 的 compute 回退路径）：直接按 `base + y*pitch + x*4` 写入 GOP 扫描缓冲，不受 pitch/4 GiB 限制（SMMU GPU VA 映射该缓冲）。这是下一轮主线。
  2. 若坚持 MDP 直接扫描：需要在低 4 GiB 内拿到连续全帧（当前分配器返回高地址），或把 MDP 路径扩展到 IOMMU/64 位。
- 新增/保留的代码：`NativeLegacyDisplay.cpp` 的 flip 钩子（验证失败安全）、`CheeseburgerGpuCp.cpp` 的全帧分配/绘制/flip 测试、`CheeseburgerGpuBlit540.h` 的 2D copy、`tests/gpu_blit540_model.cpp` 的 copy/全帧流用例。flip 失败路径会保留 8 MiB 缓冲到重启（可追踪，但下轮应在未映射/未提交时释放）。
- 构建：内核 `61f75a9f…`（编译 `92556165…`）、payload `6814645e…`、固件 `0873c26c…`、镜像 `86fe2e9a…`、gate `20261104T173349Z` PASS、misc 回读 `8698abf4…`；`17:34:07Z` 单次 `fastboot boot`，400 s 自动回 Recovery。

### 三、下一步（按 sagit 路线）
1. 用 Mesa `src/freedreno/isa`/computerator 的 IR3 汇编器产出一个最小 compute shader（`stg` 写常量/渐变到绝对 GPU 地址，无 SSBO/常量窗口），按 sagit `dispatch()` 的 SP/HLSQ CS 状态与 NDRANGE 下发；目标是把四条色带直接写进 GOP 扫描缓冲（4320 pitch 也能写），CPU 回读与屏幕确认。
2. 该 compute 路径通过后，把 2D 渲染结果作为源（sagit 的 compute copy）或接 IOSurface 导入，形成 CPU 不搬帧的显示交接。
3. 3D 三角形仍走 sagit `PipelineEncoder` 的 VSC/RB + IR3；它与 compute 共享 shader 上传/常量与 flush 语义。
4. 不要恢复逐行 `CP_MEM_WRITE`；本轮的 2D copy 保留为 GPU→GPU/对齐表面路径。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Curlew** | `e649c0c1…` | `17aaec73…` | 诊断：GOP pitch 4320 非 64B 对齐，2D 无法直接写扫描线；2D copy 编码落地 |
  | 2026-11-04 | **P21-Dunlin** | `86fe2e9a…` | `61f75a9f…` | GPU 全帧 + MDP flip 尝试：帧落在 4 GiB 以上，MDP 32 位地址限制，安全跳过；清屏/提交链无回归 |

## 2026-11-04：P21-Bittern（A540 2D 图形路径实机：CP_BLIT 四象限清屏 match=4/4；服务期资源持有/隔离）

### 一、源码来源与复用边界
- **小米 6 公开源码**已拉到独立参考目录 `/Volumes/NAS-MacData/Project/IOS/upstream/ios_sagit`，commit `673f85070fe9f7872793015eb230a5c3987d4a8c`（2026-11-03）。`drivers/gpu/QcomGPU`（A540 内核后端）与 `QcomMTL`（Metal 前端）为完整 native Metal 栈，许可证见各文件 SPDX（MIT / BSD-2-Clause，Mesa 24.3.4 IR3 参考）。
- 本轮只复用其 **A5xx 2D 状态与命令序列**：`AdrenoPipeline.h` 的 `restore()` 基线与 `clearRows()` 的 CP_BLIT solid fill。sagit 的启动/板级地址、QcomMTL 前端、IOSurface/显示交接未引入；一加 5 已验证的电源、CP/GPMU/ZAP、SMMU、ring/fence、用户提交链未改动。
- 新增 `package/BoardRuntime/CheeseburgerGpuBlit540.h`（逐寄存器移植）与 `package/BoardRuntime/A5xxRegs.h`（由 Mesa `a5xx.xml` + `gen_header.py` 生成；Mesa 26 把 RB_2D/UCHE 的 LO/HI 合并为 reg64，头内保留旧名别名）。MIT 版权与许可保存在 `package/BoardRuntime/A5XX_NOTICE.txt`。

### 二、图形路径（P21-Bittern 源码）
- `restore()`：A5xx 线性/GMEM-bypass 基线（SP/RB/TPL1 64 位地址、UCHE invalidate、HLSQ、PC/GRAS/RB/VPC/SP/TPL1 默认、`RB_CCU_CNTL=0x10000000`、`RB_CNTL=BYPASS`）。
- `begin2D()` + `solidFill()`：Mesa fd5 sysmem event 25/24，BLIT2D（`0x6c`=5）、`RB_2D_*` 目标格式 75（R32_UINT）、solid source、CP_BLIT（`0x2c`）像素边界、END2D、`CACHE_FLUSH_TS(29)` + `CP_WAIT_REG_MEM`，最后由既有 Engine540 fence 收尾。
- 开机探针在第二次 keep-alive 之后执行（`cheeseburger-gpu-clear`，默认开）：分配 8 KiB IB、64 KiB 线性 256x64 RGBA8 目标、4 KiB flush 页，映射在 `0x2040000/0x2060000/0x2050000`（**提交锁之外**）；一次 restore + 四个 128x32 象限填充，CPU 读回四象限中心与一个边缘样本；随后尝试用同一引擎填一条 64 行扫描线带。
- 宿主模型 `tests/gpu_blit540_model.cpp` 解析真实编码流（寄存器、BLIT2D/END2D 次数、填充边界、DST pitch/array pitch、flush/等待），失败关闭（小缓冲）与参数校验；`scripts/run-gpu-start-model-test.py` 已纳入，全部 PASS。

### 三、服务端生命周期（与图形同一候选）
- **导入 BO 映射**：`BoMap` 不再要求自有 `buf`；导入 BO 以钉页描述符的物理段逐段映射，`BoCpuMap` 拒绝导入 BO（ABI 规定其无 CPU token）。
- **提交期持有**：每个 BO 获得全局 serial；提交前先预留 job 槽，把本客户端所有已映射 BO 的 serial 计入持有表；fence 完成才释放。无空槽时返回 `kIOReturnBusy`，不会“先提交后漏记”；失败提交保留持有（fail-closed），不释放 GPU 可能仍在读的页。
- **异常退出**：client close 时未能释放的对象移入可追踪 quarantine（保留 buffer/描述符/映射记录），fence 消失且引擎 idle 后再 unmap、`complete()`、恢复 pmap、释放；unmap 失败则保留并退休 IOVA。新 ABI 客户端在存在 quarantine 时拒绝打开。
- **可见性**：自有 BO 在提交前 clean+invalidate（`flush_dcache`），fence 后再次 flush 再回读；导入 BO 的 cache 属性归调用方（ABI 原文），服务不触碰。

### 四、实机（P21-Bittern，日志 `out/p21/persistent-logs/20261104T170051Z/`，boot `0x409f33da`，400 s 自动回 Recovery）
- 镜像 `19b766e5…`、内核 `c222817f…`（编译 `4ec0b1d9…`）、固件 `5e5e9edf…`、payload `ef72ce2e…`、gate `20261104T165253Z` PASS（670 B，三项 pin 命中）、misc 回读 `8698abf4…`（`boot-recovery`，push→dd→回读一致）；`16:53:13Z` 单次 `fastboot boot` OKAY。
- **门禁与清屏**：`keepalive0/1 rc=0`；`gate A/B/C rc=0`（cp=`70268000/70ec8005`）；`CB GPU clear: rc=0 seq=8 words=491 px=ff102030/ff405060/ff8090a0/ffb0c0d0 edge=ff102030 match=4/4`。持久行：`CLR rc=0 seq=8 w=491 px=ff102030/ff405060/ff8090a0/ffb0c0d0 edge=ff102030 match=4/4 scan=0/0 px=0 ib=527f9000 tgt=52809000 bid=409f33da`。
- 该结果证明 **A540 RB/GRAS 2D 引擎**由 CP 执行并写出四个不同颜色的象限，CPU 回读与写入逐一相等，边缘样本未越界；这不是逐行 `CP_MEM_WRITE`，也不是用户态渲染。
- 用户态提交链无回归：`GPSVC open=2 bo=2 map=2 unmap=2 free=2 sub=1 fence=1 wait=1 err=0 st=0`；`GBOD 1:adc0de1/feed0002/600d0003`。系统继续运行到 399 s 后自动复位回 Recovery。
- **扫描线带未执行**（`scan=0/0` 表示未尝试）：`clearPrepareScanout()` 的 PE 控制台前提未通过，本轮未记录具体原因；因此**不能**声称面板上出现本轮 GPU 像素。离屏清屏是唯一图形证据。
- 过时镜像说明：`254f8d0a…`（内核 `6fe8358a…`）在 `cheeseburger-gpu-clear` 默认门修复前构建，实机启动一次（日志 `20261104T164552Z`，boot `0x45631556`）只验证了基线与用户提交链；其 gate 已移到 `out/p21/p21-bittern/logs/superseded/` 保留，未再次启动。

### 五、下一步（可直接执行）
1. `CheeseburgerGpuCp.cpp`：在 `clearPrepareScanout()` 的失败分支记录 `PE_current_console` 的 base/width/height/depth/rowBytes 与失败码（当前静默返回），修复后把 64 行带画到面板；这是“GPU 像素可见”的下一步。
2. 最小三角形：以 sagit `PipelineEncoder` 的 VSC/RB + IR3 着色器（Mesa 24.3.4 编译链）为准，先做离屏三角形回读，再谈 IOSurface/Metal。
3. `CheeseburgerGpuService.cpp`：把 IOSurface 的 `BoImport` 接入 2D blit 的源/目标，验证 no-copy 与 fence 后 cache 语义。
4. 扫描线映射与清除仍只在提交锁外；不要恢复逐行 `CP_MEM_WRITE` 绘制或 CPU 帧搬运冒充 GPU 合成。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Bittern** | `19b766e5…` | `c222817f…` | A540 CP_BLIT 2D 清屏实机 `match=4/4`；服务导入映射/持有/隔离；提交链无回归 |

## 2026-11-04：P21-Auk（Turnip Darwin-KMD 构建链恢复 + 服务端 tunnel/import/持有语义 + A540 拷贝路径）

### 一、官方 Turnip Darwin-KMD 构建链（workstream A）
- **输入（本机固定框架 `16e592ab…`）**：Mesa `85fda3b36405bcaea8bffc64aa4f2651dc90770d`（freedesktop 稀疏检出到 `.work/p21-official-framework/.cache/mesa-work`，94 MB），官方 9 个补丁按 `scripts/data/adreno-compiler-inputs.json` 顺序全部 `git apply` 成功，其中 `patches/mesa/turnip-darwin-kmd.patch` 即 `src/freedreno/vulkan/tu_knl_darwin.cc`（Darwin KNL，面向 NativeAdreno IOKit 服务）。
- **构建依赖**：meson 1.12.1（venv）、ninja、glslang 16.6、GNU bison 3.8.2（Apple 自带 bison 不支持 `-W`，脚本 PATH 里放 `.cache/mesa-tools/bin/bison` 软链）。iOS 目标需要 `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`，否则 `xcrun --sdk iphoneos` 失败。
- **结果（仅构建，未跑 GPU）**：
  - `--target host`：**PASS**，`build/mesa-tu/src/freedreno/vulkan/libvulkan_freedreno.dylib` 22,918,696 B，sha256 `7bc75a02…`。
  - `--target ios`：**PASS**，`build/mesa-tu-ios/src/freedreno/vulkan/libvulkan_freedreno.dylib` 17,879,288 B，sha256 `b2cb2342…`。
  - 这是“Darwin KMD 构建成功”，**不是** A540 Vulkan 可运行；Mesa 官方文档明确 Turnip 只面向 A6xx+，没有 A5xx 计划。

### 二、服务端按 KNL 实际调用补齐（workstream B）
对照 `tu_knl_darwin.cc` 的实际调用逐项修正：
- **NTNL tunnel**：KNL 以 `IOAcceleratorES` + type `0x4147` 打开，把每个 ABI 方法包成 `{0x4e544e4c, method, payload}` 走**同一个被 sandbox 放行的 selector**，并用 `method=0xffffffff` 探测。`AGXDeviceUserClient::externalMethod` 现在回答探测并转发 tunnel 调用到同一套 handler（`GTUN calls/probes` 记录）。
- **BoAlloc `contiguous=0`**：KNL 传 0，旧实现直接拒绝；现在接受（连续分配满足非连续请求）。
- **BoImport**：原来 `kIOReturnUnsupported`，现已实现——`IOMemoryDescriptor::withAddressRange(调用方地址, 长度, task_)` 钉页并 `prepare`，BO 标记 imported；`BoMap` 按每个物理连续段映射（`bo.spans[64]`），`freeBo` 逐段 unmap 并 `complete()` 解钉，导入页的 cache 属性归调用方所有、服务不碰。
- **提交期持有**：每笔成功提交记录 `{fence, handles}`（8 组），对应 BO `held++`；`waitFence` 成功才 `releaseJob` 递减；`freeBo` 对 `held>0` 直接拒绝并保留映射，客户端提前释放/异常退出都不会让 GPU 仍引用的页被回收。

### 三、A540 图形/拷贝路径（workstream C）
- `Encoder540::copy(dst, src, dwords)`：用 `CP_MEM_TO_MEM`（0x73）逐 dword 拷贝，是本机第一条非 `CP_MEM_WRITE` 的 GPU 数据路径，已编译并由门禁 C 段在实机执行（把客户端页 4 个 dword 拷进内核 scratch）。
- **实机限制（新证据）**：16 个连续 0x73 包会让本 CP 在拷完 2 个 dword 后停住、fence 不触发（P21-Warbler/Zephyr），即使每 4 个插入 `0x12/0x26` 屏障也一样；因此一批最多 4 个，更大拷贝必须拆成多次提交。编码器注释已记录。
- A5xx 真正的绘制（VSC/RB/着色器）不在 Turnip 支持范围，需要另立 A5xx 图形后端；本轮未伪造任何绘制结果。

### 四、实机（P21-Auk，`out/p21/persistent-logs/20261104T154715Z/`，400 s 自动回 Recovery）
- 镜像 `9b76208e…`、内核 `a065c69b…`、固件 `915a89c7…`、gate `20261104T153918Z` PASS、misc 回读一致、15:39:40Z 单次 `fastboot boot` OKAY。
- `keepalive0/1 rc=0`；门禁 `A rc=0 / B rc=0 / C rc=0`（C 段走新的 copy 路径）；`GPSVC open=2 bo=2 map=2 unmap=2 free=2 sub=1 fence=1 wait=1 err=0`；`GBOD 1:adc0de1/feed0002/600d0003`；客户端 `v4`。
- 分项状态：BO/submit/fence **device-verified**；GPU 文本/条带像素 **device-verified（历史轮）**；IOSurface 导入路径 **源码实现，实机未验证**；Vulkan/Turnip **构建成功、未运行**；Metal 与系统 GPU 合成 **未开始**。

### 五、下一个 agent 可直接继续
1. `package/BoardRuntime/CheeseburgerGpuService.cpp`：`boImport`/`boMap`(spans)/`releaseJob`/tunnel 分支——用 Turnip 的 iOS dylib 在设备内实际打开一次（`TU_DARWIN_SERVICE=NativeAdrenoGpuPublished TU_DARWIN_OPEN_TYPE=0` 走直连 selector），看 `GTUN` 与 `GPSVC` 的调用序列。
2. A5xx 图形后端：从 sagit `QcomGPU` 的 Encoder 与 Mesa `a5xx`/IR3 参考入手实现最小清扫/绘制；Turnip 不提供 A5xx，必须自建。
3. `Engine540::copy` 的 4 个/批限制说明已写入头文件，跨提交拷贝需要引擎侧队列化（当前每次提交自带 fence）。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Auk** | `9b76208e…` | `a065c69b…` | Turnip Darwin-KMD host+iOS 构建成功；服务补 tunnel/BoImport/持有；A540 拷贝路径实机通过；提交链路无回归 |

## 2026-11-04：P21-Ruff 结果（合并 sheng 的每次提交 TLBIALL；移除逐行填充；ABI 与 sheng 完全一致）

- **合并的 sheng 案例（`NativeAdrenoVm::invalidateTlb`）**：每次用户提交前对 context bank 做 TLBIALL，避免上一次提交之后新写的 leaf 被陈旧 TLB 命中；失败即 fail-closed（`rc=-8 stage=0x83`）。这是 `Engine540` 移植时丢掉的一步。
- **移除逐行扫描填充**：`cheeseburger-gpu-fill` 与相关代码整体删除（按用户要求，不再用逐行 `CP_MEM_WRITE` 当绘制手段）。GPU 写扫描线的结论保留在 `P21-Oriole`/`P21-Pintail` 两轮记录里。
- **ABI 核对（关键发现）**：`package/BoardRuntime/NativeAdrenoABI.h` 与 sheng `Adreno740/NativeAdrenoABI.h` **逐字节相同**（`diff` 为空，各 165 行）。因此 sheng 的前端（Metal 插件 → metal2vulkan → Turnip）在 ABI 层可以直接对接本驱动；缺口只在器件相关的队列/能力字段与 Turnip 的 a5xx 后端。
- **sheng 前端工件的可得性**：`build-turnip-darwin.py` 引用 `patches/mesa/turnip-darwin-kmd.patch`（`src/freedreno/vulkan/tu_knl_darwin.cc`，即“面向 NativeAdreno IOKit 服务的 Darwin KNL 后端”），但该补丁**不在 sheng 公共仓库 main 的 `patches/` 下**（sparse clone 只拿到 `xnu-12377` 相关补丁，`patches/mesa` 不存在）。这一项需要先向 sheng 取得该补丁或其构建产物，才能继续。
- **P21-Ruff 实机（日志 `out/p21/persistent-logs/20261104T140947Z/`，400 s 自动回 Recovery）**：`keepalive0/1 rc=0`；门禁 `A/B/C rc=0`；`GPSVC sub=1 fence=1 wait=1 err=0`；`GBOD 1:adc0de1/feed0002/600d0003`；客户端 `v4`。**TLBIALL 合并未破坏提交链路。**
- **下一步**：
  1. 取得 sheng 的 `turnip-darwin-kmd.patch`（或询问作者），按 `build-turnip-darwin.py --target ios` 的流程把 Turnip 的 Darwin KNL 后端接到本驱动；ABI 已对齐，主要工作是 a5xx 的器件能力字段（GMEM、通道数、`fd_dev_info`）与 shader 编译器输入。
  2. 在此之前，内核侧可以继续补 sheng 的 BO 生命周期语义（`holdJob`/`retireJobAfterCompletion`、`publishForDevice`/`acquireDeviceWrites`），让 Vulkan 前端所需的“提交期间持有、fence 后释放”在 ABI 上完整。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Ruff** | `abb91554…` | `ef6751fe…` | 合并 sheng 每次提交 TLBIALL、删除逐行填充；提交链路仍 device-verified |

## 2026-11-04：P21-Pintail 结果（扫描线填充改为可校验的 2D 渐变图案）

- **改动**：条带内容从单色改为 2D 图案——红色沿 x 渐变、绿色沿行渐变、蓝固定；回读时用同一公式重算每个采样点，结果是 `match=n/n` 而不是“颜色相同”。
- **P21-Pintail 实机（日志 `out/p21/persistent-logs/20261104T133421Z/`，400 s 自动回 Recovery）**：门禁 `A/B/C rc=0`；`CB GPU fill rc=0 bars=4 rows=24 ibs=24 pattern=gradient px=ff003440/ff7f3440 **match=8/8**`；`DISPLAY frames=5`（仍全部早于 148 s 的填充）。
- **sheng 的适配路线（本轮核对，作为后续主线）**：
  1. 内核层：`NativeAdreno*`（Gmu/GmuPlatform/Firmware/CpInit/Ring/Submit/Vm/Memory/SmmuService）在 A740 上把 CP/SQE、ring、BO 生命周期和 SMMU 4K 上下文封装成自有 ABI。
  2. 前端层：`NativeAdrenoMetalPlugin.mm` 被 Apple 的 Metal 发现流程（IOAcceleratorES → MetalPluginName → bundle → `initWithAcceleratorPort:`）加载；“published” 模式下只对名单内进程（backboardd 等）返回原生 `MTLDevice`，其余进程返回 nil 保持软件渲染。
  3. 翻译层：`NativeBridgeMetalFrontend.mm` = **Metal → metal2vulkan → Vulkan loader → Turnip**（Metal 在 Vulkan 之上）。Turnip 是已构建输入（`build/mesa-tu-ios/.../libvulkan_freedreno.dylib`），源自独立的 mesa 工作树，未随仓库分发。
  4. 显示交接：`GpuScanoutClient`/`GpuScanoutPresenter` 把合成好的 IOSurface 页与固件扫描缓冲都以 no-copy 方式导入，由 GPU 做一次 blit 写进扫描线并完成 swap。
- **对 A540 的结论**：内核提交链路我们已经等价具备（自有 ABI + ArmSmmu4K + ring/IB/fence），短路点只在**前端**：需要一个把 Turnip 的 **a5xx** 后端接到我们 ABI 的适配层（Turnip 默认走 kgsl ioctl，我们的驱动没有 kgsl 兼容层）。这是一项独立的大工程，应作为下一阶段主线，而不是继续在填充上打转。
- **下一步**：
  1. 完成填充的**限时重复重绘**（例如 40 s 内每 2 s 一次），让 GPU 像素在 CPU 刷新后仍保留（用户已确认条带可见，但只在没有新帧时保留）。
  2. 立项 Turnip a5xx 适配：先做接口核对（Turnip 的 fd 后端对 kgsl ioctl 的最小集合 vs 我们的 ABI），再决定“在驱动里加 kgsl 兼容层”还是“给 Turnip 加自有后端”，并写进 porting.toml 的 GPU next_action。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Pintail** | `cfee1045…` | `a6767ae5…` | 扫描线 2D 渐变 + 逐点校验；`match=8/8`，可启动、自动返回 |

## 2026-11-04：P21-Oriole 结果（GPU 产出的像素第一次显示在面板上：4 条 24 行绿条，用户确认可见）

- **改动**：把扫描线填充从 1 行加粗到 **4 条 × 24 行 × 448 像素**，每 4 行一个 IB（共 24 个 IB，每个自带 fence），既保持单个 IB 落在 GPMU 页内，也让条带在 1080×1920 面板上可见。映射仍在提交锁之外（`fillPrepare()`），提交在锁内。
- **P21-Oriole 实机（boot 日志 `out/p21/persistent-logs/20261104T131119Z/`，400 s 自动回 Recovery）**：
  - 门禁 `A/B/C rc=0`（`kview=703d8003`）。
  - `CB GPU fill rc=0 bars=4 rows=24 ibs=24 color=ff00ff00 px=ff00ff00/ff00ff00/ff00ff00/ff00ff00` —— 24 个 IB 全部完成，四个条带中点回读颜色与写入一致。
  - `DISPLAY frames=5`，5 次 CPU 呈现都发生在 141.8 s 填充之前，因此条带之后没有被覆盖。
  - **用户目视确认：面板上出现 4 条绿条**。这是本项目第一次由 GPU（A540 CP）产出并显示的像素。
- **说明**：绿条画在 `PE_current_console()` 指向的**固件 GOP 控制台帧缓冲**（与开机文字、panic 屏蓝/洋红阶段条同一块内存），不是 iOS 的合成缓冲；它与早期阶段条共存，只是位置不同（画面高度 1/5…4/5）。持久性目前依赖“之后没有新的 CPU 呈现”，下一轮将加限时重复重绘以稳定保留。
- **下一步**：
  1. 限时重绘（例如 40 s 内每 2 s 一次）让 GPU 像素在 CPU 刷新后仍保留；随后换成 2D 图案（渐变/棋盘）并记录哈希。
  2. 真正的渲染仍需 Metal 前端产出绘制命令；当前填充走的是 `CP_MEM_WRITE`。
  3. 每帧都要走这条路径时，扫描线映射应在初始化阶段完成（一次性），不要在提交锁内做任何映射。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Oriole** | `e6056e26…` | `bab52d12…` | 4×24 行扫描线填充；24 个 IB 完成、回读一致，**用户目视确认绿条** |

## 2026-11-04：P21-Tanager 结果（GPU 首次把像素写进固件扫描线，CPU 从 DRAM 回读到正确颜色）

- **改动（提交 `9c1e0f4` 之后）**：重做扫描线填充，按 sheng `GpuScanoutPresenter` 的结构——扫描线只映射一次，且**在提交锁之外**：`cheeseburger_gpu_engine_submit()` 入口先调 `fillPrepare()`（`PE_current_console()` 取扫描线 → 4 条横条所在页映射到 GPU IOVA `0x2040000+i*0x10000`），再取 `gSubmitLock` 做门禁与 ring 提交。P21-Skua 的自锁（在持锁路径里调 `cheeseburger_gpu_smmu_map`）不再存在。
- **P21-Tanager 实机（boot `0x48fd0738`，日志 `out/p21/persistent-logs/20261104T124153Z/`，400 s 自动回 Recovery）**：
  - `keepalive0/1 rc=0`；门禁 `A/B/C rc=0`（`kview=703d8003`）。
  - **`CB GPU fill rc=0 bars=4 color=ff00ff00 px=ff00ff00/ff00ff00/ff00ff00/ff00ff00`** —— 4 条 448 像素宽、1 行高的绿条由 CP `CP_MEM_WRITE` 写进固件扫描缓冲，fence 通过后 CPU 用非缓存映射从 DRAM 逐个条带回读，**四个采样点都等于写入的颜色**。
  - 用户态链路不受影响：`GPSVC sub=1 fence=1 wait=1 err=0`、`GBOD 1:adc0de1/feed0002/600d0003`、客户端 `v4`。
  - `DISPLAY frames=5`：填充发生在 144.9 s，此前的 5 帧 CPU 呈现已完成，之后没有再刷新，因此条带不会被 CPU 覆盖。
- **仍待确认（只能由屏幕确认）**：面板上是否真的显示出这 4 条绿条。回读只证明“GPU 写进了固件扫描缓冲”，不证明扫描控制器正在扫描这块内存；若屏幕无变化，下一步查显示控制器实际扫描地址（MDP/GOP 基址）是否与 `PE_current_console()` 一致。
- **下一步**：屏幕确认后，把条带换成真正的 2D 填充/图案并记录哈希；随后按计划接 IOSurface 与 Metal 前端；sheng 的每次提交 TLBIALL 仍待单独恢复复测。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Tanager** | `ec700f4c…` | `8eddfbf1…` | 锁外映射 + 4 条扫描线填充；**GPU 写入、CPU 回读颜色一致（ff00ff00×4）** |

## 2026-11-04：P21-Vesper / P21-Skua 结果（GPU 填充扫描线；自锁死导致 panic，已回退）· 基线回到 P21-Linnet

- **目标**：合并 sheng `GpuScanoutPresenter` 的思路——让 GPU 把像素写进固件正在扫描的帧缓冲，做出第一帧“GPU 产出、屏幕可见”的画面。实现为 `cheeseburger-gpu-fill=<argb>`：门禁通过后一次性把 8 条横条用 `CP_MEM_WRITE` 写进扫描缓冲、fence、再从 DRAM 回读每个条带的一个像素。
- **P21-Vesper（boot `0x477bc534`，日志 `out/p21/persistent-logs/20261104T114332Z/`）**：提交链路仍正常（`GBOD 1:adc0de1/feed0002/600d0003`、客户端 `v4`），但 `CB GPU fill failed rc=-1`：导出扫描线参数的 GOP presenter 在本机这条显示路径上没有被实例化，两个全局保持为 0，填充函数在入口就返回。
- **P21-Skua（同一镜像两次启动）**：
  - 第 1 次（boot `0x48cc3e33`，日志 `out/p21/persistent-logs/20261104T120300Z/`）：`DISPLAY clients=0 frames=0`——已知的首帧偶发未出现，`mediaserverd` 没有打开 GPU 服务，因此门禁与填充都没有执行；内核本身正常跑满 400 s 并自动回 Recovery。据此把填充改为调 `PE_current_console()` 取同一块帧缓冲。
  - 第 2 次（boot `0x4908b95c`，日志 `out/p21/persistent-logs/20261104T121434Z/`）：**panic `Mutex 0x… is unexpectedly owned by thread 0xf @lock_mtx.c:175`**（屏幕原文由用户提供：`DISPLAY st=10 opens=5 rawframes=0`、`OPEN tries=5`、`EXITS total=227`、`TOUCH frames=3363`）。持久日志本身只写到 139.4 s（没有 `CB GPU fill` 行、panic 文本不进持久环），与“日志写手先停、panic 在后”一致，因此 panic 时刻以屏幕计数为准。根因已定位：`gpuScanoutFill()` 在 `cheeseburger_gpu_engine_submit()` 持有 `gSubmitLock` 时调用 `cheeseburger_gpu_smmu_map()`，后者经 `cheeseburger_gpu_mapping_begin()` 再次申请**同一个非递归 IOLock**，提交线程自锁。
  - 过错记录：第 2 次启动前的 misc BCB 重写实际失败——上一轮重启清空了 `/tmp`，`/tmp/misc-request.bin` 已不存在，`dd` 未写入，回读为全 0（`misc-readback2.bin`），所以这一轮没有预置 `boot-recovery`，panic 屏 hold 后需要手动回 Recovery。以后每次 fastboot 前必须先 `push` 再 `dd` 并确认回读非零。
- **处理**：`c732e55` 把这两个提交整体回退，工作树回到 P21-Linnet（`git diff d862f05 -- package` 为 0），可启动、提交链路 device-verified 的版本。
- **下一步（重做填充时）**：
  1. 映射必须在**提交锁之外**完成（例如在门禁前用同一把锁之外的序列化，或在 `engine_submit` 入口先 map、再取锁），绝不能在持锁路径里调 `cheeseburger_gpu_smmu_map`。
  2. 扫描线用 `PE_current_console()` 取（GOP presenter 不一定实例化），并在填充前确认 `v_depth==32`、`v_rowBytes>=width*4`。
  3. 填充仍需 `cheeseburger-gpu-fill` 开关，默认关闭；同时保留“单个像素回读”作为唯一证据，不用截图推断。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Vesper** | `e706bd22…` | `a6b69407…` | 扫描线填充（GOP 全局未实例化 → rc=-1）；提交链路正常 |
  | 2026-11-04 | **P21-Skua** | `26a35ce7…` | `9553e9c6…` | 改用 PE 控制台；自锁 IOLock → panic（lock_mtx.c:175），已整体回退 |

## 2026-11-04：P21-Linnet 结果（用户态 GPU 提交全链路成功：CP 执行客户端 IB 并写回目标 BO）· 新基线 = P21-Linnet

- **改动链**：
  - `7e6299b`（P21-Rook）：上电命令只允许在 CP 停止时写入 —— ZAP 之后重复写 `0x778000` 会在 1 ms 内停住 CP（Quail keep-alive 实测）。
  - `4fe4e71`（P21-Snipe）：空 IB 在 CPU 上完成，不踢 ring。
  - `b0e4a3f`（P21-Talon）：BO 增加自己的内核视图 `bo.cpu`（低池 BO 只是 32 KiB 块中的一页，`getBytesNoCopy()` 指向块首）。
  - `2b6b0d1`（P21-Vireo）：视图回退不再拒绝分配，并记录被拒的分配参数。
  - **`0dc7a0e`（P21-Linnet）**：池内页偏移按**块长度**判定（0x7000 偏移合法，之前被 8 KiB 上限误判）。
- **P21-Linnet 实机（boot `0x477aef4e`，日志 `out/p21/persistent-logs/20261104T110602Z/`，400 s 自动回 Recovery）**：
  - `keepalive0 rc=0`、`keepalive1 rc=0`；门禁 `A rc=0`、`B rc=0`、`C rc=0`（`cp=703d8003/4001000 kview=703d8003` —— CP 与内核看到同一页）。
  - 用户态：`GPSVC open=2 close=2 bo=2 map=2 unmap=2 free=2 **sub=1 fence=1 wait=1 err=0** calls=16`。
  - **GPU 实际写回**：`GBOD n=2 0:703d8003/4001000/0 1:**adc0de1/feed0002/600d0003**` —— 客户端 IB（3 条 `0x3d CP_MEM_WRITE`）被 A540 执行，目标 BO 里正是它要求写的三个值。
  - 客户端自检：`GPSTXT P21 plugin done s12 e0 **v4** ta572c130`（4/4 校验通过、无错误）。
- **结论（实机证据，逐项）**：GPU 内核启动与服务发布 ✔；用户态 `IOServiceOpen` ✔；两个 BO 分配与映射 ✔；客户端命令经 A540 CP 执行 ✔；fence 完成并被等待 ✔；CPU 从目标 BO 读回预期值 ✔；BO 解除映射与释放 ✔。仍属 CPU 软件显示（`frames=5`），**GPU 渲染/Metal/IOSurface 尚未开始**。
- **下一步**：
  1. 在同一后端上做最小像素填充：用 `0x3d/0x46` 之外的 A5xx 绘制包（或先做 `CP_MEM_TO_MEM` + 校验）验证 RB/MRT 通路，再交给 IOSurface。
  2. 让 Metal 前端产出真实命令流：现在验证链路用的是 `adrenoprobed` 的内核可证明 IB；下一步需要 `NativeAdrenoMetal` 插件提交带渲染命令的 BO，并用同一 GBOD/GPSTXT 机制校验。
  3. 恢复 sheng `NativeAdrenoVm::invalidateTlb` 的每次提交 TLBIALL（现在提交链路可用，可单独加回并复测）。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Linnet** | `d4960599…` | `1282e4a4…` | 池内页视图按块长度判定；**用户态提交全链路成功**：CP 写入 `adc0de1/feed0002/600d0003`，客户端 4/4 校验通过 |

## 2026-11-04：P21-Snipe 结果（用户态提交首次走通：空 IB 完成 + fence/wait/释放全链路成功）· 新基线 = P21-Snipe

- **改动（提交 `d1f0e3f` 前为 `7e6299b`）**：客户端第一个 buffer 不含任何非零 dword 时，整笔提交在 CPU 上完成（写 fence 字、推进序号、返回可等待的 fence），**不踢 ring**——P21-Rook 证明本 CP 会把全 0 的 IB 取到环尾却永远不写 fence，之后所有提交都会被 fail-closed 拒绝。
- **P21-Snipe 实机（boot `0x477677a7`，日志 `out/p21/persistent-logs/20261104T100541Z/`，400 s 自动回 Recovery，`frames=5`）**：
  - `keepalive0 rc=0`、`keepalive1 rc=0`（发布后 CP 存活）；门禁 `A rc=0 / B rc=0 / C rc=0`（ring 内联写、`0x3f` 取指、CP 读客户端页全部成立）。
  - `empty IB seq=8 words=12; completed without a kick`；`GSUB rc=0 stage=0x21 fenced=1 seq=8`。
  - 用户态全链路：`GPSVC open=2 close=2 bo=2 **map=2 unmap=2 free=2 sub=1 fence=1 wait=1 err=0** calls=16`——**提交返回成功、fence 被等待成功、两个 BO 正常解除映射并释放**（历史上第一次没有 `err=5`）。
  - 本轮 `sub=1`：客户端只提交了一笔空 IB，随后关闭；`backboardd` 以观察者身份打开（type=1, gate=0）。
- **结论（实机证据）**：GPU 内核提交后端与用户态 ABI 的提交/fence/等待/BO 生命周期在真机上已经完整走通一次。剩余问题在**上层命令内容**：客户端递交的 IB 是 12 dword 全 0，说明它把命令写在别处（或尚未准备命令队列），而不是我们分配的 BO。
- **下一步**：
  1. 对照 sheng `NativeAdrenoClient`/`NativeAdrenoVm` 与 Apple IOGPU 的 buffer 协商，确认 `NativeAdrenoAbiSubmitBuffer` 的 `handle/iova/bytes` 是否被上层填对，以及命令是否在 IOGPU 的命令队列/共享内存里（`clientMemoryForType` 目前只返回 BO 描述符）。
  2. 让客户端在空提交之后继续工作：观察 `GPSVC calls` 是否出现第二笔带内容的提交；若一直为空，需要在加速器的 `query`/`deviceInfo` 上补齐上游字段（GMEM、UBWC、通道数等），让 Apple 侧 GPU 栈进入真正的渲染初始化。
  3. 真正的像素验证仍待 GPU 渲染路径；`frames=5` 仍是 CPU 显示，不能当作 GPU 合成成功。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Snipe** | `055597c4…` | `4682af91…` | 空 IB 在 CPU 完成；用户态 `sub=1 fence=1 wait=1 err=0`、BO map/unmap/free 全部成功；可启动、自动返回 |

## 2026-11-04：P21-Rook 结果（找到并修掉停 CP 的根因；内核 IB 全链路通过，客户端 IB 内容为全 0）· 新基线 = P21-Rook

- **上电时序根因（提交 `f4bb1c7` 之前为 `9ea90a5`+`d52ea88` 诊断）**：探针在 ZAP 之后调用 `powerCore()`，此时 RBCCU/SP 状态位读回为 0（`after-zap rbccu=778801/1 sp=778001/1`），于是它向 `0xa887/0xa881` 写入 **0x778000 上电命令**——而这发生在 CP 已经启动、正在取指的时候。P21-Quail 的握手 keep-alive 把这一点钉死：`keepalive0 rc=0`（114.627 s），紧接着该写入，`keepalive1 rc=-4`（117.158 s），随后 `Unclassified kernel abort (fault_code=0x10)`。
- **修复**：`powerCore(...,allowWrite)`——只有 **reset 之前** 的那次调用可以写；ZAP 之后与每次提交前的采样都是只读。`cheeseburger_gpu_pre_kick()` 不再要求电源状态位（只读无法修复它），只在总线不可读、CP fault 已锁存或 micro-MMU 无法恢复时拒绝；是否真正执行由该次提交自己的 fence 判定。
- **P21-Rook 实机（boot `0x490c841d`，日志 `out/p21/persistent-logs/20261104T094646Z/`，400 s 自动回 Recovery，`frames=5`）**：
  - `keepalive0 rc=0 fr=3`、`keepalive1 rc=0 fr=4` —— **发布之后 CP 仍然存活**（此前每一轮在这里就已死）。
  - 门禁三段全过：`gate A rc=0 sent=a5a50001/a5a50002/a5a50003 fr=5`（ring 内联写）、`gate B rc=0 sent=b6b60001/b6b60002/b6b60003 fr=6`（**`0x3f` 间接缓冲取指成功**）、`gate C rc=0 cp=0/0 kview=0 fr=7`（CP 读客户端页）。
  - 客户端提交仍失败：`GSUB rc=-4 stage=5 seq=8 fenced=0 pf=74/0`；`GRNG seq=8 103..116 n=13 w=70928000/70268000/70bf8003/4000000/0/c …`——客户端第一个 IB 是 **12 dword、内容全 0**（`kview=0`，门禁 C 也从 CP 侧读到 0，`GBOD 0:0/0/0 1:0/0/0`）。CP 把整条环取到 `rptr=0x74=116`（等于 wptr）却未执行到 fence，说明它停在 IB 内容上。
- **结论（实机证据）**：GPU 提交后端在真实硬件上是通的——环、门铃、`0x3f` 取指、`0x46` fence、CP 读用户页都成立。当前唯一挡住用户态的，是客户端递交的 IB 为全 0；这属于上层 ABI/前端把命令写进哪个 BO 的问题，不再是 CP/提交后端问题。
- **下一步**：
  1. 在提交入口对客户端 IB 做**非零预检**（内核视图前若干 dword 全 0 即拒绝，`rc=-9`），避免一个空 IB 把 CP 顶死、连带拒绝后续真正有内容的提交。
  2. 查客户端为何递交空 IB：`mediaserverd` 通过我们发布的 IOAccelerator 提交，命令可能在别的共享内存（IOGPU 命令队列/IOSharedDataQueue）而不是我们分配的 BO；需要对照 sheng `NativeAdrenoClient`/`NativeAdrenoVm` 的 `describe/submit` 与 Apple IOGPU 的 buffer 协商，确认 `NativeAdrenoAbiSubmitBuffer` 的 iova/bytes 是否被上层填对。
  3. 空 IB 被拒后，观察是否有第二次带真实命令的提交（`GPSVC calls`）出现。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Rook** | `2243f292…` | `dba4917e…` | 修掉 ZAP 后重复上电写；keepalive0/1 全过、门禁 A/B/C 全过；客户端 IB 全 0 → 仍超时；可启动、自动返回 |

## 2026-11-04：P21-Quail 结果（握手 keep-alive + 每次提交 TLBI，启动后未回 Recovery，已回退）· 基线 = P21-Merlin

- **改动（提交 `d52ea88`）**：把门禁第一段（ring 内联 `CP_MEM_WRITE` + fence）在**探针 fence 之后**（`k0`）和**服务发布之后**（`k1`）各再跑一次，用来区分“CP 在探针处就停了 / 发布路径 / 客户端映射”；同时恢复 sheng `NativeAdrenoVm::invalidateTlb` 的每次提交前 bank TLBIALL（`CheeseburgerGpuEngine540` 移植时丢失的一步）。宿主模型与内核语法检查通过。
- **P21-Quail 实机**：`out/p21/p21-quail/uefi/boot-p21-up1.img` SHA-256 `ea3d323a5f9cef558d6619dc0b6c02395c55ecc97b70d556f66b910b940d4c2f`，内核 `a19bb207…`，固件 `da3f09b1…`，payload 展开 `df898d6a…`，gate `20261104T090403Z` PASS（670 B）。09:04:27Z 单次 `fastboot boot` Sending/Booting OKAY；**到 09:13:35Z 仍未回到 Recovery，也没有出现在 `adb devices` / `fastboot devices` / IOUSB 枚举中**（与 P21-Harrier 相同签名）。设备电源/屏幕状态未由本轮观察，400 s 自动返回没有产生落点。
- **处理（提交 `d6f18f4`）**：整体回退 `d52ea88`，工作树回到 P21-Merlin（`a14fc50`，`git diff a14fc50 -- package tests` 为 0）——该版本映射成功、门禁运行、可启动并自动返回。
- **未确认**：Quail 卡住的具体步骤（没有日志）。`k0` 段是新增代码中最早执行、也最可疑的一处：它在 ZAP 之后、服务发布之前直接向 ring 提交并轮询 fence；若要重新引入，应先只保留 `k0`（不恢复 TLBI），并给它一个更短的有界等待和“失败即放弃整个 GPU 服务”的明确路径。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Quail** | `ea3d323a…` | `a19bb207…` | 握手 keep-alive（k0/k1）+ 每次提交 TLBI；启动后未回 Recovery、USB 未枚举，已回退 |

## 2026-11-04：P21-Nighthawk / P21-Peregrine 结果（强制停流使 BO 映射失败，已回退）· 当前基线 = P21-Merlin

- **P21-Nighthawk（boot `0x478151eb`，日志 `out/p21/persistent-logs/20261104T083113Z/`）**：`GPSVC map=0 sel=3 err=1 st=e00002bd`、`sub=0`——客户端 BO 映射被 `cheeseburger_gpu_mapping_begin` 拒绝，没有任何提交；系统仍正常启动并自动回 Recovery。
  - 原因可从启动快照读出：`GCP2 … vbif=0/f0000`，即 **`VBIF_HALT`(0x3080) 空闲时读 `0xf0000`**（每客户端状态在 bits 19:16，请求/确认在 bits 3:0），而新的前置条件按整寄存器 `vb==0` 判断，永远不成立。
- **P21-Peregrine（boot `0x4783256a`，日志 `out/p21/persistent-logs/20261104T085010Z/`）**：提交 `38051d0` 把前置条件与恢复校验改成只看 bits 3:0，`gpu_quiesce_model` 也按真实回读建模（`HOST_MODELS_PASS`），但**映射仍然失败**（`map=0 sel=3`，客户端 155.6 s 打开、没有 `bo atos`/提交记录）。该轮没有打印 `dr=`（GATE 行只在门禁运行时输出），所以**是哪一个检查拒绝的未确认**：可能是 `drainForTables` 内部的 ack/空闲校验，也可能仍是前置条件。
- **处理（提交 `9ea90a5`）**：回退 `2839d8a` + `38051d0`，工作树回到 P21-Merlin（`a14fc50`）——映射成功、门禁运行、系统启动并自动返回。**保留结论**：本机 `VBIF_HALT` 不能用整寄存器判断；任何“改表前停流”的策略必须先输出 `dr=`/各检查结果，再决定是否强制。
- **下一步（按优先级）**：
  1. 把 `mapping_begin` 的每个拒绝点写成独立状态字并在心跳里**无条件**打印（当前只在门禁运行时输出），用一次启动确认 `drainForTables` 在本机能否完成（precondition / ack / idle-status / r==w 四项分别可见）。
  2. 若停流可完成：保持“改表前后停流”，观察门禁 A 段是否恢复执行；若门禁 A 仍超时，问题就不在改表与 CP 的竞态，转查 `cheeseburger_gpu_pre_kick`/服务发布之间的其它 GPU 侧动作。
  3. 若停流不可完成：不要强制，改为在**首次映射前**做一次 ring 存活性验证（门禁 A 的 ring 内联写 + fence），并在失败时明确报“环在映射后失效”，把修复点定位到 `ArmSmmu4KLiveUpdateContext` 的 TLBI/上下文同步。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Nighthawk** | `39a779fa…` | `56fa7c6e…` | 改表前后强制停 CP+VBIF；`VBIF_HALT` 整寄存器判断失效 → `map=0`，无提交 |
  | 2026-11-04 | **P21-Peregrine** | `4993afe7…` | `8c242e4f…` | 只测 bits 3:0；映射仍 `map=0`，拒绝点未打印、未确认；已回退 |

## 2026-11-04：P21-Merlin 结果（映射后 CP 连 ring 内联写都不执行）· P21-Nighthawk

- **源码**：`7eccff1` 把 `516c248`（黑屏轮）整体回退，工作树与 P21-Falcon（`f7c26ba`）逐字节一致；`a14fc50` 用**三段门禁**替换单一 preflight IB（A：ring 内联 `0x3d` CP_MEM_WRITE；B：同样写入改从内核 IB 取指；C：CP 侧 `CP_MEM_TO_MEM` 读客户端页），每段自带 fence，任一段失败即拒绝客户端 IB；失败后只读自己的 scratch 页，**不再采样任何 GPU 寄存器**。
- **P21-Merlin 实机（boot `0x4928782a`，日志 `out/p21/persistent-logs/20261104T081117Z/`，68 轮、0 写错误、显示 `frames=5`，400 s 自动回 Recovery）**：
  - probe 与前几轮一致：116.066 s `fence OK seq=1`、116.068 s `userpath fence rc=0 seq=2`。
  - **门禁 A 失败**：`GATE a=-4/5 0/0/0 b=0/0 0/0/0 c=0/0 cp=0/0 kv=0`，`gate A rc=-4 stage=5 sent=0/0/0 fr=0`——CP 连**直接写在 ring 里**的 `0x3d` 都没执行；`GRNG seq=3 34..49 n=15 w=70928000/70268000/703d8005/2012700/0/a5a50001 a5a50002/a5a50003/70928000/70268000` 说明环内内容正确。
  - 内核未卡死：`GSUB rc=-2 stage=5`、`GPSVC sub=1 err=5 st=e00002ca`（提交返回错误），日志到 338 s 仍在写，系统正常回 Recovery。
  - 两次成功提交（116 s）与首次失败提交（146.4 s）之间，GPU 侧唯一动作是**客户端 BO 的 SMMU 映射**（LiveUpdate + 上下文观察 + TLBI）。338 s 快照里 CB0 `fsr=400`（存在一个故障状态），`ttbr=a802000`、`sctlr=1167`、`smr=80000000`；`SIRQ count=0`（fault 中断没有上报）。
- **P21-Nighthawk 修复（提交 `2839d8a`）**：按 kit 的 live-update 契约与 sagit `idlePowerTransition`，引擎存在后**每次改表都先停 CP + 排空 VBIF，改完再恢复**（原来只在环非空时停流）；环不可证明空闲时拒绝改表，停流未确认完成则 fail-closed。停流结果写入 `GATE … dr=%x`。
- 候选 `out/p21/p21-nighthawk`：内核/镜像/固件哈希见下方表格补齐。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Merlin** | `3aee74ac…` | `205e4e70…` | 三段门禁；A 段（ring 内联写）即超时 → 映射后 CP 不再消费；可启动、自动返回 |
  | 2026-11-04 | **P21-Nighthawk** | 待补 | 待补 | 改表前后停 CP + VBIF 排空；结果待回收 |

## 2026-11-04：P21-Falcon 结果（CP 完全忽略新提交流）· P21-Harrier

- **源码**：提交 `f7c26ba` 把用户提交迁到 sagit 风格引擎（新文件 `package/BoardRuntime/CheeseburgerGpuEngine540.h`）；`CheeseburgerGpuCp.cpp` 的 `cheeseburger_gpu_engine_submit()` 改用该引擎，并移除“只允许一次用户提交”的限制；`CheeseburgerGpuService.cpp` 提交前发布**所有**已映射 BO。宿主模型新增 `tests/gpu_engine540_model.cpp`（`HOST_MODELS_PASS`）。
- **P21-Eagle 结果（补记，boot `0x4670c72a`，日志 `out/p21/persistent-logs/20261103T172058Z/`）**：`GSUB rc=-4 stage=3 fenced=0 fr=30`；CP 侧 `0x53/0x43` 表/TLB 更新序列**没有改变任何结果**；该轮 `GBOD n=2 0:0/0/0 1:0/0/0`（客户端 IB 页在提交时全 0），`GIBH` 未出现。
- **P21-Falcon 实机（boot `0x4957c61d`，日志 `out/p21/persistent-logs/20261103T175911Z/`，68 轮、0 写错误、`frames=5`）**：
  - probe 仍正常：`fence OK seq=1`、`userpath fence rc=0 seq=2 fence=2`；116 s 时 `rw=22/22`、`me=0 vbif=f0000`、`trust=0`。
  - 首次用户提交（142.4 s）先运行**内核自有 preflight IB**（GPMU 页，CP 在 116 s 已取指过的同一页）：`GPFL rc=-4 seq=3 cp=0/0/0/0 kview=0 sent=0/0/0/0 fr=0 st=5`——**连哨兵都没写**，即整条新流一个包都没执行；`fr=0` 表示 fence 页 +0 的 RPTR shadow 也没动。
  - `GRNG seq=3 34..47 n=13 w=70928000/70268000/70bf8003/2012200/0/22 …`：环内内容与写入指针都正确（`0x12/0x26` 屏障 → `0x3f` IB@`0x2012200` len 0x22 → 屏障 → `0x46` fence）。
  - `GSUB rc=-4 stage=5`；`GPSVC open=1 bo=2 map=2 sub=1 fence=0 err=5 st=e00002ca`；`SIRQ count=0`（无 SMMU fault IRQ）；`GBOD n=2 0:0/0/0 1:0/0/0`；总线在本轮**没有**挂死（338 s 心跳仍能读 GPU 寄存器）。
  - 事实对比：同一环、同一门铃写入方式，probe 的两条 fence 流能执行；唯一新增元素是 `0x3f` 取指，而该取指地址（GPMU 页）在 116 s 曾被 CP 成功取指。**没有证据表明 CP 执行了任何新包，也没有新的 SMMU fault。**
- **P21-Harrier 修复（提交 `516c248`）**：按 sagit，把用户 IB 包进 `CP_SET_PROTECTED_MODE`（`0x5f 0` … `0x5f 1`）——本机唯一被 CP 成功执行的间接缓冲（GPMU）正是这样包裹的；提交前采样并清除 owned IRQ latch（sagit `start()`/旧提交路径行为），遇到已锁存 CP fault 拒绝踢门铃；超时后做**一次**有界寄存器采样（`RBRead/RBWrite/InterruptStatus/CPInterruptStatus`）区分“门铃没被看到”与“时间戳丢失”。
- 候选 `out/p21/p21-harrier`：内核 `781fa87710db6e0aa51bfb8132cf007d7aacf0d9311f1a2c76ef1048a4356453`（编译产物 `744cef52…`）、payload 展开 `8f2193b2…`、固件 `100367d6…`、镜像 `out/p21/p21-harrier/uefi/boot-p21-up1.img` SHA-256 `68f88fa611dc4feacb2274489e10520e1026352905d7de4e0182dad665006b53`，gate `20261103T181306Z` PASS（670 B，三项 pin 命中）。misc（`/dev/block/sda4`）仅写首 14 字节 `boot-recovery\0`，其余 4082 字节与写前镜像一致，回读一致；18:13:30Z 单次 `fastboot boot` Sending/Booting OKAY（`out/p21/p21-harrier/boot-once.log`）。
- **P21-Harrier 实机（boot `0x47806558`，日志 `out/p21/persistent-logs/20261104T074813Z/`）**：18:13:30Z 启动，设备在自动返回时限内没有回到 Recovery；用户手动恢复后回收。
  - msgbuf 停在 142.842 s，**最后一条 GPU 记录是 141.862 s 的 `CB GPU engine: pre pwr=0 …`（提交入口）**，之后没有 `GPFL`、没有 `GSUB`；`GPSVC ready=1 open=1 bo=2 map=2 **sub=0** err=0`，即提交调用**没有返回**；`GRNG seq=3 34..51 n=17` 记录下了带 `0x5f` 的完整流（`70928000/70268000/70df0001/0/70bf8003/2012200 …`）。
  - 该轮唯一新增且在超时后执行的动作是**一次 `RBRead/RBWrite/InterruptStatus/CPInterruptStatus` 采样**；日志正好停在 1 s fence 超时之后、该采样所在位置，随后再无内核输出，与“GPU 总线上的读把调用方卡死”一致。**没有拿到 panic 文本**。
  - 处理：提交 `7eccff1` 整体回退到 P21-Falcon（`516c248` 的全部改动，`git diff f7c26ba` 为空），恢复为可启动的版本；失败后不再读任何 GPU 寄存器。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Falcon** | `455975b3…` | `41a74e7f…` | sagit 引擎 + preflight IB；CP 未执行任何新包（sent=0/fr=0） |
  | 2026-11-04 | **P21-Harrier** | `68f88fa6…` | `781fa877…` | 用户 IB 包裹 `0x5f` + 超时后寄存器采样；内核在提交内卡死（日志止于 142.8 s，sub=0），已整体回退 |

## 2026-11-04：P21-Crane 结果（fence(+4) 有效，但用户批次仍不执行）· P21-Eagle

- **P21-Crane 实机（boot `0x45799f9b`，日志 `out/p21/persistent-logs/20261103T170449Z/`）**：68 轮、0 写错误；显示 `frames=5`。probe `CB GPU start: fence OK seq=1`（fence 改到 +4 后仍工作，说明机制正确）；用户提交仍 `GSUB rc=-4 stage=3`、`fr=28`（RPTR shadow）。**fence 位置修复没有单独解决问题。**
- 结论：CP 没有执行用户批次，而 probe（count=0、写 fence 到同一 +4）可执行。差别只剩：用户批次内的命令/`0x3f` 以及**新建映射对 CP 翻译缓存是否可见**。内核侧 SMMU TLBI（pinned `tlbSync` 与 kit LiveUpdate）已做，但 CP 自身的翻译缓存需要 sagit 的 CP 侧 `0x53 CP_SMMU_TABLE_UPDATE` + `0x43 TLB invalidate`。此前该序列被认为"停滞"，但那是在 fence 检测本身错误的前提下得到的结论。
- **P21-Eagle 修复（单变量）**：关闭 inline（恢复 670 B 参数），在用户提交的 ring 批次中发送 **kit 已编程的 TTBR0**，触发 sagit 的 CP 表更新/TLB 失效序列；TTBR 来自 `ArmSmmu4KProgramResult.readback.ttbr0`。
- 候选 `out/p21/p21-eagle`：内核 `768981a481da2bc9b1eff0844a08994357b1f253804a3564ef6ce44565a79332`、payload 展开 `17f730bb…`、固件 `390851f5…`、镜像 `out/p21/p21-eagle/uefi/boot-p21-up1.img` SHA-256 `6effa30baf480b40bbf6f44a7b568884a2a4d31f7c2d87a698197ee88b4c99c5`，gate `20261103T171210Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；17:12:40Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`GSUB rc=0/fenced`、目标 BO 是否出现 `adc0de1`。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Crane** | `2500410b…` | `8ae3f102…` | fence 移到 +4；probe 正常，用户批次仍 stage=3 |
  | 2026-11-04 | **P21-Eagle** | `6effa30b…` | `768981a4…` | 发 CP 侧 TTBR 表/TLB 更新；结果待回收 |

## 2026-11-04：P21-Raven 结果（内联 IB 仍不动；fence 与 RPTR shadow 冲突）· P21-Crane

- **P21-Raven 实机（boot `0x4545dd2f`，日志 `out/p21/persistent-logs/20261103T164835Z/`）**：68 轮、0 写错误；`GSUB rc=-4 stage=3`、`fr=28`，内联 IB 没有改变结果（诊断路径是否真正生效仍需日志标记，但结论指向更基础的问题）。
- **sheng 案例（`NativeAdrenoRing.cpp`）**：`emitFence`/`emitFramedFence` 明确 "The timestamp goes to +4 (msm_rbmemptrs.fence) … +0 is the CP's RPTR shadow"。我们的引擎把 `0x46` fence **写到 dword0** 并轮询 dword0，正好与 CP 的 RPTR shadow 冲突；历轮的 `fr=0x28/0x2d/0x34` 正是 RPTR 值，而我们的 sequence 可能被影子覆盖 → **完成检测误判超时**，CP 也可能写入了目标。
- **P21-Crane 修复**：fence 写/轮询改到 `fence+4`（`+0` 保留给 RPTR shadow）；`engine_wait` 与 probe 的三处 fence 读取同步改为 `[1]`；宿主模型同步更新并通过（`HOST_MODELS_PASS`）。
- 候选 `out/p21/p21-crane`：内核 `8ae3f10285a01bc379d09e33aaeb252d6da1a6db9296d87a1f74bd0eee17bb35`、payload 展开 `a50fe00f…`、固件 `6484b87c…`、镜像 `out/p21/p21-crane/uefi/boot-p21-up1.img` SHA-256 `2500410bd2784539910cc10a5bf75ffaf70e9757f2d44b75cacd19abab450311`，gate `20261103T165729Z` PASS（686 B）。misc 首块仅 `boot-recovery`、回读一致；16:57:48Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`GSUB rc=0/fenced`、probe `fence OK seq` 与 `userpath fence`、目标 BO 是否出现 `adc0de1`。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Raven** | `912ee99d…` | `643793ec…` | 内联 IB 仍 stage=3；发现 fence 与 RPTR shadow 冲突 |
  | 2026-11-04 | **P21-Crane** | `2500410b…` | `8ae3f102…` | fence 移到 +4，避免与 RPTR shadow 冲突；结果待回收 |

## 2026-11-04：P21-Pelican 结果（prefence 排除，IB 取指成唯一差异）· P21-Raven

- **P21-Pelican 实机（boot `0x455dbfb8`，日志 `out/p21/persistent-logs/20261103T163200Z/`）**：68 轮、0 写错误；显示 `frames=5`。
- `pf=0/0`（prefence 已移除、批次结构回到"屏障→IB→屏障→fence"），但提交仍 `GSUB rc=-4 stage=3`、`fr=2d`。**prefence 诊断包被排除**；probe（count=0）批次可用，唯一新增元素是 `0x3f` IB 取指。
- `fr` 记录来自 CPU fence 页；历史值 `0x2d/0x34` 更像是 RPTR 影子（与 fence 共用 dword0）的值，而不是我们的 sequence，说明批次没有被完整执行。
- **P21-Raven 决定性对照（诊断开关 `gpu-inline-ib=1`）**：Service 把客户端第一个 IB 的内核视图交给引擎；`Startup::submit` 在开关打开时把这些 dword **直接内联进 ring**（不再发 `0x3f`），其余批次结构不变。若 CP 写 fence/目标 BO → 问题在 IB 取指；若仍不动 → 问题在 CP 执行/门铃路径，与 IB 无关。
- 候选 `out/p21/p21-raven`：内核 `643793ec5c6445dfcbfe99fae0fb4f30ed570f7b00477ef6e356a5879926f2eb`、payload 展开 `6a5ab431…`、固件 `e57e5672…`、镜像 `out/p21/p21-raven/uefi/boot-p21-up1.img` SHA-256 `912ee99d4252836582033036b81edfed8244ae612f2b1c2079582c70cb27bc0e`，gate `20261103T163958Z` PASS（686 B，含 `gpu-inline-ib=1`）。misc 首块仅 `boot-recovery`、回读一致；16:40:18Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：内联后 `GSUB`/fence/目标 BO 是否变化。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-04 | **P21-Pelican** | `ce2c3cec…` | `b7a03453…` | 移除 prefence 诊断包；仍 stage=3 → prefence 排除 |
  | 2026-11-04 | **P21-Raven** | `912ee99d…` | `643793ec…` | 诊断：客户端 IB 内联进 ring，验证是否 IB 取指问题；结果待回收 |

## 2026-11-03/04：P21-Owl 结果（ATOS 变量已生效，排除）· P21-Pelican

- **P21-Owl 实机（boot `0x4590ad33`，日志 `out/p21/persistent-logs/20261103T155827Z/`）**：68 轮、0 写错误；显示 `frames=5`。
- `bo atos0/1 ... par=10ac25000/10ac24000`（等于 PA，非硬件 PAR）——**两处 ATOS 都已跳过，变量真正生效**；提交仍 `GSUB rc=-4 stage=3`、`pf=0/1`。**ATOS 对 live CB0 的改写被排除。**
- 剩余差异只剩 **提交前的 prefence 诊断包**：`userPath && count!=0` 时在 IB 前写入一个 `0x46 CACHE_FLUSH_TS`，目标是 `fence+4`，值为 `0x1000|seq` 且**不带 IRQ 位**；probe 已验证的批次没有这个包，而 `pf=0/1` 表明 CP 连它都没执行。
- **P21-Pelican 修复**：`prefence` 固定为 false，用户批次回到已验证结构（屏障 → IB → 屏障 → fence）。宿主模型同步更新（`cpufence/no-prefence-diagnostic`、`ib-before-fence`），`HOST_MODELS_PASS`。
- 候选 `out/p21/p21-pelican`：内核 `b7a03453316b710c03ba0c0da5ad5a1d27b4e72015182565622b1681d538521f`、payload 展开 `864f096b…`、固件 `7268d5d0…`、镜像 `out/p21/p21-pelican/uefi/boot-p21-up1.img` SHA-256 `ce2c3ceca6b31b76060a874057b25fc68640b4be0cc40289a4d9688817f87736`，gate `20261103T160555Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；16:06:15Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`GSUB` 是否越过 stage=3、目标 BO 是否出现 `adc0de1`。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Owl** | `c3d160ce…` | `a5d8ab7a…` | 两处 ATOS 都跳过（变量生效）；仍 stage=3 → ATOS 排除 |
  | 2026-11-04 | **P21-Pelican** | `ce2c3cec…` | `b7a03453…` | 移除 IB 前的 prefence 诊断包，恢复已验证批次结构；结果待回收 |

## 2026-11-03：P21-Kestrel 结果（ATOS 仍有第二处未改，变量未真正生效）· P21-Owl

- **P21-Kestrel 实机（boot `0x474ff1cd`，日志 `out/p21/persistent-logs/20261103T154312Z/`）**：67 轮、0 写错误；显示 `frames=5`；提交仍 `GSUB rc=-4 stage=3`。
- **关键纠正**：日志仍有 `bo atos0/1 ... par=40000001…` —— ATOS **实际仍在执行**。原因是 ATOS 有两个调用点：`engine_submit` 的预检（Kestrel 已跳过）和 **Service 提交里的每个 BO 遍历**（`CheeseburgerGpuService.cpp:548`，Kestrel 未覆盖）。因此上一轮"跳过 ATOS"的变量没有真正改变（这也说明后续实验必须先验证变量确实生效）。
- **P21-Owl 修复**：Service 的 BO 遍历在 4K 激活时也跳过 pinned ATOS，`par` 直接填软件 lookup 得到的 PA；这样门铃前不再有对 live CB0 的 halt/SCTLR/CB_RESUME/FSR 改写。日志会出现 `bo atos … par=<PA>`（而非 `40000001…`），用于确认变量真的生效。
- 候选 `out/p21/p21-owl`：内核 `a5d8ab7a1e15c8b508f84ca01372cf94e79fd440aecca31adfdb1d1979685a2a`、payload 展开 `6c9d967a…`、固件 `7f05727c…`、镜像 `out/p21/p21-owl/uefi/boot-p21-up1.img` SHA-256 `c3d160ce675bbfeab6480b3881eaf54d8f6a50e3c5946d3bed17ea79b05a229c`，gate `20261103T155018Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；15:50:38Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`bo atos … par=<PA>`（确认变量生效）、`GSUB` 是否越过 stage=3。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Kestrel** | `edaf2e01…` | `34368330…` | 只跳过 engine 预检的 ATOS；Service 遍历仍执行，变量未生效 |
  | 2026-11-03 | **P21-Owl** | `c3d160ce…` | `a5d8ab7a…` | 两处 ATOS 在 4K 下都跳过；结果待回收 |

## 2026-11-03：P21-Jay 结果（缓存维护无效；ATOS 改写 live CB0 成新嫌疑）· P21-Kestrel

- **P21-Jay 实机（boot `0x460367ea`，日志 `out/p21/persistent-logs/20261103T151759Z/`）**：68 轮、0 写错误；显示 `frames=5`。BO 页 `pa=10ac24000/10ac23000`（ring 同区），ATOS `rc=0`；提交仍 `GSUB rc=-4 stage=3`，**IB 缓存维护（内核视图 clean+invalidate）无效**。
- **纠正一个判读**：`rptr/wptr/irq=ffffffff` 在 `cpuFenceOnly` 路径里是**占位值**（提交失败后不读总线），不是总线挂死的证据；事实是 prefence 未被执行（`pf=0/1`），CP 未消费该批次。
- 仍失败的环节：用户提交前有**两次 ATOS 预检**会改写 live CB0——halt micro-MMU、清 SCTLR.stall、`CB_RESUME=TERMINATE`、清 FSR；probe 的 ATOS 在 CP 启动前执行，而这里的 ATOS 在 139 s 对运行中的上下文执行，可能导致 CP 后续取指停住。另注：sagit 的 `0x53/0x43` page-table 序列此前实测会令 CP 在 prefence 前停滞（`pf=0/1`），故没有重新启用。
- **P21-Kestrel 修复**：当 4K 激活时，提交预检只做 kit 映射的软件 lookup（LiveUpdate 已验证 leaf），**跳过 pinned ATOS 的硬件改写**，把 live CB0 原样留给即将取指的 CP。
- 候选 `out/p21/p21-kestrel`：内核 `34368330430f4d8108d0ac23341a39528e138b39d996b771b3520dff811ae2cc`、payload 展开 `6ffaed9d…`、固件 `e02a6c3d…`、镜像 `out/p21/p21-kestrel/uefi/boot-p21-up1.img` SHA-256 `edaf2e01c841f37b1b4a979e78ab4640bbb335afba98b0401dc6094607e03c1d`，gate `20261103T153005Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；15:30:27Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`GSUB` 是否越过 stage=3、目标 BO 是否出现 `adc0de1`。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Jay** | `4c594aba…` | `3e211b01…` | 门铃前 IB 缓存维护；仍 stage=3（无效） |
  | 2026-11-03 | **P21-Kestrel** | `edaf2e01…` | `34368330…` | 4K 下跳过会改写 live CB0 的 ATOS 预检；结果待回收 |

## 2026-11-03：P21-Ibis 结果（PA 假设推翻）· P21-Jay

- **P21-Ibis 实机（boot `0x4d418916`，日志 `out/p21/persistent-logs/20261103T145439Z/`）**：66 轮、0 写错误；显示 `frames=5`。
- `GLOP pool=8 first=10ac1f000`，BO 页 `pa=10ac26000/10ac25000`——**已与 ring/PM4 同区**（`0x10ac…`，CP 取指正常区），ATOS `rc=0 par` 正确；提交仍 `GSUB rc=-4 stage=3`、总线 `ffffffff`。**用户页物理地址假设被推翻。**
- **sheng 案例（`NativeAdrenoSubmit.cpp` 注释与实现）**：记录"用户 BO 为 write-combined、未做维护时，IB 若仍在 CPU 缓存，CP 会读到旧/零命令并停在第一个 IB"；其修法是在门铃前对 IB 做缓存维护（`reassertWriteCombined` + `auditRange`）。用户 IB 是首次 `0x3f` 取指，probe 自身从未提交 IB，因此这一症状与本轮完全吻合。
- **P21-Jay 修复**：`AGXDeviceUserClient::submit()` 在门铃前对每个被提交 BO 的物理页做 `flush_dcache`（clean+invalidate，经内核同物理页视图），再执行 `engine_submit`；与 sheng 的提交前维护一致。
- 候选 `out/p21/p21-jay`：内核 `3e211b0119a348ff3d587968f8ecb8e0de3c03297fe57ead20254e5e9bdfc591`、payload 展开 `07704579…`、固件 `f529a34a…`、镜像 `out/p21/p21-jay/uefi/boot-p21-up1.img` SHA-256 `4c594abaf8113b4784a17830b8ac8f5dde372d391c6f108d34c738293c40ffe8`，gate `20261103T150610Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；15:06:31Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`GSUB` 是否越过 stage=3，目标 BO 是否出现 `adc0de1/feed0002/600d0003`。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Ibis** | `0497cf11…` | `709fa722…` | 用户页已在 ring 同区、ATOS 正确，仍 stage=3 → PA 排除 |
  | 2026-11-03 | **P21-Jay** | `4c594aba…` | `3e211b01…` | 门铃前维护 IB 缓存行（sheng 提交前维护）；结果待回收 |

## 2026-11-03：P21-Heron 结果（4 KiB 分配类仍落高区）· P21-Ibis

- **P21-Heron 实机（boot `0x46fbb26b`，日志 `out/p21/persistent-logs/20261103T143834Z/`）**：67 轮、0 写错误，msgbuf 到 ~398.7 s，400 s 计时正常回 Recovery；显示 `frames=5`。
- 用户页改在 ring 之后用 4 KiB 连续分配预留，但 `CB GPU low pool: count=8 first=128668000`，BO 页 `pa=128680000/12866e000`——**4 KiB 连续分配这一类仍落在 0x128… 高区**；ATOS 仍 `rc=0`，提交仍 `GSUB rc=-4 stage=3`、总线 `ffffffff`。
- 观察：ring/PM4/PFP/GPMU 是 **32/20 KiB 连续块**，落在 `0x10ac…`（CP 取指正常）；4 KiB 连续块（fence、pool）落在 `0x1275c/0x1286…`。分配大小类别对应不同的物理区。
- **P21-Ibis 实验**：按 ring 的尺寸类别分配一块 **32 KiB 连续块**，切成 8 个 4 KiB 页给用户 BO；`boMap` 改用分配时记录的 `bo->physical`（不再用 `getPhysicalSegment(0)`，那只返回块首）。若 IB 取指恢复 → 用户页 PA 假设成立；若仍 stage=3 → 排除 PA。
- 候选 `out/p21/p21-ibis`：内核 `709fa72267cabae04d0248710e8ba69924126b24c111d6b3bea6cc4c4fde180f`、payload 展开 `d2e65e80…`、固件 `36ca5061…`、镜像 `out/p21/p21-ibis/uefi/boot-p21-up1.img` SHA-256 `0497cf1195570b904834445d0b2933e2b350086a2be7c85d18f808f6027a5ded`，gate `20261103T144640Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；14:47:11Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`CB GPU low pool` 的 PA 是否落在 `0x10ac…`、`GSUB` 是否越过 stage=3。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Heron** | `5612905c…` | `81dcbd26…` | 4 KiB 分配类落 0x1286…，提交仍 stage=3 |
  | 2026-11-03 | **P21-Ibis** | `0497cf11…` | `709fa722…` | ring 尺寸的 32 KiB 块切页；验证用户页 PA 假设；结果待回收 |

## 2026-11-03：P21-Finch 结果（ATOS 通过，CP 取用户 IB 挂总线）· P21-Heron

- **P21-Finch 实机（boot `0x46772d33`，日志 `out/p21/persistent-logs/20261103T141233Z/`）**：68 轮、0 写错误，msgbuf 到 ~398.7 s，400 s 计时正常回 Recovery；显示 `frames=5`。
- 用户映射/提交继续正常：`GPSVC map=2 sub=1`；**ATOS 预检通过**：`bo atos0 par=4000000127e6cb80` 与 `expect=127e6c000` 一致，`atos rc=0`。
- 提交回到 `GSUB rc=-4 stage=3`：`rptr=ffffffff wptr=ffffffff irq=ffffffff cpirq=ffffffff fr=34`。门铃后总线不可读，说明 CP 在取用户 IB 时把 GPU/AHB 挂死。
- 对比：probe 的 ring/PM4/PFP/GPMU 页在 `0x10ac…`（CP 取指正常），用户 BO 页在 `0x127e6c000`（早先 pool/分配器落点）。IB 头部内容有效（两个 `0x3d` CP_MEM_WRITE 到 `0x4001000/0x4001004`），`0x3f` 编码与 sheng `NativeAdrenoSubmit.cpp` 的 3-word payload 一致。
- **P21-Heron 实验**：把用户页池改为在 **ring 分配之后**立刻用同一连续分配器预留 8 个单页（不再用带 4 GiB 掩码的失败路径 + 回退池），让用户页尽量落在与 ring 相邻的物理区；若 IB 取指恢复，则问题是用户页 PA 落在 CP 不可读区；否则排除 PA，转查 IB 取指/属性。
- 候选 `out/p21/p21-heron`：内核 `81dcbd26c17b642da2d1f2debfb0652a11dade71b298b1e1610f772dd828bb73`、payload 展开 `b2993860…`、固件 `63b8a04e…`、镜像 `out/p21/p21-heron/uefi/boot-p21-up1.img` SHA-256 `5612905cfb1811c3591efe31a4a353bb56ceaed1f72895164126d2e8e40f5cd7`，gate `20261103T142128Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；14:21:48Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：低池首个 PA 是否落在 ring 相邻区、`GPSVC map=2 sub=1` 下 `GSUB` 是否越过 stage=3。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Finch** | `dd9a8267…` | `c3c356c5…` | ATOS 通过；CP 取用户 IB 时总线 `ffffffff`，stage=3 |
  | 2026-11-03 | **P21-Heron** | `5612905c…` | `81dcbd26…` | 用户页改在 ring 分配窗口预留，验证 PA 假设；结果待回收 |

## 2026-11-03：P21-Kite 结果（用户映射+提交打通，ATOS 门禁用错表）· P21-Finch

- **P21-Kite 实机（boot `0x4e145190`，日志 `out/p21/persistent-logs/20261103T135359Z/`）**：66 轮、0 写错误，msgbuf 到 ~395 s，400 s 计时正常回 Recovery；显示 `frames=5`。
- **用户态映射与提交首次打通**：`GPSVC open=2 bo=2 map=2 pages=2 unmap=2 sub=1 calls=14`（`GPSVC map=2`、`sub=1`）。
- 提交仍失败：`GSUB rc=-1 stage=129`（0x81）。日志 `bo atos0/1 ... pte=0 rc=-1 par=0`、`atos rc=-1`：提交前的预检 `cheeseburger_gpu_smmu_lookup()` 仍走 **pinned 软件表**，4K 激活时该表为空，直接判失败，ATOS 未执行。
- **P21-Finch 修复**：新增 `cheeseburger_gpu_smmu4k_lookup()`（按 kit 的 `gRanges` 软件镜像查 IOVA→PA）；`cheeseburger_gpu_smmu_lookup()` 在 4K 激活时转调它（PTE 位置放 PA 供诊断），随后 ATOS 仍做硬件校验。同时修正 `ArmSmmu4KRange` 字段名（`physical` 而非 `pa`）。
- 候选 `out/p21/p21-finch`：内核 `c3c356c5c9fdd132d5bdbd3d451b51ddd678d045202e956e0315ee017b67adb9`、payload 展开 `292ed0f5…`、固件 `8a6ec4b5…`、镜像 `out/p21/p21-finch/uefi/boot-p21-up1.img` SHA-256 `dd9a8267657dc4832dc9da59e0f3e92f44c27fadb4cf5608c2097a6caa278f18`，gate `20261103T140402Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；14:04:22Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`GPSVC sub=1` 下 `GSUB` 是否越过 stage=129（是否出现 fence 或新 stage）。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Kite** | `99da8f55…` | `4aa05439…` | 空环 quiesced；用户 map=2/sub=1 打通，ATOS 预检用错表 → stage=129 |
  | 2026-11-03 | **P21-Finch** | `dd9a8267…` | `c3c356c5…` | 4K 下 lookup 走 kit 映射；让提交预检通过并观察 fence；结果待回收 |

## 2026-11-03：P21-Osprey 结果（4K 下 probe 全通，BO 映射被运行时排空拦截）· P21-Kite

- **P21-Osprey 实机（boot `0x488980ca`，日志 `out/p21/persistent-logs/20261103T133820Z/`）**：67 轮、0 写错误，msgbuf 到 ~395 s，400 s 计时正常回 Recovery；显示 `frames=5`。
- 4K 激活后 probe 全部通过：ring/PM4/PFP/GPMU/fence 的 `CB GPU CP map ... r=0`、`GCTL rc=0 stage=7 fence=2`、`fence OK`、`userpath fence rc=0`、`kick rc=0`。
- 用户服务 `GPSVC open=1 bo=2 calls=5`，但 `map=0 err=1 st=e00002bd sel=3`：selector 3 是 `BoMap`，`cheeseburger_gpu_smmu_map` 失败后 boMap 返回 `kIOReturnNoMemory`。日志中 GPW：`me=0 vbif=f0000`，新 `mapping_begin` 在运行时引擎要求 `me==0 && vbif==0` 才能停流，`f0000` 使其失败并置 `gEngineSubmitFailed`。
- **sheng 对照**：sheng 更新映射时以"masters quiesced + 每次更新 TLBIALL"为前提，不做 CP/VBIF 停流；探针完成后环为空（`rptr=wptr`），没有在途取指，正是该前提。
- **P21-Kite 修复（提交 `04e5568`）**：`mapping_begin` 在 `gEngine` 存在时先读 `RBRead/RBWrite`；环为空（`r==w`）则按 sheng 前提直接更新表，不做停流；仅当环非空才执行 `drainForTables`。engine 未构造时依旧直接放行。
- 候选 `out/p21/p21-kite`：内核 `4aa05439955631245e01902cd9c59a72bae5d80c7020088a0dafe5f45d03f7d0`、payload 展开 `13848960…`、固件 `fb33457e…`、镜像 `out/p21/p21-kite/uefi/boot-p21-up1.img` SHA-256 `99da8f559ec435d96c8d135e5d0920ad7916bf9e7387f2766e16b151d090fbf5`，gate `20261103T134609Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；13:46:39Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`GPSVC map=2`、`sub=1`、`GSUB` 是否越过 stage=3。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Osprey** | `90f28c3d…` | `06911dcd…` | 4K 下 probe 全通；用户 BO 映射被运行时排空拦截（NoMemory） |
  | 2026-11-03 | **P21-Kite** | `99da8f55…` | `4aa05439…` | 空环视为 quiesced，不再停流；让用户映射与提交继续；结果待回收 |

## 2026-11-03：P21-Nightingale 结果（4K 首次激活；engine 前映射被排空拦截）· P21-Osprey

- **P21-Nightingale 实机（boot `0x4f5fdd5a`，日志 `out/p21/persistent-logs/20261103T132004Z/`）**：66 轮、0 写错误，msgbuf 到 ~396.6 s，400 s 计时正常回 Recovery；显示 `frames=8`。
- **4K 首次真正接管**：`route slot=0 sid=0 s2cr=0 smr=80000000`、`secure sid=2 smr=0 not writable; left to firmware`、`bank0 sid0 routed (sid2 secure-owned)`、`CB GPU SMMU: sheng ArmSmmu4K path active`（75.37 s），`SIRQ ready=1`。
- 但 probe 随即失败：`CB GPU CP map begin: iova=2000000 ... seq=0` → `CB GPU CP map: ... r=-3`（85.4 s）。原因在新 `cheeseburger_gpu_mapping_begin()`：即使 `gEngine` 尚未构造也执行 `drainForTables`，失败即置 `gEngineSubmitFailed` 并返回 -1；随后 `GCTL rc=-16657 stage=0 fence=dead`、`GPSVC open=0`。
- **对照**：sheng 在 SMMU 编程时不排空，只要求平台保证 master 静止；本机映射发生在 engine 构造与上电之前，没有 CP/VBIF 属主可排空。
- **P21-Osprey 修复（提交 `2abd266`）**：`mapping_begin` 仅在 `gEngine` 已存在时执行 `drainForTables`；engine 未构造阶段直接返回 token，映射照常建立（与 sheng 的时序一致）。
- 候选 `out/p21/p21-osprey`：内核 `06911dcd908421b596d70e2d543463dc0967034fdf4c952b0d9a5f5eb4f1b005`、payload 展开 `9edc3c17…`、固件 `a0a3dd2c…`、镜像 `out/p21/p21-osprey/uefi/boot-p21-up1.img` SHA-256 `90f28c3de14ce5a2c5fcb0f5b833868c634039499dfba1c528b851e2a622fada`，gate `20261103T132830Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；13:28:55Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：ring/PM4/fence 的 `CB GPU SMMU map ok=1`、`GCTL rc=0`、`GPSVC open=1`，以及 `GSUB` 是否越过 stage=3。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Nightingale** | `65e65476…` | `af6aca86…` | 4K 激活（sid0 路由、sid2 交给固件）；engine 前映射被排空拦截，probe 失败 |
  | 2026-11-03 | **P21-Osprey** | `90f28c3d…` | `06911dcd…` | engine 未构造时不做排空，让 4K 下的映射与 probe 继续；结果待回收 |

## 2026-11-03：P21-Martlet 结果（SID0 路由成功，SID2 被安全世界锁定）· P21-Nightingale

- **P21-Martlet 实机（boot `0x44ab7124`，日志 `out/p21/persistent-logs/20261103T130402Z/`）**：68 轮、0 写错误，msgbuf 到 ~398 s，400 s 计时正常回 Recovery；显示 `frames=5`；4K 仍 `-8`（GPU 禁用）。
- `route slot=0 sid=0 s2cr=0 smr=80000000`：**SID0 路由成功**（sheng bypass 序列在用户流上有效）。
- `route slot=2 sid=2 s2cr=0 smr=0`：SID2（`gfx3d_secure`）的 SMR 写入被忽略、回读 0 → 该 secure 流由安全世界（TZ）持有，NS 不可写；随后 `-8`。
- **sheng 对照**：上游没有 secure 流的接管案例（`SmmuBypass.h` 只覆盖普通 bypass 流）。本机 DTB `gfx3d_secure`=SID2，属于受保护内容通道；普通渲染只用 SID0。
- **P21-Nightingale 修复**：路由循环中 SID0 必须成功；SID2 若 SMR 不可写则记录 `secure sid=2 smr=… not writable; left to firmware` 并继续，不把整个 4K 路径判失败。随后仍执行 `program → observe → visibility` 与 fault 检查。
- 候选 `out/p21/p21-nightingale`：内核 `af6aca8629708af8fe2e44353a5dbc24c91bab2a879499fc8929d8f44289a696`、payload 展开 `8fea3351…`、固件 `d4463114…`、镜像 `out/p21/p21-nightingale/uefi/boot-p21-up1.img` SHA-256 `65e65476e96ac38cb3c29962de3fb18e436df4f70723471f08eaf5dca191203b`，gate `20261103T131227Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；13:12:53Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`bank0 sid0 routed (sid2 secure-owned)`、`cb_smmu[20]=0x4b340001`，以及用户提交 `GSUB` 是否从 `stage=3` 前进。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Martlet** | `d2f3c460…` | `ae8ddaf1…` | SID0 路由成功；SID2 secure SMR 不可写 → `-8` |
  | 2026-11-03 | **P21-Nightingale** | `65e65476…` | `af6aca86…` | SID2 交给安全世界，仅路由 SID0；结果待回收 |

## 2026-11-03：P21-Robin 结果（FAULT 不可回写；找到 sheng bypass 案例）· P21-Martlet

- **P21-Robin 实机（boot `0x44a78308`，日志 `out/p21/persistent-logs/20261103T124601Z/`）**：68 轮、0 写错误，msgbuf 到 398.7 s，400 s 计时正常回 Recovery；显示 `frames=8`；4K 仍 `-8`（GPU 禁用），`GPSVC open=0`。
- `normalize s2cr slot=0 banked=0 fault=0`：写 bank(0) 生效并回读 0；再写 FAULT(`0x20000`) 回读仍为 0。说明本固件不接受 FAULT 回写（sheng `SmmuBypass.h` 注释亦指出某些 TYPE 在该固件回读成 FAULT）。
- **找到可合并的 sheng 案例**：`upstream/UniXNU-sheng/package/SmmuBypass.h` 是该平台自己的流路由方案——`S2CR=bank` 先写并回读，再写 `SMR=0x80000000|sid`；从不写 FAULT。kit 的"必须先精确 FAULT"前置条件在此固件不适用。
- **P21-Martlet 修复**：上下文仍由 kit `prepare/program/observe` 与 `LiveUpdateContext` 负责；流的接管改用 sheng bypass 序列（S2CR=bank 回读 → SMR=Valid|sid 回读），跳过 `CheckFaultedStreams`/`attach`，随后 `observe()`、可见性绑定与 fault 检查。
- 候选 `out/p21/p21-martlet`：内核 `ae8ddaf14736cb720987c29aa091ab69895b3272a2c46bb4a02eaea2df492b46`、payload 展开 `6ee1b535…`、固件 `3406a42e…`、镜像 `out/p21/p21-martlet/uefi/boot-p21-up1.img` SHA-256 `d2f3c4603ca3c088f504e1ba0bc03441ae2135d0225b1d84498f8a74bf3bcbc4`，gate `20261103T125544Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；12:56:04Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`route slot=0 sid=0 s2cr=0 smr=80000000`、`route slot=2 sid=2 ...`、`cb_smmu[20]=0x4b340001`，以及用户提交是否仍 stage=3。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Robin** | `0c9e26e6…` | `e1eeeb30…` | 先写 bank 再写 FAULT；FAULT 不可回写，`-8` |
  | 2026-11-03 | **P21-Martlet** | `d2f3c460…` | `ae8ddaf1…` | 合并 sheng `SmmuBypass.h` 序列接管 SID0/SID2；结果待回收 |

## 2026-11-03：P21-Wren 结果（4K 到 slots，CBNDX 不可直写）· P21-Robin

- **P21-Wren 实机（boot `0x45587522`，日志 `out/p21/persistent-logs/20261103T122923Z/`）**：68 轮、0 写错误，msgbuf 到 **397.49 s**，心跳 338 s，400 s 计时正常回 Recovery；显示 `frames=5`；因 4K 在 66 s 失败并走 `-8`，`GPSVC ready=0 open=0`、无提交。
- 4K 已越过冷启动采样：`CB GPU SMMU4K: cold r/w/me/vbif/im=0/0/303/0/0`，随后 `step=slots slot=0 smr=0 s2cr=200ff` → 写 `0x20000` 后回读仍是 `0x200ff`，`-8` 禁用 GPU。
- **sheng 对照（`ArmSmmu4KStreams.c`）**：`FAULT = 2<<16`；翻译路由写 `bank|valid_s2cr`；`scan()` 要求未附着目标流**精确等于 FAULT**；`bank>255` 拒绝。S2CR 的 CBNDX 字段只在翻译类型下可写，直接写 FAULT 不会清掉 `0xff`。
- **P21-Robin 修复**：规整序列改为"先写 bank(0)（翻译类型、写 CBNDX），再写 FAULT"，并记录 `banked`/`fault` 两次回读；成功后再走 kit 的 `CheckFaultedStreams → program → attach`。
- 候选 `out/p21/p21-robin`：内核 `e1eeeb302675f027503d2214690680bbf04fcec13566878d9e165db5b1475039`、payload 展开 `c603f4cb…`、固件 `45d84968…`、镜像 `out/p21/p21-robin/uefi/boot-p21-up1.img` SHA-256 `0c9e26e6bb6f99af7c8ef7c8c257020a5fddc90fd2e31b672640aef5933aeb40`，gate `20261103T123806Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；12:38:24Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`normalize s2cr slot=0 banked=… fault=20000`、`bank0 streams sid0/sid2 attached`、`cb_smmu[20]=0x4b340001`。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Wren** | `2d9f2bca…` | `a2a614c1…` | 冷启动只读采样、不排空；4K 到 slots，`0x20000` 写入不生效 → `-8` |
  | 2026-11-03 | **P21-Robin** | `0c9e26e6…` | `e1eeeb30…` | 先写 bank 再写 FAULT 规整 S2CR；结果待回收 |

## 2026-11-03：P21-Tern 结果（冷启动读值有效，排空仍失败）· P21-Wren

- **P21-Tern 实机（boot `0x469fd0f4`，日志 `out/p21/persistent-logs/20261103T121338Z/`）**：68 轮、0 写错误，msgbuf 到 **398.43 s**，心跳 338 s；400 s 计时正常回 Recovery；显示 `frames=5`，`GPSVC open=1 sub=1`、2 个 BO 映射，用户提交仍 `GSUB rc=-4 stage=3 fr=34`（CP 取环失败，既有问题）。
- `SIRQ ... ready=1`，`CB GPU SMMU IRQ: attach rc=0 rb=a002 ctlr=12 ds=0 id=361`。
- `CB GPU SMMU4K: cold drain unverified r/w/me/vbif/im=0/0/303/0/0 unpowered=0` → 冷读**不是**全 1，排空检查失败（我们不做写入前的排空判断），按 `-10` 回退旧序列 → GPU 保持可用，系统稳定。
- **sheng 对照**：`NativeAdrenoGmuContext` 的顺序是 `读 ID2 → allocate → prepare → program → attach → vm.attach → map`，**没有 drain 步骤**；`ArmSmmu4KLive.h` 只把"所有 master 静止"列为平台前置条件。本机 4K setup 发生在 `CB GPU start: begin`（~106 s）之前，冷读 `r=0 w=0` 证明环为空、CP 未启动，满足该前置条件。
- **P21-Wren 修复（提交 `9c6ecb7`）**：
  1. 冷启动改为**只读采样**（不再写 MEControl/VBIFHalt 做排空）；`r==w` 即视为 quiescent 继续 4K 编程，仅当读取失败或 `r!=w`（有在途提交）才 `-10` 回退。
  2. 为 4K 的每个 `-8` 站点加步骤日志：`step=micro/slots/preflight/fault-after-preflight/program/fault-after-program/attach/visibility/fault-after-attach`，下次可直接定位。
- 候选 `out/p21/p21-wren`：内核 `a2a614c1f099f505784c948e4bfa9bbab8ffbf97d54335fc7fbe0bbe1469cbff`、payload 展开 `cdf699c2…`、固件 `3faac30c…`、镜像 `out/p21/p21-wren/uefi/boot-p21-up1.img` SHA-256 `2d9f2bca0d3cb81af8824fd2798dbf41ac7d885054fa29499bfef97715ba496b`，gate `20261103T122129Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；12:21:54Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`cold r/w/me/vbif/im=0/0/303/0/0` 后应出现 `bank0 streams sid0/sid2 attached` 与 `cb_smmu[20]=0x4b340001`；任何 `step=…` 失败都按该步定位。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Tern** | `47eceeb4…` | `4a721f90…` | 冷启动不再直接禁用 GPU；冷读有效但排空失败，按 `-10` 回退；提交仍 stage=3 |
  | 2026-11-03 | **P21-Wren** | `2d9f2bca…` | `a2a614c1…` | 按 sheng 顺序冷启动只读采样、不排空；4K 各失败点加步骤日志；结果待回收 |

## 2026-11-03：P21-Swift 结果（IRQ 修复生效，4K 冷启动 -8 禁用 GPU）· P21-Tern

- **P21-Swift 实机（boot `0x44abb46f`，日志 `out/p21/persistent-logs/20261103T115652Z/`）**：68 轮、0 写错误，msgbuf 到 **394.63 s**，心跳 338 s；Recovery 11:53:19（命令后约 400 s），计时正常。显示 `clients=125 frames=5`。
- `SIRQ count=0 attach=0 rb=a002 route=0 id=361 ready=1` 与 `CB GPU SMMU IRQ: attach rc=0 rb=a002 ctlr=12 ds=0 id=361`：**fault IRQ 安装修复生效**（注意 GICD_CTLR=0x12，DS=0；rb 的 Group 位仍为 0，说明该中断在读取视角属于 group 0，但 enabled 位可写）。
- 紧接着 `CB GPU SMMU: 4K hardware state uncertain; GPU disabled`（66.01 s）：`drainColdGpu()` 在冷启动失败返回 `-8`，分发把 `-8` 视为状态不确定并**直接禁用 GPU**（不回退）。结果 `GPSVC ready=0 open=0`、无 submit；显示不受影响。
- **P21-Tern 修复（提交 `649b124`）**：
  1. 冷启动排空失败改为返回 `-10`（此时尚未写任何 SMMU 寄存器），分发在非 `-8` 失败时调用新增的 `cheeseburger_gpu_smmu4k_abort()`：屏蔽刚安装的 fault IRQ、清 `gFaulted`，再回退旧序列，避免影响旧路径与提交闸门。
  2. 记录冷读 `RBRead/RBWrite/MEControl/VBIFHalt/InterruptMask` 到 `native_smmu_cold`；若五者全为 `0xffffffff`（GPU 尚未上电、没有 master 需要排空，旧路径同样在上电前编程该 SMMU），则继续 4K 编程而不是回退。
- 候选 `out/p21/p21-tern`：内核 `4a721f904312e426add22fd80ea8356ae8993b686425dcc618744df5566e45d7`、payload 展开 `82d41769…`、固件 `bbfeb122…`、镜像 `out/p21/p21-tern/uefi/boot-p21-up1.img` SHA-256 `47eceeb4a9cecf270d70d314cc13be73cb285c4dec2117b5b281d4311690db1b`，gate `20261103T120529Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；12:05:49Z 单次 `fastboot boot` Sending/Booting OKAY。验证点：`cold drain ... unpowered=?`、`bank0 streams sid0/sid2 attached`、`cb_smmu[20]=0x4b340001`；若出现 `-10` 回退，记录冷读值供下一步定位。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Swift** | `fd8a9879…` | `b96bb4ae…` | 修复 fault IRQ 的 `-9`；IRQ `ready=1`，但 4K 冷启动 `-8` 禁用 GPU |
  | 2026-11-03 | **P21-Tern** | `47eceeb4…` | `4a721f90…` | 冷启动未验证不再禁用 GPU；全 1 视为未上电继续 4K；结果待回收 |

## 2026-11-03：SMMU fault IRQ `-9` 修复 · P21-Swift

- **上游检索**：`upstream/UniXNU-sheng` 与 `Adreno740` 没有 SMMU fault IRQ 实现；`ArmSmmu4KLive.h` 只要求平台自行"安装 fault handling 并确保所有 master 静止"。可对照的本机案例是触摸的 SPI attach（`CheeseburgerTouch.cpp:359`），它只检查 attach 返回码，不检查 Group 位。
- **根因（日志实证）**：P21-Lark 的 `SIRQ count=0 attach=0 rb=a002 route=0 id=361 ready=0` —— `armcpu_spi_attach` 返回成功（rc=0）、enabled 位=1、priority=0xa0，但 **Group 位=0**；旧代码要求 `(rb&7)==3`，于是 `installFaultIrq()` 判失败并返回 `-9`。`rb=a002` 是 GIC 单安全态（GICD_CTLR.DS=1）下 IGROUPR 为 RAZ/WI 的读数，并非中断不可用（secure-only 中断的 ISENABLER 写入也会被忽略）。
- **修复**：只要求 enabled 位（`rb&2`），并记录 `attach rc/rb/GICD_CTLR/ds/id`。提交 `35414cc`。
- **版本代号**：**P21-Swift**。镜像 `out/p21/p21-swift/uefi/boot-p21-up1.img` SHA-256 `fd8a987971575e0ade78844e6ec480630fb29e4c9c87ea40e675c53a9fc833bf`；内核 `b96bb4aef05fa9f924b03fa2556b9560abb0a5f9b4af782ddf5d25b70ec2d226`；payload 展开 `b8a4d6c2…`；固件 `7a0d590f…`；gate `20261103T114608Z` PASS（670 B）。misc 首块仅 `boot-recovery`、回读一致；11:46:27Z 单次 `fastboot boot` Sending/Booting OKAY。
- **验证点**：`SIRQ ready=1`、`CB GPU SMMU IRQ: attach rc=0 rb=… ctlr=… ds=1`；随后 `drainColdGpu()` 若在冷启动返回失败会走 `-8`（注意 `-8` 在分发中被视为硬件状态不确定、**不回退**，GPU 会被禁用）；若通过应出现 `CB GPU SMMU4K: bank0 streams sid0/sid2 attached` 与 `cb_smmu[20]=0x4b340001`，再观察用户提交 `GSUB`。

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Swift** | `fd8a9879…` | `b96bb4ae…` | 修复 SMMU fault IRQ 的 `-9`（DS=1 回读），让 4K 路径真正接管；结果待回收 |

## 2026-11-03：进入系统（代号规则启用）· P21-Lark

- **用户确认已进入系统**：本次运行的是当前最新 HEAD（sheng 完整同步 + SMMU4K + SMMU 故障 IRQ/停流 + panic-hold），**仅移除了 batch15（r15）引入的诊断**：`cheeseburger_gpu_smmu_dump`、`cheeseburger_gpu_engine_pool_late_test`、`submit()` 内两处调用、BootLog 的 GPLT/GSMM 行（提交 `51f76be`）。
- 镜像事实：`out/p21/no-b15-diag/uefi/boot-p21-up1.img` SHA-256 `fa3748fb5d9ff20f829591c0556e1c52f5ca2e97e014fc4a8d08da5105dab90d`；内核 `8ec8232a539d6c9d60f1ccc465476857b9e07d4258537bd7f2ce13b9b97a13f3`；payload 展开 `cd82aef8…`；固件 `bf997599…`；gate `20261103T112731Z` PASS。11:27:56Z 单次 `fastboot boot`。
- 注意：移除前后内核文件大小相同（均 125,992,960 B），payload 展开也相同（128,413,696 B）；变化在内部符号布局。此前证据显示被移除代码在 `GPSVC open=0` 的轮次不可达，因此“移除即恢复”目前只能记为**相关性假设**，需用本轮日志（`GPSVC open`、`frames`、4K `-9`、停机点）确认，不能直接断言根因。
- 待回收：设备应在 400 s 计时后自动回 Recovery（misc 已写 `boot-recovery`）；回收新 boot ID 日志，核对：`GPSVC open=1`/`clients>0`/`frames>0`、`sheng ArmSmmu4K setup failed -9` 是否仍出现、日志是否越过 ~138 s 到 ~399 s。
- **版本代号规则（自本轮起）**：每个候选必须有唯一代号，格式 `P21-<Name>`，在 HANDOFF 的代号表登记“日期 / 代号 / 镜像 SHA-256 / 内核 / 目的 / 结果”，代号不复用；候选目录仍用描述性名字，代号只用于指代版本。
- 代号表：

  | 日期 | 代号 | 镜像 SHA-256 | 内核 | 目的 / 结果 |
  |---|---|---|---|---|
  | 2026-11-03 | **P21-Lark** | `fa3748fb…` | `8ec8232a…` | 最新 HEAD 去掉 batch15 诊断；实机稳定到 400 s、显示 `clients=123 frames=13`、GPU 用户服务打开；用户提交仍 `GSUB rc=-4 stage=3`；4K `setup failed -9`（fault IRQ 安装失败）后回退 |

- **P21-Lark 实机结果（boot `0x46e1467b`，日志 `out/p21/persistent-logs/20261103T113529Z/`）**：67 轮、0 写错误，msgbuf 到 **394.98 s**，心跳 328 s；Recovery 11:34:48 启动（命令后约 412 s），400 s 计时正常生效。显示 `clients=123`、`frames=13`；`CB GPU open: ok ... name=mediaserverd ready=1`（139.39 s）、2 个 BO 映射成功、`GPSVC open=1 sub=1 calls=9`。
- GPU 用户提交仍失败：`submit rc=-4 stage=3 seq=3 rptr=ffffffff wptr=ffffffff irq=ffffffff fr=34 fenced=0`，`GSUB rc=-4 stage=3`；`GPWR pre=778000/140000 post=140000/140000 me/vbif=0 st=80000101 micro=178/0 gpmu=babeface/0`。即客户端与映射已就绪，失败仍在 CP 取环/门铃这一阶段（既有的用户提交问题），不是 launchd/显示。
- 4K SMMU：`setup failed -9`；源码定位为 `CheeseburgerGpuSmmu4K.cpp:143` 的 `installFaultIrq()` 返回失败（`armcpu_spi_attach` 未安装成功），随后安全回退到钉住的旧序列。
- 结论（设备已验证，机制未定）：同一 HEAD 下，**带 batch15 诊断的 panic-hold 停机，去掉后 P21-Lark 稳定**；但被移除代码在失败轮次不可达，因此只记录为"移除后设备验证通过"，不宣称已解释停机机制。
- 下一步：①先修 `installFaultIrq()` 的 `-9`（SPI 0x149 的附加条件/参数），让 S2 的 4K 路径真正接管；②用户提交 `GSUB rc=-4 stage=3` 仍是主线，按 sheng Ring/Submit 迁移（S3）处理，不要再回到 batch14。

## 2026-11-03：batch14→15 代码对比与 batch15 诊断移除（候选已启动）

- **batch14→15 唯一代码提交 `b61ceab`（4 文件，110 行）**：
  - `CheeseburgerGpuSmmu.cpp`：新增只读 `cheeseburger_gpu_smmu_dump()` 与 `native_adreno_smmu_dump[24]`（SCR/前 6 组 SMR/S2CR/CB0/CB1/全局 FSR）。
  - `CheeseburgerGpuCp.cpp`：新增 `native_adreno_pool_late[8]` 与 `cheeseburger_gpu_engine_pool_late_test()`——从低位池取一页、在提交时建 `0x201a000` 映射、发一个 `0x3d` CP_MEM_WRITE、成功才 unmap；失败会把引擎置为 failed。
  - `CheeseburgerGpuService.cpp`：在**每次用户 submit** 开头调用上述两项；pool_late 失败时直接返回 `kIOReturnIOError`。
  - `BootLog.c`：新增 GPLT/GSMM/GSMC 诊断行（仅当上述全局被填过才输出）。
- **当前 HEAD 曾完整保留这四处**（本轮移除前 grep 可证）。两次构建的其它输入无代码差异：batch14/15 内核符号对比只有这 6 个新符号（及 `kalloc_type_view` 编号变化），没有任何函数大小变化；固件 overlay 差异只有 pins 与生成的 `rootfs.c`；payload 的 ADT/seed/trustcache 完全相同。
- **可达性结论：这些代码不是失败轮次停机的执行原因**。batch15/17/18/panic-hold 的日志均为 `GPSVC ready=1 open=0 sub=0`，且从无 `CB GPU service: client open`（该行在 `AGXDeviceUserClient::start` 里，open 成功即打印），所以 dump/pool_late 从未执行。反证：batch14 控制在 **138.56 s** 打印 `client open` 后继续到 399 s；失败轮次恰在 **135.9–138.35 s** 停止且没有该行——停机紧邻“GPU 用户服务启动”这一时刻。
- **本轮处理**：提交 `51f76be` 移除上述诊断（dump/pool_late/两处 submit 调用/BootLog GPLT+GSMM 块），保留 GSMMU 等既有诊断。
- **候选 `out/p21/no-b15-diag`**（复用既有固件 overlay，prelink 输入用 unstripped）：内核 `8ec8232a539d6c9d60f1ccc465476857b9e07d4258537bd7f2ce13b9b97a13f3`，payload 展开 `cd82aef8…`，固件 `bf997599…`（三项 pin 命中），镜像 `out/p21/no-b15-diag/uefi/boot-p21-up1.img` SHA-256 `fa3748fb5d9ff20f829591c0556e1c52f5ca2e97e014fc4a8d08da5105dab90d`，gate `20261103T112731Z` PASS（670 B）。misc 首块仅 command=`boot-recovery` 并回读一致；2026-11-03T11:27:56Z 单次 `fastboot boot` Sending/Booting OKAY，结果待回收。
- **下一步**：若仍停在 ~136–138 s（预期，因代码不可达）→ 下一变量关闭 `cheeseburger-gpu-start`（跳过 GPU probe）对照，判断停机是否与 probe/引擎状态有关；同时排查 `adrenoprobed` 启动路径（launchd job/AMFI/trustcache 校验）在 138 s 附近是否阻塞。若移除后恢复正常 → 说明是布局/链接副作用而非运行路径，需要保留移除并重做 A/B。

## 2026-11-03：batch14 控制启动结果（回归在镜像侧，非 Data/环境）

- 日志 `out/p21/persistent-logs/20261103T111306Z/`，boot `0x45ca2f65`，68 轮、0 写错误，msgbuf 到 **399.16 s**，心跳 338 s。
- `clients=75`、`frames=3`、`GPSVC ready=1 open=1`、`GCTL rc=0 stage=7`；Recovery btime 11:08:17，即启动命令（11:01:38）后约 **399 s** 由 400 s 计时自动返回。
- 对比 batch13/14 原记录（`clients=117 frames=5`）与 batch15+/panic-hold（`clients=0 frames=0`、136–188 s 停止、400 s 不复位）：**同一硬件与持久 Data 下 batch14 仍工作**，因此不是 Data/环境问题，而是 batch15 起的镜像差异。
- userland addon/trustcache 在 batch14/15/18/panic-hold 完全一致（`2cf8f934…` / `0a7d5a32…`），排除这两项。
- batch14→15 唯一代码提交是 `b61ceab`（SMMU dump + 池页晚测 + BootLog 字段）；其运行路径都在 `AGXDeviceUserClient::submit()` 内，而失败轮次 `GPSVC open=0` 说明该路径未执行，单凭源码无法解释。后续 batch17+ 又叠加了 `cheeseburger-gpu-smmu4k=1`（本轮日志为 `setup failed -9` 后回退）。
- 下一步（按单变量）：
  1. 用当前 HEAD 仅把 `cheeseburger-gpu-smmu4k` 设为 0（其余不变）重建 payload/固件并单次启动：仍停在 ~136 s → 4K 尝试不是触发点；恢复到 ~399 s → 4K 的 SMMU 编程/回退副作用是触发点；
  2. 若 1 仍停，再关闭 `cheeseburger-gpu-start`（跳过 GPU probe）对照，区分“GPU probe/总线”与“其它内核差异”；
  3. 不建议在触发点未定位前继续 sheng 隔离或 S2 硬件验证。

## 2026-11-03：日志"独立线"分析 + batch14 控制启动（结果待回收）

- **日志独立线不可行（有证据）**：日志 writer 与心跳本来就是独立内核线程，但本轮一起停在 kernel ~138 s；更关键的是 `AUTORESET` 线程不碰日志/UFS（只 `IOSleep` + PSCI），60 s 时 `mstart=1 gstart=1` 却在 400 s 没有触发复位（设备为 14 分钟后手动恢复）。因此停的是整机调度/CPU（或全局自旋锁），第二条软件日志线同样无法存活；可改进的只是取证：writer 提前启动（当前 ~60 s）、周期缩短（当前 ~4.8 s）、增加 stall supervisor + stackshot/panic 落盘，但不能跨越整机停机。
- **历史日志把回归点指到 batch14→15，而不是 sheng 同步**：
  - batch13 `20261103T050613Z`（boot `0x561d238f`）与 batch14 `20261103T052325Z`（boot `0x452f3116`）：`clients=117`、`frames=5`，日志到 ~398 s，400 s 自动回 Recovery；
  - batch15 `20261103T061746Z`（`0x7aefa0c4`）起：`clients=0`、`frames=0`，日志停在 136–188 s；batch17/18/panic-hold 与本轮均同（~136/136/138 s）。
  - batch14→15 代码差异仅 `BootLog.c`/`GpuCp`/`GpuService`/`GpuSmmu`（SMMU dump + 池页晚测），启动参数完全相同；`git diff bc044ac b61ceab` 可复现。池页晚测已不在当前代码，dump 仍在。
- 两个待区分原因：①batch15 遗留代码（`cheeseburger_gpu_smmu_dump` 等）或后续构建差异；②**持久 Data 状态**在 05:30 后被改写，之后所有镜像（含同镜像重跑）都在同一位置停住。
- **控制实验**：2026-11-03T11:01:25Z 对 `3b25cc7c` 单次 `fastboot boot` batch14 镜像 `out/p21/gpu-dev-batch14/uefi/boot-p21-up1.img`（SHA-256 `f6cda6f99f5e0c08e46995f2f3fd521bf994b5f64f5252492882827f351b38ea`，当时 `clients=117 frames=5`、400 s 自动返回），记录 `out/p21/gpu-dev-batch14/boot-once2.log`。misc 首块仅 command=`boot-recovery`，回读一致（`8698abf4…`），未写其他分区。
- 判定：若 batch14 现在也停在 ~136 s/`clients=0` → 环境/Data 状态问题（隔离构建无意义，需先取得 Data reseed 授权或查 launchd/APFS 状态）；若恢复 `clients=117 frames=5` 且 400 s 自动返回 → 是镜像代码回归，二分 `bc044ac..b61ceab`（dump/池页晚测/构建差异）。

## 2026-11-03：panic-hold 实机结果（boot `0x47d33baa`，黑屏且不自动返回）

- 日志 `out/p21/persistent-logs/20261103T104649Z/`，boot `0x47d33baa`，16 轮、写错误 0，窗口 SHA-256 `090e69d0…`。Recovery btime 10:46:41（启动命令 10:32:19），即用户约 14 分钟后手动恢复，**400 s 计时没有把设备自动带回 Recovery**。
- 内核正常跑到约 **138 s** 后日志停止（msgbuf 最后为 tccd/studentd/analyticsd 等 launchd 服务，无 panic 文本）；`BootLog` 从 ~60 s（`kprintf initialized`）写到 ~138 s，共 16 轮。`AUTORESET requested=400 elapsed=60 mstart=1 gstart=1` 已启动但日志里从未到点。
- **panic-hold 未被触发**（`mhold=0 ghold=0`）：本轮不是 panic 屏导致的复位/黑屏；上轮 ~48 s 的快速复位与本轮现象不同，仍未解释。
- GPU 路径：`CB GPU SMMU: sheng ArmSmmu4K setup failed -9; using the pinned sequence`（较 batch17 的 `-4` 更早失败，与新增的 cold-start `drainForTables` 失败返回一致），随后旧序列成功：`CB GPU start: fence OK seq=1`、`userpath fence rc=0 stage=7 fence=2`、`CB GPU service publish rc=0`、`kick rc=0`、`GCTL rc=0 stage=7`。
- 用户态阻塞：`GPSVC ready=1 open=0`、显示 `clients=0 frames=0`（`TOUCH frames=2012` 是触摸轮询，不是显示帧）。即 `adrenoprobed`/GPU 用户服务从未被 launchd 启动，显示也没有客户端，黑屏与此一致；这与 batch18 的 `GPSVC open=0`、`frames=0` 同一症状。
- 结论：本轮复现 **batch18 的 ~138 s 后停机**，不是 panic；panic-hold 改动本身未被考验。下一步仍应是 batch18 记录的隔离计划（sheng 模块只 vendor 不编译，单次启动对比），并单独查 launchd 为什么没有加载 GPU 用户服务；若继续 SMMU4K，先修正 `mapping_begin` 在冷启动返回 -1（新 drain 读取未上电 GPU 的总线风险）。

## 2026-11-03：暂停"panic 后的自动复位"（候选 panic-hold 已启动，结果待回收）

- **问题**：`gpu-smmu-irq-drain` 镜像启动后设备约 48 s 就回到 Recovery（Recovery btime 09:47:22 主机时间，Fastboot 命令 09:46:34），本轮持久日志窗口完全未变（仍为 boot `0x3f5e540c`、SHA-256 `12b25457…`），即内核没有写入任何记录。用户报告 UEFI 选择后"闪一下自动重启"，判断为 panic。
- **代码定位**：仓库中**没有**单独的"panic 触发复位"实现。唯一会在一轮里自动发 PSCI SYSTEM_RESET 的板级代码是 `package/BoardRuntime/CheeseburgerAutoReset.cpp`（`cheeseburger-auto-reset=400`，主线程 400 s、guard 410 s），它原先**不区分是否已 panic**，因此 panic 后仍会在计时到点时复位。`patch-xnu-entry-probe.py --panic-screen --no-reset` 的 panic screen 只绘制并 `park()`，不复位；本机 `PE_halt_restart` 也没有板级/kext 安装实现，XNU 的 panic/debugger 重启路径在本镜像上不会走到 PSCI。`ArmDTPlatform::platformStageReset` 只在 `sheng-platform-stage=1/2/3` 时触发，当前参数未启用。
- **改动（提交 `fe3fd3a`）**：
  - `CheeseburgerAutoResetPolicy.h` 的 `State` 增加 `panic`；主线程和 guard 的 `should_fire` 都要求 `!panic`。
  - `CheeseburgerAutoReset.cpp` 在到达各自 deadline 时读取 `boot_progress_panic_vinfo`（与 panic screen 相同的 vc_info ABI）末行前两个像素的标记 `0xC0FFEE11/0x5CEEDA7A`；命中则不发 PSCI，分别记 `cb_reset[9]`（main held）/`cb_reset[10]`（guard held）。默认启用；`cheeseburger-panic-hold=0` 可恢复旧行为。未命中时 400 s 计时完全不变。
  - `BootLog.c` 的 `AUTORESET` 行增加 `mhold/ghold`；`tests/auto_reset_policy_model.cpp` 增加 panic/hold 用例，宿主模型 PASS。
- **未解释**：上一轮约 48 s 的复位早于 400 s 计时（而且没有日志说明显示探针是否运行），本改动**不能**证明能阻止那次复位；若下次仍在 <400 s 无日志复位，应先做同镜像/旧镜像差分并确认 UEFI/DarwinBoot 实际显示的内容，而不是继续归因于 panic。
- **候选**：`out/p21/panic-hold`（复用已验证的固件 overlay，增量编译；prelink 输入为编译产物的 `mach.development.bcm2837.unstripped.noctf`，不是 stripped 主文件）。
  - 内核 `9bd74c5a30fc2fa2e32d95d293fdcd6376c5b9ffc165dfeea6ff37162aa81ef3`（prelink `2416412f…`；`check-kernel` PASS）；
  - payload 展开 `8a45d44d19e2906d5bd6747cd66fea1e976ef021959cdae1e7b6322dfe56839b`（128,413,696 B）；
  - 固件 `bbbdb2720c333d8139bb41683efb689624ca33e63ee93e0c303a864c95811331`，三项 pin 在 FV/EFI 命中；
  - 镜像 `out/p21/panic-hold/uefi/boot-p21-up1.img` SHA-256 `5283186c92ee975ca1416605a40ed2dd05d77470332783d408436208b2c3219d`（82,767,872 B），gate `20261103T103137Z` PASS（args 670 B）。
  - misc：仅首块 command=`boot-recovery`，备份/请求/回读一致（`8698abf4…`），未写其他分区。2026-11-03T10:32:06Z 对 `3b25cc7c` 单次 `fastboot boot` Sending/Booting OKAY；本轮结果与新 boot ID 日志尚未回收。
- **下一步**：回收新日志；若仍早停/无日志，先区分"内核未启动（UEFI/DarwinBoot）"与"内核早期失败"，再决定是否回退本轮 GPU cold-start drain 改动（`drainForTables` 在电源未确认前读取 MEControl/VBIF 存在总线挂死风险）。

## 2026-11-03：SMMU4K 接入修复（已编译，尚未实机）

- 对照 `.work/p21-official-framework/drivers/Adreno740/NativeAdrenoSmmuContext.cpp` 与 `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KLive.h`：128 MiB aperture 的预算修为 32768 页，连续表池扩大到 128 页（原先 8192/32 必然拒绝；真实 kit 宿主测试确认需 66 页）。保留 upstream 的 32 位输入及本机 36 位输出，不声称等同原 36 位输入配置。
- 按官方更新契约维护按 IOVA 排序的现有映射范围，整段 map/unmap 一次更新且 unmap 使用实际物理范围；锁住引擎提交并检查 idle。验证已知未使用的 fault 槽后调用官方 stream preflight；发生硬件写入后失败禁止回退旧路径，probe 发现 setup 失败时不启动 CP，失败状态保留供诊断。
- 本地 XNU 源码构建 `out/p21/gpu-smmu-fix/kernel-source-build.json` PASS，编译结果 SHA-256 `8f8c43aefa0f9137e8fd7cd727d769f03819b51b813a77339635cb9189948e53`；官方 kit 布局测试 `tests/p21_smmu_layout_test.c` PASS（原预算拒绝、原表池拒绝、新配置成功 66 页）；现有 7 组宿主模型 PASS（`out/p21/gpu-cp/host-tests/20261103T090112Z/`）。这些测试不覆盖设备 MMIO、master 静止及故障中断。
- 未完成：官方 `ArmSmmu4KLive.h` 要求安装 fault handler 并确保全部 master 静止；本机目前未找到能证明这两项均满足的实现，且 sheng 的 coherent walk 对 MSM8998 的适用性未验证。新的路径尚未硬件验证、尚未打包或启动；不要称黑屏或 GPU 已修复。先按同 SoC/vendor 源码完成故障处理、master 所有权和 coherency 接线并测试，再打包本轮内核、验证 pins 后单次实机运行。设备上次已在 Recovery，本轮未操作设备或写分区。

## 2026-11-03：batch18 第二次黑屏日志与快速源码审计

- 用户报告黑屏，设备现处 Recovery；只读回收 `out/p21/persistent-logs/20261103T085351Z/`，boot `0x3f5e540c`，17 rounds、0 写错，窗口 SHA-256 `12b254576458297fa433fffc25fb5c32e70e78ab8e4fbd241e431903a1251d68`。batch18 启动记录见 `out/p21/gpu-dev-batch18/boot-once.log`，本轮为同镜像重跑；未做新启动或设备写入。
- 日志确认 launchd 已运行，4K SMMU `setup failed -4` 后走旧路径；GPU 内核 probe、userpath 测试 fence 成功，显示 `clients=0 frames=0`、GPU 用户服务 `open=0`。心跳最后 78 s、内核 msgbuf 到约 135.95 s，快照没有观察到 panic，日志中断不证明 SoC 停止或自动回 Recovery 失败原因。本次返回 Recovery 的方式未核实；不要推断本轮自动重启成功。
- 确定代码错误：`CheeseburgerGpuSmmu4K.cpp` 用 128 MiB aperture 和 leaf budget 8192；官方 `ArmSmmu4KLive.c` 的 `layout` 要求 `bytes/4096 <= leaf_budget`，因此 32768 > 8192，必然预算拒绝。修正预算后还需检查 32 页容量：32-bit input 的覆盖 128 MiB 需要约 66 个表页；需按 kit 算法、36 位硬件匹配与内存来源一并适配。wrapper 把多个不同 prepare 错误折叠为 -4，不应从日志单独断定其唯一运行原因。
- 早期构造器假设未获支持：本地 runtime-build 22 个 `a740-*` 对象均未发现 `__mod_init_func` 节；且对照 `git show 1083213:scripts/p21-build-board-runtime.py`，batch17 在大规模搬目录前已经编译同样的 9 个上层模块及两套 kit。当前本机对象不单独证明设备镜像对象身份；不应直接断言 22 个对象是 batch18 才新增或是黑屏根因。
- 下一步：核对 batch17/18 真实构建输入与最终链接差异、核对 GPU 4K setup 失败后回退之前的寄存器副作用；修正预算及表池容量并保持失败安全，随后针对同一功能批量实现/验证，优先读实际日志与官方源码，不要把时间花在重复猜测。

## 2026-11-03：sheng GPU 代码完整同步（除 A540 驱动）

- 新增 `package/BoardRuntime/sheng/`：`Adreno740/`（官方驱动全部文件，tests 不编译）、`kits/{AdrenoPM4,ArmSmmu4K,AdrenoFirmware,AirNir}/`、`frontend/AdrenoMetal/`（最新 main `243d63c`）、`tools/`（102 个 GPU/Metal 脚本，只 vendor）。旧 `package/BoardRuntime/adreno740/` 已逐文件比对一致后删除。
- `CheeseburgerGpuSmmu4K.cpp` 仅改 4 行 include 到 `sheng/...`；`scripts/p21-build-board-runtime.py` 的 include 根与 kit 路径改到新结构。A540 板级文件（`Cheeseburger*`、`BootLog.c` 等）未改。
- 构建只编译 22 个硬件无关模块（9 个 Adreno740 + ArmSmmu4K 8 + AdrenoPM4 5）。`out/p21/gpu-dev-batch18` 由 batch17 复制并改路径后跑 `p21-compile-atos-gate.py`：`OFFICIAL_KERNEL_SOURCE_BUILD_PASS`，内核 `out/p21/gpu-dev-batch18/native/kernel.macho`，SHA-256 `028187e8b198ab542f1247ba460d53e20edd86a4741cf4c61876c23c7913988f`，board runtime 66 objects、`archive_sha256=de98b7ca…`。
- Adreno740 上游 clone 未成功，使用固定副本 `4e0314d`；AdrenoMetal 使用新 clone `243d63c`。A740 硬件模块、前端、AirNir/AdrenoFirmware、tools 均未编译；sheng 的 SM8550 GPU 胶水（GpuPowerProbe、prepare-sheng-gmu* 等）未同步。详见 `docs/P21_SHENG_GPU_SYNC_20261103.md`。
- 本轮未接触设备、未写分区、未生成 prelink/UEFI/payload/boot 镜像。下一步：如需实机验证，先按 batch18 记录构建 payload/UEFI 与 gate，再做单次 `fastboot boot`；A540 的 SMMU4K `prepare -4` 问题仍需按 batch17 日志单独修复。

## 2026-11-03：batch18 早停可复现（同步后回归待隔离）

- batch18 第一次（boot `0x3e102a7e`）：日志 ~139 s 停止、心跳 78 s/14 records、`frames=0`、`GPSVC open=0`、`GCTL` 正常、`SMMU4K setup failed -4` 回退；无 panic 文本。
- 同一镜像第二次（08:17:54Z 启动）：到 08:28（>10 min）USB 无设备、无自动返回、无新日志 → **早停在该镜像上可复现**（不同于 batch7 的同镜像重跑恢复）。
- 现行怀疑：同步进来的 22 个 sheng 模块被编进板级归档，若含全局构造器/静态初始化会在启动早期执行（仓库既有规则：板级新增类必须移出 `__mod_init_func`，在 publish 时显式初始化）。
- **隔离计划（设备恢复后执行）**：
  1. 先回收第二次日志，确认停止点；
  2. 建 `batch19`：同一同步代码，但构建清单**只 vendor 不编译** sheng 模块，并让 `CheeseburgerGpuSmmu.cpp` 的 4K 分发在未编译时编译为空（`#if` 或弱符号），即回到 batch14 的运行时行为；单次启动对比；
  3. 若恢复 → 逐模块二分（先 `NativeAdrenoVm/Memory`，再 `Ring/Submit`），并把需要的构造器移出 `__mod_init_func`（参照 sheng `build-adreno740-accelerator.py` 的 late 处理）；
  4. 若不恢复 → 查 payload/固件层（与代码无关）。
- 检查命令：对 `package/BoardRuntime/sheng/**` 的 .o 或归档执行 `llvm-objdump --macho --section=__DATA,__mod_init_func` / `nm | grep __GLOBAL__sub_I`，确认是否存在早期构造器。

## 2026-11-03：完整同步后的首次编译/启动验证（batch18）

- batch18 = 完整同步后的候选（`package/BoardRuntime/sheng/` 397 文件）：内核源码构建 PASS（`028187e8…`）；prelink/patch/check/payload PASS。
- **上游拉取带来的破坏点**：`UniXNU-sheng` 7cff07e 已无 `board.toml`，`prelink.py` 读 `[kernel.prelink]` 失败。已在 batch18 的 `scripts/prelink.py` 中**固化**该输入（kernelcache `f18b4df8…`、early_security_init `com.apple.driver.AppleMobileFileIntegrity=0xfffffff005ab4ae0`，取自 batch17 记录）。审计时请核对这两个值与 `out/p12/inputs/A/kernelcache.macho` 及早期安全初始化地址一致。
- 待完成：固件构建 → pack/gate → 单次启动 → 回收日志；判定点是“合并后能正常启动、框架无 panic、GPU 探针与既有一致”（4K SMMU 路径当前会在 `prepare` 失败后回退，属已知）。

## 2026-11-03：同步来源更正（用户指出）

- **官方仓库在 `/Volumes/NAS-MacData/Project/IOS/upstream/`**：`UniXNU`（框架/移植指南/通用接口）与 `UniXNU-sheng`（设备适配：GPU 服务、BO 内存、缓存一致性、提交生命周期；sheng 用 A740，硬件寄存器/固件不可套用）。
- 已执行：`UniXNU-sheng` fast-forward 到 `7cff07e`（docs: record the latency report script）；`UniXNU` 本地 HEAD `b6ae99c`，`git pull --ff-only` 失败（与 origin 分叉），需 `git pull --rebase` 或对齐后再取。
- **同步边界（用户要求）**：只同步 GPU **上层**（GPU service、BO 内存管理、缓存一致性、提交生命周期、前端/Metal 接口与渲染链接），**不要改动驱动部分**（A740 硬件初始化/Pwrup/CpInit/GmuConfig/Irq/SM8550 平台、以及本机 Cheeseburger 板级驱动与 A540 寄存器/固件）。
- GPU 上层源码的实际来源：`UniXNU/Adreno740` 与 `UniXNU/AdrenoMetal`（框架文档中的固定链接，本地副本在 `.work/p21-official-framework/drivers/Adreno740`、`drivers/AdrenoMetal`、`kits/*`）。**下一步**：把这两个仓库拉到 `upstream/`（或 fetch 框架对应源），与 `package/BoardRuntime/adreno740/` 做 diff，仅同步上层文件；此前停掉的两个执行代理未留下改动，工作树干净（仅 launchd cache 三个未跟踪文件）。
- 之前基于 `.work/p21-official-framework` 的 vendored 内容（S1/S2）保留：它们与固定版本一致；同步最新后再更新文档与构建清单。

## 2026-11-03：batch17 实机（S2 首次验证，setup -4）

- boot `0x3fa116fe`，日志 `out/p21/persistent-logs/20261103T064452Z/`，镜像 `fee46f3a…`、内核 `69d26bb5…`、gate `20261103T063439Z` PASS（args 670 B）、misc 回读一致。
- 结果：`CB GPU SMMU: sheng ArmSmmu4K setup failed -4; using the pinned sequence` —— 4K 路径在 `NativeAdrenoSmmuContext::prepare` 失败并**安全回退**旧序列（probe `GCTL` 仍 OK，自动回 Recovery 正常）。本轮 `frames=0`、`GPSVC open=0`，用户客户端未启动，因此没有 `GPLT/GBOA/用户提交` 数据。
- `-4` 的最可能原因：vendored 的 `NativeAdrenoSmmuContext::prepare` **把 input_bits 写死为 32**，而我传的 aperture 是 128 MiB/leaf_budget 8192/32 页表池；kit 对表池容量与 leaf 数、以及 32 位输入窗口的校验不满足。修复方向：
  1. 把表池加大（例如 128 页 = 512 KiB，`NativeAdrenoMemory::allocate(128*4096,36,true,1MiB)`），`leaf_budget` 按 aperture 实际需要设置（128 MiB/4K = 32768，或收窄 aperture）；
  2. 或给 `NativeAdrenoSmmuContext` 增加 `input_bits` 参数（本地补丁，保持 sheng 语义）以用 36 位；
  3. 失败时把 `prepare` 的返回码/`ArmSmmu4KStatus` 记入 `cb_smmu`，避免只有 `-4`。
- 另注：连续两个 boot（batch15、batch17）出现 `frames=0` 且用户客户端不启动；这与 GPU 改动无关（用户态发现未推进），下一轮需先用**旧路径**确认是否本轮镜像的偶发（同镜像重跑一次）再判定 S2。

## 2026-11-03：S2 候选 batch17（含 `cheeseburger-gpu-smmu4k=1`）构建状态

- batch17 = batch16 内核（`1cedb7d4…`，含 sheng vendored 层 + 4K SMMU 路径）+ `unixnu.toml` args 追加 `cheeseburger-gpu-smmu4k=1`（约 674 B，仍 <700）。payload 已重建并写入 `config/CompressedPayload.h`、`Expected.h`；`check-kernel.py` PASS。
- 正在构建固件；完成后按标准链：复制 `MSM8998PKG_UEFI.fd`/DTB → `logs/build-uefi-01.json` → `scan-pins.py` → `pack.py` → `preboot-gate-p21.py` → 写 misc（点名 3b25cc7c/misc/首块 command=boot-recovery）→ 单次 `fastboot boot` → 400 s 后回收。
- 验证点：日志出现 `CB GPU SMMU: sheng ArmSmmu4K path active`、`CB GPU SMMU4K: bank0 streams sid0/sid2 attached`、`cb_smmu[20]=0x4b340001`；随后 `CB GPU start: fence OK`、`GRTM/GRT2`、`GBOA`、用户提交 fence。任何一步失败即按 `setup` 日志的返回码定位（-1 映射/-2 micro-MMU/-3 表池/-4 prepare/-5 program/-6 attach/-7 visibility）。

## 2026-11-03：S2 落地（ArmSmmu4K 接管 bank/流）

- 新增 `package/BoardRuntime/CheeseburgerGpuSmmu4K.cpp`：用 sheng 的 `NativeAdrenoSmmuContext` + `ArmSmmu4K` 建表（32 页表池，`NativeAdrenoMemory` 分配）、`program()` 编程 CB0、`attach()` **精确接管 DTB 流**（`gfx3d_user` SID0→slot0、`gfx3d_secure` SID2→slot2），增量映射走 `ArmSmmu4KLiveUpdateContext`（每次更新内含 TLB 同步与镜像校验）。板级只保留 kit 明确留给平台的部分：SMMU 全局 SCR、vendor ACTLR `0x70000000`、micro-MMU `0x6000` resume。
- 迁移桥：`cheeseburger_gpu_smmu_setup` 在 `cheeseburger-gpu-smmu4k=1` 时优先走 4K 路径，成功即返回；失败回退到已固化的 sagit 序列。`cheeseburger_gpu_smmu_map/unmap` 在 4K 激活时转发给 kit。**实机确认 probe fence + 映射 + 用户提交后，删除旧路径、默认使用 kit。**
- 启用方法：在候选 `config/unixnu.toml` 的 args 里追加 `cheeseburger-gpu-smmu4k=1`（当前 644 B，仍 <700）。下一设备轮次应先用该参数做 A/B。
- 构建：S1 的 vendored 层（Memory/Vm/SmmuContext/Ring/Submit/Cpu/Sha256/Timer + ArmSmmu4K + AdrenoPM4）在 batch16 链接通过（内核 `a50cec2c…`）；S2 的 4K 路径在 batch17 链接通过（内核 `1cedb7d4…`）。
- **下一步（设备恢复后按序）**：
  1. 先回收 batch15 日志（boot 未知，`GSMM/GSMC/GPLT`），确认 05:32 那次为何没有自动回 Recovery；
  2. 在 batch17 的 `config/unixnu.toml` args 追加 `cheeseburger-gpu-smmu4k=1`，重建 payload/固件、跑 gate、单次启动；用 `cb_smmu[20]=0x4b340001` 与日志 "SMMU4K ... attached" 确认 kit 生效；
  3. 验证点：probe fence（`CB GPU start: fence OK`）、`GRTM/GRT2`、`GBOA`、用户提交 fence；全部通过后删除 `CheeseburgerGpuSmmu.cpp` 的旧序列，默认走 kit，并继续 S3（Ring/Submit）与 S4（GMU/power）、S5（Service/Publish）。

## 2026-11-03：转向 sheng 官方方案迁移（进行中）

- 用户指示：**完全采用 sheng（官方 Adreno740）适配方案迁移合并，不再自研，禁止重复造轮子**。所有后续 GPU 工作以 sheng 的接口/生命周期/同步为基准；A540 的寄存器、固件、命令序列仍取 sagit，不搬 A740 硬件细节。
- 迁移分阶段（每阶段可编译；可启动的以设备回到 Recovery 为前提）：
  1. **S1（已完成）**：把 sheng 的硬件无关层与 kit 引入仓库并编译——`package/BoardRuntime/adreno740/` 下 vendored `NativeAdrenoMemory/Vm/SmmuContext/Ring/Submit/Sha256/Timer/Cpu` 与 `ArmSmmu4K/`、`AdrenoPM4/`；`scripts/p21-build-board-runtime.py` 增加 include 与编译条目（排除 `test_*`）。行为不变，先保证 `-Werror`/链接通过。
  2. **S2（下一步）**：用 `NativeAdrenoVm` + `NativeAdrenoMemory` + `ArmSmmu4K`（自有 bank/ASID/streams，DTB SID0=`gfx3d_user`、SID2=`gfx3d_secure`）替换自研的 `CheeseburgerGpuSmmu` 映射与 service 的 BO；每提交 TLBIALL、`publishMapped/acquireImported`、WCOMB 重设。
  3. **S3**：用 sheng `Ring/Submit` 替换自研环/ME_INIT/提交；A540 的 `CP_ME_INIT`/GPMU 命令取 sagit。
  4. **S4**：用 sheng `GmuPlatform/GpuContext/Board` 替换 probe/GMU/电源，A540 寄存器（RBCCU/SP `0xa887/0xa881`、micro-MMU `0x6000`、DTB `attach-impl-defs`）作为板级输入，保留 PAS/ZAP。
  5. **S5**：采用 sheng 的 Service/Publish/UserClient/Accelerator 与 Info.plist personality，保留本机 sandbox grant 与 trust cache 集成。
- **batch15 日志已回收（boot `0x7aefa0c4`，日志 `out/p21/persistent-logs/20261103T061746Z/`）**：`frames=0`、`GPSVC open=0`、`GLOP pool=64 first=10ff8d000`、`AUTORESET requested=400 elapsed=105 main=0 gfire=0`；日志约 105 s 停止、USB 全程无枚举。**用户客户端未启动（`GSMM/GPLT` 从未运行）**，失败是启动早期卡死/hung，与新增诊断无关；该 boot 的 `GSMMU 00200626 … 000200ff …` 仅显示 slot0 仍为 fault（与 `SMR0=0/S2CR0=0x200ff` 一致）。
- 原记录：batch15（镜像 `7fa6f4b5…`）05:32:57Z 启动后未在 400 s 内回到 Recovery、USB 上完全没有设备（05:53 仍无）。疑似 `GSMM` SMMU 寄存器读取或 `GPLT` 提交触发总线卡死/关机。**恢复电源并回到 Recovery 后，先回收 batch15 日志**（新 boot ID）再继续设备轮次。
- 迁移完成后，S2 应作为第一优先实机验证点（它直接替换当前失败所在的映射/BO 路径）。

## 2026-11-03：SMMU 流 dump + 池页晚映射（batch15）

- **batch14 实机（boot `0x452f3116`，日志 `out/p21/persistent-logs/20261103T052329Z/`，镜像 `f6cda6f9…`，内核 `5d4c48b1…`）**：
  - `GLOP pool=64 first=127cee000`、`GLOW low=2 high=0`：probe 期低位页池成功，两个 BO 都用池页（`0x128051000`、`0x12803d000`，≈4.97 GB，与可用 fence 页同一区间）。
  - **`GSUB rc=-4 fr=34`、`GBOD` 目标页仍 `0xdeadbeef`** → **“约 5 GB 物理地址边界”被推翻**：同样物理区间的 probe 页可读写，BO 页不行。
  - 至此排除：IB 内容、暂存、ATOS/翻译、PTE 属性、WCOMB、运行期映射（probe 期建的运行期映射 GRTM/GRT2 成功）、物理地址、分配时间（池页是 probe 期分配的）。
  - 剩余两类差异：**①映射在 ~145 s 用户提交时才建立**；**②这些页被映射进了用户任务**（`clientMemoryForType`/`IOConnectMapMemory64`）。
- 外部分析（sheng/ArmSmmu4K）：官方驱动**自有 SMMU 流路由**（SMR 精确匹配 + `S2CR=bank`，要求目标流原本 fault、全局 unmatched 必须 fault、禁止 bypass），自有 bank/ASID/aperture/表池，`output_bits` ≤ OAS；每次提交前 TLBIALL，并做 cache publish/acquire 与 WCOMB 重设。**我们从未写 SMR/S2CR**，只校验 `SMR0==0 && S2CR0==0x200ff` 后编程 CB0；DTB 里 `gfx3d_user`=SID0、`gfx3d_secure`=SID2，且节点带 vendor `attach-impl-defs` 寄存器表（0x6000…0x6b68），我们只处理了 0x6000 的 micro-MMU。
- 本轮（batch15）：
  1. 只读 `cheeseburger_gpu_smmu_dump()`：SCR、前 6 个 SMR/S2CR 槽、CB0 SCTLR/TTBR0/TCR/FSR/FSYNR0、CB1 SCTLR/TTBR0、全局 FSR（`GSMM/GSMC`）。
  2. `cheeseburger_gpu_engine_pool_late_test()`：取一个 probe 期池页（低位、无用户映射），在**提交时**新建 SMMU 映射 `0x201a000` 并用已知 IB 写入、fence、回读（`GPLT`）。失败则 fail-closed（保留页，不再碰总线）。
- 判定：`GPLT rc=0 tgt=5f5f0001…` → 运行时新建映射与低位页都可用，问题在**用户映射**或 0x4000000 这条 IOVA 路径；`GPLT rc=-4` → 问题在**提交时新建映射**本身（对照 sheng 的表更新/TLB 纪律），结合 `GSMM/GSMC` 看流是否真的路由到 CB0。

## 2026-11-03：启动期低位页池（batch14）

- **batch13 实机（boot `0x561d238f`，日志 `out/p21/persistent-logs/20261103T050613Z/`，镜像 `010b0dca…`，内核 `c0a1c292…`）**：`GLTE rc=-4 pa=142dc7000 map=0 tgt=0/0/0` —— **提交时新分配的内核页（无用户映射，PA 5.41 GB）同样不可写**。
  - 结论定稿：**失败随“晚分配/高物理地址页”走，与用户映射、BO、SMMU 翻译无关**。早期分配（≤4.96 GB，ring/fence/stage/rt）可读写；晚分配（≥5.0 GB）读写都超时。GPU 可访问的物理范围约为 <5 GB，来自启动链配置（XPU/VBIF 类），不是 XNU 侧属性。
  - 失败路径已按规则保留页（不再 unmap/释放可能仍被 GPU 访问的页）。
- 本轮（batch14）：probe 阶段（早期分配天然落在 4.1–4.7 GB）预留最多 64 个 4 KiB 页（优先带 `<4 GiB` 掩码，失败则普通、接受 <5 GiB），`GLOP pool/first` 记录；4 KiB 的 BO 从池中取页（其余大小仍走原路径），用户提交恢复正常执行。
- batch14 判定：`GLOP pool>0` 且 `GBOA` 的 PA <5 GB、`GCPY`/用户提交出现 fence → 边界成立且修复有效；`GLOP pool=0` → probe 期也拿不到低页，需要 DarwinBoot 预留低位池（boot args + UEFI 改动）。

## 2026-11-03：晚分配内核页对照（batch13）

- **batch12 实机（boot `0x5fa8920e`，日志 `out/p21/persistent-logs/20261103T044448Z/`，镜像 `ec1e90d9…`）**：`GLOW low=0 high=2` —— 单页、无连续标志、带 `<4 GiB` 掩码的分配**仍失败**（PA `0x138c17000`、`0x138ae9000`，约 5.24 GB）。`GCPY rc=-4`；fail-closed 正常。
- 结论：**运行期拿不到 4 GiB 以下的页**（低位内存已被系统占用/掩码在运行期不满足），要验证“物理地址边界”就得在启动期预留。
- 在投入启动期页池之前，本轮（batch13）先补一个缺失对照：提交时用**与 BO 相同的分配调用、同一时刻**分配一个 4 KiB 内核页（无用户映射），映射到 `0x2019000` 用已知 IB 写入并 fence（`GLTE rc/pa/map/tgt/seq`）。它把“晚分配/高地址页不可达”与“用户映射本身有问题”分开：
  - `GLTE rc=0 tgt=5e5e0001…` → 晚分配页可写，问题只在用户映射 → 改 `clientMemoryForType`/映射属性。
  - `GLTE rc=-4` → 晚分配/高地址页整体不可达 → 进行启动期低地址页池。

## 2026-11-03：低位内存分配（batch12）

- **batch11 实机（boot `0x55bce295`，日志 `out/p21/persistent-logs/20261103T042904Z/`，镜像 `17babe5b…`）**：`GLOW low=0 high=2` —— 带 `kIOMemoryPhysicallyContiguous` 的 4 GiB 以下分配**失败**，两个 BO 都回退普通池（PA `0x147db9000`、`0x147d4c000`，约 5.5 GB）；`GCPY rc=-4` 仍失败；fail-closed 正常（`GPSVC sub=0`、`GPWR` 全 0、无卡死）。
- 结论：不是“低地址没有页”，而是**低地址的*物理连续*页**在 100+ s 时不可得。
- 本轮（batch12）：4 KiB 的 BO 天然单页连续，低地址尝试改为 `inTaskWithPhysicalMask(kernel_task, kIODirectionInOut|kIOMemoryMapperNone, 4096, 0xfffff000)`（不含 contiguous 请求）；更大 BO 仍走原路径。若 `GLOW low≥2` 且 `GCPY rc=0 dst=deadbeef` → 低地址页可得且 GPU 可访问，随后用户提交应出现 fence；若 `low=0` 仍高 → 需要在启动期预留/保留低位页池（DarwinBoot 低池），而不是按需分配。

## 2026-11-03：GPU 物理地址边界（batch11）

- **batch10 实机（boot `0x556d12d9`，日志 `out/p21/persistent-logs/20261103T041233Z/`，镜像 `a868069b…`，04:05:50Z 启动、04:12:26Z 自动回 Recovery）**：
  - `GCPY rc=-4 seq=3 src=4001000 dst=0/0`：CP_MEM_TO_MEM **读**用户写目标（PA `0x13f66c000` ≈ 5.364e9 B）超时；不是“写特有”。
  - `GBOA0/1 rc=0`，PAR 命中 leaf（PA `0x13f54b000`、`0x13f66c000`）：翻译完全正确。
  - `GPSVC sub=0`、`GPWR` 全 0：copy 失败后服务按 fail-closed 直接返回，**没有再碰 GPU 总线**（`GPWR` 全 0 = `pre_kick` 未执行），也没有卡死/panic。
- **物理地址边界**：所有成功的 GPU 访问（ring/pm4/pfp/gpmu/fence/stage/rt/fence 写）都 ≤ `0x127b41000`（4.957e9 B）；所有失败都在 `0x13c768000`（5.311e9 B）及以上，包括 `0x13f66c000`（5.364e9，低于 5 GiB 但高于十进制 5 GB）。**边界最可能是十进制 5 GB 的 GPU 可访问物理范围**（XPU/VBIF 类），与用户映射、PTE、TLB 无关；这也解释了为什么“用户页”看起来特殊——用户 BO 分配得晚，PA 落在边界之上。
- 本轮（batch11）功能修复：GPU BO 改用 `inTaskWithPhysicalMask(..., 0xfffff000)` 分配到 **4 GiB 以下**（与本机 SMMU 页表相同的低池），失败回退普通池；`GLOW low/high` 记录两条路径次数。保留只读 `GCPY`，若低地址分配生效，它应先返回 `rc=0 dst=deadbeef`，随后用户提交才可能成功。
- batch11 判定：`GLOW low≥1` 且 `GCPY rc=0` + 用户提交 fence 出现 → 边界确认且修复有效；`GLOW low=0`（低池耗尽）→ 需要另找低位内存（预留在 DarwinBoot 低池或早期预留）。

## 2026-11-03：CP 读用户页（batch10）

- **batch9 实机（boot `0x558056e6`，日志 `out/p21/persistent-logs/20261103T035610Z/`，镜像 `422f4c02…`，03:49:20Z 启动、03:55:58Z 自动回 Recovery）**：
  - `GCTL` OK、`GPSVC sub=1`、`GSUB rc=-4 seq=3 pf=0/1 fr=34 stg=0`、`GBOD` 目标页仍 `0xdeadbeef`。
  - 新增 `GBOA0/1`（原始 IOLog）：IB `0x4000000` 与**写目标 `0x4001000` 的硬件走表都是 `rc=0` 且 PAR 命中 leaf**（PA `0x1519bb000`、`0x1519fc000`）。BootLog 里 `GBOA1 rc=2` 是“行计数与最后一行共用索引”的解码 bug，已修复。
  - 结论：**写目标的映射完全正确**，失败不是翻译缺失；问题在“该页/该访问本身”。
- 本轮（batch10）唯一新测试：`cheeseburger_gpu_engine_copy_test(src)` 用一条 `CP_MEM_TO_MEM(0x73)` 把用户写目标 `0x4001000`（内容是插件写入的 `0xdeadbeef`）复制到已验证 GPMU 页，再 fence 并回读（`GCPY rc/seq/src/dst`）。它只读用户页、不写用户数据；失败会在 helper 内置 `gEngineSubmitFailed`，服务直接返回，不再碰总线或门铃。
- batch10 判定：`GCPY rc=0 dst=deadbeef` → GPU **能读**用户页，只有写失败（下一轮查写权限/页属性/XPU）；`GCPY rc=-4` → 连读都失败，即该页整体对 CP 不可访问（与 ATOS 结果矛盾，需要查 SMMU stream 归属）。

## 2026-11-03：用户 BO 写目标的硬件翻译（batch9）

- **batch8 实机（boot `0x4a508e1d`，日志 `out/p21/persistent-logs/20261103T034012Z/`，镜像 `a7ed9c9f…`，03:33:14Z 启动、03:40:04Z 自动回 Recovery）**：
  - **稳定性验证**：`GPSVC sub=1`（用户提交真正执行）、`GPWR` 正常（`post=140000/140000 micro=178/0`）、`frames=5`、自动回 Recovery 正常。**pa_test 导致的卡死/panic 不再出现**，回退有效。
  - `GSUB rc=-4 stage=3 seq=3 fenced=0 pf=0/1 fr=34 stg=0`：用户 IB（0x4000000）仍然超时，`fr=0x34` 等于最终 WPTR。
  - `GSAT atos=0 par=4000000142d90b80 expect=142d90000 pte=60000142d90e43`：用户 IB IOVA 的硬件走表正确（PA `0x142d90000`）。
  - `GIBH` 仍是合法 PM4；`GBOD` 目标页仍 `0xdeadbeef`。**移除 WCOMB 没有改变结果**，页级写失败不是 WCOMB 引起的。
- 下一步（batch9，只读诊断，按审计建议）：服务在提交前对**每个已映射 BO**（包括从未提交过、只在 IB 里作为写目标的 `0x4001000`）做 `lookup + ATOS`，记录 iova/PA/leaf PTE/raw PAR/rc（`GBOA0/1`）。ATOS 不改页表、不敲门铃。
- batch9 判定：若目标 `0x4001000` 的 ATOS 与 leaf 匹配 → 翻译正确，问题在“该物理页的 GPU 写”本身（XPU/页归属），下一步换分配来源或查询 XPU；若目标 ATOS 失败/不匹配 → 目标映射缺失，直接修映射。

## 2026-11-03：回退风险改动 + 单一功能变量（batch8）

- **batch7 实机（镜像 `56263820…`，两次启动）**：
  - 第一次 boot `0x3f6d67b8`（日志 `20261103T030125Z`）：`frames=0`、`GPROBE exec=0`、`GPSVC open=0`，日志写入器约 140 s 停、心跳到 348 s，没有用户提交。
  - 重跑 boot `0x561a49ee`（日志 `20261103T031108Z`）：`frames=5`、`GPSVC open=2 sub=0`；`GSIB/GRTM/GRT2` 全部成功；**`GPAT map=0 pa=13c768000 seq=6 rc=-4`** —— 把用户目标 BO 的物理页 `0x13c768000` 重新映射到已验证窗口 `0x2018000` 再写，仍然中止。**用户 BO 的物理页对 GPU 不可写，与 IOVA 无关**。pa_test 失败后引擎被标记失败，用户 ABI 提交没有执行（sub=0）。
  - 用户报告本轮出现 panic。两次 boot 的持久 `msgbuf` 均无 `panic(cpu …)`（第二次覆盖 72.8–397.5 s），panic 文本可能落在日志窗口外或只显示在屏上；`CheeseburgerPlatformPower` 有多处显式 `panic()`（PMGR 老问题候选）。**没有屏上原文前不下结论。**
- 回退（提交 `58a62ab`）：移除 probe 的 IB 自检、运行期映射测试、暂存复制与 `pa_test`，用户提交恢复原样（不再替换 IB 地址）；保留 fence-only 对照（GCTL）、prefence barrier、ATOS 门禁、GIBH/GBOD/GSAT。
- 本轮唯一功能变量：**移除 `b9f698f` 的 `VM_WIMG_WCOMB`**。用户页 GPU 写失败只剩这一处属性差异（SMMU leaf 与可用页完全相同）。
- **两轮独立审计（只读）结论（batch7 的“panic/卡死”归因）**：
  1. `pa_test` 失败只置 `abandonedFlag`，没有置 `gEngineSubmitFailed`；服务随后仍进入 `engine_submit`，`pre_kick` 在**失败后的 GPU/SMMU 总线**上读 `InterruptStatus/CPInterruptStatus/micro-MMU` 并写 `0xa887/0xa881`，即记录过的黑屏/卡死模式。与 boot `0x561a49ee` 的 `GPAT rc=-4` → `GPSVC sub=0`、`GPWR` 全 0、提交线程卡住、心跳仍继续吻合。**这是本轮可见 panic/卡死最可能的来源，相关代码已回退。**
  2. `pa_test` 用第二别名改写用户目标 BO 前 3 字，破坏 `0xdeadbeef` 判据；`0x2017000/0x2018000` 实际落在 stage（16 KiB，4 页）内部，测试映射覆盖 stage 页；另有映射不 unmap 的悬挂风险。均已随回退移除。
  3. 新增 submit 全为 `cpuFenceOnly`，不重走 `kickTo` 的 `InterruptStatus` 轮询；无新 IOKit 类/元类，`OSMetaClass`/launchd 老 panic 无新增触发。
  4. BootLog 的 `GSAT` 读取弱符号前缺少 `&` 判定，已补；其余新增日志行均有弱符号守卫。
- 回退后 batch8 只保留：probe fence、fence-only 对照（GCTL）、prefence barrier、ATOS 门禁、GIBH/GBOD/GSAT；用户提交路径与 b9f698f 一致，唯一功能变量是移除 WCOMB。
- batch8 候选 `out/p21/gpu-dev-batch8/`：内核 `02981db7…`，固件 `8b874135…`，expanded payload `465e7c8a…`，gate `20261103T033155Z` PASS（644 B args）。镜像 `out/p21/gpu-dev-batch8/uefi/boot-p21-up1.img` SHA-256 `a7ed9c9f621d9371fb85304941ce71a07dc4969bf1688e2bbd4dab9920334687`；03:33:14Z 单次 `fastboot boot`，misc command 字段写后回读一致。运行结果待回收。
- batch8 判定：用户提交成功（fence 出现、`GPSVC fence≥1`）→ WCOMB 就是页级失败原因；仍 `rc=-4` 且 `GBOD` 目标未变 → 转向用户映射本身（`clientMemoryForType`/`IOConnectMapMemory64`）或 XPU/页归属。若再次出现卡死/panic，先确认 `GPSVC sub` 与 `GPWR` 是否全 0，再对照本轮回退范围。


## 2026-11-03：物理页 A/B（batch7 构建中）

- **batch6 实机（boot `0x3f63b493`，日志 `out/p21/persistent-logs/20261103T024143Z/`，镜像 `f9ec6822…`，02:33:18Z 启动、02:40:16Z 自动回 Recovery）**：
  - `GRTM rc=0 tgt=5b5b0001/0002/0003 iova=2017000`：运行期内核页在已验证窗口可写。
  - `GRT2 rc=0 tgt=5c5c0001/0002/0003 iova=4020000`：**同一内核页映射到用户窗口 `0x4020000` 也可写**（PTE `60000127605e43` 与 0x2017000 相同）。
  - `GSUB rc=-4 stg=1 fr=6a`、`GBOD` 目标页仍 `0xdeadbeef`：用户 IB 写 `0x4001000` 仍失败。
  - 结论：窗口、运行期映射、PTE 属性、暂存取指与命令流**全部排除**；失败随**用户 BO 的物理页**走。
- 本轮提交（batch7）：新增 `cheeseburger_gpu_engine_pa_test(pa)`。服务在用户提交前把目标 BO 的 PA 映射到已验证窗口 `0x2018000`，用已知 IB 写三个哨兵并 fence；结果 `GPAT map/pa/seq/rc`，随后 `GBOD` 可直接看到该 BO 是否被写入。若 `GPAT rc=0` 且 `GBOD` 出现 `5d5d0001…` → 物理页可写，问题在“用户窗口 IOVA + 用户页”的组合；若 `GPAT rc=-4` → 该物理页对 GPU 本身不可写（XPU/页状态/分配来源），与地址无关。
- batch7 候选 `out/p21/gpu-dev-batch7/`，内核与 gate 见下文；宿主模型 PASS。

## 2026-11-03：用户窗口 A/B（batch6 构建中）

- **batch5 实机（boot `0x3f8c4e97`，日志 `out/p21/persistent-logs/20261103T022050Z/`，镜像 `76391219…`，02:12:28Z 启动、02:19:25Z 自动回 Recovery）**：
  - `GRTM rc=0 map=0 pte=60000127abce43 tgt=5b5b0001/5b5b0002/5b5b0003 iova=2017000`：**CP 启动后新增的内核映射**（0x2017000）能被 CP 写入并完成 fence。→ 运行期映射本身可用，SMMU 路径没问题。
  - `GSAT atos=0 pte=6000010ab7ce43`（暂存页）：PTE 属性与运行期页同格式（`...e43`）。
  - `GSUB rc=-4 stg=1 fr=58`、`GBOD` 目标页仍 `0xdeadbeef`：用户 IB（复制到暂存页）写 `0x4001000` 仍失败。
  - `GSIB` 再次成功。结论：只剩两种可能——(a) **0x4xxxxxx 用户窗口**对 CP 不可用；(b) 用户 BO 的物理页不可写。
- 本轮提交（batch6）：把运行期测试做成两次：`0x2017000`（已验证窗口，`GRTM`）与 **`0x4020000`（用户窗口内，内核页）**（`GRT2`）。若 `GRT2` 成功 → 窗口没问题，差异在用户 BO 物理页；若失败 → 窗口/地址范围问题（再看 SMMU 侧 SID/stream 配置，而不是继续改用户 ABI）。
- batch6 候选 `out/p21/gpu-dev-batch6/`，内核与 gate 见下文；宿主模型 PASS。

## 2026-11-03：运行期映射 A/B（batch5 已构建，待启动）

- **batch3（boot `0x3f75922b`，日志 `out/p21/persistent-logs/20261102T181438Z/`，镜像 `c3738225…`）**：
  - `GSIB rc=0 stage=7 fenced=1 fence=3 tgt=5a5a0001/5a5a0002/5a5a0003`：内核在 GPMU 页（启动前映射、CP 已取过）构造的 6 dword IB 经 user-path 提交后**执行、CP_MEM_WRITE 写回、fence 完成**。IB 执行、MEM_WRITE 与 fence 全部可用。
  - `GSUB rc=-4 seq=4 pf=0 fr=46`：用户 IB（0x4000000）仍在 IB 处中止，`fr=0x46` 等于该提交最终 WPTR。
  - `GCTL rc=0 fence=2` 仍成立；`GPSVC sub=1 fence=0`。
- **batch4（boot `0x3f809cbb`，日志 `out/p21/persistent-logs/20261103T011034Z/`，镜像 `a742b3a6…`）**：
  - `GIBH 703d8003 4001000 0 adc0de1 …`：用户 IB 内容从内核自有视图读出，正是插件写入的 PM4（三条 `CP_MEM_WRITE` 写 `0x4001000/4/8`）。**IB 不是零页/陈旧页**。
  - `GBOD n=2 0:703d8003/4001000/0 1:deadbeef/deadbeef/deadbeef`：目标 BO 仍为插件预填的 `0xdeadbeef`，写入没有发生。
  - `GSUB rc=-4 stg=1 fr=46`：把同一 IB **复制到启动前映射的暂存页 0x2016000** 再提交，仍然中止。取指位置不是决定因素，失败点是命令中的**写入目标在运行期映射的用户页**。
  - `GSIB` 同轮再次成功（写入启动前映射页）。结论收敛为：**启动前映射页可读可写；运行期新增映射页的 GPU 写入失败**（ATOS 硬件走表却显示其可翻译）。
- 本轮提交 `105633a`（batch5）：probe 自检后再做 **运行期映射 A/B**——启动后把内核页映射到 `0x2017000`，用已知 IB 写入三个哨兵并回读（`GRTM rc/map/pte/tgt`）。同时把提交 IB 的原始 leaf PTE 写入 `GSAT pte=`，与已验证页比较属性。
- batch5 候选 `out/p21/gpu-dev-batch5/`：内核 `95afce84…`（native `895a4a0e…` → prelink `b4ec25af…`），expanded payload、固件与 gate 见下文补充；宿主模型 PASS。**尚未启动。**
- batch5 判定：`GRTM rc=0 tgt=5b5b0001/0002/0003` = 运行期映射内核页可用，问题只与用户 BO 页有关（再看 PTE 差异）；`GRTM rc=-4` = 运行期映射对 CP DMA 整体不可见，需要改 SMMU 映射/TLB 路径，而不是继续查用户窗口。

## 2026-11-03：内核 IB 自检（batch3 已临时启动）

- **batch2 实机结果已回收**（boot `0x448ab1c5`，日志 `out/p21/persistent-logs/20261102T174905Z/`，17:41:48Z 启动、17:48:39Z 自动回 Recovery，约 411 s）。关键三行：
  - `GCTL rc=0 stage=7 fence=2`：probe 后同一 user-path（cpuFenceOnly、无 IB）的 fence-only 提交**成功**，环/门铃/引擎在 probe 后仍可执行。
  - `GSAT atos=0 par=4000000111193b80 expect=111193000`：用户 IB IOVA `0x4000000` 的 SMMU 硬件走表与软件 leaf 一致，IB 在页表层面可翻译。
  - `GSUB rc=-4 stage=3 seq=3 pf=0/1 fr=34`：用户提交超时；`0x34=52` 恰好等于该提交环的最终 WPTR，而 control 完成后 fence 页 dword0 仍是 `2`（未被 RPTR 影子覆盖），说明这次是 CP 在 IB 处**中止批次**（RPTR 直接置到 WPTR），完成 fence 未执行；prefence（dword1）也为 0。
  - 因此“门铃/环/翻译”都已排除，剩余二分点是 **IB 内容/执行** vs **0x4000000 取指路径**。`GPSVC sub=1 fence=0 err=5`，CPU 显示 5 帧。
- 本轮提交 `e2b4ab1`：probe fence 与 control 之后，在 CP 已取过的 GPMU 页（`0x2012000`，偏移 0x200）构造 6 dword 的已知 IB（一条 `CP_MEM_WRITE(0x3d)` 写三个哨兵到同页 0x400），走同一 user-path 提交并回读；结果写 `GSIB rc/stage/fenced/fence/tgt/ib`。若自检成功，则引擎能执行内核构造的 IB，问题在用户 IOVA 取指；若失败，则 IB 执行本身不成立。另在服务端提交前用 `IOMemoryDescriptor::readBytes` 复制用户 IB 头 8 dword 到 `GIBH`（内核自有视图，无 GPU MMIO），用于判断 IB 是否真是 PM4/是否陈旧。
- 候选 `out/p21/gpu-dev-batch3/`：内核 `7947eb1f…`（native `29229384…` → prelink `4df033f0…`），expanded payload `00c0478f…`，固件 `b5101e08…`（overlay `unixnu-up1-gpu-dev-batch3`，LOWER 前置 batch2 upper），pins 命中，gate `20261102T180542Z` PASS（644 B args）。镜像 `out/p21/gpu-dev-batch3/uefi/boot-p21-up1.img` SHA-256 `c37382254d9da242ab7dee3b5860831f5fb3e37aa2a57ffa1141a97b3de2f15f`。
- **本轮启动**：18:06:34Z 对 `3b25cc7c` 唯一一次 `fastboot boot`，Sending/Booting OKAY（`boot-once.log`）；misc command 字段重写并回读一致（`8698abf4…`），未写其它分区。运行结果待回收。
- 判定参考：`GSIB rc=0 且 tgt=5a5a0001/0002/0003` = 内核在已验证页内的 IB 能执行并写回（IB 执行无墙）；`GSIB rc=-4` = 连该 IB 都不执行（引擎/批次执行问题，与用户地址无关）；`GIBH` 第一个 dword 不是 type-7 头 = 用户 IB 内容/可见性问题。

## 2026-11-03：用户 IB 硬件翻译门禁 + user-path 对照（batch2 已临时启动）

- **上一轮镜像 `2cf41785…` 的实机结果已回收**：新 boot `0x3dc9ecb3`，日志 `out/p21/persistent-logs/20261102T171817Z/`（67 rounds、心跳 sec=338、写错误 0）。设备于 17:17:52Z 自行回到 Recovery（boot 17:10:53Z，约 420 s，符合 400 s 自动复位 + misc BCB）。结果与 `b9f698f` 之前完全相同：`GPWR pre=778000/140000 post=140000/140000 me/vbif=0 st=80000101 micro=178/0`、`GSUB rc=-4 stage=3 fenced=0 seq=2 pf=0/1`、`GPSVC sub=1 fence=0 err=5 sel=5`，probe fence seq=1 成功。CPU 显示 5 帧。即：缓存属性改动没有改变用户提交结果。
- 本轮提交 `228561e`（批量实现 + 必要诊断，同一失败点）：
  1. 用户环在 prefence 后、IB 前加 `CP_WAIT_MEM_WRITES(0x12)+CP_WAIT_FOR_IDLE(0x26)`；probe fence 已证明这两条在本机可执行，因此 `pf=0` 不再能被“写还在 CP 缓冲里”解释。
  2. probe fence 成功后新增一次 **user-path 对照提交**（`cpuFenceOnly`、`count=0`、seq=2），结果写 `GCTL rc/stage/fence`。它区分“probe 后环/门铃/引擎还能执行”与“用户 IB 本身”。
  3. 用户 IB 门铃前对每个 IB IOVA 做 SMMU **ATOS 硬件走表**，PAR 与软件 leaf 不符则拒绝门铃（rc=-3/stage=0x81）；失败后仍不读 GPU MMIO。记录 `GSAT atos=/par=/expect=`。
  4. 超时后只读 CPU fence 页：`fr=` 为 dword0 原始字（CP 的 RPTR shadow 与完成 fence 共用该页），`pf=` 为 dword1；`GPWR` 追加 gpmu ready/error 与 IRQ 预采样。
  宿主模型 `run-gpu-start-model-test.py` PASS（环序号已按新 barrier 更新）。
- 候选 `out/p21/gpu-dev-batch2/`（新隔离候选，复用 ADT/trust/Data seed `1e8b97fb…` 与 `unixnu.toml`）。内核 `e5dd01ea…`（native `.unstripped.noctf` `96935a7c…` → prelink `cbd01c30…` → panic-screen 补丁），expanded payload `f699b3e7…`，固件 `1ab473a0…`（新 overlay `unixnu-up1-gpu-dev-batch2`，FVMAIN 仍 99%），pins 扫描 kernel/payload/ADT 均命中，gate `20261102T174106Z` PASS（644 B args）。
- **本轮启动**：镜像 `out/p21/gpu-dev-batch2/uefi/boot-p21-up1.img` SHA-256 `42cac48fb7094d33ca1690a59ded8ef9a6bc3eb142f9d8c00a5e55e581add820`；`boot-once.log` 记录 17:41:48Z 唯一一次 `fastboot boot` Sending/Booting OKAY。misc 仅改首 32 字节 command 为 `boot-recovery`，备份/请求/回读 `misc-before.bin`/`misc-request.bin`/`misc-readback.bin` 一致（`8698abf4…`）；未写 userdata、boot、recovery、GPT。运行结果与自动返回待回收。
- 判定要点：`GCTL rc=0 stage=7` = probe 后 user-path 环可用；`GCTL rc=-4` = 连 user-path fence-only 都不执行，问题在引擎/门铃而不是 IB。`GSAT atos=-3` = ATOS 译出的页与软件 leaf 不符（IB 不可取指，已被拦下）；`GSAT atos=0` 而 `GSUB rc=-4` = 翻译正确，问题在 IB 内容/CP 执行或 fence 可见性。`fr` 与 `pf` 供交叉验证，不得把未观测当“CP 未执行”。

## 2026-11-03：GPU 开发批次（已临时启动，实机日志待回收）

- 当前方向：优先实现 A540 用户态提交与 Metal 接口，按仓库固定的 sheng GPU 适配及同 SoC sagit 源码移植，不把软件显示或内核 fence 当作用户态 GPU/Metal 成功。每轮尽量批量完成相关功能和必要探针。
- 上一轮 `f804131` 实机 boot `0x4633ae0d` 日志：`out/p21/persistent-logs/20261102T165108Z/`，68 rounds、写错误 0；`GPWR` 的 RBCCU/SP 状态均 `0x140000`，micro 状态 `0x178/0`；用户提交 `GSUB rc=-4 stage=3`、`fenced=0`，显示 6 帧是 CPU 显示。`pf=0/1` 仅表示预置标记未被 CPU 看见，不证明 CP 一定未执行。
- 本轮提交 `b9f698f`：参考 sheng `NativeAdrenoCpu.cpp` / `NativeAdrenoMemory.cpp`，GPU 自有 BO 分配时先发布缓存，再把物理页设为 `VM_WIMG_WCOMB`；GPU 安全解绑后恢复默认页属性；提交前加完整 barrier。用户 doorbell 前检查已读出的两个电源域和 micro-MMU 返回，失败时不提交并保留对象；提交次数检查移入锁中。`AGENTS.md` 已写入 GPU 开发优先和单轮请求约束。此为代码与主机编译结果，尚未证明实机 GPU 成功。
- 本机隔离源码树 `/Users/dinnerabc/unixnu-p21/atos-gate/source-tree`；候选 `out/p21/gpu-dev-batch/`；使用 `P21_CANDIDATE=$PWD/out/p21/gpu-dev-batch /opt/homebrew/bin/python3.13 scripts/p21-compile-atos-gate.py` 增量同步并编译，内核构建通过，原有 7 组宿主模型通过；复用 Data seed `1e8b97fb…` 与现有 trust/启动配置。固件使用 p2 的独立 overlay `unixnu-up1-gpu-dev-batch`；磁盘扩到 40 GiB，固件构建、pins 和 gate `20261102T171025Z` 通过。
- **本轮启动**：镜像 `out/p21/gpu-dev-batch/uefi/boot-p21-up1.img` SHA-256 `2cf41785989e2e23202d0075de9a056ce98adbd121639bd3146ece875a016070`；内核 `d1dbe111bffc3ad71d7bd99cbc2507f48e4f14047561465e2a00948273aeb319`，固件 `2fedc7553d713097a7c64d717be2fec9a1c4d6d3e33466db9d137a57a15b61e6`，payload `18a3f6c660ce31ba2a60d44cd25ab95ca0ef0be2a5af10043eada7027958e9f5`。对 `3b25cc7c` 仅一次 fastboot boot，Sending/Booting OKAY；`out/p21/gpu-dev-batch/boot-once.log`。misc 仅授权 command 字段变更并回读，其余首块一致。未直接写 userdata、boot 或其他分区。**本轮运行结果和新日志尚未回收**；用户要求启动后停止、不监视，由下一 agent 读取新 boot ID 的日志。
- 下位 agent 接手前先读根 `AGENTS.md`、本文件、`docs/P21_GPU_HANDOFF_20261102.md`、官方 `.work/p21-official-framework/docs/porting/gpu.md`，并按新 boot ID 分析原始持久日志；保留未跟踪的 launchd cache 三个文件。

## 2026-11-02：热 Data 重跑同一镜像 `0x4986dc09`，画面恢复

- 目的：上轮 `0x5856c881` 的 `frames=0` 与授权 reseed 后的冷 Data（`state=3`）混在一起，先用同一镜像、不改代码做热 Data 对照。镜像仍为 `out/p21/gpu-plugin-lifecycle/uefi/boot-p21-up1.img` SHA-256 `a6d0ced82788442c0949f8c44016fbd335a25d489ac0cc11dd602c3fa55fae22`（等于上轮启动收据），未重编、未改 seed/trust。新 gate `gate-20261102T153150Z.json` = `PREBOOT_GATE_P21_PASS`（644 B args，pins 复核）。
- BCB：本轮只改已授权的 misc 首块 command 为 `boot-recovery`，备份/请求/回读在 `out/p21/gpu-plugin-lifecycle/warm-rerun-20261102T153150Z/`（before `ad7facb2…`、request=readback `8698abf4…`）。2026-11-02T15:34:04Z 对 `3b25cc7c` 一次 `fastboot boot` Sending/Booting OKAY；15:42:04Z 单次检查设备已在 Recovery（约 480 s，符合 400 s 自动复位 + BCB；具体复位线程未由日志证明）。未写 userdata、boot、recovery、GPT。
- 只读回收 `out/p21/persistent-logs/20261102T154240Z/`：boot `0x4986dc09`、67 rounds、0 写错误，窗口 SHA-256 `a793a57247b4ce3b5dbd02105e019e36ba429e66301cd008dc2cbccfb0511b06`；冻结于 `out/p21/gpu-cp/rounds/20261102T154403Z-warm-rerun-4986dc09/`。
- **画面恢复（本轮实测）**：`DATA persist state=2 header=0`（热 Data 恢复，未再 reseed）、`EXITS total=974`；`DISPLAY stage=10 clients=127 frames=26`、`SWAP check=26 reject=0 transfer=26 rc=0`，首次 swap 在 t=146 s；`FB2 backboardd p56 in/out=123/123 sel4/5/6=26/26/52 rc=0`；`backboardd fault exits=0`；`TOUCH publish=79 HID=79`。这证明上轮 `frames=0` 是冷 Data 首次启动的日志窗口边界（4 个 state=3 参考轮首次 swap 都在 318–338 s，而上轮窗口止于 318 s），不是显示回归；修复后的插件（仅 mediaserverd 跑探针）不阻止显示。
- **GPU 仍未完成（本轮实测）**：内核 probe fence `seq=1` 成功；`mediaserverd` 145.29 s 以 0x4147 打开，2 BO/2 map，用户提交 `GSUB rc=-4 stage=3 seq=2 pre=0/0 fenced=0`（CPU fence 超时，按设计不读 MMIO、标记 abandoned）；`GPSVC open=1 bo=2 map=2 sub=1 fence=0 err=5 sel=5`。失败后 `GOPEN name=backboardd ostg=7` 是 discovery 的 type-1 open 被 Busy 拒绝，不是 ABI 客户端。`irq/cpirq=ffffffff` 仍为未采样占位，不是寄存器真值。
- 下一步（GPU 单独一轮，需新候选与授权）：先做**只提交 fence、不带 IB**（`buffer_count=0`）的对照，区分“probe 后的 ring/doorbell/engine 是否还能执行”与“运行期 0x4000000 IOVA 取指”；该实验不触碰用户 IB/目标，失败后仍只读 CPU fence。不要再用 `frames=0` 归因插件或显示，也不要把本轮 `frames=26` 说成 GPU/Metal 工作。

## 2026-11-02：插件生命周期修复实机回收 `0x5856c881`

- 用户报告设备已重启进入 Recovery；只读回收 `out/p21/persistent-logs/20261102T143713Z/`，新 boot `0x5856c881`、64 rounds、0 写错误。当前详细交接与下一步见 `docs/P21_GPU_HANDOFF_20261102.md`。
- 相比前一轮 backboardd 14 次 fatal exit，本轮为 0；`mediaserverd` 成功打开 ABI，2 BO/map，用户 GPU 提交 `rc=-4` / `fenced=0`。`frames=0`、`SWAP check=0`；不能宣称画面或 GPU 工作。`DATA state=3 writes=136728 err=0` 只说明最后快照仍在初始化/持久化，未证明完成。返回 Recovery 的触发方式未证实，触摸未实测。
- 设备现处 Recovery，本轮禁止自动监控；下一步先分析 backboardd 存活后仍无 swap/vsync 和 GPU IOVA/fence，保留本轮 seed，避免未经授权再次 reseed。

## 2026-11-02：backboardd 首次崩溃符号化及插件生命周期候选

- boot `0x4dcc2685` 的 `backboardd p56` 首次 sig11，用本机只读挂载的 21H461 SystemOS dyld cache 和对应 symbols UUID `a1a859a2479c31998a34f4afbd808cf5`，按日志 cache slide `0x1ce44000` 解析：PC `0x1ac39bf50` 是 `libdispatch _dispatch_release$VARIANT$mp+8`，LR `0x1a499777c` 是 `Metal -[_MTLDevice dealloc]+0x44`，下层经 `_MTLRegisterDevices` / `MTLDeviceArrayInitialize` / `CA::WindowServer::AccelServer::renderer`。`0x1016ba4c4` 不在共享缓存，其镜像身份未确认。详见 `out/p21/gpu-plugin-lifecycle/symbolication.txt`。`FAR=0` 和栈表明 Metal 设备发现释放路径空指针；并不单独证明插件源码是唯一原因。
- 已部署旧插件 CDHash `6b740b7173e8d6b73a39f3c3c13e4acdfb3bac88` 在只读挂载的 `/Volumes/OwnerDataP17 3/...` 可复核；当前源码先前已调整为 mediaserverd 单宿主但**没有部署**。旧及先前新插件在 `initWithAcceleratorPort:` 内直接返回 nil，未调用 `[super init]`；官方 `.work/p21-official-framework/drivers/AdrenoMetal/NativeAdrenoMetalPlugin.mm` 先调用 `self = [super init]`，测试工作放独立线程、保留 service port。高度吻合释放栈，但需实机验证。
- 源码提交 `fbd4e88`：按官方先初始化父类；仅 mediaserverd 上以单次原子门执行探针；保留 IOService 端口引用，异步运行 GPU 用户态探针，避免 Metal 发现线程被 GPU 超时阻塞。编译签名通过；plugin CDHash `6ff9b3b19210bc4896900adbe3e823358abaaf0d`；trust bundle SHA `0a7d5a328be859eb3d66349318fe93bd6530c2670250a44fb40211fb6ff8e1a4`。测试线程仍可能超时，GPU 用户 IB/fence 未修复、不可称 Metal 加速。
- 本机仅从已用 seed `5446b3a2...` 生成插件替换副本 `out/p21/gpu-plugin-lifecycle/dev-seed.dbr`，SHA `1e8b97fbc0a2020c5e359e4a1acc93f9e435a45dcf79bf5f415d7fbd3db81993`；更改 seed identity 会使手机持久 Data reseed，覆盖既有 Data；用户已明确授权本轮重置 Data 并写入修复插件。原始 seed 未修改；未用主机命令直接写手机 userdata 或其它系统分区。候选保留上轮 CPU fence 内核 `98ba2408...`，不重编 XNU，payload、独立 UEFI、pins 和 gate `gate-20261102T141223Z.json` 已通过。镜像 `out/p21/gpu-plugin-lifecycle/uefi/boot-p21-up1.img` SHA `a6d0ced82788442c0949f8c44016fbd335a25d489ac0cc11dd602c3fa55fae22`，设备 `3b25cc7c` 临时 Fastboot boot 一次 Sending/Booting OKAY；用户授权的 Data reseed 是否发生尚待本轮日志确认。已授权的 misc 首块仅 command 改为 boot-recovery 并回读；记录保存在候选目录。用户之前要求停止 Recovery 监控，本轮未启动监视器，等待用户通知设备返回后只读回收日志；不能用旧 boot 结果推断本轮显示或 GPU 成功。

## 2026-11-02：CPU fence 候选实机结果 `0x4dcc2685`

- 用户报告没有进入系统，设备自动返回 Recovery。只读回收 `out/p21/persistent-logs/20261102T135923Z/`，新 boot `0x4dcc2685`、66 rounds、write_errors=0，窗口 SHA-256 `844798851488ee5423a63e2df926983f16bc4687c0bfbde836bfead3a745497f`；对应本轮唯一临时启动镜像 `2d2ae77d8b6eee1a034f34af3c843803e1ad3c24e5c55aeefde7b3b70ceb1cbc`。回收前按用户要求已停止后台监控，本轮 Recovery 返回是用户报告，日志中复位线程没有 fire 后记录，不据此判断具体线程。
- 内核 GPU probe fence seq=1 成功；用户态 backboardd 成功 open、2 BO 分配映射，用户 seq=2 未完成：`GSUB rc=-4 stage=3 irq=ffffffff cpirq=ffffffff pre=0/0 fenced=0 seq=2 post=0`。`ffffffff` 是本轮刻意不采样 MMIO 的占位，不表示真实 GPU 寄存器值。心跳运行到 writer sec=328，未见上轮 `kickTo` 读 MMIO panic；但持久日志不覆盖所有 panic，不能保证不存在其他 panic。
- 显示 `clients=17 frames=0 SWAP check=0 transfer=0`，不是 SWAP 被拒；backboardd fault exits=14，首个已捕获 `sig11 PC=1ac39bf50 LR=1a499777c ESR=92000005 FAR=0`，启动进程反复更换；SpringBoard fault exits=0。backboardd 多次死亡是零帧的重要线索，但尚未符号化，不能断言由插件或 GPU 超时引起；sandbox 拒绝写 `/dev/console`、`/private/var/tmp/p21-plugin.txt`，不能从插件文本缺失推断未执行。下一步优先符号化对应同版本 dyld cache / 实际插件和首个 backboardd crash 栈、确认资源清理与发现阶段，在不关闭 AMFI 和不伪造成功的前提下修复 crash；并继续查 CP 未完成 fence 的实际原因。设备当前 Recovery；不自动重复启动。

## 2026-11-02：GPU 提交外部中止与 CPU fence 轮询修复（已临时启动）

- 用户提供 panic：PC `0xfffffff00bf9d34c`、LR `0xfffffff00bf9d330`，ESR `0x96000210`，FAR 对应访问 `[x8,#0x1384]`。Recovery 已只读回收 `out/p21/persistent-logs/20261102T133308Z/`，boot `0x49a91db8`，17 rounds、0 errors，末条 CBHB `submit`；启动镜像身份应以实际启动记录复核。当前 `out/p21/atos-dev-job/kernel/kernel.macho` 的反汇编在该 PC 是 `kickTo` 中读取 GPU `InterruptStatus`（offset `0x1384`），而寄存器 x8 加 offset 与用户 FAR 一致；这说明 MMIO read 触发 external abort，不证明 GPU 卡死的根因或精确出错命令。屏幕转录的地址存在多余 f，符号化使用规范 64 位地址；候选内核身份尚须与实际启动收据关联。
- 提交 `c6dd16c`：用户提交门铃后改为只轮询 CPU fence，超时禁止后续提交、不读写 GPU MMIO、不假定停止 DMA；恢复/内存保留仍不保证总线不会在写门铃或其他位置中止。启动时内核 fence 仍用原有检查。删除旧 scratch 测试：其要求 `buffer_count>=2`，而插件实际只提交一个 IB，且静默重写用户 IB/目标会破坏 ABI 语义；不能用该测试结果宣称用户目标写回。
- 宿主模型 `out/p21/gpu-cp/host-tests/20261102T133458Z/` 7/7 PASS，包括新增门铃后无 MMIO 读取的成功/超时模型。模型不证明硬件或 panic 已修复。新候选 `out/p21/panic-cpufence/`，本机树 `/Users/dinnerabc/unixnu-p21/atos-gate/source-tree`；编译成功（90.6 秒），内核 `98ba240854d2cbd44a7d13f85efa2dc43fcc62e5aded0417bb85218fc7c3d251`；独立 VM overlay `unixnu-up1-panic-cpufence` 的固件构建完成（3 分 22 秒），使用同轮 payload、ADT 与 firmware 重打，门禁 `gate-20261102T135013Z.json` PASS。
- 临时启动：镜像 `out/p21/panic-cpufence/uefi/boot-p21-up1.img` SHA-256 `2d2ae77d8b6eee1a034f34af3c843803e1ad3c24e5c55aeefde7b3b70ceb1cbc`，目标 `3b25cc7c` 一次 `fastboot boot` Sending/Booting OKAY；不等于内核或 GPU 成功。此前 misc 只更新已授权的首 32 字节 command、其余首块保留并回读一致；保存 `misc-before.bin`、`misc-request.bin`、`misc-readback.bin`。本轮未写 userdata、boot、recovery 或 GPT。启动记录 `boot-once.log`；用户要求停止 Recovery 监控：只读监视已停止，`monitor.log` 未观察到本轮 Recovery；本轮新日志尚未回收，设备状态待用户确认。构建修复还删除了无 provider 的 GCRYPT 诊断符号（提交 `f4c8e19`）。
- 后续：等用户通知恢复健康 Recovery 后读取对应新 boot ID，检查 CPU fence 超时或成功、panic 是否再现及实际返回方式。CPU fence 超时不证明 DMA 停止，也不会修复 IOVA 可见性；继续对照 sagit 排查 runtime 映射、表可见性和合法提交，不要把内核 fence 或 CPU 显示帧等同 GPU 用户回读、渲染或 Metal。

## 2026-11-02 受控启动 `0x439f93bf`：`0x2020000` 窗口 panic，回退并改用预映射 scratch 自检

- 镜像 `1ca0ca37…`、内核 `bfd84b28…`（用户窗口 `0x2020000`）于 11:51:33Z 启动；用户报告显示 panic 屏后黑屏并手动回 Recovery。日志 `out/p21/persistent-logs/20261102T120953Z/`（窗口 `c8d2f258…`）。
- 日志：探测 fence OK（sec 52），用户 `tag=submit`（sec 81）后心跳停止；msgbuf 到 ~139.6 s；设备未自动返回。panic 文本没有落盘（启动时 `gPanicBase is still not initialized`），所以没有 panic 的具体 pc/寄存器；不得猜测 panic 原因。
- 唯一变量是用户窗口：上一轮同内核窗口 `0x4000000` 只是停顿并 400 s 自动返回，本轮换到 `0x2020000` 即 panic。已将 `cheeseburger_gpu_engine_window` 回退到 `0x4000000`。
- 下一步（不改 seed/插件）：把用户 IB 复制到探测阶段已映射、CP 已取过的 GPMU 页（`0x2012000`），把 MEM_WRITE 目标改到同缓冲区的第二页（`0x2013000`），在 fence 后由内核直接回读三值+哨兵。这样可区分“命令流/MEM_WRITE 可用性”与“运行期用户 IOVA 可达性”。候选已构建、未启动：镜像 `f7f8e0b7…`、内核 `6ae33985…`、固件 `7246f5ef…`、payload `b651b863…`，gate `20261102T131811Z` PASS；窗口已回退到 `0x4000000`，seed/trust 不变。设备当前 Recovery，misc BCB 已消费，启动前需重写。

## 2026-11-02 受控启动 `0x45655a90`：pageTable 更新未解决取指，改测用户 IOVA 窗口

- 镜像 `bcef55af…`、内核 `3ab90dee…`（用户 IB 前发送 sagit pageTable 0x53/0x43，屏障 0x12+0x26）于 11:34:16Z 启动，11:41:06Z 自动回 Recovery。日志 `out/p21/persistent-logs/20261102T114114Z/`（窗口 `74cbb9e6…`）。
- 结果与 NOP 探针相同：心跳 `f=-1`、`tag=submit` 后无 `submitdone`，日志到 338 s 仍继续；提交线程仍卡在取指，pageTable 刷新没有让 CP 看到 `0x4000000`。
- 现有证据把问题收敛到“CP 可访问的 IOVA 窗口”，而不是 TLB 刷新顺序：探测页 `0x2000000–0x2015fff` 一直被 CP 正常取指，用户窗口 `0x4000000` 从未成功。本轮功能实验：把 `cheeseburger_gpu_engine_window` 的起点从 `0x4000000` 移到探测页之后的 `0x2020000`（16 MiB），插件按 `deviceInfo.va_start` 自动跟随，无需改 seed。候选已构建、未启动：镜像 `1ca0ca37…`、内核 `bfd84b28…`、固件 `b81e25ad…`、payload `480ea44a…`，gate `20261102T115026Z` PASS；seed/trust 不变。设备当前 Recovery，misc BCB 已消费，启动前需重写。

## 2026-11-02 受控启动 `0x48f4c3b3`：NOP 探针证明 CP 取不到运行期 IOVA，加入 pageTable 更新

- 镜像 `51841998…`、内核 `5337eb8e…`（用户 IB 临时替换为单个 `CP_NOP`、长度 1）于 11:16:56Z 启动，11:23:58Z 自动回 Recovery。日志 `out/p21/persistent-logs/20261102T112404Z/`（窗口 `9ac23a5c…`）。
- 结果：`GPSVC open=1 bo=2 map=2 calls=9 sub=0 sel=5`、无 `GSUB`、心跳 `f=-1`、无 `submitdone`；提交线程卡在 `engine_submit` 内，但 msgbuf/心跳继续到 406 s。即单 NOP IB 从“opcode error”变成“取指停顿”，与 CP 看不到运行期新增 IOVA（0x4000000）一致；探测的 GPMU IB 在 CP 启动前映射（0x2012000），一直正常。
- 官方核对：`0x26` 是 `CP_WAIT_FOR_IDLE`，不是 `CP_WAIT_MEM_WRITES`；后者是 `0x12`，sagit barrier 为 `0x12 + 0x26`。sagit `Encoder::pageTable()` 在每次提交前发送 `0x12/0x26 → 0x5f/reg 0x831 → 0x13 → 0x53(ttbr) → 0x43(invalidate) → reg 0x831/0x5f → 0x13`，用于内核侧 A5xx SMMU 转换。
- 本轮功能修复：`Startup::submit` 新增 `pageTableVA` 参数并在 IB 前发送 sagit 的完整 pageTable 序列；屏障改为 `0x12 + 0x26`；`cheeseburger_gpu_engine_submit` 传入 `cheeseburger_gpu_smmu_root()`。探针 fence 仍走 `pageTableVA=0`。宿主模型新增 page-table 序列断言并 PASS。候选已构建、未启动：镜像 `bcef55af…`、内核 `3ab90dee…`、固件 `a6224a21…`、payload `94ae1f85…`，gate `20261102T113251Z` PASS；seed/trust 不变。设备当前 Recovery，misc BCB 已消费，启动前需重写。

## 2026-11-02 受控启动 `0x4f7fca90`：一次性切换后仍是 CP_OPCODE_ERROR，转 IB 取指/NOP 探针

- 镜像 `54d36703…`、内核 `25ed4f9f…`（`secureLeft`，用户流为 `IB → WAIT → FLUSH`）于 10:56:49Z 启动，11:03:28Z 自动回 Recovery。日志 `out/p21/persistent-logs/20261102T110333Z/`（窗口 `87e402de…`）。
- 心跳仍为 `f=-3/3/0/1/2/0/0/0`：`rc=-3, cpirq=1`（CP_OPCODE_ERROR），说明重复 secure-mode 切换不是（唯一）原因。msgbuf：`open ok`（149.53 s）、用户 BO 映射 `0x4000000/0x4001000 tlb=0`、`submit rc=-3 rptr=24 wptr=ffffffff irq=0 cpirq=1 fenced=0`；`GPSVC sub=1 fence=0 wait=0`。
- 剩余差异只有用户 IB 的内容与取指：ring 里的 WAIT/FLUSH 已被探测 fence 证明；IB 的 `CP_MEM_WRITE` 编码与 sagit/官方 PM4 一致。IB 位于运行期新增的 IOVA（0x4000000），而取指正常的 GPMU IB 位于 CP 启动前映射的 IOVA（0x2012000）。
- 本轮诊断（不改 seed/插件）：内核在用户提交时把 IB 首字临时替换为单个 `CP_NOP` 并把环内 IB 长度改为 1。若 fence 完成 → CP 能取到该 IOVA，问题在 MEM_WRITE；若仍 `cpirq=1` → 问题在运行期 IOVA 的 CP 取指/页表。候选已构建、未启动：镜像 `51841998…`、内核 `5337eb8e…`、固件 `569f6d4a…`、payload `38c60659…`，gate `20261102T111525Z` PASS。
- 另核对到：官方 A740 ring 与 sagit 都将 `0x26` 定义为 `CP_WAIT_FOR_IDLE`，`CP_WAIT_MEM_WRITES` 是 `0x12`；sagit barrier 为 `0x12 + 0x26`。本机 `submit()` 目前只用 `0x26` 且注释错误。该差异不解释 opcode error（探测 fence 已证明 0x26 合法），但下一版需要按 sagit 修正屏障顺序，确保 MEM_WRITE 在 fence 前完成。设备当前 Recovery，misc BCB 已消费。

## 2026-11-02 受控启动 `0x4989a7fb`：拿到 `CP_OPCODE_ERROR`，修复重复 secure-mode 切换

- 镜像 `fd0f8ff4…`、内核 `ff662e87…`（IB-first、无用户 ATOS、保留新故障记录）于 10:11:29Z 启动；10:18:20Z 自动回 Recovery。日志 `out/p21/persistent-logs/20261102T101828Z/`（窗口 `b6cbb203…`）。
- 心跳首次给出完整内部结果：`f=-3/3/0/1/2/0/0/0` → `rc=-3, stage=3, irq=0, cpirq=1, pre=0/0, fenced=0, seq=2`；日志继续到 328 s，没有停写。`cpirq=1` 按 Linux A5xx `a5xx.xml` 是 **CP_OPCODE_ERROR**（bit0；bit1 reserved、bit2 HW fault、bit3 DMA、bit4 register protect、bit5 AHB）。
- IB 的 `CP_MEM_WRITE(0x3d,3)` 与 sagit `Encoder::fill` 和官方 PM4 测试头 `0x703d8003` 一致，不是编码错误。探测 fence 已经把 CP 切到非安全模式；用户提交又重复发送 `CP_SET_SECURE_MODE(0)`，CP 对重复切换报 opcode error，后面的 fence 从未执行（`fenced=0`）。之前 IB-first 的无 ATOS 内核能返回同一个 `rc=-3` 也与此一致。
- 修复：`Startup` 新增 `secureLeft`，只在第一条完成的流（探测 fence）发送 `CP_SET_SECURE_MODE(0)+等待转换`；后续用户流为 `IB → CP_WAIT_MEM_WRITES → CACHE_FLUSH_TS`。宿主模型新增第二次提交断言，确认环中不再出现 0x66。候选已构建、未启动：镜像 `54d36703…`、内核 `25ed4f9f…`、固件 `ba949846…`、payload `d5a66152…`，gate `20261102T103058Z` PASS；seed/trust 不变。设备当前 Recovery，misc BCB 已消费，启动前需重写。

## 2026-11-02 受控启动 `0x46d23365`：去掉 ATOS 仍卡，锁定 secure-mode 环顺序

- 镜像 `381a0e1c…`、内核 `54485e9d…`（删除用户路径 ATOS，保留 WAIT）于 09:43:35Z 启动；设备于 09:50:35Z 返回 Recovery（约 420 s）。日志 `out/p21/persistent-logs/20261102T095051Z/`（窗口 `84b65bf7…`）。
- 现象与黑屏轮一致：心跳停在 `sec=80 tag=submit`，17 rounds，无 `submitdone`；msgbuf 到 142.5 s；槽位里的 `GPSVC open=0` 是发现之前的旧快照，不能据此判断 open。`GSUB rc=-1` 说明提交仍未返回。
- 因此 ATOS 不是主因；用户路径与旧内核 `e5612490`（能返回 `rc=-3`）剩下的唯一差异是 `CP_SET_SECURE_MODE(0)` 被移到 IB 之前。sagit 顺序的 WAIT 也没有避免停顿。
- 本轮修改：把环顺序恢复为 `e5612490` 的 `IB → CP_SET_SECURE_MODE(0) → CP_WAIT_MEM_WRITES → CACHE_FLUSH_TS`，保留新的故障记录（pre/irq/cpirq/fenced）和失败关闭；宿主模型 PASS。候选已构建、未启动：镜像 `fd0f8ff4…`、内核 `ff662e87…`、固件 `4351a938…`、payload `99709541…`，gate `20261102T095939Z` PASS；seed/trust 不变。设备当前 Recovery，misc BCB 已被消费，启动前需重写。此版预期返回 `rc=-3`，目的是拿到第一个真实 irq/cpirq。

## 2026-11-02 受控启动 `0x55637174`：提交卡在 engine_submit 内，定位到用户路径 ATOS

- 镜像 `f61ffaac…`、内核 `ec646223…`（secure-mode 后插入 WAIT）于 09:09:38Z 启动；设备在 400 s 整（09:16:18Z）自行回到 Recovery。日志 `out/p21/persistent-logs/20261102T091635Z/`（窗口 `eba7a004…`）。
- 与上一轮不同：日志写到 318 s、65 rounds、0 write errors，**UFS/心跳没有再停写**；`AUTORESET requested=400 … mstart=1 gstart=1`。说明 WAIT 改变了故障形态，但提交仍未返回。
- 提交链：`CB GPU open: ok … name=mediaserverd`（153.28 s）→ 两条 `CB GPU client atos … rc=0` → 心跳 `tag=submit`（writer sec 78）→ 之后没有 `submitdone`；`GPSVC open=1 bo=2 map=2 calls=9 sub=0 sel=5`（最后方法 Submit），`GSUB rc=-1`、心跳 `f=-1`。即线程卡在 `engine_submit` 内，失败记录未生成。
- 关键差异：探测路径的 ATOS 只在 CP 启动前使用（`CheeseburgerGpuCp.cpp:260`，早于 start）；用户提交是在**运行中的 CP** 上 halt/resume micro-MMU，随后立即写门铃。无 ATOS 的旧内核 `e5612490` 在 open 后 6 ms 返回 `rc=-3`；两个含 ATOS 的内核都通过 `rc=0` 探测后在 `engine_submit` 内卡住。
- 本轮修改：删除用户路径的 ATOS 门禁（保留 `cheeseburger_gpu_smmu_atos` 供启动前探测），其它不变。新候选已构建、未启动：镜像 `381a0e1c…`、内核 `54485e9d…`、固件 `4713e6f7…`、payload `c71c804d…`，gate `20261102T092530Z` PASS；seed/trust 不变。设备当前 Recovery，misc BCB 已再次被消费，下一轮启动前需重写。

## 2026-11-02 受控启动 `0x5d6a3728`：复位成功，提交仍停顿（已定位到 secure-mode 顺序）

- 镜像 `9df13c4b…`、内核 `0aabfb26…` 于 08:32:19Z 临时启动；设备在约 400 s 后自行回到 Recovery（08:39:06Z），**无需手动回收**。日志 `out/p21/persistent-logs/20261102T083940Z/`（窗口 `e89bd9c9…`）。
- 新复位路径生效：`AUTORESET requested=400 … mstart=1 gstart=1`，两个 reset 线程创建成功；设备按 400 s 返回。复位路径设计上不再写心跳/日志，因此无法从心跳区分 main 还是 guard 触发，也没有 post-fire 字段；返回位置和计时是唯一证据。
- 用户提交仍然停顿：心跳最后一帧是 `sec=81 tag=submit`，没有 `submitdone`；`GSUB rc=-1`（提交没返回），槽位停在 `elapsed=79`，17 rounds、0 write errors。msgbuf 文本到内核 161.7 s（内核仍活），说明是提交后的 UFS/日志停写而不是整机死亡；随后 400 s PSCI 把设备带回。
- 同轮证明 ATOS 与门铃可用：82.9 s 的 `CB GPU SMMU atos … rc=0` 之后，107 s 探测 fence `seq=1` 成功。
- A/B：上一轮内核 `e5612490`（IB 在 `CP_SET_SECURE_MODE(0)` **之前**，无 ATOS 门禁）用户提交返回 `rc=-3` 且系统继续；本轮两个内核都改成 `SET_SECURE_MODE(0)` 在 IB 之前，均在门铃后停住。sagit 的顺序是 `SET_SECURE_MODE(0) → CP_WAIT_MEM_WRITES →` 后续 DMA。因此最小修复是在切换后、IB 前插入 `CP_WAIT_MEM_WRITES`（等待 secure-mode 转换），`SubmitFenceWords` 从 8 改为 9；宿主模型已更新并 PASS。
- 设备当前 Recovery；misc BCB 已被本次 Recovery 消费（回读全 0），下一轮启动前必须重写。新候选已构建但**未启动**：镜像 `f61ffaac…`、内核 `ec646223…`、固件 `c71a5912…`、payload `2bc2139d…`，gate `20261102T084944Z` PASS；seed/trust 仍为 `5446b3a2…`/`e92415cc…`。宿主 `gpu_start_model` PASS（含切换 WAIT 后的环序号）。不得把本轮的“自动回 Recovery”说成 GPU 提交成功。

## 2026-11-02 GPU 失败路径硬化候选（仅构建，设备未启动）

**设备与证据**：最近一次已回收日志是 boot `0x405db723`（`out/p21/persistent-logs/20261102T025610Z/`）：`CB GPU open: ok … type=4147 abi=1 name=mediaserverd`、`GPSVC sub=1 fence=0 err=5`、`CB GPU engine: submit rc=-3 stage=3 seq=2 rptr=25 wptr=25`；该内核在失败后还读取了 rptr/wptr，且没有记录 irq/cpirq，因此**不能判断错误位或是否为陈旧 IRQ**。之后启动的镜像 `4411fd8b…` 黑屏，其 boot 日志尚未回收，本轮没有向设备发送任何命令，也没有把 `0x405db723` 当作这次黑屏的证据。设备当前仍需用户手动确认/返回 Recovery。

**本轮源码修复（已构建，未启动）**：
- `CheeseburgerGpuStart.h`：`kickTo()` 不再重试 doorbell，错误 IRQ/超时后不再读 CP/VBIF/Interrupt/Status/ME（未采样字段为 `0xffffffff`，`postRead=false`）；`fail()` 先建立 abandoned/rc/stage，再只发 posted halt，不写日志/不等待 UFS。用户 `submit()` 在 doorbell 前采样 `InterruptStatus`/`CPInterruptStatus`：RBBM 陈旧位按 sagit 方式用 `InterruptClear=OwnedIRQs` 清除（记录在 `pre=`），已锁存的 CP fault 拒绝提交（`-7`），不可访问总线拒绝（`-6`）。失败后仅读 CPU fence 页记录 `fenced`，不再碰 MMIO。
- `CheeseburgerGpuCp.cpp`：每次 boot 只允许一次用户提交；失败全局记录 rc/stage/irq/cpirq/pre/fenced/seq/post；失败后 `engine_idle()=-1`、新 ABI open 返回 Busy。
- `CheeseburgerGpuService.cpp`：新增 retained-IOVA 表；任何无法拆除/同步的 SMMU 叶表（teardown 或 map 回滚失败）都会保留 IOVA，并在 gate 释放后仍拒绝新 ABI 连接（`ostg=8`），防止后续连接覆盖旧叶 PTE。
- `native_adreno_metal_plugin.mm`：硬件测试只在 `mediaserverd` 内运行（`getprogname()`），其它发现宿主立即返回 nil；open 重试从 240×0.5 s 缩短到 20×0.25 s（5 s）。**此插件已编译进 `userland/`，但未写入本轮 Data seed**（见下）。
- `CheeseburgerAutoReset.cpp` + `CheeseburgerAutoResetPolicy.h`：主线程到点直接 PSCI，不依赖 IOLog/UFS；guard 在 deadline+10 s 只补发一次；记录 main/guard 创建、触发与 SMC 返回值；宿主模型覆盖日志停、线程创建失败、normal/guard 路径。

**宿主测试**：`out/p21/gpu-cp/host-tests/20261102T080745Z/` 七组模型全部 PASS（`gpu_start_model` 新增陈旧 IRQ 清除、锁存 CP fault 拒绝、ErrorLate fence 证据、MMIO 不可达、日志停、单次 doorbell）；`tests/test_heartbeat_codec.py` 11/11 PASS（tick 的 `f=` 增加 `pre/precp/fenced`）。

**本轮候选**（`out/p21/atos-dev-job/`，gate `20261102T082022Z` = `PREBOOT_GATE_P21_PASS`）：
- 内核 `0aabfb26f17de0b7bbf0e70af16257a687dc71f06f4a089f7bc642193dfc387f`（编译 `f87f2af0…`、native `03e133c1…`、prelink `5a5fd33a…`）；固件 `ba43c2e4…`；payload expanded `44efa98c…`；镜像 `uefi/boot-p21-up1.img` `9df13c4b…`；boot args 644 B；FVMAIN/AARCH64 pin 扫描通过。
- **Data seed 未更换**：沿用当前实机镜像 `4411fd8b…` 中的 seed `5446b3a2…`（base `46d03a5a…`）和 trust `e92415cc…`，避免未授权 reseed；其 owner 模块仍是 `6b740b71…`（旧插件）。新插件 cdhash `c6936b5a…` 与 trust `0726b249…` 已构建但未部署，记录在 `out/p21/atos-dev-job/deployed-userland.json`。因此本轮不会有插件单宿主/短重试的行为变化。
- 设备下次受控启动后先只读回收当前 boot 日志，核对 boot id、心跳、AUTORESET 与实际返回位置，再执行一次 `fastboot boot`（misc BCB 规则不变）。重点看心跳 `f=` 的 `pre/irq/cpirq/fenced`：`-3` + 新 CP 位=真实 CP fault；`-7`=doorbell 前已锁存 CP fault；`-4`=超时；`fenced=1`=错误位锁存但 ring 实际完成。

## 2026-11-02 GPU 用户态迁移（官方插件路线，进行中）

**最新状态（21:40Z）**：为了在快照提前停止时仍能看到插件 open 的拒绝阶段，本轮在 open 路径加了限流 `IOLog`，并让 CBHB tick 心跳附带 `m/l/o/s/a`（marker/最后 marker/open/拒绝阶段/ABI open）。候选镜像 `ae1bb6db…`、内核 `73bbf95f…`、固件 `e1bf2f9a…`、payload `b6482609…` 于 21:27:48Z 启动后**没有自动回到 Recovery**（超过 12 分钟 adb/fastboot 均不可见），与此前每轮 400 s 自动返回不同。不要重复启动；需要用户检查屏幕并手动回到 Recovery 后先回收该 boot 的日志，再决定是否回退本轮诊断改动。可疑点：open 路径的 `IOLog` 与 auto-reset 线程的 `IOLog` 共享 console/msgbuf 锁（项目已知该锁可能阻止 400 s 复位）；下一版应去掉 open 路径 IOLog，只保留心跳字段（心跳不经 msgbuf）。

用户决定按上游（UniXNU/UniXNU-sheng）方式迁移 GPU 用户态。已完成从“自建 launchd 作业”到“**IOAcceleratorES + 插件在 backboardd 内运行**”的切换，并推进到插件实际执行、用户态客户端建立连接。**尚未完成 P1 的 BO/submit/fence/回读**。

- 关键实现（已提交源码）：
  - `package/BoardRuntime/CheeseburgerGpuAccelerator.cpp` + `OriginalAcceleratorABI.h` + `accelerator-original-bindings.S`：`AGXAccelerator : IOAcceleratorES`，发布 `MetalPluginName=NativeAdrenoMetal`、`MetalPluginClassName=NativeAdrenoMetalDevice`、`NativeSandboxGrantProcess=backboardd`，attach 到 `IOService::getPlatform()` 并 `registerService()`。类地址按本机 prelink 静态审计：构造 `0xfffffff004fa6b20`、析构 `0xfffffff004fa6b64`、元类 `0xfffffff006ebe688`（多次重编后不变）。
  - `CheeseburgerGpuService.cpp`：发布时先 `OSKextLoadKextWithIdentifier("com.apple.iokit.IOGPUFamily")`（仅 prelink 不会注册 `IOAcceleratorES`），再 `preModLoad` + 本类 `__GLOBAL__sub_I` + `postModLoad`；用户客户端接受任意 open 类型；单客户端门只约束 0x4147 ABI 客户端，其它类型为 observer（ABI/内存一律拒绝）；记录 opener 名称/uid/拒绝阶段。
  - `CheeseburgerGpuProbe.cpp`：探测开始时（约 61 s）提前发布 accelerator。
  - `package/Userland/native_adreno_metal_plugin.mm` + 构建脚本：owner 插件 bundle（`NativeAdrenoMetal.bundle`，principal class 继承私有 `_MTLDevice`，链接 Metal/IOKit/Foundation），在 `initWithAcceleratorPort:` 内跑 P1 后返回 nil；带 IOServiceOpen 进度 marker、`/private/var/tmp/p21-plugin.txt` 标记与 SetMetadata 结果。cdhash 加入 owner trust module。
  - seed staging 写入 `/private/var/mobile/ngpu/addons/NativeAdrenoMetal.bundle`；既有 addon union mount 呈现到 `/System/Library/Extensions`（`GADDON ext=5/0` 已验证）。
- 实机序列（均 400 s 自动回 Recovery）：
  1. `0x3ee8a3aa`（镜像 `353227a6…`、内核 `6017bc11…`、payload `09ad09dc…`）：`open=1 close=1 uid=1f5`（backboardd 打开 GPU 用户客户端），但 `markers=0`——插件类不是 `_MTLDevice` 子类，被 Metal 发现跳过。
  2. `0x3eddc234`（镜像 `da60d9f6…`、内核 `e3cb72fe…`、payload `2e257e22…`）：`GOPEN name=backboardd uid=1f5`；`markers=0`；`/private/var/tmp` 无插件标记文件。
  3. `0x3ee780a4`（镜像 `b288102a…`、内核 `e3cb72fe…`、payload `1f6ff98d…`）：继承 `_MTLDevice` 后**插件开始执行**，`GPSV2 markers=442 last=3`，但 `open=1 close=0`——发现的 type-1 observer 客户端占住单客户端门，插件的 0x4147 open 得 Busy（`refused=428`）。
  4. `0x3ee4fbce`（镜像 `8958e6e2…`、内核 `a4b6c14f…`、payload `b3f99009…`）：A/B 门拆分后仍 `markers=511 refused=507 open=1 close=0`，插件 open 仍被拒。
  5. `0x3f552af0`（镜像 `905a7823…`、内核 `d1018ec3…`、payload `b51895cb…`）：本轮加入 open 阶段计数（`GOPEN … ostg=1..6`），但**快照只写到 17 rounds（约 85 s）**；心跳 tick 到 348 s、400 s 自动复位正常（日志/快照提前停止是已知现象）。accelerator 61.3 s 发布、服务 107.5 s 发布，发现活动在日志停止之后，未取得 `ostg`。
- 结论/下一步：
  - 官方路线已打通到“backboardd 的 Metal 发现加载并执行 owner 插件”；剩余是插件 0x4147 open 被拒的确切阶段（`ostg`：1 未就绪、2 Busy、3 内存、4 init、5 attach、6 start）。下一轮先在拒绝分支加**限流的 IOLog**（快照停止时仍能在 msgbuf 看到阶段与 open type），再启动；若为 Busy，需确认 observer 的 open type 与门持有者；若为 start/attach，按阶段修复。
  - 之后才是 `--publish`（backboardd 返回原生设备、CARenderServer 合成）与 Metal 前端；A540 没有现成的 metal2vulkan/Turnip 等价物，P1 阶段只用我们的 ABI，不声称 Metal。
  - 设备当前 Recovery；misc BCB 每轮启动前重写（已消费）。证据在 `out/`，冻结于 `rounds/20261101T211939Z-plugin-mass-gate` 等目录。

## 2026-11-02 launchd 注入结论与转向官方插件路径（本轮完成，等待迁移实现）

- 本轮合并了上一轮本机构建候选并完成三次实机启动，设备 `3b25cc7c` 每次都自动回 Recovery（约 410–470 s，`CB auto reset: 400 s reached`）：
  1. boot `0x4bce371e`（镜像 `56d9ad08…`、内核 `203c5c9d…`、固件 `6070871b…`、payload `e829d961…`）：GPU fence `seq=1`、服务 `publish rc=0`、用户态启动、触摸 `HID=12`、显示 `frames>0`、400 s 自动回；`GPROBE exec=0`。日志 `out/p21/persistent-logs/20261101T172951Z/`，冻结于 `rounds/20261101T174801Z-local-build-baseline`。
  2. boot `0x55a9e4ea`（镜像 `8ba834fd…`、内核 `cc8636d0…`、固件 `947c5b31…`、payload `6271f231…`）：`/Developer` union `GDEV stage=5 status=0`，但 launchd 未导入 `/Library` 工厂作业，`GPROBE exec=0`；日志 `20261101T181318Z`，冻结于 `rounds/20261101T183050Z-dev-job-vfs`。
  3. boot `0x3ee5d06f`（镜像 `84dbfc4c…`、内核 `985f9a7e…`、固件 `b835fe34…`、payload `75d24c0b…`）：`/System/Cryptexes` union `GCRYPT stage=5 status=0`；`webbookmarksd` 由 XPC activity 触发、xpcproxy exec 后被 **AMFI launch constraint（enforcing）** 拒绝；日志 `20261101T184447Z`，冻结于 `rounds/20261101T184636Z-cryptex-amfi-constraint`。
- **已确认的硬墙**：launchd 系统域只导入签名缓存搜索路径（不含 `/Library/LaunchDaemons`）；`/System` 作业的 owner 二进制替换被 AMFI launch constraint 以 “Constraint not matched” 拒绝（owner trust cache 不能替代平台身份）；`launchd_unsecure_cache=` 经反汇编确认会完全跳过 `SecStaticCode`/AMFI 缓存校验，禁止启用。详细证据、log 读取方法与 Data overlay 重建步骤见 `docs/P21_GPU_USERSPACE_INTEGRATION_20261102.md`。
- **用户决定**：按上游方式迁移——调研 UniXNU / UniXNU-sheng 的 GPU 用户态接入并以 sheng 适配为主。结论是上游不靠 launchd 作业：内核发布 `IOAcceleratorES` 服务（`MetalPluginName`/`NativeSandboxGrantProcess`），backboardd 的 Metal 发现加载 `/System/Library/Extensions/NativeAdrenoMetal.bundle` 插件，插件在 backboardd 内打开用户客户端；官方 `zzw-native-adreno-sandbox-grant.patch` 负责放行。我们的 `NativeAdrenoABI.h` 与官方 165 行版本完全一致，差距在服务形态、插件、sandbox grant 与 `IOGPUFamily` 前置。
- **本轮代码**：GPU 用户客户端 open 放宽为 root 或 mobile(501) 并记录 `uid`（`GPSVC … uid=`）；探针完成一轮后进入 idle，避免 KeepAlive 反复提交；新增 XNU 补丁 `0011-cheeseburger-developer-daemon-mount.patch`（先 `/Developer`、后改 `/System/Cryptexes`，现作为被拒证据保留）；BootLog 增加 `GCRYPT` 行；seed staging 支持 `P21_SEED_BASE/OUT/PROBE_BUILD` 并写入 developer/cryptex 树。
- **下一步（迁移实现）**：按 `docs/P21_GPU_USERSPACE_INTEGRATION_20261102.md` 第 4 节执行——prelink 加入 `IOGPUFamily` 并按 21H392 审计 `IOAcceleratorES` 基类 ABI；改造 accelerator 服务与属性；纳入 sandbox grant 补丁；构建最小 owner 插件在 backboardd 内跑 P1（Query→BO→Map→Submit→Fence→回读）；再加 `--publish`/`--clients` 与 Metal 前端。不要在 `/Developer`、Cryptexes 路径或新 launchd plist 上继续投入。
- 设备状态：Recovery（ADC 5037，root adb）；本轮三次启动前均写并回读 misc BCB，每次自动回后 API 已消费，下一轮启动前必须重写。证据与镜像在 `out/`（gitignored），源码与文档见本轮 commit。

## 2026-11-02 本机编译及本轮候选（已并入上一节）

- 使用 `/Users/dinnerabc/unixnu-p21/atos-gate/source-tree` 隔离构建，NAS 负责源码和候选证据；`scripts/p21-compile-atos-gate.py` 支持 `P21_LOCAL_ROOT` 与 `P21_CANDIDATE`。本机 build 成功、136.7 秒，回写 NAS 的 stripped 内核 SHA-256 `5b84d06e8475cec30e0d9ad2cd181cddfc6ae81a8e690833533e5fa468ab314a`，unstripped `ec19c052ec040d738c90b3f9373a539239d020f310999519b4230d7c6af60373`，来源 `out/p21/atos-gate/kernel-source-build.json`。本轮独立候选 `out/p21/atos-local-round/`。
- 用户停止了另一 agent；其已经启动的本机编译子进程完成后被复用。保留该 agent 的 launchd cache、脚本及测试未提交改动；不重置或覆盖。`AGENTS.md` 已补本机编译和 NAS 同步规则。
- 近期未提交候选包含 `sgac=1`、`launchd_unsecure_cache=/System/Library/xpc/launchd.plist`，其安全语义和数字解析与原设计不一致，本轮不启用。使用原 boot `dbd33465...` 中提取的同一 Data seed `46d03a5a264af71d0e4851cdafca0ff9f646aeaf42e764eda9aafb9166f366ba` 与 trust bundle `17a5441b5c88c55d06ac2742777cc2e797cbff161fe484bc7ffa6e1f2d19fc2c`，避免变更 seed 身份。此候选仅验证本机构建及启动链，不声称用户态 GPU 提交成功。
- 宿主 GPU 模型通过：`out/p21/gpu-cp/host-tests/20261101T171340Z/`。镜像打包、启动及设备结果待补；不得把 host PASS 作为设备成功。

## 2026-11-01 GPU 用户态提交（最小修正轮，用户态仍未验证）

- 设备 `3b25cc7c` 在 Recovery；最近一次实机镜像 `2d1b4d3e…`、内核 `80474673…`、
  firmware `84fbbe2c…`、payload `067f0b07…`、boot `0x3db45895`，日志
  `out/p21/persistent-logs/20261101T135707Z/`。本轮只构建未启动的内核 `402e9197…`
  （关闭策略 + GPROBE exec 观测 + 文档），未打包/未换 UEFI/Data。
- **已验证**：宿主模型 5 组 PASS（含 fence 等待成功/超时）；Fastboot 接受；内核
  `fence OK seq=1`、`CB GPU service: publish rc=0`；CBHB 含 `zapdone/submit/submitdone/fencedone`；
  快照 70 rounds 到 400 s；`AUTORESET issued=1` 自动回；UI/显示路径仍在
  （DISPLAY stage=10 frames=4 clients=25 SWAP=4）。
- **未验证**：未观察到用户态 GPU 服务连接（`GPSVC open=0`、`GPROBE exec=0`）；
  `markers=0`/`exec=0` 不能推断进程未启动，只能说明没有成功的 exec/marker。像素、Metal、
  GPU 合成均未验证。不能宣称 P1 完成。
- **launchd 进展（boot `0x3f3e3c8f`，镜像 `dbd33465…`、内核 `a39b12a3…`）**：官方
  `bindfs + MNT_UNION` 挂载成功（`daem=5/0`、`flags=0x4101029`、原/自有 plist 双 lookup
  均 0）；`GADDON3 data=1f5/1f5/81a4 mnt=0/0/81a4 orig=0/0/81a4 boot=1` 证明挂载后
  `BINDM_FORCE_ROOT` 报告 root:wheel，Data 原始 501:501 未改。`LimitLoadToSessionType=System`
  已加入，仍 `GPROBE exec=0`。所有权不是原因；剩余是 launchd 枚举/plist 接受/加载时机。
- **本轮修正（Kernel 402e9197，仅构建）**：`CheeseburgerGpuBoPolicy.h` 统一
  clientClose/free/BoFree 的“引擎运行或 unmap 失败→保留”规则，`ClientGate` 用 generation
  防旧连接清零新连接；宿主 `gpu_bo_policy_model` 覆盖关闭交错/unmap 失败；`GPROBE`
  记录成功 exec 计数。`CheeseburgerGpuWait.h` 统一 ns 预算（IODelay(50)=50000 ns）；服务单客户端、
  IOVA 重叠拒绝、页表更新串行、BoFree 需 engine idle + SMMU unmap/TLB 成功；CPU map options
  记录在 `GPSV2`；内核 `GJOBS` 只读探测 launchd/Extensions 路径；用户态探针每个阶段写
  `/dev/console` 并先发 marker open（不依赖 SetMetadata）。
- **关键纠正**：`GJOBS` 证明 launchd job 目录是 `/System/Library/LaunchDaemons`
  （`ld=0 ldb=0`，`com.apple.backboardd.plist` 存在），SystemOS cryptex 路径不存在
  （`cx=2 cxb=2`）。之前的 ENOENT 是非 union 挂载盖住下层目录的可见性结果；本机 bindfs
  是普通 bind（无 MNT_UNION）、文件挂载 ENOTDIR，launchd 仍用原 job。最小差异应查官方
  bindfs union 支持与挂载时机，不要重复换路径/文件影子挂载。
- **Data**：为回到已验证 10 GiB seed（`b41a46e4…`），本轮发生一次 reseed
  （`DATA state=3`、writes=150585、loaded=2691），快照仍完整；此后保持该 seed。
- 显示/触摸/crash 仍是独立问题：TOUCH stage=4 error=0 但 HID=0（用户报告无响应），
  PosterBoard crash 循环仍在；不要归因 GPU，也不要为 GPU 测试关闭日志/触摸。
- 详细证据与差异清单：`docs/P21_GPU_USER_SUBMIT_20261101.md`、
  `docs/P21_OFFICIAL_MIGRATION_DIFF_20261101.md`。

## 2026-11-01 GPU 当前状态（CP + GPMU + fence 已实机验证）

- 设备 `3b25cc7c` 当前在 Recovery；最近一轮 fence 镜像 `3af1fd5b…`、内核 `10223927…`、firmware `80f81754…`，日志 `out/p21/persistent-logs/20261101T102647Z/`，boot `0x3d6b6baf`。`fence OK seq=1`、`post-auth trust=0 rptr=9->19 wptr=19`、`GCP13 rw=19/19`、ZAP rc=0、GPMU ready=babeface；400 s 自动返回（`AUTORESET issued=1`）。
- **已实机证明**：`gcc_bimc_gfx_clk` 直接使能是 SMMU/ATOS/CP 的决定性前提；低地址 SMMU 池被真实使用（`lt=3 ht=0`）；ATOS `rc=0`；CP kick `rc=0`、RPTR 前进；GPMU 上传 ready；ZAP/PAS auth 全 0；CPU 读到 GPU 写入的 fence 序列号 1。
- **未证明**：GPU 用户客户端、IOSurface 资源生命周期、渲染任务、Metal、合成器和 UI 流畅度；最近一轮 `DISPLAY frames=0` 属于已知间歇，与 GPU fence 无关。`CP_AHB_FAULT=900009c`（ME write）仍存在但未阻断 fence；SID2 secure stream 仍 `0x200ff` FAULT。
- 详细证据、边界与下一步执行表见 **`docs/P21_GPU_CP_GPMU_FENCE_20261101.md`**。下一步 P1：移植 sagit `QcomGPU` 的提交/后端，建立持久 GPU 服务与最小用户态 batch+fence；之后再 IOSurface/2D、Metal/合成，最后测流畅度。
- 仍在实行的约束：一次构建/一次启动、gate、400 s 自动回、10 GiB Data、P8 静默控制台；不写 TrustControl、不做 SID0 bypass、不关闭 ZAP/PAS；boot args <约 700 B。

## 2026-11-01：低地址 SMMU 表池（P4，进行中）

- UEFI `DarwinBoot` 在 ExitBootServices 前用 `AllocatePages(AllocateMaxAddress, EfiReservedMemoryType, 16 pages, ...)` 预留 64 KiB 低池，把 PA 作为 `cheeseburger-smmu-pool` 追加到 kernel command line；XNU `CheeseburgerGpuSmmu` 优先用该池建 root/leaf 表，失败才回退原有分配。BootLog 新增 `SMPOOL pa=... bytes=... used=...`。UEFI 修改在 `out/p21/atos-gate/uefi-src/darwinboot-smmu-pool.patch`，VM overlay 源已同步。
- **第一次尝试的参数超长**：payload 651 B 加上池的 PA+size 两个参数约 722 B，超过项目已知的约 700 B 截断线。boot `0x3eff380a` 的持久日志只到 55 s（2 rounds，无 heartbeat），但用户确认系统并未停止，蓝字到约 450 s 才手动重启；`AUTORESET requested=400 elapsed=53 issued=0`。日志/auto-reset 被 panic/console 锁住，不是 400 s 参数没写。屏幕出现 `unaligned kernel data abort`；日志没有 panic 文本（panic screen 只画屏），且没有 `SMPOOL`/`CB GPU` 行，低池代码尚未被证明执行。**不能据此判断低池成功或失败。**
- 修正：池固定 64 KiB，只追加一个 `cheeseburger-smmu-pool=<pa>`；payload 去掉 `cheeseburger-disp-probe=60`、`cheeseburger-usb-probe=0`，args 599 B，加池参数约 634 B。候选 image `2d178302…`、kernel `c78b34fb…`、firmware `7c8d3d56…`、expanded payload `0a3ca3e1…`，gate `20261101T072331Z` PASS，auto-reset 400。
- 待回收：本轮是否仍出现 `unaligned kernel data abort`、`SMPOOL` 是否出现、ATOS 是否仍 `rc=-2`、CP 是否 kick、400 s 是否自动返回。若 panic 只影响一个核而系统继续，持久日志/auto-reset 仍可能被锁；需要屏幕 panic 文本或单独的 panic-text 持久化路径。
- **panic 已符号化（boot `0x3d426cf6`，用户提供屏幕文本）**：`pc=_bzero+68` 是 `dc zva, x3`；`lr=cheeseburger_gpu_smmu_setup+972` 是 `bzero(gRootVA, 4096)`；`x20=cb_smmu`，`x0=0xfffffff78d715000` 是池映射 VA。根因是新低池用 `IODeviceMemory` 默认映射成 Device/uncached 内存，`dc zva` 在 Device 内存上触发 unaligned data abort；不是旧的随机内核问题。
- 该系统在 panic 后仍继续运行，且**用户确认 400 s 自动重启成功**；日志写入停在 panic，但 auto-reset 路径存活。400 s 计时本身有效。
- 修复：低池改用 `IOMemoryDescriptor::withPhysicalAddress(...)+map(kIOMapAnywhere|kIOMapCopybackCache)`（普通 RAM copyback），保留 `flush_dcache`；内核重编中。UEFI 的单参数、64 KiB 池和 args 634 B 保持不变。
- **copyback 池实机结果（boot `0x3f3758ae`，70 rounds）**：`SMPOOL pa=fff40000 bytes=65536 used=12288`，`SMICRO lt=3 ht=0`，root=`fff40000`；**低池完全生效，panic 消失**。但 `GCP14 ATOS rc=-2 par=ffffffffffffffff expect=10a250000 atsr=1 fsr=400`，门禁 stage=6，未 kick。**结论：高地址页表不是 ATOS 超时的根因**；低池只是排除了一个假设。
- `AUTORESET requested=400 elapsed=53 issued=1 rc=0`：**400 s 自动返回已由日志确认**，本轮系统正常、Data/touch 正常。
- 下一诊断轮：显式 `cheeseburger-gpu-atos-gate=0`（默认仍为 1），保留 ZAP/PAS、低池、失败清理，测试 ATOS 超时下 CP 是否仍能取 ring；候选 kernel `03f22cdb…`、image `10e77641…`、args 628（加池参数约 663 B）。若 CP kick 后 RPTR 前进，说明 ATOS 探针本身不可用；若仍 -4，则转向低地址 GPU buffer/NoC/XPU。
- **CP 诊断结果（boot `0x3ec9e90e`，70 rounds）**：低池生效（`SMPOOL pa=fff40000 used=12288`、`lt=3 ht=0`），ATOS 仍 `rc=-2`；跳过 ATOS 门禁后 CP 仍 `GCP5 stage=3 rc=-4`、`GCP12 rptr=0`、`GCP7 st=1 me=404 vbif=e0000`、`GCP11 rbccu=778001/1 sp=778001/1`、`GCP6 trust=1 cpcnl=0`。结论：**高页表地址和 ATOS 门禁都不是 CP 不取 ring 的根因**；系统正常、`AUTORESET issued=1`。
- **突破（boot `0x3e8f27de`）**：`GPU0 bimc2=80000000>00000001`——`gcc_bimc_gfx_clk`（GCC 0x46040）原本是 **halted**，UEFI ClockDxe 只加名字并未真正开启；内核直接写 CBCR bit0 后读回 1。同轮 `GCP14 ATOS rc=0 par=0x400000010a251b80`（页 `0x10a251000` 与期望一致），`GCP12 rptr=9`、`GCP5 r=9 w=9`：**CP 首次真实读取 ring（0→9）**，SMMU 翻译也成功。`GCP5 stage=3 rc=-4` 是 kick 完成条件/等待超时，CP 已消费 ring；不是没有取指。
- **kick 成功（boot `0x3df0a447`）**：`GCP5 stage=3 rc=0 r=9 w=9 st=80000101`，`ZAP0 rc=0`（init/setup/auth 全 0），`post-auth trust=1 rptr=9->9 wptr=9 me=0 vbif=f0000`，`GCP12 rptr=9`，`GCP9 pte=6000010a24fe43`。kick 判据修复后 CP 已正常完成 ring 消费，不再 abandon。
- **GPMU 上传成功（boot `0x3ce1812b`）**：按 sagit 顺序在 ME_INIT 后、ZAP 前增加 GPMU 上传；`CB GPU start: gpmu OK ready=babeface err=0 rptr=11`，`GCP12 rptr=11`、`GCP13 rw=11/11`，`GCP5 stage=4 rc=0`，ZAP `rc=0`，trust=1。**CP + GPMU 已实际工作。**
- **首次 CPU 验证 GPU DMA（boot `0x3d6b6baf`）**：`CB GPU start: fence OK seq=1`；`post-auth trust=0 rptr=9->19 wptr=19`，`GCP13 rw=19/19`，ZAP `rc=0`，GPMU `ready=babeface`。fence 页由 CP 写入、CPU flush 后回读为 1，证明 CP 执行了 `CP_SET_SECURE_MODE(0)`/`CP_CACHE_FLUSH_TS` 并经 SMMU 完成 DMA，而不是只前进 RPTR。
- `GCP6 cphb=900009c` 的 CP AHB fault 仍存在，但未阻断 fence；下一步在用户态 GPU/IOSurface/Metal 接入时继续观察，不能因为 fence 成功就宣称渲染可用。
- 仍有 `GCP6 cphb=9024da8`（ME write）但未阻断 GPMUReady；它出现在 GPMU/ZAP 之后，下一步做最小 `CACHE_FLUSH_TS` fence 并由 CPU 回读，同时继续观察 AHB fault。
- 新问题：`GCP6 cphb=900203c`、`cp faults 0/0/900203c`，`GPMU ready=0`；`GCP11/GCP13` 显示 SID2 `smr=0 s2cr=200ff`（secure stream FAULT）。下一步先解码 `CP_AHB_FAULT` 并核对 SID2 secure context/GPMU 上传，不要把这轮当成 GPMU 或 Metal 成功。
- **kick 误判修复**：设备上 `RPTR=9=WPTR`、`Status=0x80000101`，旧条件 `!(Status & ~1)` 只允许 bit0，把已完成的 CP halt/abandon；改为与 reset 一致的 `idleStatus(Status)`（允许 0x800001c1 的 idle 位）并保留 Status3 bit24 检查。宿主模型新增 `Status=0x80000101` 成功用例，全部 PASS（`out/p21/gpu-cp/host-tests/20261101T092004Z`）。诊断用 `cheeseburger-gpu-atos-gate=0` 已从配置移除，ATOS 默认门禁恢复；BIMC 时钟直接使能保留。
- 下一步：保留 `gcc_bimc_gfx_clk` 直接使能；把 `cheeseburger-gpu-atos-gate` 恢复默认 1（ATOS 已通过，不需要诊断旁路）；修正 kick 的完成判据（RPTR==WPTR 与 CP status/IRQ 的关系），再进入 GPMU/fence。低池保留，当前证据表明时钟是决定性变量，低池至少不阻碍 ATOS。
- 新的具体线索：DTS `arm,smmu-kgsl@5040000` 的 clocks 为 `gcc_gpu_cfg_ahb_clk`、`gcc_bimc_gfx_clk`、`gcc_gpu_bimc_gfx_clk`，但 UEFI `UniXNUGpuClock.c` 的 `mGpuClocks[]` 只使能第一个和第三个，**缺 `gcc_bimc_gfx_clk`**。该 BIMC GFX 时钟很可能就是 SMMU/CP 读写内存卡住的原因。已在 UEFI 源（`out/p21/atos-gate/uefi-src/UniXNUGpuClock.c.final`）加入该名称并重建 firmware `57e20a9b…`、image `2f62fe35…`；本轮正在启动验证。若仍无 ATOS/CP，下一步是低地址 GPU buffers/NoC/XPU，而不是继续调页表。
- 结论纪律：日志停在 55 s 不等于内核停；用户观察到的 450 s 运行是权威现场。低池结果以本轮 `SMPOOL`/ATOS 行为准，不能拿上一轮推断。

## 2026-11-01：10 GiB Data 生效 + 屏幕日志根因（P1）

- 镜像 `d107ac79…`（kernel `afb7d180…`、firmware `5cad6653…`、expanded payload `609c3007…`、args 635）启动一次；日志 `out/p21/persistent-logs/20261101T061144Z/`，boot `0x51875b27`，82 rounds，0 写入错误，uptime 408 s。
- **返回方式为用户手动回 Recovery**：`AUTORESET requested=600 elapsed=69 issued=0`；不能把本轮当作自动返回验证。
- 10 GiB Data 生效：`DATA pages=2621440 first=11261615 state=3 err=0`；ENOSPC 计数 0。
- 触摸恢复：`TOUCH stage=4 error=0 i2c=21408/0 publish/sink/send/rep=31`，用户确认可用；三次读失败永久退出的代码缺陷仍未修，见 P2。
- 显示：`frames=7`、`SWAP transfer=7`；PosterBoard 崩溃循环 36 次、`canvas-width/height` sandbox deny 仍在。ENOSPC 消失后崩溃循环仍在，说明不是 Data 写满导致的。
- **屏幕日志根因**：板级 P8 framebuffer console（`patches/xnu/0003-cheeseburger-visible-early-console.patch` + `0004-cheeseburger-console-handoff.patch`）在 `serial_console.c` 的 `p8_console_char()` 中把每个 kprintf 直接写进 1080×1920 GOP 扫描缓冲，回绕时整屏清黑；它不受 `-v/debug` 控制，与 iOS UI 共用同一扫描缓冲，所以日志和 UI 不能叠加。不是 iOS 显示栈的单线程限制。
- 新增 `patches/xnu/0008-cheeseburger-fb-console-switch.patch` 与稳定系列 `out/p21/atos-gate/patches/xnu/series`：`p8-fb-console=0` 时只写 `p8_console_ring`，不画屏；日志仍进持久日志窗口和 CBHB。`scripts/p21-compile-atos-gate.py` 已把该系列加入构建；auto-reset 恢复 400 s。该内核正在重编，尚未启动。
- 工具修正：monitor 用 Python 3.9 解码失败；`read-cheeseburger-log.py` 已固定优先用 `/opt/homebrew/bin/python3.13` 调 decoder。
- P8 开关候选 `3373cf40…` / kernel `0b6fa20b…`（args 651，`p8-fb-console=0`，auto-reset 400）已启动一次：boot `0x4338e6da`，43 rounds，心跳最后 sec=208；用户确认已进入系统并**手动重启**。`AUTORESET requested=400 elapsed=56 issued=0`，所以本轮**仍未验证 400 s 自动返回**；后续保持 400 s，下一次不手动干预。
- P8 轮数据：`DATA state=2 pages=2621440 first=11261615 err=0`（10 GiB 持久 Data 已续用）；`TOUCH stage=4 error=-7 i2c=11202/2 publish/sink/send/rep=7`；`DISPLAY frames=3 clients=55`；ENOSPC 0；PosterBoard corpse 29。触摸与 UI 均可用，P8 屏幕日志是否完全消失以用户屏幕观察为准（日志侧 `[visible-console-tail]` 仍采集）。
- 方案：当前推荐关闭屏幕绘制、保留内存/持久日志；真正“日志叠加 UI”需要显示后端合成器或 MDP5 独立平面，当前阶段不做。

## 2026-11-01 ATOS 门禁候选（已回收）

- 设备已返回 Recovery；日志 `out/p21/persistent-logs/20261101T052640Z/`，boot `0x55357fdd`，66 rounds，0 写入错误，`AUTORESET issued=1 rc=0`。上一轮为 fb-qup3，boot `0x52ba5833`。
- GPU：`GCP5 stage=6 rc=-2`、`GCP14 par=ffffffffffffffff rc=-2 expect=10a5e7000`，未执行 reset/kick（ME/VBIF 记录为 deadbeef）。门禁按预期阻止了 ATOS 失败后的 kick；**ATOS 仍超时，GPU 未前进**。
- 非 GPU：Data 卷 1 GiB 已接近写满（present=257915/262144，free≈5.4k blocks，reserved=5189），本轮控制台尾部大量 ENOSPC/cloudd；PosterBoard/壁纸扩展崩溃循环 17 次（上一轮 30 次、10-31 各轮 27–29 次，**均非本轮引入**）。sandbox 拒绝 `IOPlatformDevice` canvas-width/height、`NativeLegacyDisplay` GainMapVersion/IOMFBIntDcpUsedForExtWhenLidClose，与上一轮同型。显示本轮 frames=6/SWAP=6（上一轮 0），但 `-v debug=0x8 -enable_kprintf_spam` 的内核控制台持续覆盖帧缓冲，UI 被日志刷新。
- 触摸回归：本轮 `TOUCH stage=UINT64_MAX error=-7 frames=3919 publish=0 sink=0`；上一轮 `stage=4 publish=118 sink=118`。退出时 `TOUCH7 post qup0=0 qup400=0 qup408=0 qupc=0`，即 QUP5 寄存器整体读 0，不是单次 NACK；worker 连续 3 次读失败即退出且不恢复。尚未复跑同镜像，不能归因于门禁改动；下一次先修读取失败恢复并加 QUP/GCC 掉电诊断。
- 构建审计：`native/kernel.macho` 93,025,552 B（LINKEDIT 7,664,912）与自检产物 89,182,960 B（LINKEDIT 3,822,320）不同；已确认 native 与构建树 `mach.development.bcm2837.unstripped` 完全相同（`8f6eed2c…`），差异只是复制了未 strip 的符号表；`__TEXT_EXEC` 8,454,144 B 字节与 stripped 产物完全一致（sha256 `4b7783ff…`）。`scripts/p21-check-atos-gate.py` 已补：BoardRuntime 仓库/构建树逐文件同步、native 与编译产物 executable/data 段相等、gate 内核/镜像 pin；重跑 PASS，native `8f6eed2c…`、prelink `c6c24e0e…`、final `afb7d180…`。

# UniXNU-cheeseburger · iPadOS 17.7.11 交接

## 2026-11-01：ATOS 门禁源码修复

- round 15 ATOS 超时为 -2，但旧 CP 门禁仅拦截 <=-3，因此超时后仍然 kick；此前声称超时阻止 kick 是错误的。修复为仅在 ATOS 返回 0、PAR 非 fault 且转换页与 ring PA 一致时允许 CP 启动；其余记录 stage=6 并停止。这个修复防止已知无效转换继续 kick，**不能使 ATOS 本身成功**。
- 宿主模型（匹配、超时、fault、错误页、不对齐）和既有四套模型通过，证据 `out/p21/gpu-cp/host-tests/20261101T044216Z/`。尚未构建内核或实机测试；候选树必须同步新增 `CheeseburgerAtosGate.h`。UEFI 低地址池与本轮源码修复是不同工作，尚未实现。

**GPU 推进实施入口：`docs/P21_ADRENO540_EXECUTION_PLAN.md`；合并 SMMU 修正、心跳修复和 CP 初始化核对后一次构建/实测，目标是首个真实 GPU 任务。**

**第二次 GPU 审计勘误：D 轮后续 tick 属于旧 boot，不能证明 D 仍活跃；详情与决策见 `docs/P21_GPU_CP_SECOND_REVIEW_20261031.md`。**



**ZAP/PAS 认证链路（2026-11-01）**：ZAP loader 已实现并冻结（`zap-init-setup`、`zap-auth`）。轮 1 `0x79802690` 证明 PAS init/setup 接受 40 位物理区；轮 2 `0x523473ba`（镜像 `830a3f29…`、内核 `938e8a7f…`）**auth 原始 x0=0**，但 `TrustControl 1->1`（sagit 允许 trust=1 且要求 authenticatedZAP，不能单独当失败）。轮 3 `0x51f37561`（镜像 `58cd0feb…`）：RBCCU/SP 核心域原本 **status bit20=0（未上电）**，按 `a5xx_pm_resume` 写 0xA887/0xA881=0x778000 后为 `0x140000`；CP 仍 `RPTR=0`。轮 4 `0x5358a52f`（镜像 `ac371b8a…`）：改用 vendor ring `0x20C` + `CP_RB_RPTR_ADDR` scratch、doorbell 重发，live `MEControl=0x303`、`Status=c00003c1`（AHB busy）、scratch=0、无 fault。轮 5 `0x53a7c4e1`（镜像 `a25d8223…`）：SMMU micro-MMU 原值 `0x178`（bit2 LOCAL_HALT_REQ=0），未处于 halt，恢复假设被否定。审计复核后：micro-MMU 超时/无效读回会停止该次 GPU 初始化（`cb_smmu[0]=10`、`cb_smmu[19]<0`，CP kick 前门禁），宿主 10/10；scratch 读取改为 `dsb`+按 VA flush(clean+invalidate)+`dsb`+`dmb`，并同轮记录寄存器 RPTR/WPTR、scratch、fence PTE、nsGFSR/nsGFSYNR、secure GFSYNR、CB0 FSYNR 与 boot ID。下一候选（round 6）一次构建/启动验证。**round 6**（boot `0x51da9190`、镜像 `0e98003f…`）进入 `cfg/addr/kick` 后 `fail`，失败清理的 `dsb sy` 等在停住的 GPU AHB 上，UFS/console/心跳一起停。**round 7**（boot `0x5282ff91`、镜像 `7aef58eb…`）改为 posted-write 清理且失败后不读 GPU MMIO，得到首个明确失败点：`kick FAILED rc=-4 r=0 w=9 irq=0 cpirq=0 st=c00003c1 st3=0 me=404 vbif=e0000`（**kick 超时、无 CP 错误中断**），66 rounds 自动回。**round 8**（镜像 `d62f67ee…`）在失败路径加入 scratch flush+读取及未在本机验证可访问的 SMMU NS/secure fault 寄存器读取；临时启动一次后 900 s 无 ADB/Fastboot/USB，日志未回收，**需手动回 Recovery**。已移除这些未验证读取，只保留已安全读过的 CB0/S2CR/0x48/0x8058 与 CB0 FSYNR。
**round 9/9b（离线候选，未启动）**：按 vendor 与 sagit 的实际顺序改为 **core power → halt/SW reset → CP_ME_INIT engine start → ZAP PAS auth**（认证失败 `engine.abandon()`；用户提交前仍需 CP_SET_SECURE_MODE），并新增 **SMMU 硬件 ATOS**（CB0 ATS1PR 0x8800/ATSR 0x88f0/PAR 0x8050/0x8054）记录 ring/pm4 的真实 walk 结果；失败路径补 CPU scratch 与安全 SMMU 读。候选：镜像 `eb373f2e…`、内核 `e201368e…`、payload `ba550d40…`、固件 `1197219e…`，gate `20261031T183408Z` PASS，已冻结 `rounds/20261031T1834…-zap-order-atos`（无启动证据）。
**round 9c/10/11（实机）**：no-halt ATOS 超时（`ATSR` ACTIVE 卡住、FSR=0x400 无 fault）；按 vendor hard 路径重排后仍 ACTIVE，halt 无法 drain。round 10/11 的失败路径不再读 SMMU/GPU 寄存器，系统均 66 rounds 自动回。**根因（离线对照 vendor）**：SMMU ID2 的 IAS 在 bits 0..3、OAS 在 bits 4..7；本机 ID2=`0x7511` → **IAS=OAS=36**。此前端口误把 bits 8..11（UBS）当输入位宽（48→截 40），按 40 位 IA、T0SZ=24、4 级表配置，而硬件只支持 36 位 IA；正确为 **T0SZ=28、从 level 1 起表（3 级）**。已修 `CheeseburgerGpuSmmu.cpp` 的 IAS/OAS 解析、TCR、mapPage/软件 walker/mapTest 起始层级，并让 ATOS 始终先请求 halt；宿主 walker 增加 36 位/level-1 用例并通过。**round 12（boot `0x524f30d0`、镜像 `2ec35389…`）**：IAS/OAS 修正生效（`tcr=80201c`、level 1/3 级表），但 ATOS 仍 `rc=-2`（ATSR ACTIVE、FSR=0x400 无 fault），kick 仍 `-4`；66 rounds 自动回。**round 13（boot `0x52051ba6`、镜像 `688606ee…`）**：`GPU6 hlos1=00000001/80000000 hlos2=00000001/80000000`，即 GCC 0x7d000/0x7e000 时钟 bit0=1、0x7d024/0x7e024 GDSC bit31=1（PWR_ON）——**HLOS SMMU 电源域/时钟为 ON**，排除电源假设；ATOS 仍 ACTIVE。**round 14（镜像 `88c93af8…`、内核 `7a506feb…`）**：SID0 bypass 诊断（`smmu=2`，保留 ZAP/PAS 与 SID2），启动一次后 900 s 无自动返回；日志已回收（窗口 `20261101T040320Z`，boot `0x528dbd09`，8 rounds）：`SID0 bypass s2cr=20000` —— 写 `0x10000`（BYPASS）回读为 `0x20000`（**FAULT**），bypass 被硬件拒绝；心跳停在 `cfg/addr`（无 kick），控制台最后为 102 s 的 reset 行，停点在 addr 与 kick 之间（含解停写）。**不能据此声称 CP 执行**；SID0 bypass 在本机不可用，代码已移除。候选冻结 `rounds/20261101T040428Z-smmu-bypass-fault`。**round 15（boot `0x52ba5833`、镜像 `480a499d…`、内核 `66e0c6b8…`）**：按 vendor 修 ID2 位宽边界（保留码报错）、TTBR0/ATS1PR 单次 64 位写、ATOS 清 FSR 前记录现场、低地址页表优先分配。实机 `SMICRO lt=0 ht=3`（本机 VM 无 <4 GiB 页，已校验并回退）；`GCP15 fsrb=400 ttbr=2099b000/1 tcr=80201c root=12099b000`；ATOS 仍 `rc=-2 atsr=1 fsr=400`，CP `kick -4 r=0 w=9 scratch=0`；66 rounds 自动回。**下一步**：UEFI 侧用 `AllocatePages(AllocateMaxAddress, EfiReservedMemoryType, ...)` 预留低 4 GiB 的 SMMU 表池，经 `/chosen`（sheng `[entry.chosen]` 注入）交给 XNU，只替换页表物理位置；若仍不可得，再核对 NoC/XPU 对 SMMU master 的高地址权限。不再尝试 SID0 bypass/trust-clear。**唯一阻塞**：设备不可达，需要手动回 Recovery（长按电源+音量下；黑屏/关机先长按电源 10 s）。返回后先读 round 14 窗口：若存在新 boot ID 且 `RPTR` 前进/CP 活动，则证明 SMMU 翻译是阻塞点，再分析随后的停住并做正式映射修复；若窗口无记录，则 bypass 本身导致停住。设备返回并回收日志前不再启动。
**完整现状/证据/决策选项见 `docs/P21_GPU_CP_FULL_REPORT_20261031.md`（2026-10-31，供审计决定）。**
GPU 只读审计见 `docs/P21_ADRENO540_REVIEW_20261031.md`；依据审计确定的最新修改方向见 `docs/P21_GPU_CP_DIRECTION_20261031.md`（P0 证据冻结 → P1 reset 失败路径 → P2 独立存活判据 → P3 SMMU/CP 验证 → P4 分阶段）。当前候选门禁及 pins 为旧轮记录，不能证明最新镜像通过。

下一步首帧前黑屏排查流程见 `docs/P21_BLACK_SCREEN_NEXT_STEPS.md`；其中返回码勘误覆盖下方旧分析。

**当前 GPU/CP 阶段的完整现状、问题与解决记录、审计清单见 `docs/P21_GPU_CP_STATUS_REPORT.md`（2026-10-31，供另一 agent 审计）。**
**P0 证据冻结已完成（2026-10-31，离线）**：门禁现在写 `gate-<UTC>.json` 并校验 packaging 指向本候选镜像；新增 `scripts/p21-freeze-round.py`；最新轮产物冻结在 `out/p21/gpu-cp/rounds/20261031T114334Z-cp-early2/`（含镜像/内核/固件/payload/ADT/配置副本与 gate/pins/boot/monitor/retrieval 引用），5 个历史轮次以哈希补录，索引 `out/p21/gpu-cp/rounds/INDEX.md`。旧 `offline-gate.json`/`pins.json` 已标记失效。
**P1 reset 失败路径已收口（2026-10-31，离线 host-verified）**：`CheeseburgerGpuStart.h` 模板化 reset/configure/start；所有失败保持 CP/VBIF halt、标记 abandoned、不再 unhalt/kick（kick 超时也会重新 halt）；host 模型 8/8 通过（`out/p21/gpu-cp/host-tests/20261031T115416Z/`），内核用新模板重编通过（native `fb78e5c4…`，未打包/未启动）。
**P2 可观察性与控制 A/B 已完成（2026-10-31）**：CBHB 心跳环（日志窗口最后 31 块、独立线程每 10 s 直写、不经 console/msgbuf；auto-reset 写 `mark`/`smc`；CP reset 每步写 `halt/ack/idle/swrst/cm3/trust/cfg/addr/kick/done/fail`），读取端输出 `heartbeat.json`，codec 8/8。三轮实机：A 短行无环 `0x52b799d2`、B 短行+环 `0x512997ee`、C 长行+环 `0x511b5457`，全部 66 rounds 自动复位；B/C 心跳同型（tick 28–308、mark 329、smc 333）→ **行长、五映射、console、8 s 睡眠都不触发停住**；此前中断更可能发生在 full-start 的 reset 写入阶段。三轮已冻结。下一步 D：full-start + 每步心跳定位具体卡点。
**D 轮（full-start + 逐步心跳）结果**：boot `0x51e9e4e1`（镜像 `27d3bea3…`、内核 `520659f6…`），心跳依次 `halt/ack/idle/swrst/cm3` 后**没有 `trust`**；tick 继续到 ~370 s、无 `mark/smc`，用户手动返回。**卡点在 `RBBM_SECVID_TRUST_CNTL` 写/回读**（强制退出 secure 模式），内核/UFS 仍活、console 路径受阻。已把 secure 清除改为默认关闭（显式 `cheeseburger-gpu-trust-clear=1` 才写），并拆分 `trustw/trustwr/trust` 标记；host 模型 9/9。下一步 E：不写 TRUST_CNTL 的 full-start，看 `ready/cfg/addr/kick` 与 `RBRead`。
**E 轮（full-start 且不写 TRUST_CNTL）结果**：boot `0x50edef20`（镜像 `8e159f8d…`、内核 `8f5453bb…`），**67 rounds、自动复位、无黑屏**；`GCP5 rc=-4 r=0 w=9`、`GCP6 trust=1`、CP/SMMU fault 均 0。证明 **D 的卡死由 `RBBM_SECVID_TRUST_CNTL` 写/回读造成**，去掉后 full-start 安全；secure 模式未清，CP 仍不取 ring。下一步：安全路径下补 GCP8/GCP9 只读回读并核对 ZAP/PAS 前提，再评估 SCTLR/secure 影响。
**F 轮（full-start + GCP8/GCP9 只读回读）结果**：boot `0x5121fa54`（镜像 `c7a64a40…`、内核 `ac8f24fe…`），66 rounds 自动复位无黑屏。`GCP8` 证明 ME/PFP/RB 基址、RB_CNTL=800020c、CP_ADDR_MODE=1、CP_PROTECT_CNTL=7 正确；`GCP9` SCTLR=1027/TCR=802018/SMR=80000000/TTBR0 低字=201e9000 正确，但 **TTBR0 高字回读 1（期望 0）**、S2CR 打印 `fffffff700000000`（疑似 64-bit 打印伪影），PTE 查询少下潜一级（源码已修）。CP 仍 r=0、trust=1、无 fault；下一步：分高低字重测 TTBR/S2CR 与真实叶子 PTE，并评估 ZAP/PAS 前提。
最近一轮（镜像 `aea9a9fa…`、内核 `6ccfb681…`，GPU 工作提前到 ~75–99 s）日志 `20261031T112909Z`（boot `0x510ea570`，8 rounds）：
五 buffer 映射全通；`begin` 打印（98.7 s）后日志中断，从代码顺序与已持久证据看未观察到 reset 写入（不能证明其后未执行）；**用户已确认是手动返回 Recovery，400 s 自动复位未生效**。
按用户要求已暂停，不继续构建/启动。

## 2026-10-31：CP/GPMU 状态读取 + 五个 buffer 全部映射，整机稳定；进入 CP reset/start

- **CP 状态与五映射成功**（镜像 `570365647142d94573d8943f477ebe5913dc9cd72dea3e2d0376f0e3fe4b5df2`，内核 `aeb7b438…`、payload `1641aa56…`、固件 `7db06b9b…`，args 670；日志 `out/p21/persistent-logs/20261031T103113Z/`，boot `0x50b0c185`，67 rounds，0 写入错误，400 s 自动回 Recovery）：
  - `GCP0 map=deadbeff r=5`；ring/PM4/PFP/gpmu(3 页)/fence 全部 `r=0`、`MAPD ret=0 tlb=0`，`GCP3` 五个 GPU VA = `0x2000000/0x2008000/0x200d000/0x2012000/0x2015000`。
  - `GCP2 me=303 vbif=0/f0000 rb=0/0/0/0 st=800001c1 irq=0 trust=1 sec=0`：CP 已 halt；`Status=0x800001c1` 与 sagit `haltedPipelineIdle` 掩码完全一致；TrustControl=1、SECVID base=0。
  - `GCP4 gready=0 gerr=0 cm3rst=1 prot=0 swrst=0 ahb=1`：GPMU 固件未加载、CM3 处于复位，符合冷启动。
  - 同轮显示正常（`DISPLAY clients=129 frames=6`、`SWAP transfer=6`），说明 SMMU 映射探针不再造成整屏黑/停住。
- **上一个“第 4 个映射后停住”的归因修正**：未再复现；本轮差异是 SMMU 寄存器窗口整轮**只映射一次**（此前每个 buffer 映射都新建 IOMemoryMap）并把 CP 状态读取提到映射之前。8 s 落盘间隔仍保留在每步之前，便于再次停住时定位。
- **CP reset/start 第一轮结果**（镜像 `64fdf3d6…`，内核 `e2e767c0…`、payload `1103ba35…`、固件 `b7840be3…`，args 670；日志 `out/p21/persistent-logs/20261031T104613Z/`，boot `0x5121380b`，66 rounds，400 s 自动回 Recovery）：`GCP5 stage=3 rc=-4 vbif=f000f me=303 r=0 w=9 irq=0 cpirq=0 st=c00003c1 st3=100000`。即 reset 与 configure 全部通过（否则 rc 为 -1..-2）、`VBIFHalted&15=0xf`、`CP_ME_INIT` 已写入 ring（WPTR=9），但 **CP 从未推进 RBRead（=0）**，且 `InterruptStatus`/`CPInterruptStatus` 全 0；`Status` 只多出 bit30（GPU_BUSY_IGN_AHB_CP），说明 CP 已唤醒但不取 ring。结论：CP 微码没有执行，不是 reset/configure 失败。
- **下一轮假设与改动（未验证）**：冷状态 `TrustControl=1`（secure 模式），sagit 的提交前提要求 `TrustControl==0`，Linux a5xx 在无 ZAP 时用 `RBBM_SECVID_TRUST_CNTL=0` 手动退出安全模式；同时把 CB0 SCTLR 从 `0x1167` 改为 sagit 实测的 `0x1027`（M|EAE|CFCFG|CFRE，无 CFIE/HUPCF；`0x1167` 会在意外 context fault 时 halt SMMU，可能静默卡住 CP 取指）。并捕获 `CP_HW_FAULT/CP_PROTECT_STATUS/CP_AHB_FAULT` 与 SMMU `CB0_FSR/GFSR`，kick 等待延长到约 3 s。诊断行 `GCP6 trust/cpcnl/cphw/cpprot/cphb/smfsr/smg`。
- 判读：若 `GCP6 trust=0` 且 `GCP5 r=9` → secure 模式是根因；若仍 r=0，则看 `smfsr/smg`（SMMU 取指 fault）或 `cphw/cpprot`（CP 内部 fault）决定是修页表/stream 还是 CP 配置。

- **secure 模式/ SCTLR 轮结果**（镜像 `adb12b45…`，内核 `3b76b685…`，日志 `20261031T110117Z`，boot `0x50993220`，67 rounds，自动回 Recovery）：`GCP5 stage=1 rc=-2 vbif=f000f me=deadbeef`、`GCP6` 全 `deadbeef`——**reset 在 idle 前置检查处失败**（VBIF 停流 ack 已成功），根本没有进入 SW reset / TrustControl 清除 / kick。上一轮同代码在同位置通过，所以这不是代码路径差异，而是 GPU 状态在快照（128 s）与启动（193 s）之间漂移或 VBIF 停流后的瞬时 busy。`GCP2` 快照仍是 `me=303 st=800001c1 trust=1`。
- **下一轮诊断（未验证）**：启动阶段先读 GPUCC `0x1008/0x1094/0x1098`（CX/GX/gfx3d 是否仍在线）写入 `GCP7 cx/gx/gfx`；`GCP7 st/me/vbif/ry` 记录 reset 前 `Status`、`MEControl`、`VBIFHalted` 和 idle 重试次数（最多 20×10 ms，吸收 VBIF drain 的瞬时 busy）。映射间隔缩短（begin 2 s、之后 1 s），让启动阶段在约 155 s 而不是 193 s 执行，减少状态漂移窗口。
- 判读：`cx/gx/gfx` 若为掉电值（bit31 清零或全 1）→ 先解决 XNU 侧 GPU 供电/时钟 vote（UEFI EFIClock 的 vote 在 ExitBootServices 后是否失效）；若供电正常且 `ry>0` 后 idle 通过 → 继续看 kick。若 `st` 仍非 idle 且取值为 `0xffffffff`，说明该寄存器读取时 GPU 不可访问。
- **供电/PMRD 轮结果（整屏黑，手动回）**（镜像 `b15bb427…`，内核 `d771e2ec…`，日志 `20261031T111210Z`，boot `0x5230df48`，18 rounds）：`GCP7 cx=80001fe0 gx=a0222000 gfx=621`（启动时 CX/GX/gfx3d 均在线），`GCP5 stage=1 rc=0`，`GCP6/GCP7 st/me/vbif` 仍 `deadbeef`。控制台 `CB GPU start: begin …` 时间戳 **151.73 s**，最后一条内核消息 **157.33 s**——冻结发生在 begin 之后的 8 s 等待里、**尚未执行任何 GPU 写操作**。上两轮 start 在 ~188 s（PMRD 电源状态切换之后）均稳定；日志显示 ~150–157 s 正是系统自身 **PMRD** 做 `PowerChangeOverride / idle sleep timer` 的窗口。结论：冻结与 PMRD 电源切换窗口相关，而不是某条 GPU 寄存器写。
- **下一轮（未验证）**：`cheeseburger-gpu-probe=8`，让 GPU 探针 + CP 映射 + reset/kick 在约 **75–120 s** 完成，避开 ~150 s 的 PMRD 切换；启动阶段不再额外映射 GPUCC（改用上一拍 `cb_gpu` 里的 GPUCC 快照，`GCP7` 仍可读 `cx/gx/gfx`）。其余（TrustControl=0、SCTLR=0x1027、fault 捕获、idle 重试）保留。
- **提前执行轮结果（已回收，boot `0x510ea570`，日志 `20261031T112909Z`，8 rounds）**：76.7 s SMMU `cb=1027 fsr=400`、CP 冷状态不变；86.7–98.7 s 五 buffer 全 `r=0 tlb=0`；98.725 s 汇总、98.7258 s `CB GPU start: begin`（`cx=80001fe0 gx=a0222000 gfx=621`）。**begin 之后没有任何内核/用户态控制台行，reset 尚未执行（先 8 s 等待）**；writer 约 102 s 写最后一轮后停止。设备最终在约 503 s 被监视器发现于 Recovery，**用户已确认是手动返回，400 s 自动复位未生效**；`misc` BCB 已消费，但手动进 Recovery 也会清空，不能区分。
- **重要修正**：99 s 轮的 PMRD 活动在 69–72 s 已结束、中断不在该窗口，因此 **PMRD 归因可排除**。auto-reset 在 PSCI 前会 `IOLog`，控制台锁卡住同样能阻止复位，所以手动返回不能证明 SoC 完全停住。新相关性（2/2 vs 0/2）：两轮中断的 begin 行都是 6 参数长行，两轮正常走完的是 3 参数短行——下一轮先用短行 + `GCP7` 行验证控制台长行/锁假设，并加“不依赖 console 与 UFS writer 的存活标记”。

## 2026-10-31：A540 时钟清点、400 s 自动回 Recovery、触摸方向修正

- **GPU 时钟清点（UEFI EFIClock，只读）**：镜像 `eaa7eb87…`（内核 `dd0e4ad9…`、payload `24e72e75…`、固件 `00642106…`），日志 `20261031T051621Z`（boot `0x53b3ffcc`）。窗口尾部块 8161 的 `UP1GPU1` 行：`clock=0 ver=10005`；`VDD_GPU_CX/GX`、`VDD_HLOS1/2_GPU_SMMU` 域 ID 全部解析成功；`gcc_gpu_cfg_ahb_clk` 75 MHz 在跑；`gpucc_gfx3d_clk`/`rbcpr`/`rbbmtimer`/`isense` 关闭、19.2 MHz。同轮 XNU GPU 探针仍 `stage=2`（GPUCC 可读、CX/GX 关闭）。
- **400 s 自动复位**：新增 `package/BoardRuntime/CheeseburgerAutoReset.cpp`，启动参数 `cheeseburger-auto-reset=400`，按内核启动时间计时；到点写 `CB auto reset` 标记、等 4 s 让持久日志落盘，再发 PSCI SYSTEM_RESET（SMC64 `0x84000009`）。首次无 BCB 时复位落 Android；写入 misc BCB `command=boot-recovery`（4 KiB 块备份到 `out/p21/misc/misc-before.bin`、只改命令字段、写后回读一致）后，镜像 `416bdf28…` 在 400 s 自动回到 Recovery（日志 `20261031T053946Z`，boot `0x4f697e23`，`AUTORESET issued=1`）。**BCB 会被 Recovery 消费清零，每次启动前要重写标记**；标记写的是 `/dev/block/sda4`（misc）第一个 4 KiB 块。
- **触摸方向修正**：日志证明触摸驱动与 HID 正常（`stage=4`、F01/F12、`TOUCH19 rep=498 nodrv=0`），但面板倒装使 `transform=3` 的双轴翻转与 UI 相反，用户此前误判为“触摸不可用”。已把默认 `transform` 改为 0（保留 `cheeseburger-touch-transform` boot arg 供单轴复查），用户确认上/下/左/右方向正常。修复镜像 `285cd7d8…`（内核 `c2f78d8b…`、payload `59855d79…`、固件 `75b56a83…`，args 569）。
- **GPU 上电实验（已完成两轮）**：
  - 第一轮镜像 `49ee82a2…`（UEFI 上电后直接读 GPUCC）：上电步骤全部成功（`VDD_GPU_CX/GX`、`gpucc_cxo/rbcpr/rbbmtimer/isense`、`gfx3d` 257 MHz、`VDD_HLOS1/2_GPU_SMMU` 均返回 0），但 capture 在 SMMU 域后没有 `G:` 行，固件停住、内核未启动，屏幕黑；用户手动回 Recovery。
  - 第二轮去掉 UEFI 的 GPU/GPUCC MMIO 读取，并临时 `cheeseburger-gpu-probe=0`（不让 XNU 读 GPU），只保留上电与 `P:done`：镜像 `96fc9e20…`（固件 `1d2bb5f2…`、payload `9dc9027c…`、内核仍 `c2f78d8b…`，args 568）。设备正常启动并在 400 s 自动回 Recovery（日志 `20261031T060552Z`，boot `0x4f100452`，`AUTORESET issued=1`），capture 行以 `P:done` 结束。
  - 结论：EFIClock 上电序列与启动兼容；黑屏来自上电后的 GPUCC MMIO 读取。下一步在 UEFI 里先读 GCC AHB、必要时用 EFIClock 重新使能 `gcc_gpu_cfg_ahb_clk`，仅在 AHB 在线时才读 GPUCC/GPU，并保持每步写 capture。
- **UEFI 直接读 GPU 的异常**：在 UEFI 里加 GCC AHB 门控后，`C:gcc_gpu_cfg_ahb_clk=…,On=1` 已写入，但随后的 GCC/GPUCC `MmioRead32` 触发 `Synchronous Exception at 0x00000001B048BE030`（UEFI 自身对这些物理地址的映射不可用），不是上电失败；该读取已从 UEFI 移除。
- **XNU 读取成功（里程碑）**：UEFI 只做 EFIClock 上电（capture 以 `P:done` 结束），XNU GPU 探针用自己的映射读取；镜像 `d11f9305…`（内核 `52099388…`、payload `b859aa5b…`、固件 `91925275…`，args 569），日志 `20261031T062615Z`（boot `0x5048a3db`，400 s 自动回 Recovery）：
  - `GPU0 gcc ahb=20008001 bimc=00000001 misc=00010000 stage=3 flags=f`：AHB/CX/GX/gfx3d 全部在线；
  - GPUCC：`0x1008=80001fe0`（CX GDSC on）、`0x1094=a0222000`（GX on）、`0x1098=00000621`（gfx3d branch on）、rbcpr/rbbmtimer/isense 分支均 on；
  - GPU：`RBBM_ID=0x00010042`、第二个 ID/rev `0xb5f00000`、Status `0x1`、MEControl `0x303`（CP halted）、GPMUReady/Error=0（固件未加载）、RB ring 基址/读写指针=0；
  - GSMMU 按 `cheeseburger-gpu-smmu=0` 跳过（全 `deadbeef`），因为 SMMU 时钟尚未使能。
- **SMMU 读取成功**：UEFI 增加 `gcc_hlos1/2_gpu_smmu_clk` 使能（capture 均 `On=1`），XNU 用 `cheeseburger-gpu-smmu=1` 读取；镜像 `9ebb59f6…`（内核 `52099388…`、payload `67b9d461…`、固件 `64f45908…`，args 593），日志 `20261031T063813Z`（boot `0x4eebb8fb`，自动回 Recovery）：
  - `GPU0 … stage=3 flags=1f`（含 SMMU 读取位）；
  - `GSMMU SCTLR=00200626 ID0=48016403 ID1=20008003 ID2=00007511 SMR0=0 S2CR0=000200ff CB0=0…`：SMMU 可访问但未使能、无 stream/上下文，符合冷启动状态；ID1 的 global pages=8 与 CB0 偏移 0x8000 一致。
- **SMMU 上下文已建立（空根表）**：新增 `package/BoardRuntime/CheeseburgerGpuSmmu.cpp`，按 sagit 非安全 bank0/SID0 序列分配 4 KiB 物理连续、清洗过的空 AArch64 根表，配置 global SCTLR、CB0（0x1167）、TCR 0x802018、SMR0=0x80000000、S2CR0=0，并做两次 CBn TLB 同步。镜像 `a2ddc42f…`（内核 `502516eb…`、payload `2428a813…`、固件 `9d6ab661…`，args 623），日志 `20261031T065735Z`（boot `0x4e2f3574`，自动回 Recovery）：
  - `SSETUP st=0 root=1606cf000 sctlr=201e36 smr=80000000 s2cr=0 cb=1167 tcr=802018 fsr=400`，无 fault；FSR 0x400 是 AArch64 格式位，符合 sagit 对安静 bank 的说明。
  - 该根表尚未映射任何 buffer，GPU 仍无 DMA；CP 仍 halted。
- **叶子页表映射已验证**：在 SMMU 上下文中加一级/二级/三级表，把 4 KiB 测试 buffer 映射到 GPU VA `0x1000000`；每页写后 clean，CBn_TLBIALL+TLBSYNC 通过。镜像 `b2404510…`（内核 `c6653ea2…`、payload `d6f7db2f…`、固件 `2c91e9ac…`，args 623），日志 `20261031T071612Z`（boot `0x4fc90115`）：`SMAP st=0 va=1000000 pa=15fad9000 pte=6000015fad9e43`，root0=`15fb3a003`；自动回 Recovery。
- **综合探针第一版结果**：QGFW 私有头（`scripts/p21-gpu-firmware-header.py`，不提交固件）解析成功：`fw=48312`、PM4 `19648`、PFP `16816`、GPMU `2947` words；ring/PM4/PFP/GPMU/fence 五个物理连续 buffer 均分配成功（`GCP1 pa ring=10a5c7000 pm4=10a5cf000 pfp=10a5d4000 gpmu=10a5d9000`）。但通用 SMMU walker 在第一个 ring 映射返回 `st=5`（`GCP0 map=deadbeef`），而同轮 `SMAP st=0` 的单元映射正常；GCP2 因此未读到 CP/Trust/SECVID 状态。已加 `CB GPU SMMU map fail: page/stage/info` 与每 buffer `CB GPU CP map ... r=` 诊断。
- **黑屏根因（用户澄清 + 对照）**：黑屏是**整屏黑**（连屏幕日志都不显示），但系统在跑、ADB/日志可回收。对照 1（`270928d3…`：UEFI GPU 上电关闭、GPU/SMMU/CP 探针关闭）→ 屏幕恢复并自动回 Recovery。对照 2（`ca8d45f0…`：UEFI 上电/时钟保留，仅跳过 `SetClockFreqHz(gpucc_gfx3d_clk,257MHz)`，XNU GPU 探针开启）→ 屏幕正常，`GPU0 stage=3 flags=f`，67 轮完整运行并自动回 Recovery（日志 `20261031T085221Z`，boot `0x4e20d0e1`）。结论：**整屏黑由 `gfx3d` 频率/PLL 改动造成**，不是 CP 映射或系统卡死；此前 CP 探针轮的黑屏也都是这一步。
- **SMMU setup 安全、CP 探针是黑屏变量**：镜像 `7ee28350…`（rate=0、`smmu=1`、`smmu-setup=1`、`cp=0`）**进入桌面**并自动回 Recovery；而同配置打开 CP 探针的 `29dd9fdc…` 整屏黑。结论：SMMU 上下文/setup 本身安全，整屏黑出现在 CP 探针的 buffer 映射/状态阶段。
- **早期钩子失败**：把 GPU/CP 探针挂到 `cheeseburger_ufs_register`（UFS 注册时启动、5 s 后执行）时，日志只有 **1 轮**，启动在探针前停住；该钩子太早，已回退到 display late probe。
- **CP 多页映射已通过（关键）**：给每个 buffer 映射后加 3 s 落盘延时；镜像 `a4af3b18…`（rate=0、`smmu=1/setup=1`、`cp=1`，内核 `212411c7…`、payload `55332957…`、固件 `b6582421…`），日志 `20261031T095634Z`（boot `0x5090409d`）：
  - `CB GPU CP map: iova=2000000 pages=8 r=0`、`2008000 pages=5 r=0`、`200d000 pages=5 r=0`；
  - `MAPD … ret=0 tlb=0`（ring/pm4/pfp）；此前的 `st=5` 是旧 walker/共享表问题，已消失；
  - 该轮只到 14 轮，用户提前返回，gpmu/fence 映射与 `GCP2` CP 状态尚未执行（`GCP2` 仍 `deadbeef`）。
- **下一步**：同一镜像再跑一次，即使黑屏也等到约 200 s，让 gpmu/fence 映射与 `GCP2`（MEControl/VBIF/RB/Trust/SECVID/GPMU）落盘；然后进入 CP configure/reset/ME_INIT 与 GPMU 0xbabeface。SMMU setup 与多页映射已可保留。
- **升频**：仍保持 `UNIXNU_GPU_RATE=0`；等 CP/GPMU 跑通后再用 root clock 或 sagit PLL 顺序安全升频。
- **下一步**：让 CP 映射使用**全新根表**（CP 探针开启时跳过 mapTest，避免共享 L1/L2 表），用逐级诊断确认 walker 失败点；映射通过后再读取 CP/RBBM/VBIF/Trust/SECVID/GPMU，然后进入 CP configure/reset/ME_INIT 与 GPMU 0xbabeface。

## 2026-10-31：GPU 第 1–2 步——A540 固件/寄存器取证与只读探针（GPU 未上电）

- 按官方 `/zh/porting/gpu` 顺序，先确认 GPU 代际与输入：本机 A540（a5xx，无 A740 式 GMU；CP 固件 + GPMU 由 CP 写入）。同 SoC 参考 `out/p23/metal-review/sagit-source`（pin `898998c`）及其 QcomGPU/GPUCC/SMMU/RPM 源码，已取到 `docs/upstream/reference/sagit/`。
- **固件已取证**（Recovery 内只读）：vendor 分区 `/dev/block/sdf6` 以 `-o ro` 挂载后拉取 `a530_pfp.fw`（16820 B）、`a530_pm4.fw`（19652 B）、`a540_gpmu.fw2`（11736 B）、`a540_zap.mdt/.b00/.b01/.b02/.elf`；随后已卸载。文件与 SHA-256 在 `out/p21/gpu/firmware/` 与 `out/p21/gpu/firmware.sha256`（私有输入，不提交、不随仓库分发）。
- 只读探针（`package/BoardRuntime/CheeseburgerGpuProbe.cpp`，启动参数 `cheeseburger-gpu-probe=60`）：先读 GCC，只有 `GCC_GPU_CFG_AHB_CBCR` 使能且未 halted 才读 GPUCC；只有 GPUCC 报告 GX 上电且 gfx3d 运行时才读 GPU/SMMU；否则填 `0xdeadbeef`。
- 镜像 `d1f6ca23…`（内核 `dd0e4ad9…`、payload `24e72e75…`、固件 `b104d7f1…`，args 510），pins 命中、门禁 PASS；日志 `20261031T050242Z`（boot `0x4e273a84`）：
  - `GPU0 gcc ahb=20008001 bimc=00000001 misc=00010000 stage=2 flags=1 err=0`：AHB/BIMC 在跑；
  - GPUCC：`0x1008=00005ff4`（CX GDSC bit31=0）、`0x1094=00222001`（GX bit31=0）、`0x1098=80000620`、`0x1054/0x10d0/0x1124=80000000`（rbcpr/gfx3d/rbbmtimer/isense 全部 halted）；
  - GPU/SMMU 按设计跳过（全 `deadbeef`）；同轮 Data 以 `state=2` 恢复，`err=0`。
- 结论：GPUCC 总线可访问，但 GPU 电源域与时钟完全未开；UEFI/内核都没有对 GPU 上电。固件 `ClockDxe` 含 `VDD_GPU_CX`、`VDD_GPU_GX`、`VDD_HLOS1/2_GPU_SMMU` 与全部 `gpucc_*`/`gcc_gpu_*` 时钟名，并安装 `EFI_CLOCK_PROTOCOL`（GUID `241AFAE6-885F-4F6C-A7EA-C28EAB79C3E5`，含电源域、复位、频率与按名查找 ID）；本地头文件在 `upstream/uefi-sheng/Silicon/Qualcomm/QcomPkg/Include/Protocol/EFIClock.h`。
- 下一步（尚未执行）：在 UniXNUPkg 里做一个 opt-in 的 UEFI 探针——定位 Clock 协议、按名取 GPU 时钟/电源域 ID，先只读记录状态；确认后再使能 CX/GX 与 gfx3d 并读 GPU RBBM ID/Status。之后才是 SMMU 上下文、CP/GPMU 固件加载与提交路径。

## 2026-10-31：持久 Data 打通（mode1 只读 → mode2 初始化 → 跨重启恢复）

- 背景：此前 Data 只有 payload 内 1 GiB seed 的内存稀疏盘（`owner-data-1g.dbr`），重启即丢；官方 RamBlock 已有 overlay 机制，缺的是 UFS 侧的 `native_ufs_store_*` 回调（本仓库原先只提供 `gpt_check`，其余由 `no-store-provider.c` 返回 0）。
- 实现（`package/BoardRuntime/NativeUfs.cpp`，照官方 ShengUfs `e2b32cc4` 的存储策略）：窗口取 userdata 尾部 `sheng-log-reserve`（默认 1024 MiB）预留区下方，按 seed 页数请求大小；`sheng-data-persist=1` 只读校验，`2` 打开写窗口。写命令仅 WRITE(10)+FUA（`logWrite` 之外的第二个受限窗口）、每次读回校验、SYNCHRONIZE CACHE；提交路径按 CDB 的 LBA/长度再次检查窗口。`native_ufs_store_gpt_check` 仍用既有 GPT/GPT-entry CRC 与 userdata 边界；`submit()` 只放行 log 或 store 两个 active 窗口。BootLog 新增 `DATA persist state/header/gpt/pages/loaded/writes/syncs/err/first/present` 行。
- 构建：`scripts/p21-build-board-runtime.py` 不再编译 `no-store-provider.c`（否则与 NativeUfs 的新符号重复）。
- 窗口几何（4096 字节 LBA）：partition `2441472..14145279`（11703808 块）；log `5062912+8192`；**store `13620983..13883135`（262153 块 = 1073778688 字节，userdata 相对 42.65–43.65 GiB）**；尾部预留 `13883136..14145279`。根卷镜像 9382658048 字节，距 store 起点有约 34 GiB。启动前只读抽样 store 首/中/尾与预留区均为全零。
- **mode 1（只读）**：镜像 `1934533c…`（内核 `3f790395…`、payload `c03dc95c…`、固件 `8f646079…`，args 484），日志 `20261031T042950Z`（boot `0x5331cf57`）：`CB Data store: mode=1 lba=13620983 blocks=262153`、`DATA persist state=1 header=1 gpt=0/0 pages=262144 writes=0 syncs=0 err=0 first=13620983 present=0`，Data 挂载 `result=0`。窗口可读、空白、GPT 未变、零写入。
- **mode 2（初始化）**：同一内核，payload `b6a8ea0a…`、固件 `480533f5…`、镜像 `d8bfbb91…`；日志 `20261031T044135Z`（boot `0x4e1d0ef9`）：`state=3 gpt=0/0 writes=161233 syncs=25108 present=102895 err=0`，Data `result=0`。
- **跨重启恢复**：同一镜像 `d8bfbb91…` 再启动一次；日志 `20261031T044943Z`（boot `0x4fa89bdb`）首行 `state=2 header=0 gpt=0/0 loaded=111 present=103367 err=0`，后续 `present=147881`，Data `result=0`。**持久 Data 成立**。
- 限制：只验证了该 Data 窗口与日志窗口，未做掉电/复位故障注入；seed 大小或内容（如 GPU 插件）改变会更换持久化身份，需 `sheng-data-reset=1` 重新初始化；窗口位置依赖 reserve 值，镜像生命周期内不要改。未写 GPT/根卷/日志窗口以外区域。
- 附带确认（上一轮待查）：`display-scale=3` 后渲染面仍是 **1242×2208**（`P21 SCALE 1242x2208 -> 1080x1920`），CPU 缩放仍在；scale 只改逻辑布局，不改像素尺寸。
- 下一步：按官方顺序进入 GPU（Adreno 540），先做只读硬件/电源/固件探针；同 SoC 的 sagit（`out/p23/metal-review/sagit-source`，pin `898998c`）含 A540 的 GPUCC/RPM/SMMU/CP/GPMU 与固件参考。

## 2026-10-31：界面改成手机比例——`/chosen/display-scale` 由 1 改为 3

- 身份核对（官方 `/zh/guide/mobilegestalt`）：ADT 早已是 iPhone 行（`iPhone10,3`/`D22`/`iPhone X`/idiom `phone`/subtype 2436/scale 3/无 menu/`home-button-type=2`），与 P23 iOS 16 donor 一致；sheng 用 iPad 行（`iPad7,1`/`J120`、scale 2）。问题不在 model/product。
- 真正原因：启动参数 `offline-legacy-unit-scale=1` 让驱动把 `/chosen/display-scale` 发布成 1，逻辑点=物理像素（1080×1920 pt 的大画布），任何界面都会铺成平板布局。按身份改为 **3**（360×640 pt），用户确认界面变为**手机比例**。
- 附带预期：渲染 surface 从 1242×2208 回到 1080×1920（1:1，不再 CPU 缩放），帧率与 MDP 翻转都受益；待日志确认。
- 镜像 `a41b663e…`（内核 `37e500c3…` 未变、payload `0880db2d…`、固件 `6b0cf1b2…`，args 463），pins 命中、门禁 PASS，未写分区。
- 若后续身份仍被判定为平板，按手册删除 Data 里的 `.../systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist`；本机 Data 是自定义 seed（`owner-data-1g.dbr`），需要从 seed 剥离并更新 payload 固定哈希。

## 2026-10-31：卡顿真因是扫描缓冲走非缓存映射（每帧约 0.9 s），改为缓存写 + 每帧一次 clean

- 翻转没有生效（日志 `flip=0/0`）的原因：**iOS 渲染的 surface 是 1242×2208、stride 5120**，而面板是 1080×1920；驱动每帧走 `p21_scale` 做 CPU 缩放，翻转要求尺寸一致，因此从未进入翻转分支。日志实测 `P21 SCALE 1242x2208 -> 1080x1920 … ticks=17147001`（19.2 MHz 计数器 ≈ 0.89 s/帧）。
- 真正的瓶颈：GOP 扫描缓冲此前用 `kIOMapWriteCombineCache` 映射（arm64 上为 Normal-NC），每笔 64 位存储都是独立总线事务，1080×1920 一帧上百万笔 → 0.9 s。
- 修正（提交见 git）：扫描缓冲改用 `kIOMapDefaultCache`（可缓存写回），每帧写完调用内核自身的 `flush_dcache(va,len,FALSE)` 清到 DRAM 再交给显示；三条写入路径（缩放、普通拷贝、全黑填充）都加了 clean。`flush_dcache` 是内核全局符号，本板运行时与内核同镜像链接，可直接调用。
- 镜像 `57e4513c…`（内核 `a1a42602…`、payload `0ad86d39…`、固件 `a6b0f20e…`，args 463），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区。
- 仍保留：MDP5 页翻转代码（`cheeseburger-fb-flip`，在尺寸一致时才生效）。若缓存写后仍不够流畅，下一步是用 MDP 的 QSEED3 缩放器做 1242×2208→1080×1920（需要从厂商 msm-4.4 内核取 mdp5 缩放配置，Linux 主线 mdp5 已无该实现）。

## 2026-10-31：MDP5 扫描配置已读出，页翻转路径已实现（可选开关）

- 探针日志 `20261031T022932Z`（`stage=9`，读完 pipe 后卡在 DSI 寄存器读取——XNU 侧读 `0xc994000` 会挂住，后续不再读 DSI）：
  - `MDSS_HW=0x30000001`、`MDP_HW=0x30000000`；
  - **VIG0** 是活动 pipe：`SRC0_ADDR=0x9d400000`（GOP 扫描缓冲）、`SRC_STRIDE_A=0x10e0`（4320 字节 = 1080×4）、`SRC_FORMAT=0x000237ff`；
  - **LM0** `OUT_SIZE=0x07800438`（1080×1920），其余 LM 为 0；
  - **CTL0** 的 `LAYER(LM0)=0x01000005`（VIG0 参与混合），`CTL1` 的 `OP=0x00020020`（CMD_MODE + INTF2，命令模式 DSI，因此翻转后需要发 `CTL_START`）。
- 实现（`package/BoardRuntime/NativeLegacyDisplay.cpp` 的 `flippath`，开关 `cheeseburger-fb-flip=1`）：
  1. 自检：在 8 条 pipe 中找 `SRC0_ADDR == GOP 物理地址` 的那条（本机为 VIG0），记录原始 stride；
  2. 每帧取 `IOMemoryDescriptor::getPhysicalSegment(0)`，要求物理连续且覆盖 `stride*height`，否则走拷贝；
  3. 写 `PIPE_SRC0_ADDR`/`SRC_STRIDE_A`，写 `CTL_FLUSH=(pipe|LM0)` 再写 `CTL_START=1`（照 Linux `mdp5_ctl_commit`）；首次用 5 个 CTL 逐个试探，读 `PIPE_CURRENT_SRC0_ADDR` 确认真的是目标地址，命中即记住该 CTL；
  4. 任何一步不满足都把地址/stride 恢复成 GOP 值并回退到 CPU 拷贝；计数 `cb_disp[126]`（翻转成功帧）/`[127]`（回退帧），日志行 `DISP … flip=ok/fb`。
- 镜像 `2d8c0767…`（内核 `33df5596…`、payload `74de0b75…`、固件 `c0ccaa55…`，args 463，含 `cheeseburger-fb-flip=1`），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区。
- 判读：若界面不再“从上到下逐行刷新”、`flip` 计数增长 → 零拷贝生效；若 `flip=0/N` 且画面与之前一致 → 翻转条件不满足（物理不连续/CTL 未命中），仍走拷贝，不影响可用性。

## 2026-10-31：确认“USB 之后不进桌面”是间歇性早期卡死；启动参数内置化

- 对照实验（同一镜像 `214ba057…` 连开两次）：第一次 `backboardd absent`、`EXITS total=50`、`clients=0 frames=0`；第二次（日志 `20261030T193108Z`）`clients=51 frames=14`、`EXITS total=612`、`backboardd p56 threads=16`、触摸 `sink/send/rep=287` 全部正常。结论：**不是 USB/探针改动引入的回归**，是该端口已知的间歇性早期用户态卡死（与首帧停顿不同一阶段）。
- 参数串截断证据：`214ba057` 的 args 为 714 字节，尾部的 `cheeseburger-disp-probe` 未生效（日志无 `DISP` 行），而中段 `offline-legacy-gop=1` 生效。
- 参数内置化（提交 `2818695`、`7cefac6`）：新增 `package/BoardRuntime/BoardBootArgs.cpp`/`.h`，`cb_board_arg()` 先读启动参数再落回编译期默认值；14 项只被本仓库读取的参数写死（触摸/按键/HID/AKS/日志/观察器等），官方框架与内核早期读取的参数仍留在 payload。说明见 `docs/P21_BAKED_BOOT_ARGS.md`。参数串 **714 → 440 字节**，探针参数移到串首。
- 新镜像用于取 MDSS 数据：`bbb31127…`（内核 `e2d96f51…`、payload `527c57fa…`、固件 `a5c04ad2…`，args 440），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区；`cheeseburger-disp-probe=60`、`cheeseburger-usb-probe=0`。
- 判读：下一轮日志应出现 `DISP layout` + `DISP1..DISP7` + `DISP gop=`，用其中 `PIPE_SRC0_ADDR` 与 `gop` 对比即可确认固件扫描用的是哪条 pipe/LM/CTL。

## 2026-10-31：CPU 渲染卡顿 → 改走 MDP5 直扫（零拷贝页翻转）

- 现象与现有实现：`NativeLegacyDisplay` 每帧把 IOSurface 逐行 64 位存储拷进固件 GOP 缓冲（单缓冲），日志记录 143–285 ms/次；屏幕逐行刷新正是“边写边扫”的撕裂。上游 UniXNU 的 `NativeMobileDisplay` 也是同一套 GOP 拷贝，没有 MDP 支持，所以这部分要自己写。
- 参考基线（Linux v6.6，已取到）：`drivers/gpu/drm/msm/disp/mdp5/mdp5_cfg.c` 的 `msm8998_config` 给出块地址（CTL 0x1000/0x1200/0x1400/0x1600/0x1800、VIG 0x4000/0x6000/0x8000/0xa000、RGB 0x14000/0x16000/0x18000/0x1a000、DMA 0x24000/0x26000、LM 0x44000..0x49000、PP 0x70000/0x70800/0x71000/0x71800、INTF 0x6a000..0x6c000；LM0→PP0、LM1→PP1、LM2→PP2、LM5→PP3；INTF1/2 是 DSI）；`mdp5.xml.h` 给出寄存器（PIPE SRC_XY 0x08、OUT_XY 0x10、SRC0_ADDR 0x14、SRC_STRIDE_A 0x24、SRC_FORMAT 0x30、OP_MODE 0x200；CTL TOP 0x00/OP 0x14/FLUSH 0x18/START 0x1c；PP TEAR 0x00/SYNC 0x04/HEIGHT 0x08；LM OUT_SIZE 0x04）。MDP 基址 0xc901000、MDSS 0xc900000、DSI0 0xc994000。翻转要点：改 `SRC0_ADDR`（必要时 `SRC_STRIDE_A`）后写 `CTL_FLUSH` 的对应 pipe 位，寄存器在帧边界生效。
- **本轮加只读探针**（`package/BoardRuntime/CheeseburgerDispProbe.cpp`，提交见 git）：启动 60 s 后 dump MDSS/MDP 版本、5 个 CTL 的 TOP/FLUSH/OP/START、6 个 LM 的 OUT_SIZE/blend、4 个 PP 的 TEAR/SYNC/HEIGHT、三个 INTF、8 条 pipe（VIG0-3/RGB0-1/DMA0-1）的 ADDR/STRIDE/FORMAT/XY/OUT_XY/OP_MODE、DSI0 前三个寄存器，并记录 GOP 物理基址/stride/分辨率，用于把 `SRC0_ADDR` 与 GOP 基址对上、确定固件用的是哪条 pipe/哪个 CTL。
- 为聚焦本轮，USB 探针暂时关闭（`cheeseburger-usb-probe=0`），启动参数增加 `cheeseburger-disp-probe=60`。
- 镜像 `214ba057…`（内核 `63aa0759…`、payload `f9c421c2…`、固件 `ef38d448…`，args 714 字节），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区。

## 2026-10-31：USB 实时通道调研 + 阶段 0 探针（CDC-ACM 路线）

- **上游结论**：UniXNU 的 `drivers/` 没有 USB host/device 驱动；sheng 设备页 USB 一栏是 ❌；上游日志通道只有 UART 串口、屏幕诊断页、panic 屏、QEMU 文件。上游文档明确写了替代方案：*"A board with USB device mode can expose a CDC ACM port instead."*
- **Apple 栈不可直接复用**：donor kernelcache 有 `AppleUSBXDCI`/`AppleT8011USBXDCI`/`AppleUSBDeviceMux`/`usb.cdc.acm`，但 Apple 开源 XNU 里没有 `IOUSBDeviceController/XDCI` 头，用它等于再逆向一套私有 ABI，成本高。
- **硬件与参考**：DTS 给出 wrapper `usb@a8f8800`、控制器 `usb@a800000`（`snps,dwc3`）、QUSB2 PHY `@c012000`、QMP USB3 PHY `@c010000`、GCC 的 USB30 GDSC/时钟；**uefi-sheng 自带完整源码的 DWC3 device-mode 驱动**（`Silicon/Samsung/SamsungPkg/Drivers/UsbfnDwc3Dxe`，Dwc3Core/Dwc3Endpoint 约 2400 行），作为寄存器序列蓝本。
- **阶段 0 探针**（`package/BoardRuntime/CheeseburgerUsb.cpp`，提交见 git）：启动参数 `cheeseburger-usb-probe=<秒>`（默认 60）+ `cheeseburger-usb-mode=1|2`。
  - mode 1 只读；mode 2 在 GDSC/主时钟为关时先上电使能（GDSC 0xf004、master 0xf008、sleep 0xf00c、utmi 0xf010、phy aux 0x50000、pipe 0x50004、cfg_noc 0x5018、aggre1 0x82024、clkref 0x88008/0x88014、ahb2phy 0x6a004），再读一次；上电前状态保留在 `cb_usb[12..22]`。
  - 仅在“GDSC 状态位 + 主时钟”都读回为开时才访问控制器/wrapper/PHY（未上电三块访问可能触发 NoC 错误），跳过区域填 0xdeadbeef。
  - 日志行：`USB0`（GCC 11 项 + flags）、`USB1 pre`（上电前）、`USB2..USB8`（DWC3 32 项、QSCRATCH 16 项、QUSB2 16 项、QMP 32 项）。
- 镜像 `f283d5e1…`（内核 `380db8b0…`、payload `8a3d9c3d…`、固件 `9b3f627b…`、args 712 字节），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区。
- 判读：`flags` bit0=GDSC 上电、bit1=主时钟在跑、bit3=本探针上电成功、bit4/5=上电前状态；`USB2` 首项是 GSNPSID（`0x5533xxxx` 表示 DWC3 应答，`0xdeadbeef` 表示未访问）。

## 2026-10-31：未解决问题清单（用户要求存档）

1. **显示首帧间歇**：同一镜像会在 `sel6 (n,0x80000001)`（成功）与 `sel6 (0,0x80000003)` → `rc=e00002c7`（黑屏）之间翻转；`[fb events]` 记录可见分界在 backboardd 的配置序列，非身份/触摸。这是目前最影响体验的遗留问题。
2. **触摸多点/边缘手势**：单指点按与滑动已确认可用；双指捏合、边缘回主屏/切换器手势尚未在实机验证（`native_touch_hid_multi` 有计数，但未做手势级确认）。
3. **GPIO125 中断模式**：当前 25 ms 轮询（`irq=0`），`cheeseburger-touch-irq=1` 的 TLMM summary SPI208 路径未启用。
4. **休眠/唤醒恢复**：挂起后触摸与显示能否恢复未验证。
5. **诊断探针待清理**：`TOUCH10/15/16/17/18/19` 与 `cb_touch_extra[64..78]` 是定位期探针，触摸稳定后可裁剪（保留 `TOUCH` 主行与 `i2c/F01/F12/frames/HID` 即可）。
6. **构建环境**：本机构建树 `/Users/dinnerabc/unixnu-p21/fb-qup3/source-tree`；不要复制整个候选（3 GB/50 分钟），只同步 `package/BoardRuntime` 改动与产物。

## 2026-10-31：触摸在实机上可用（用户确认）

- 修复的最后一环（提交 `4ca8bc1`）：HID 探测阶段合并键盘设备先调用 `native_touch_set_sink()`，此时触摸驱动尚未分配 `sinkLock`，请求被丢弃；随后 worker 又创建了没有事件驱动的独立 digitizer 设备，导致 `TOUCH19 nodrv` 每次 +1 而 `HID=0`。现在把早到的 sink 请求挂起，`cheeseburger_touch_probe()` 分配锁后补装（`pend=1`）。
- 镜像 `out/p21/fb-qup3/uefi/boot-p21-up1.img` `5ce1d727…`（内核 `3dfbe6bd…`、payload `68e2be27…`、固件 `f9c4cccd…`），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区；用户确认**触摸可用**。
- 触摸通路全貌：GPIO87/88 → `qcom,i2c-qup-v2.2.1`（0x207 + 0x408=1 + 0x81/0x82/0x83/0x87 字节 tag）→ F01(0x2a)/F12(0x47) 发现 → 10 指 80 字节包经 IN block 握手读取 → `cb_decode` → HID 合并设备（`sheng-touch-hid=1`）digitizer 报告。轮询 25 ms；GPIO125 中断仍未启用。
- **实机收据**（日志 `20261030T173257Z`，boot `0x4ef807f2`，83 rounds，写入错误 0）：`DISPLAY frames=71`，`TOUCH19 nz=0 sink=543 send=543 nodrv=0 rep=543 pend=1`，即 543 次触摸发布全部找到事件驱动并成功发出 HID 报告（`pend=1` 证明挂起的 sink 已补装），HID 设备 `stage=3`。触摸通路由此闭环。
- 仍未验证：多点捏合/边缘手势在 UIKit 侧的行为、GPIO125 中断模式（当前 25 ms 轮询）、休眠/唤醒后的恢复、以及显示首帧间歇分支的根因。

## 2026-10-31：触摸 HID 的 sink 早到问题（时序）已修

- 日志 `20261030T171838Z`（用户已触摸、桌面正常 14 帧）：`TOUCH19 sink=503 send=503 nodrv=503 rep=0` —— sink 已安装，publish 每次都会调用 `touchSend`，但**每次**都因为 `hasEventDriverCached()` 为空而早退。
- 根因（时序）：合并键盘+digitizer 设备在 `native_hid_input_probe()` 里就 `start()` 并调用 `native_touch_set_sink()`，此时触摸驱动还没跑、`sinkLock` 为空，sink 请求被直接丢弃；触摸 worker 后来看到 sink 为空，又创建了**独立 digitizer 设备**（`sheng-touch-split` 才该用的形态），它没有事件驱动，于是 `nodrv` 每次都加 1。
- 修正：`native_touch_set_sink()` 在 `sinkLock` 未就绪时把请求挂在 `pendingSink/pendingCtx`；`cheeseburger_touch_probe()` 分配锁后立即补装（`TOUCH19 pend=1` 可确认）。这样合并设备成为 sink 所有者，独立设备不会再被创建。
- 候选镜像 `5ce1d727…`（内核 `3dfbe6bd…`、payload `68e2be27…`、固件 `f9c4cccd…`），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区。
- 判读：下一轮应为 `pend=1 sink>0 send>0 nodrv=0 rep>0` 且 `HID>0`；触摸应可点按/滑动。若 `nodrv` 仍增长，说明合并设备的事件驱动确实没附着，需要检查 IOHIDEventDriver 的匹配属性。

## 2026-10-31：HID=0 的真因是独立 digitizer 设备覆盖了合并设备的 sink

- 日志 `20261030T170145Z`（boot `0x50...`，用户已触摸）：触摸解出 `contacts=1 publish=363`（`raw=77800000000`），但 `HID=0`；同一轮 `DISPLAY frames=0`，`[fb events]` 是已知的间歇分支 `E03 sel6 (0,0x80000003)` → `E04 rc=e00002c7`，与触摸改动无关（同镜像会翻转，此前基线也是这样）。
- 代码核对：`cheeseburger_touch_hid_start()` 走的是 `native_hid_touch_allocate()`（`touchOnly`），而 `NativeHidInput` 注释写明：实机上事件驱动附着的是**键盘+digitizer 合并设备**（`sheng-touch-hid=1`），独立 digitizer 设备是 opt-in 的 `sheng-touch-split`。`HidInputProbe` 先启动合并设备并装上 sink，触摸 worker 随后又启动独立设备，把 sink 覆盖成那个没有事件驱动的设备，于是 `touchSend` 早退、`HID` 永远 0。
- 修正（提交 `...`）：新增 `cheeseburger_touch_sink_ready()`；worker 在 `native_hid_stage==3` 之后**已有 sink 就不再创建独立设备**。同时加了四个计数器区分路径：`TOUCH19 sink/send/nodrv/rep`（sink 安装后的 publish 次数、touchSend 次数、因无事件驱动早退次数、成功取报告次数）。
- 候选镜像 `674381a9…`（内核 `b354e0ad…`、payload `e940c727…`、固件 `d6352763…`），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区。
- 判读：下一轮 `TOUCH19 sink>0 send>0 nodrv=0 rep>0` 且 `HID>0` 即打通；若 `nodrv>0`，说明合并设备的事件驱动也没附着，需要回到 HID 描述符/匹配属性；若桌面本轮仍不亮，属显示间歇分支，同镜像再启动一次即可对照。

## 2026-10-31：触摸已解出坐标；HID 只是因为缺 `sheng-touch-hid=1` 没被启用

- 日志 `20261030T164836Z`（boot `0x501a...`，用户已实际触摸）：`TOUCH stage=4 error=0 i2c=20674/0 F01=2a F12=47`、`range=1079x1919 objects=10 data=6/80`、`frames=10317`、**`publish=348`**，最后一帧 `raw=69c00000284` 即 x=644、y=1692（都在 0..1079 / 0..1919 内），`mapped` 也已生成；`TOUCH19 nz=0` 说明所有非零数据包都成功解码，没有“有数据但解不出”的情况。
- `contacts=0` 只是日志抓取时最后一帧已抬手（`publish` 记录的是变化次数）；触摸本身已验证。
- **`HID=0` 的根因**：`NativeHidInput` 只有在启动参数 `sheng-touch-hid=1` 时才安装触摸 sink（`native_touch_set_sink`）并加入 digitizer 描述符；候选 `config/unixnu.toml` 的 args 里没有这一项，所以 `publish()` 的 sink 为空，事件没有进入 HID。已在 `cheeseburger-touch=1` 后加 `sheng-touch-hid=1`（args 从 644 增到 662 字节）。
- 候选镜像 `2dc35c79…`（内核沿用 `b1269b4e…`、payload `7dada6e2…`、固件 `6c9be14a…`），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区。
- 判读：本轮日志的 `TOUCH` 行 `HID` 应 >0；桌面应能响应点按/滑动。若 `HID>0` 但界面无反应，下一步查 `IOHIDDevice::handleReport` 返回值与 backboardd 的 digitizer 匹配（`Built-In`/`DigitizerCollectionDispatch`）。

## 2026-10-31：触摸链路打通到 stage=4；只差接触数据

- 日志 `20261030T163537Z`（boot `0x5018e76c`）：`TOUCH stage=4 error=0 i2c=30285/0 F01=2a F12=47`，`range=1079x1919 objects=10 data=6/80`，`frames=15122`。F01/F12 发现成功，F12 的 80 字节数据包（10 指 ×8，跨 64 字节 FIFO 的块模式读）连续 6 分钟 30285 次传输、0 失败。
- 车 `max_x=1079 / max_y=1919` 与原厂 dmesg 的 `max_x=1080, max_y=1920`（最大索引 1079/1919）一致，`data1=0x06` 也与 F12 data base 一致。
- `contacts=0 publish=0 HID=0`：没有任何一次解出接触。此时无法区分“没摸屏幕”和“状态字节/偏移对不上”，因此加了一个极小探针（提交 `ff...`：「Capture the first non-zero F12 packet when no contact decodes」）：只要包里有非零字节但没解出接触，就把前 16 字节和 F01 status 记到 `TOUCH19 nz/p0/p1/f01`；不影响原有读写路径。
- 新镜像 `d10f0d1c…`（内核 `b1269b4e…`、payload `f26f7f0c…`、固件 `546f1ba1…`），pins 命中、门禁 PASS，临时 boot 一次 OKAY。
- 判读：本轮回 Recovery 后若 `TOUCH19 nz>0`，直接看 `p0/p1` 的对象状态字节（bit0=present，0x01 手指、0x02 笔、0x03 手掌、0x04 未分类），据此修正 `cb_decode` 的状态判断或对象偏移；若 `nz=0`，说明面板根本没上报，下一步查 F01 control/IRQ 使能与扫描使能。

## 2026-10-31：PDT 一直是对的；真正卡点是 F12 函数号写成了十进制 12

- fb-qup3 日志 `20261030T161738Z`（boot `0x500700cb`）的寄存器图（`TOUCH17/18`，单字节连续读）：
  - `0xDD..0xE2` = `47 00 15 06 01 12` → **F12**（query 0x47 / control 0x15 / data 0x06 / fn=0x12），与整机 dmesg 的 `F12_2D_QUERY_BASE=47 … DATA_BASE=6` 逐项一致；
  - `0xE3..0xE8` = `2a 25 0e 04 01 01` → **F01**（fn=0x01）；
  - `0xE9..0xEE` = `26 00 0a 00 21 34` → **F34 flash**（query 0x26 / ctrl 0x0a / data 0x00），与 dmesg 的 `F34_FLASH_*` 一致。
  - `0xFF` 连续三次回读都是 0，分体事务（地址写带 STOP + 无地址指针读）与合并事务返回相同 6 字节 → 寻址、锁存、字节序都正常。
- **更正上一轮判断**：之前说“6 字节反序”是我把打包值按高位优先解码的误判。`packBytes` 是低位在前，`0x120106150047` 就是 `47 00 15 06 01 12`；PDT 从 v2 重写后一直读得对，不需要任何反转。
- **真正阻塞**：`SynapticsRMI4.h` 的 PDT 扫描写成 `if(p[5]==12)r.f12=f;`，十进制 12 = 0x0C，永远匹配不到 fn=0x12，于是 `f12.query=0`，`cb_discover` 直接返回 `-32`。已改为 `p[5]==0x12`（F01 同时写 0x01），提交 `9b467b0`。
- 候选镜像 `c9739407…`（内核 `e111ae32…`、payload `a9d2f14e…`、固件 `68ff6168…`），pins 命中、门禁 PASS，临时 boot 一次 OKAY，未写分区。
- 本轮耗时（本机构建树）：增量内核 34 s、预链接 8 s、补丁 5 s、payload 10 s、VM 固件 70 s、打包+门禁 15 s ≈ **2.5 分钟**。
- 判读：下轮日志 `TOUCH` 行应从 `stage=2/error=-32` 前进到 `stage=3` 且 `F01/F12` 非零；若停在 `-33/-34`，看 F01 identity/status；停在 `-35/-36` 看 F12 query 与三个寄存器描述符；进入 `stage=4` 后看 `frames/publish/HID`。

## 2026-10-31：构建树搬到本机，单轮构建从约 28 分钟降到约 4 分钟

- **位置**：构建树在本机 `/Users/dinnerabc/unixnu-p21/fb-qup3/source-tree`（1.3 GB，构建后约 2 GB；本机剩余约 79 GB）。NAS 的 `out/p21/fb-qup3/source-tree` 保留一份源码副本，候选的 `kernel/ payload/ uefi/ config/ logs/` 仍全部在 NAS，证据链不变。
- **同步方式（增量，不再整目录复制）**：改代码在仓库 `package/BoardRuntime/`，然后 `cp package/BoardRuntime/{CheeseburgerTouch.cpp,BootLog.c} <本机树>/package/BoardRuntime/`；构建驱动 `scripts/p21-compile-fb-qup3.py` 已把 `R` 指向本机树、`C` 仍是 NAS 候选，内核产出由脚本自己写回 `out/p21/fb-qup3/native/kernel.macho`。本机树里 `tree/kits/cheeseburger` 已重指到本机 `package`，`build/native-ram-block`、`build/native-keystore-events` 两个绝对软链也改成指向本机树。
- **DriverKit 头种子**：本机树重新 prepare 时缺 6 个头会去 GitHub 抓（SSL EOF 失败）；驱动改为优先用**本候选** NAS 树的 `.cache/xnu-work` 作种子（37 个文件齐全），fb-probe 只作兜底。
- **实测**：内核 `kernel.build` 111 s（NAS 冷态 709 s、热态 109 s，CPU 238 s 真正并行）；预链接 8 s；panic 补丁 5 s；payload 约 10 s；VM 固件 70 s；打包+门禁约 10 s → **整轮约 4 分钟**。之前判断的“预链接 7 分钟”是错读时间戳，实际只有 8 秒。
- 本轮探针镜像：内核 `bb5d32ca…`、payload `bad654b0…`、固件 `730e599f…`、镜像 `b41443d3…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。

## 2026-10-30：QUP v2 通了——PDT 读出真实字节，但 6 字节内容与原厂 dmesg 反序

- fb-qup3 日志 `20261030T150933Z`（boot `0x4a5fb48d`，70 rounds）：`TOUCH12 ack=1000000`（0x20 正常 ACK）、`w=0 r=0 page=0`、`i2c=21/0`（21 次成功、0 失败）、`TOUCH14 fifo=64/64 blk=16/16`、`TOUCH10 pdt=3421000a0026/101040e252a/120106150047`。I2C 读写事务已真正工作，`error=-32` 只是 `cb_discover` 找不到 F01/F12。
- **关键对照**：整机原厂 dmesg（`out/p21/phone-ui/evidence/recovery-touch-dmesg.txt`）给出 `F12_2D_QUERY_BASE=47 / CMD=0 / CTRL=15 / DATA=6`、`F34_FLASH_QUERY_BASE=26 / CTRL=a / DATA=0`。把 `0xDD` 读到的 6 字节 `12 01 06 15 00 47` **整体反转**即 `47 00 15 06 01 12`，与 F12 的 query/command/control/data/function 五项逐项吻合；`0xE9` 的 `34 21 00 0a 00 26` 反转后 query=0x26、ctrl=0x0a、data=0x00 也吻合 F34。说明数据是真的，只是字节序与标准 PDT 相反。
- 本轮不加猜测，直接加判定探针（提交 `ef77b64`）：① 页寄存器回读——写 `0xFF=0x00` 读 0xFC 起 4 字节、写 `0xFF=0x01` 再读，页字节出现在第 4 位=顺序正常、出现在第 1 位=读流反序；② 偏移测试——`0xE8` 与 `0xE9` 各读 6 字节（顺序正常时 `e8[1..5]==e9[0..4]`），另读 `0xEE`；③ 按两种字段序（byte5 或 byte0 作函数号）扫描 0xE9→0x05，记录各自看到 F01/F12 的地址；④ 在 F12 query(0x47)/control(0x15) 各读 6 字节做内容验证。报告行新增 `TOUCH15`/`TOUCH16`。
- 候选 `out/p21/fb-qup3`（复用同一候选目录，旧证据保留在 `boot-once.log`/历史日志）：内核 `54bfa72a…`、payload `1b6e87dc…`、固件 `a4a2ca81…`、镜像 `b7eb4776…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。设备 `3b25cc7c` 临时 boot 一次，未写分区。
- 判读：若 `TOUCH15` 的页字节在 `pg0` 的第 4 字节 → 读流正常，问题在设备字段序，按反转字段解析 PDT 即可；若在第 1 字节 → 读流反序，需要修正传输层的字节顺序（下一步要确认是 per-word 还是整块）。`TOUCH16` 的 `f01n/f12n` 与 `f01r/f12r` 会直接给出两种解析各自的命中地址。

## 2026-10-30：根因更正——QUP5 是 v2.2.1，改用 v2 字节 tag 与重复起始读

- fb-probe2 日志 `20261030T133244Z`（boot `0x4ff8d59c`，93 rounds）：`TOUCH12 ack=0,0 w=0 r=-8 b=0`、`TOUCH10 pdt=0/0/0`、`TOUCH13 status=c110000`、`TOUCH hw=20070000`。写事务"完成"但整条总线没有任何地址 ACK、读超时，和 v1 编程的预期不符。
- **根因**：本机参考 DTS `out/p22/reference/arch_arm64_boot_dts_qcom_msm8998.dtsi` 中 `blsp1_i2c5` 的 compatible 是 `qcom,i2c-qup-v2.2.1`，同一份 `i2c-qup.c` 因此走 `qup_i2c_xfer_v2`，而不是上一轮照抄的 v1 路径：
  - `QUP_CONFIG = I2C_MINI_CORE|I2C_N_VAL_V2 = 0x207`（不是 0x20F），只写时 `|0x80`；
  - `QUP_I2C_MASTER_GEN(0x408)=1` 使能 v2 tag；缺这一步 tag 不会被解释；
  - FIFO 字节流是 `0x81 addr`、`0x82`(无 STOP)/`0x83`(STOP) `+len`、`0x87`(读 STOP)`+len`，数据按 4 字节/字裸排；v1 的 16-bit tag 半字在这里完全无效；
  - 寄存器读是**一次 transfer 的两条消息**：写地址（不 STOP）+ 读（STOP，重复起始），与 `rmi_i2c.c` 一致；上一轮拆成两次独立事务。
- 重写（提交 `925cc56`、`13b26cf`）：复位/状态机按参考实现（RESET→配置→RUN→写 clock_ctl→PAUSE→写 FIFO→RUN→等完成），reset 态读 IO_MODE 计算 FIFO/块大小，大读用 `IN_BLOCK_READ_REQ` 块握手，RX 首 2 字节是收到的 tag 需丢弃，第二个子传输的计数字段带 `QUP_I2C_MX_CONFIG_DURING_RUN(BIT31)`，结束前等待 `BUS_ACTIVE` 清零。诊断行同步为 `TOUCH12 … page=`、`TOUCH13 st/op/err/status`、`TOUCH14 fifo/blk/pre/pdt/cnt`。
- 构建链注意：候选树的 `tree/kits/cheeseburger` 是指向上一个候选 `package` 的绝对软链，复制候选后必须重指；预链接/门禁脚本需要 `python3.13`（`tomllib`）；固件复用 guest overlay `unixnu-up1-fb-touch`，pin 扫描显示 kernel/payload/ADT 三个哈希都进入 `FVMAIN.Fv` 与 `P9Probe.efi`/`DarwinBoot.efi`。
- 候选 `out/p21/fb-qup3`：内核 `fbdbb96043e451b2…`、payload `e6fa821a704ab466…`、固件 `1668d94d4249c8e9…`、镜像 `out/p21/fb-qup3/uefi/boot-p21-up1.img` `1c054d5e19a21dcd…`；pins 命中、门禁 PASS；设备 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区，`out/p21/fb-qup3/boot-once.log`；监视器 `scripts/unattended/monitor.py --timeout 3600` 已启动等待回收。
- 判读：`TOUCH12 ack` 中地址 0x20（从 0x08 起第 24 位，即 `m0` 的 `0x1000000`）应置位；`TOUCH10 pdt` 应出现 `fn=01`/`fn=12` 的合法条目；`TOUCH` 行 `i2c` 成功计数增长、`F01/F12` 非零；若 ACK 正常但 PDT 仍全 0，下一步查页选择与读块握手（`TOUCH14` 的 `fifo/blk` 与 `cnt`）。

## 2026-10-30：三轮审计发现探针根本错误（时钟未设），已修正并重测

- fb-probe 结果（`20261030T130947Z`，boot `0x50f62b74`）：`TOUCH12 ack=0,0 w=-6 r=-6`、`TOUCH10/11` 全 0、`i2c=0/12`。
- **审计第一轮（对照 i2c-qup.c）**：我方错误地把 `I2C_STATUS bit8` 当 NACK。上游 `I2C_STATUS_BUS_ACTIVE=BIT(8)`、`QUP_I2C_NACK_FLAG=BIT(3)`，错误掩码 `0x38000fc` 已含 NACK；该检查已删除，NACK 仍由 `status&8` 报 `-6`。
- **审计第二轮**：① 配置前遇 `BUS_ACTIVE` 直接 `-4`，一次失败会污染后续所有传输 → 改为 20 ms 有界等待；② 缺少上游每次配置都会写的 `QUP_OPERATIONAL_MASK(0x28)=0x300` → 已补。
- **审计第三轮（关键）**：三个探针块都位于 `clockCtl=0x300|93;` **之前**，`wr(qup,0x400,0)` 让 QUP 没有 SCL 时钟，所以 TOUCH9/10/11/12 的结果全部无效（发现阶段的 25 次成功才是有时钟的真实结果）。已把时钟设置提前到任何探针之前。
- **审计第四轮**：删除旧的 `0x81/0x82/0x87` 帧探针（在 tag 化传输下会向任意 RMI 寄存器写入数据）与两字节地址探针（会写命令寄存器 0x00）；探针改为对 0xE9 PDT 头做只读访问 + 全地址只读 ACK 扫描。错误掩码、报告行、数组边界均复核通过。
- 候选 `out/p21/fb-probe2`：内核 `8d54f5ea…`、payload `9caccca3…`、固件 `73a44d59…`、镜像 `a5292ff3…`；pins 命中、门禁 PASS；提交 `20c67f5`；临时 boot 一次 OKAY，未写分区。
- 判读：`TOUCH12 ack` 现在才是有意义的 ACK 位图（0x20 位应置位），`TOUCH10` 应显示真实 PDT 字节；`TOUCH13` 的 `st/cnt` 用于确认装载与结束状态。

## 2026-10-30：两种地址形式都读回 0；改为一次性全面探针

- fb-pdt2 日志 `20261030T124405Z`（boot `0x51ca5b32`）：`TOUCH10 pdt=0/0/0`、`TOUCH11 pdt2=0/0/0` → 1 字节与 2 字节地址都拿不到数据，`error=-32`、`i2c=25/7`。
- 审计发现的判定缺口：`qupFinish()` 只看 status 的 `0x38000fc`，**没有把 NACK 位（`0x100`）当作错误**，所以"25 次成功"可能包含被 NACK 的写操作。
- fb-probe（提交 `1d7f7c4`）一次性补齐信息（单次启动）：
  1. `TOUCH12 ack=…`：对 0x08–0x77 全部地址做读探针，输出两个 64 位 ACK 位图（第一字=0x08–0x47，第二字=0x48–0x77）；
  2. `w=`/`r=`：写寄存器 0x00 指针、再读 8 字节的返回码，`b=` 为读到的 8 字节；
  3. `TOUCH13 st=`：读后 QUP `STATE/OPERATIONAL/I2C_STATUS/ERROR_FLAGS` 打包；
  4. `cnt=`：`MX_OUTPUT_CNT/MX_WRITE_CNT` 与 `MX_READ_CNT/MX_INPUT_CNT` 的实际寄存器值；
  5. NACK 现在会让事务返回 `-6`，日志里会直接体现被拒。
- 构建流程修正：GitHub 抓取间歇性 SSL EOF，驱动改为从已准备好的 `fb-pdt` 树**本地种子**恢复 DriverKit 头（哈希仍校验），不再依赖网络；`scripts/p21-compile-fb-probe.py` 可复用。
- 候选 `out/p21/fb-probe`：内核 `4800a1a0…`、payload `d450c215…`、固件 `a1c507a3…`、镜像 `27852270…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：ACK 位图里有 0x20 → 总线寻址正常，问题在寄存器/数据阶段；没有 0x20 → 从机地址或写事务本身不对。`cnt=` 显示 QUP 实际装载的计数能验证 tag/长度是否按预期进入控制器。

## 2026-10-30：PDT 原始字节全 0；对比 1 字节 / 2 字节地址

- fb-pdt 日志 `20261030T122342Z`（boot `0x4f772688`）：`TOUCH error=-32 i2c=25/4`、`TOUCH10 pdt=0/0/0`——页面 0 的 0xE9/0xE3/0xDD 三次读回全 0，且 I2C 状态没有错误位。
- 说明读事务"完成"但拿不到真实数据，怀疑地址形式（RMI 16 位地址是写 1 字节低地址，还是写"页,偏移"两字节）。
- fb-pdt2（提交 `2a89d52`）对同一寄存器做两种地址形式的对照，新增 `TOUCH11 pdt2=...`（两字节地址），与 `TOUCH10`（一字节地址）并列比较。
- 候选 `out/p21/fb-pdt2`：内核 `19e407a1…`、payload `cb3e935f…`、固件 `5a011a63…`、镜像 `7318ac0e…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：若 `TOUCH11` 出现合法 PDT（fn=01/12），采用两字节地址；若两者仍全 0，说明问题在写事务本身（地址/START/STOP tag 或从机地址），下一步用 QUP 的 I2C_STATUS 逐位与上游 `I2C_STATUS_ERROR_MASK` 对照，并检查 0x40 是否被当作数据字节。

## 2026-10-30：读事务已通（i2c=25/0），PDT 仍无 F01/F12；已加原始 PDT 转储

- fb-qup2 日志 `20261030T120148Z`（boot `0x4fad997b`）：`TOUCH error=-32 i2c=25/0`、`status=c110000`、`op=a20`；`TOUCH5 raw=7 gpiopull=3 muxed=3`（固件阶段起 SDA/SCL 就已是高，说明 UEFI 的 NPA 投票在 XNU 之前就把触摸轨打开了）。
- 结论：I2C **读写事务都已工作**（25 次成功、0 失败），但扫描仍找不到 F01/F12。不再靠推断，改为把页面 0 顶部三个 PDT 表项（0xE9/0xE3/0xDD）的原始 6 字节读出来写入日志 `TOUCH10 pdt=a/b/c`。
- 构建流程记录：本机代理当前对 GitHub TLS 返回 EOF，而 `raw.githubusercontent.com` 直连可用；`scripts/p21-compile-fb-pdt.py` 因此不再注入代理（api.github.com 的清单已缓存，无需访问）。可重复使用。
- 候选 `out/p21/fb-pdt`：内核 `e00ba25b…`、payload `42aeb9fe…`、固件 `ddec1875…`、镜像 `out/p21/fb-pdt/uefi/boot-p21-up1.img` `9af808c5…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：若 `TOUCH10` 里出现 `fn=01`/`fn=12` 的合法条目，说明扫描逻辑有偏差（如 break 条件）；若全 0/0xFF，则读到的地址内容不对（页选择或地址字节需再核对）。

## 2026-10-30：审计 fb-qup——写事务已通、读事务超时；改为整块排空后重测

- fb-qup 日志 `20261030T114202Z`（boot `0x4f9e7096`）：`TOUCH error=-30 i2c=3/3`、`status=c110000`、`op=820`、`lines sda=1 scl=1 rst=1`。
- 审计判断：`i2c=3` 成功说明**写事务（页选、写地址）在 tag 化之后已经正确**；读失败是因为 `qupFinish()` 每次 INPUT_SERVICE(`0x200`) 只读 1 个 FIFO 字（2 字节），6 字节读永远凑不满而超时。上游做法是：一个 REC tag 时整块数据已在 FIFO 中，服务标志一次即可全部读出，并以"字节收满"为完成条件（不依赖完成位）。
- 修正（提交 `25d28b0`）：INPUT_SERVICE 置位后清标志并循环排空 `ceil(nr/2)` 个字；`got==nr` 即成功并回 RESET。写事务仍以完成位判断。
- 候选 `out/p21/fb-qup2`：内核 `af541d8b…`、payload `da2b3baa…`、固件 `ecc6dea1…`、镜像 `out/p21/fb-qup2/uefi/boot-p21-up1.img` `410a8d18…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：`TOUCH` 的 `i2c` 失败数应变为 0，随后 `F01/F12` 非零、`stage` 前进；若读仍失败，用 `status/errors/op` 与 `TOUCH9` 判断是 FIFO 排空过多（读到 0）还是 REC tag 长度问题。

## 2026-10-30：QUP 传输已按上游重写（tag 化写 + 独立读事务），fb-qup 已启动

- 依据上一轮的 5 点差异（提交 `6feceba`），把 `CheeseburgerTouch.cpp` 的 I2C 层重写为上游 `i2c-qup.c` 的 v1 编程：
  - `QUP_CONFIG`：写 `0x28F`（`I2C_MINI_CORE|15|NO_INPUT`），读 `0x20F`；
  - `QUP_IO_MODE 0x8`：`REPACK_EN(0xC000)`，块模式才加 `OUTPUT_BLK_MODE(1<<10)` / `INPUT_BLK_MODE(1<<12)`；
  - OUT FIFO 每 32 位字装两个半字，半字 = `(tag<<8)|byte`：START `0x100`（含从机地址 0x40/0x41）、DATA `0x200`、STOP `0x300`、REC `0x400`；
  - 写事务：START|addrW、数据/STOP tag，`MX_WRITE_CNT=len+1`；
  - 读事务：OUT FIFO 写 `(START|addrR)|((REC|len)<<16)`，`MX_READ_CNT=len`，IN FIFO 每字取 2 字节；
  - 顺序：RESET → RUN(写 clockCtl) → PAUSE(写 tag) → RUN → 轮询完成；错误位与 cb_touch 计数保持原有诊断。
- 这样 `cb_wire_page`/`cb_wire_read`/`writeRmi` 的既有语义（页选 + 写地址 + 读）在 RMI 标准下才成立。
- 候选 `out/p21/fb-qup`：内核 `13eeb0ae…`、payload `84f9d893…`、固件 `31a54194…`、镜像 `out/p21/fb-qup/uefi/boot-p21-up1.img` `26f4a3c9…`（82,575,360 bytes）；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：`TOUCH` 行出现 `i2c>0` 且 `F01/F12` 非零 → RMI 通了；若 `TOUCH9 plain` 仍为负，说明写事务或 tag 仍有偏差，下一步用 `TOUCH` 的 `status/errors/op` 三元组定位到具体阶段。

## 2026-10-30：双帧取样定因——QUP 传输本身不符合上游编程

- fb-frame 结果（`20261030T112102Z`，boot `0x4d540669`）：`TOUCH9 plain=fffffff9 old=a002600000000`
  - 纯地址帧：返回码 **-7**（QUP 控制器错误），数据全 0；
  - 旧帧：返回码 0，但数据 `00 26 0A 00` 不是合法 PDT 头。
- 对照 `out/p22/reference/drivers_i2c_busses_i2c-qup.c`（设备原厂驱动）逐项核对，发现我们的 QUP 编程有多处偏差，这才是 RMI 读不出来的根因：
  1. `QUP_CONFIG` 的 `I2C_N_VAL` 应为 **15**（`I2C_MINI_CORE|15 = 0x20F`，只写时再 `|0x80`）；我们写的是 `0x207`；
  2. OUT/IN FIFO 的每个 32 位字必须带 **tag**：低半字 `START(0x100)|addr8`，数据 `DATA(0x200)`/`STOP(0x300)`，读请求 `REC(0x400)|len`，第二个字节在 bit16 起；我们的代码直接往 FIFO 写裸数据，没有 tag、也没有从机地址；
  3. 读是**独立的 QUP 事务**：OUT FIFO 写一个字 `(START|addrR) | ((REC|len)<<16)`，`MX_READ_CNT(0x208)=len`；我们从未这样编程；
  4. `QUP_IO_MODE(0x8)` 应为 `REPACK_EN(0xC000)` 加必要的 `OUTPUT_BLK_MODE(1<<10)`/`INPUT_BLK_MODE(1<<12)`；我们只写 `0xC000|0x1000`；
  5. 状态时序：RESET→RUN→(写 clockCtl)→PAUSE→(写 FIFO tags)→RUN→等待完成；我们的顺序接近但 FIFO 内容错误。
  - 结论：之前所谓的"21 次 ACK 成功"是没有真正寻址的完成事件，不能当作总线可用。
- 下一步（新候选）：按上述 5 点重写 `CheeseburgerTouch.cpp` 的 `transfer()` 为上游 write_op/read_op 两个事务（含 tag 编码、按 FIFO 大小分块、IN FIFO 每字读 2 字节），然后 `cb_wire_page`/`cb_wire_read`/`writeRmi` 用"写地址（独立事务）+ 读（独立事务）"的 RMI 标准访问。完成后仍用 `TOUCH9` 和 `TOUCH` 行验证。

## 2026-10-30：标准 RMI 帧反而更早失败；双帧取样探针已启动

- fb-rmi 结果（`20261030T110354Z`，boot `0x4e5a3ae7`）：NPA 投票仍成功（捕获块同前），SDA/SCL 仍为 1/1，但触摸在第一次 RMI 读就失败：`error=-30 i2c=0/1`、`status=c960000`（旧帧时是 `1321c4`，且能完成 21 次 ACK）。说明"标准地址帧"在我们这套 QUP 传输上没有正确发出，而旧的 `0x81/0x82/0x87` 帧虽能 ACK 但返回数据不可用。
- 为避免继续在两种帧之间猜（提交 `3eb2419`）：`hardware()` 在 QUP 初始化后、`cb_discover()` 之前，对同一地址（页 0 的 0xE9 PDT 头）分别用两种帧各读一次，把返回码与前 4 字节写进 `cb_touch_extra[24]/[25]`，日志新增 `TOUCH9 plain=.. old=..`（低 32 位=传输返回码，其后每字节依次为数据）。
- 候选 `out/p21/fb-frame`：内核 `8d1367bd…`、payload `4312eaba…`、固件 `094d5d25…`、镜像 `ec30620e…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：`TOUCH9` 里哪一种帧返回的 6 字节呈现合法 PDT（如 fn=0x01/0x12、基址合理）就采用哪种，并据此修正 `cb_wire_read` 或 QUP 的读写时序；若两种都拿不到合法数据，则转向修正 QUP 的 combined write→read 时序（对照 `out/p22/reference/drivers_i2c_busses_i2c-qup.c`）。

## 2026-10-30：触摸供电已解决（NPA 投票成功），RMI 传输按上游修正

- NPA 投票结果（`20261030T104504Z`，boot `0x50d41a7b`）：
  `UP1NPA3 ... cli=/pmic/client/touch_screen cre=0 h=1B44F2ADC req=0` → 客户端创建成功、请求被接受；
  XNU 侧 `TOUCH lines sda=1 scl=1 rst=1 int=0`（此前一直 0/0），`TOUCH2 pins=7`，**I2C 传输 21 次成功 0 次失败**。**触摸供电问题解决。**
- 新的失败点：`TOUCH error=-32 i2c=21/0 F01=0 F12=0` → `cb_discover` 的 `if(!r.f01.query||!r.f12.query)return -32;`，即 PDT 扫描没找到 F01/F12。核对 `out/p22/reference/drivers_input_rmi4_rmi_driver.c`：PDT 字段（buf[0]=query、buf[5]=function number、buf[4]&0x1f=中断数）与扫描方向（0xE9→0x05，步长 6）**我们的实现与上游一致**；差别在 I2C 帧格式。
- 修正（提交 `ab0614f`）：`SynapticsRMI4.h`/`CheeseburgerTouch.cpp` 的 RMI 传输改为上游 `rmi_i2c.c` 的标准帧——页选写 `{0xFF, page}`，读为"写低地址字节 + 读 n"，写为"低地址字节 + 数据"，替换原先自定义的 `0x81/0x82/0x87` 帧。
- 候选 `out/p21/fb-rmi`：内核 `1d2730ad…`、payload `f9bafc4d…`、固件 `ab7dd8d7…`、镜像 `out/p21/fb-rmi/uefi/boot-p21-up1.img` `2c3f9f5f…`（82,587,648 bytes）；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：若 `TOUCH` 行出现 `F01`/`F12` 非零、`stage=4`、`HID>0`，则 RMI 与 HID 都通了，可以开始验证触摸坐标；若仍 `-32`，下一步在固件侧或驱动里直接读 PDT 头 6 字节做原始核对（页面 0 的 0xE9 项）。

## 2026-10-30：DisplayPwr 未安装；从 UsbConfigDxe 拿到 NPA 客户端 ABI，已投票触摸供电

- 上一轮结果（`20261030T103345Z`，boot `0x50872df8`）：捕获块 `UP1NPA2 ... disp=FFFFFFFF get=E set=3` → **DisplayPwr 协议未安装**（`E` = NOT_FOUND），该路径关闭；XNU 触摸仍 `-30`、SDA/SCL 仍 0。
- **关键突破（照抄固件现有调用，不自己写 RPM）**：`UsbConfigDxe` 用同一 NPA 协议（`79D6C870…`）给 `/pmic/client/usb_ss1` 投票，反汇编得到 ABI：
  - `[iface+0x10]` 创建客户端：`(resource_name, client_name, type=0x40, param=0, extra=NULL, &handle)`；
  - `[iface+0x28]` 发请求：`(handle, state_u32)`（USB 用 state=2 表示 active）。
- 据此在固件启动时：先尝试 `/pmic/client/touch_screen`，失败再试 `/pmic/client/disp_prim_touch`，创建成功后发 state=2；结果写入捕获块 `UP1NPA3 ... cli=<name> cre=<status> h=<handle> req=<status>`。仍不调用任何未知偏移、不写 PMIC 寄存器。
- 提交 `6e89bf8`。镜像 `out/p21/fb-diag8/uefi/boot-p21-up1.img` `9eaf0a0d…`；固件 `eb604705…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：若 `cre=0` 且 XNU 侧 `TOUCH lines sda/scl` 变 1，则供电已通、触摸应开始 ACK；若 `cre` 非 0，说明这两个客户端名不是 NPA 资源，需改用 `/pmic/client/<ldo>` 形式的资源名或走 DAL 客户端节点。

## 2026-10-30：NPA/DAL 逆向进展 + DisplayPwr 实验

- 更正上一轮的 GUID 归属（重新核对 `/tmp` 反汇编与二进制）：
  - `79D6C870-725E-489E-A0A1-27E7A5D0CB35` 是 **NpaDxe 安装的协议**（PmicDxe 定位它、变量名叫 NpaProtocol；表在 NpaDxe `.data:0x180F0`，`+0x90 = 0x1864` 是进入 `0x48f0` 的 thunk，参数按对象而非字符串解释——说明它先要资源对象，再建客户端）；
  - `AE37B942-457F-4C91-A196-D9669FD347A3` 由 **DALSys 安装**（`.data:0x1B050`，接口表 `0x1B090`）；
  - `276732AB-BB39-4DCE-B5D9-2746CDD6D518` 也是 NpaDxe 安装（接口表 `0x182A0`）。
- **DALSys 内含现成的电源节点**（`.efi` 字符串）：`/pmic/device/ldo/A/ldo6/vec`、`/pmic/device/ldo/A/ldo28/vec`、`/node/pmic/client/disp_prim_touch`、`/pmic/client/touch_screen`——即固件 DAL 配置里已经知道这两路 LDO 和触摸供电路径；缺的是从我们的 DXE 调用 DAL 的方式。
- 已找到并采用**有本地头文件依据**的接口：`EFI_DISPLAY_POWER_PROTOCOL`（GUID `F352021D-9593-4432-BF04-67B9F3B76008`，结构 `{Revision, SetDisplayPowerState, GetDisplayPowerState}`，来自 `Silicon/Qualcomm/QcomPkg/Include/Protocol/EFIDisplayPwr.h`）。本轮固件在启动时读取显示电源状态，若存在且不为 On 就请求 On，让其面板上电路径（含 `disp_prim_touch` 客户端）执行；结果写入日志窗口第 8161 块（`UP1NPA2 ... disp=.. get=.. set=..`）。
- 分支提交：`717edf5`（NPA 可用）、`57edcfd`（DisplayPwr 探测）。镜像 `out/p21/fb-diag8/uefi/boot-p21-up1.img` `e6bc9806…`；固件 `7b9d2042…`；pins 命中、门禁 PASS；临时 boot 一次 OKAY，未写分区。
- 判读：若回收块里 `disp` 从非 2 变为请求 On 且 XNU 侧 `TOUCH lines sda=1 scl=1`，说明面板上电路径确实包含触摸轨；若仍为 0，则下一步按 NpaDxe/DAL 的对象式 API 继续（需要先找到资源对象获取函数，再 issue request）。

## 2026-10-30：NPA 协议运行时可用（捕获块读取）

- `20261030T102044Z` 的 `window.bin` 第 8161 块（UEFI 写入的保留块）内容：
  `UP1NPA1 npaA=0 ifA=1B57BC090 npaB=0 ifB=1B57660F0 npaC=0 ifC=1B57662A0`
- `npaA/B/C = 0` = EFI_SUCCESS：`AE37B942-457F-4C91-A196-D9669FD347A3`（PmicDxe 称为 NpaProtocol）、`79D6C870-725E-489E-A0A1-27E7A5D0CB35`、`276732AB-BB39-4DCE-B5D9-2746CDD6D518` 在运行时**都已安装**；NPA 接口指针 `0x1B57BC090`。
- 同轮 `20261030T101551Z`（boot `0x510d8b69`）进入桌面 `frames=8`；触摸仍 `-30`、SDA/SCL 仍为 0。
- 结论：**方案 A 可行**——下一轮可按 PmicDxe 的方式（`LocateProtocol(AE37B942)` → `[iface+0x90]` 三参数）创建 NPA 客户端，再找到 issue-request 的 vtable 偏移与 LDO 资源名，向 RPM 请求 L6/L28 上电。
- 通道已固化：UEFI 结果写窗口第 8161 块（绝对 LBA 5071073），无需看屏幕；本轮验证了写入与回收链路都正常。

## 2026-10-30：方案 A 启动——离线确认 NPA/PMIC 接口，fb-diag8 只读探测已启动

- 触摸供电结论（已确认）：Recovery 里原厂内核把 `pm8998_l6`（1,808,000 µV）和 `pm8998_l28`（3,008,000 µV）置为 enabled、各有 1 个使用者；而我们的 fastboot→UEFI→XNU 路径没有任何 RPM 投票，SDA/SCL 在未写之前就是低、~50 kΩ 内部上拉拉不动、8 mA 强驱动能顶高——未上电芯片钳位。AP 的 SPMI arbiter（v3，244 项）看不到 0x40–0x5f 的 LDO 页，**从 AP 直接写 PMIC 不可行**。
- 离线固件分析（`rebuild/ipados-up1/npa-audit/`）：
  - 从 27 个 `.depex` 反推出真实协议 GUID 词表；`AE37B942-457F-4C91-A196-D9669FD347A3` 被 PmicDxe 以字符串“NpaProtocol”定位，且被 AdcDxe/ClockDxe/I2C/SPI/DAL 等 9 个驱动依赖 → 即 NPA 协议；
  - 反汇编 PmicDxe `0x15b3c`：`LocateProtocol(&AE37B942…, NULL, &iface)` 后 `blr [iface+0x90]`，三参数（名称、类型、客户端名）；PmicDxe 内含 `pm_ldo.c`/`pm_ldo_driver.c`/`pm_smps_driver.c` 与 `/protocol/rpm/rpm`，即 LDO 通过 NPA→RPM 控制；
  - NpaDxe `0x1548/0x1594` 安装的是 `79D6C870-725E-489E-A0A1-27E7A5D0CB35`（接口表 `0x180F0`）与 `276732AB-BB39-4DCE-B5D9-2746CDD6D518`（`0x182A0`）；`0x12FC4` 处只定位 `AE37B942`。因此 `AE37B942` 的安装者仍待确认。
- 本轮代码（提交 `d61bbd9`，两轮审计后编译）：`UniXNURails.c` 新增**只读** `LocateProtocol` 探测，启动时打印三行 `UniXNU npa: <GUID> <status> if=<ptr>`；不调用任何接口、不写寄存器。
- 候选 `out/p21/fb-diag8`（内核/payload 与 fb-diag7 相同）：固件 `ad87e327…`、镜像 `out/p21/fb-diag8/uefi/boot-p21-up1.img` `9f05cb55…`（82,591,744 bytes）；pins 命中、探测字符串已确认编入 `UniXNUDxe.efi`（3 处）、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。
- 追加（提交 `fac2475`、`dc2ccf5`）：UEFI 探测结果不再依赖屏幕——`RailWriteCapture()` 把一行 ASCII（`UP1NPA1 npaA=… ifA=… npaB=… ifB=… npaC=… ifC=…`）写进持久日志窗口的第 **8161** 块（绝对 LBA 5071073 / userdata 相对 2629601）。该块属于窗口尾部 31 个内核不使用的块（内核只用 0..8160），按精确块数匹配 userdata（11703808 块）或 LUN0（14145536 块）后才写，找不到就不写。构建时发现并修复 INF 缺少 `gEfiBlockIoProtocolGuid` 的链接错误。
- 判读：屏幕上若出现 `npa: AE37B942 Success if=0x…`，说明 NPA 协议可用，下一轮按 PmicDxe 的调用方式实现 `create_client`/`issue_request`；若全部 `Not Found`，方案 A 需要改走别的协议或退回方案 B（XNU RPM-SMD）。

## 2026-10-30：fb-diag6 结果——写位正确、线路被外部钳低；fb-diag7 驱动实验已启动

- fb-diag6 日志 `20261030T092128Z`（boot `0x5039ae38`，67 轮）：进桌面 `frames=8`；触摸仍 `-30`。
  - `TOUCH5 raw=0 gpiopull=0 muxed=0`：**在任何写之前** SDA/SCL 就是低；用正确的 pull 位（[1:0]）仍拉不高；
  - `TOUCH6 cfg87=c cfg88=c cfg89=2c3 cfg125=3`：mux/上拉/OE 写入**确实生效**（87/88 = mux3、无上拉；89 = mux0/8 mA/上拉/OE；125 = mux0/上拉）；
  - `TOUCH7 post qup0=0 qup400=0 qup408=0 qupc=0`：结合代码路径可知 `transfer()` 在第一次 `state(0)` 就超时返回，后面的 QUP 配置写根本没执行——QUP 状态机没有进入 idle。
- 综合判断（推断）：SDA/SCL 被外部强制钳低（内部上拉无效），同时 QUP 无法完成状态转换。最可能仍是触摸 IO 供电缺失（未上电芯片用 ESD 二极管把两条线钳在 0 V），其次是总线被短接；PM8998 的 regulator 页不归 AP EE，**只能走 RPM 投票**。
- fb-diag7（提交 `fc26e25`）增加两个判定实验：
  1. `TOUCH8 drive=..`：把 gpio87/88 切成 GPIO 输出、驱动高电平约 1 ms 后回读——回读 0 = 外部强钳低（供电缺失/短路），回读 1 = 线是自由的，之前的低电平来自我们自己的配置；
  2. `TOUCH8 bc=..`（GCC_BLSP1_QUP5_BCR `0x21000`）与 `TOUCH3 qup0state=..`（QUP CONFIG + STATE）用于判断 QUP 是否被 block reset 或时钟没有真正生效。
- 候选 `out/p21/fb-diag7`：内核 `84638732…`、payload `a0fa5dba…`、固件 `2d81762e…`、镜像 `out/p21/fb-diag7/uefi/boot-p21-up1.img` `1684dd86…`（82,591,744 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。

## 2026-10-30：fb-diag5 结果 + 两遍代码审计（发现 pull 位错误）+ fb-diag6 已启动

- fb-diag5 日志 `20261030T085525Z`（boot `0x50eb51b4`，76 轮）：进桌面 `frames=9`；触摸 `-30` 不变。
  - `TOUCH5 raw=0 gpiopull=0 muxed=4`；
  - 修正后的 PMIC 行：`PMIC entries=244 sid0=12 pages=1`（只有 `0x61` 一个 0x40–0x7f 页，`vset=00 en=01`）。
- **审计发现（第一遍，已确认的错误）**：
  1. `TOUCH5 raw` 采样点在 I2C 复用**之后**，并非"固件原状"，标签失真；
  2. 内部上拉实验写的是 bits 10-11，而设备原厂 `pinctrl-msm8998.c` 明确 `pull_bit = 0`（bits 1:0）、`mux_bit=2`、`drv_bit=6`、`oe_bit=9`、`in_bit=0`、`out_bit=1`。所以 `gpiopull=0` **不能**证明外部钳位，该实验无效；
  3. QUP `0x0/0x400/0x408/0xC` 在首次传输前读取，必然为 0，信息量低；
  4. 复位脚与中断脚没有按原厂 `ts_reset_active`/`ts_int_active` 的 `bias-pull-up` 配置。
- 时钟/地址复核（第二遍，已确认正确）：`gcc_blsp1_qup5_i2c_apps_clk` CBCR `0x21008` bit0、RCG `CFG_RCGR` `0x21024`、`gcc_blsp1_ahb_clk` `0x52004` bit17（halt `0x17004` bit31）均与设备原厂 `gcc-msm8998.c` 一致；数组边界（`cb_touch[28]`/`cb_touch_extra[24]`/`cb_pmic_dump[24]`/`cb_pmic_pages[8]`/`cb_rails[24]`）互不重叠。
- fb-diag6（提交 `cb27e11`）修复上述四点：raw 采样提前到任何写操作之前、独立 `muxed` 采样、pull 位改为 [1:0]（复位脚/中断脚同时按原厂加上拉）、QUP 高偏移寄存器改在 `fail:` 时（首次真实传输失败后）读取，新增 `TOUCH7 post ...` 行；报告缓冲区 160→200。
- 候选 `out/p21/fb-diag6`：内核 `e0855e4f…`、payload `c3287e0a…`、固件 `b1bed02f…`、镜像 `out/p21/fb-diag6/uefi/boot-p21-up1.img` `bb11e21b…`（82,579,456 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。

## 2026-10-30：fb-diag4 结果 + 代码审计（发现并修复诊断自身 bug）+ fb-diag5 已启动

- fb-diag4 日志 `20261030T083537Z`（boot `0x5143e37f`，73 轮）：进桌面 `frames=9`；触摸仍 `-30`。
- 结果行：`PMIC entries=244 sid0=12`，`PMIC sid0ppid 8 9 24 28 31 61`、`34 35 100061 39 3d 3e`，`PMIC page ppid=61 vset=00 en=01`，另有 0x39/0x3d/0x3e 被误当成页面。
- **审计发现（已确认的自身 bug）**：`cb_pmic_dump[8..19]` 存 SID0 PPID、页面结果又写 `cb_pmic_dump[16..23]`，两者重叠；页面扫描还从错误的槽位（4..15）取 PPID。因此 `0x100061` 是打包值被当成 PPID、`pages=1` 与那三行"页"都是伪数据。修复：新增独立 `cb_pmic_pages[8]`，页面扫描改为遍历真实 SID0 列表（提交 `89ce640` 之后的修复轮）。
- 修正后的可信结论：
  - arbiter v3 表共 **244** 项，SID0 只有 `0x08/0x09/0x24/0x28/0x31/0x34/0x35/0x39/0x3d/0x3e/0x61`（另 1 项被旧 bug 覆盖）；**没有 0x40–0x5f 的 LDO 页** → PM8998 regulator 页不归 AP 这个 EE，**从 AP 侧写 PMIC LDO 不可行**（推断，证据已很强）。
- 上游核对（`out/p22/reference/arch_arm64_boot_dts_qcom_msm8998-oneplus-common.dtsi`）：OnePlus 5 触摸节点为 `syna,rmi4-i2c@20`，`vdd-supply = vreg_l28_3p0`、`vio-supply = vreg_l6a_1p8`；`ts_reset_active` 与 `ts_int_active` 都是 gpio 功能、8 mA、**bias-pull-up**；`syna,reset-delay-ms=20`、`startup-delay-ms=20`。即复位低有效、空闲应被上拉到高；我们释放复位后仍读到低，与 `vio`（L6A 1.8 V）未上电一致。
- fb-diag5（本轮）：修复上述 PMIC 槽位重叠；并新增**三点电平采样** `TOUCH5 raw=.. gpiopull=.. muxed=..`——分别取固件原状、GPIO 输入 + SoC 上拉、切到 BLSP I2C 之后，用来排除"低电平是 QUP 造成的"。
- 候选 `out/p21/fb-diag5`：内核 `4a95f243…`、payload `cf41c9b6…`、固件 `f2af1b9d…`、镜像 `out/p21/fb-diag5/uefi/boot-p21-up1.img` `255dada9…`（82,579,456 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。
- 若 `raw`/`gpiopull` 仍为 0：线路在固件阶段就是低的，供电缺失成立，下一步只能走 **RPM* 投票（AP 侧 PMIC 写放弃）；若 `gpiopull` 变高而 `muxed` 变低，则是 QUP 把线拉低，转向 QUP 配置。

## 2026-10-30：fb-diag3 结果——arbiter 计数来源错误；fb-diag4 已启动

- fb-diag3 日志 `20261030T081640Z`（boot `0x515f1c03`，69 轮）：进桌面 `frames=6`；触摸仍 `-30`。
- `PMIC core0=30000000 core1=3` + `PMIC map0 24b00 51400 51500 51600 800 900 27400 3c000`、`map8 20800 2400 2800 51e00 3100 6100 3400 3500`：
  - arbiter 版本是 **v3**，`core1&0x7ff=3` 不是 APID 数量（之前据此只扫 3 条，才得出"表里没有 0x45"的错误结论）；
  - 按 `entry>>8` 解码，前 16 项里 SID0 只有 `0x008(PON)/0x009/0x024/0x028/0x031/0x034/0x035/0x061`，**没有 0x045/0x05B**——说明 PM8998 的 regulator 页很可能不归 AP 这个 EE，而是 RPM 拥有，从 AP 侧写 PMIC LDO 这条路大概率不通。
- `TOUCH4 pullup sda=0 scl=0 rst=0 int=0`：内部上拉也拉不高（线被钳低）；但该采样点在复位仍为 0 时，需要排除复位态的影响。
- fb-diag4（提交 `89ce640`）修正两点并保留其他诊断：
  1. arbiter 改为**扫到第一个 0 表项为止**（上限 512），日志 `PMIC entries=.. sid0=.. pages=.. core0=.. core1=..` + 两行 `PMIC sid0ppid ...`，并对其中 0x40–0x7f 的页读 VSET/enable（`PMIC page ppid=.. vset=.. en=..`）；
  2. 把内部上拉实验**移到复位释放之后**（`rst=1`）再采样，排除复位态混淆。
- 候选 `out/p21/fb-diag4`：内核 `0c0c8235…`、payload `6253a84b…`、固件 `42eb3d8c…`、镜像 `out/p21/fb-diag4/uefi/boot-p21-up1.img` `e8423e29…`（82,583,552 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。
- 若 sid0 列表仍无 regulator 页，则确认必须走 RPM 投票；若出现且 en=0，则可在 AP 侧写使能位。

## 2026-10-30：fb-diag2 结果——arbiter 解码本身是错的；fb-diag3 原始 dump + 上拉实验已启动

- fb-diag2 日志 `20261030T075820Z`（boot `0x47fe5973`，80 轮）：进桌面 `frames=8`；触摸仍 `error=-30 i2c=0/1`。
- 关键行：
  - `TOUCH2 gcc=1 rcg=0 mode=a5 status=c000000 op=c0 pins=4`——控制器使能后四线仍是 sda=0 scl=0 rst=1 int=0；
  - `TOUCH3 qup0=0 qup400=0 qup408=0 qupc=0 gcc52004=808a9d80 qupver=20070000`（QUP 未开始传输前读，0 属预期）；
  - `PMIC apids=3 revid=0 pages=0 err=2`——**arbiter 的 APID 表解码不成立**（真实表远不止 3 项、PMIC revision 也读不到），因此此前所有 PMIC 读/写（XNU 与 UEFI）都建立在错误地址上，"供电假设"仍未验证。
- fb-diag3（提交 `050f0df`）改为一次性记录原始数据与独立验证：
  1. `PMIC core0..7`：arbiter 版本/特性等原始寄存器；
  2. `PMIC map0/8`：映射表前 16 个原始 32 位表项（用于离线正确解码 PPID/APID）；
  3. `TOUCH4 pullup sda=.. scl=.. rst=.. int=.. after=..`：临时给 gpio87/88 打开 SoC 内部上拉再读取，随后恢复原配置——用来区分"外部上拉电源缺失（线会抬起）"与"未上电芯片把线钳在低位（仍为 0）"。
- 候选 `out/p21/fb-diag3`：内核 `e64d4e44…`、payload `cebefa07…`、固件 `fcf5f88c…`、镜像 `out/p21/fb-diag3/uefi/boot-p21-up1.img` `53b6e179…`（82,595,840 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。

## 2026-10-30：fb-lines 结果 + fb-diag2（一次性全链诊断）已启动一次

- fb-lines 日志 `20261030T073143Z`（boot `0x50006662`，108 轮）：进桌面 `frames=13`；触摸 `error=-30 i2c=0/1`；新增行 `TOUCH lines sda=0 scl=0 rst=1 int=0`——**复位已释放但 I2C 两线空闲为低**，与上拉所在的 `vcc_i2c_1v8`（pm8998_l6）未供电一致；`RAILS2 ppid4x5x=61/62/73/50/74/55/60/54`（列表被截断在 8 个，未覆盖整表）。
- 按用户要求改为**一次把触摸链路的日志加全**（提交 `21e1ed6`），本轮不再逐条追加：
  1. `TOUCH lines`：I2C 复用后、控制器使能前的 sda/scl/rst/int 电平；
  2. `TOUCH2`：GCC BLSP5 CBCR/RCG、QUP mode、控制器使能后的 QUP status/op、以及此时的四线电平；
  3. `TOUCH3`：QUP 0x0/0x400/0x408/0xC、GCC 0x52004、QUP 版本；
  4. `PMIC apids=.. revid=.. pages=.. err=..`：arbiter APID 总数、PMIC revision（PPID 0x001）、0x040–0x07f 段页面数与错误位；
  5. 每个 regulator 页一行 `PMIC page ppid=.. vset=.. en=.. err=..`（最多 12 页，读 +0x40/+0x46），用于直接识别 L6/L28 是否存在及是否已编程/使能。
  本轮仍然**只读**，不写任何 PMIC 寄存器。
- 候选 `out/p21/fb-diag2`：内核 `d1c5b5d2…`、payload `01ac7eda…`、固件 `3c0ce46b…`、镜像 `out/p21/fb-diag2/uefi/boot-p21-up1.img` `15a110f9…`（82,591,744 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。
- 判读：`revid` 能确认 arbiter 读取路径与 PMIC 身份；若页面列表里出现 VSET≈0xE2（1.808 V）与对应 enable bit7，则 L6 实际已开、问题转向 QUP/时序；若全部 en=0 或页面缺失，则供电假设成立，下一步按真实页面写使能或走 RPM。

## 2026-10-30：fb-pmic 结果 + fb-lines（总线电平 / PPID 页）诊断已启动一次

- fb-pmic 日志 `20261030T070854Z`（boot `0x5133a1d8`，71 轮）：进入桌面（`frames=10`），触摸 `error=-30 i2c=0/1` 不变；`RAILS stage=1 l6 apid=-1 vset=00 en=00 l28 apid=146 vset=00 en=00 err=1`。
- 判读：我们假定的 PPID `0x045` 在 arbiter APID 表里**不存在**，`0x05B` 虽命中但 VSET/enable 读到 0，不能证明是 LDO28。由此推断上一轮 UEFI 的 rail 代码同样没有找到 L6、且 L28 因 VSET=0 而未写入——**PMIC 实际从未被写过**，"供电假设"仍未验证。
- 新增只读诊断（提交 `ca75a3e`）：
  1. `CheeseburgerTouch.cpp` 在 I2C 复用后、控制器使能前读取 gpio87/88/89/125 的输入电平，写入 `cb_touch[27]`，日志新增 `TOUCH lines sda=.. scl=.. rst=.. int=..`；
  2. `CheeseburgerButtons.cpp` 在 arbiter 线程里把 APID 表中 `0x040..0x07f` 的 PPID 收集到 `cb_rails[8..15]`，日志新增 `RAILS2 ppid4x5x=..`。
- 候选 `out/p21/fb-lines`：内核 `859faaf7…`、payload `2043a87f…`、固件 `29f54a5a…`、镜像 `out/p21/fb-lines/uefi/boot-p21-up1.img` `f9766c0e…`（82,587,648 bytes）；pins 命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 OKAY，未写分区。
- 判读方式：`TOUCH lines` 若 SDA/SCL 空闲为 0 → 上拉所在 IO 轨没电（供电问题）；若为 1 → 供电基本正常，转向 QUP 时序/地址。`RAILS2` 列出真实存在的 PPID 页，用于定位 PM8998 regulator 页后再考虑写入。

## 2026-10-30：fb-pmic（XNU 只读读取 L6/L28 状态）已启动一次

- 目的：前一轮 UEFI 侧开 L6/L28 后触摸仍是 `-30`，且 UEFI 打印未进持久日志；本轮在 XNU 内用**只读**方式读回两路 LDO 状态，直接把证据写进持久日志。
- 实现（提交 `b4197a7`）：`CheeseburgerButtons.cpp` 在既有 SPMI arbiter 线程里，按 PPID `0x045`/`0x05B` 找 APID，读 PMIC4 LDO 页 `+0x40`(VSET)/`+0x46`(enable)，结果存 `cb_rails[8]`；`BootLog.c` 新增 `RAILS stage=.. l6 apid=.. vset=.. en=.. l28 apid=.. vset=.. en=.. err=..` 行。**不做任何 PMIC 写。**
- 候选 `out/p21/fb-pmic`（基于 fb-rail，保留 UEFI 侧 rail 代码）：内核 `b9125cef…`、payload `bcba2d02…`、固件 `6bb562d9…`、镜像 `out/p21/fb-pmic/uefi/boot-p21-up1.img` `d2374f97…`（82,595,840 bytes）；pins 三项命中、门禁 PASS；目标 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区；监视器已挂起。
- 判读方式：`RAILS` 行里 `en` 的 bit7 若为 1 而触摸仍 `-30`，说明供电已开但 NACK 另有原因；若为 0，说明 UEFI 写入没有生效或供电不在这两路；`err` 位标记 APID 未找到/映射失败/读失败。
- 关于“未进桌面是否与固件改动有关”：本次之前失败轮的签名与 10-26 R17 一致，且固件改动只涉及 PMIC 读/写、不触及显示路径；目前判断为该偶发分支，但需更多成功/失败对照才能定量。

## 2026-10-30：fb-rail 结果——落回已知黑屏分支，触摸 NACK 未变

- 回收 `out/p21/persistent-logs/20261030T064815Z/`：boot ID `0x4f96c356`，140 轮，写入错误 0。
- 显示：`DISPLAY clients=5 frames=0`、`SWAP check=0 rc=ffffffffffffffff`、`[fb events]` 只有 `s6 (0,0x80000003)` 与 `s50`，没有 `s4/s5`——与 `050326Z`/`052133Z` 相同的**首帧前黑屏分支**；本次与固件改动无因果证据（该分支同镜像会翻转，见前几条记录）。
- 触摸：与 fb-touch **逐项相同**：`error=-30 i2c=0/1`、`reset=1`、`hw=0x20070000`、`status=0x1321c4`、`op=0x90`。说明 UEFI 侧对 L6/L28 的读/写**没有改变触摸结果**：可能是写入未生效、RPM 覆盖，或供电不在 L6/L28。
- 缺口：UEFI 的 `UniXNU rails:` 行只出现在屏幕控制台，持久日志只含内核控制台尾部，未捕获；本轮不能判断 UEFI 代码是否找到 APID、是否写入成功。
- 下一步（尚未执行，建议）：在 XNU 板级运行时增加**只读** PMIC 探针——用与按键驱动相同的 arbiter 读路径，按 PPID `0x045`/`0x05B` 找 APID，读 `+0x40`(VSET)/`+0x46`(enable) 并写进持久日志；这样不依赖 UEFI 屏幕输出，可直接判断供电状态与 UEFI 写入是否生效。不做任何 PMIC 写。

## 2026-10-30：fb-rail（UEFI 打开 pm8998 L6/L28）已启动一次

- 目标：触摸在 `reset=1` 后第一次 I2C 传输被 NACK，判断为传感器未供电；按用户选择在 **UEFI 侧**打开 `pm8998_l6`（ldoa 6，1.808 V）与 `pm8998_l28`（ldoa 0x1c，3.008 V）。
- 实现（提交 `8e4f3f4`，回滚 tag `rollback-before-fb-rail` = `390a608`）：`rebuild/ipados-up1/fb-rail/UniXNURails.c` + `apply.py`，装进项目自有的 `UniXNUPkg/Driver/UniXNUDxe`，在 BDS 之前执行：
  - 通过 SPMI PMIC arbiter（core `0x0800f000`、chnls `0x08400000`、obsrvr `0x09400000`，来自设备 FDT）按 PPID `0x045`/`0x05B` 找 APID；
  - 先只读 VSET（`+0x40`）与 enable（`+0x46`）；仅当 VSET 非 0 且 bit7 为 0 时置 bit7，随后回读并打印 `UniXNU rails: ...`；
  - 命令格式依据上游 sheng 固件 SPMI 读实现与 Linux `spmi-pmic-arb.c` v2 `(opcode<<27)|((addr&0xff)<<4)|(bc&7)`；不读/写其它 PMIC 寄存器、不改电压。
- 候选 `out/p21/fb-rail`：内核/payload/ADT 与 fb-touch 完全相同（`96102686…` / `09c1e9b1…` / `775d411b…`），仅固件不同；FD `952c603f…`，镜像 `out/p21/fb-rail/uefi/boot-p21-up1.img` `87b07b0c…`（82,591,744 bytes）；pins 三项命中、门禁 PASS；FVMAIN 内确认含 `UniXNU rails` 字符串；目标 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区。
- 验收：若供电确实缺失，下一轮 `TOUCH error` 应从 `-30` 变为 0 或更后面的阶段（F01/F12 出现）；若仍是 `-30`，说明供电不在 L6/L28 或写入未生效，需读回 `UniXNU rails` 控制台行判断。

## 2026-10-30：fb-touch 进桌面正常，触摸停在 I2C NACK（供电）

- 回收 `out/p21/persistent-logs/20261030T061244Z/`：boot ID `0x4feb4fc9`，86 轮，写入错误 0；`DISPLAY frames=14`、`SWAP check=14 transfer=14 rc=0`，用户确认进入桌面；`[fb events]` 与 fb-diag 相同（`s8 out2=438/780` → t≈377 s `s4/s5` 成对），说明触摸改动没有改变显示分支。
- 触摸进度（已确认）：`reset=1`（GPIO89 回读高，复位已释放）、`hw=0x20070000`（QUP v2）、`clock=0`（GCC XO 配置检查通过）、`gpio=0/e2`、`status=0x1321c4`（含 bit8，NACK）、`op=0x90`；`error=-30 i2c=0/1`——第一次 I2C 传输被从机 NACK。
- 判读：地址 0x20 在 Recovery 2.4 s 能正常探测（`out/p21/phone-ui/evidence/recovery-touch-dmesg.txt`，固件 ID `0xeb101035`），所以不是地址/接线/复位问题，最可能是**触摸供电未开启**。
- 供电事实（设备自身 FDT）：`pm8998_l6`、`pm8998_l28` 是 **RPM-SMD regulator**（`qcom,rpm-smd`，`resource-name=ldoa`，`resource-id=6 / 0x1c`，1.808 V / 3.008 V，`qcom,set=3`），不是可直接用 SPMI 打开的 LDO 节点；当前 fastboot → UEFI → XNU 路径没有任何 RPM 投票。
- 下一步（三选一，均需新候选与门禁）：
  1. **UEFI 侧**在 ExitBootServices 前用固件自带的 PmicDxe/SPMIDxe 打开 LDO6/LDO28（改动最小；本机固件含这两个 DXE）；
  2. **XNU 侧**直接写 PM8998 LDO 寄存器（需要 PMIC4 寄存器表；RPM 可能覆盖，风险较高，不推荐先做）；
  3. 实现 RPM-SMD 请求（最正统，工作量最大）。
- 验收：`TOUCH i2c` 计数增加且 `error` 不再是 `-30`、F01/F12 出现，随后 HID 安装；触摸方向仍待实机核对。

## 2026-10-30：fb-touch（成功版 + S3320 复位修复）已启动一次

- 在实机验证可进入桌面的 `fb-diag` 基础上同步触摸修复（`CheeseburgerTouch.cpp`：GPIO89 mux 0、输出电平写 IO bit 1，提交 `b42b68a`），其余源码、ADT（`775d411b…`）、启动参数（644 字节）与 `fb-diag` 逐字一致。
- 候选 `out/p21/fb-touch`：内核编译 `6507872e…` → 预链接 → panic-screen 后 `96102686…`；payload `09c1e9b1…`；固件 `486db499…`；镜像 `out/p21/fb-touch/uefi/boot-p21-up1.img` `30345d4f…`（82,591,744 bytes）；pins 三项命中、门禁 PASS。目标 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区；监视器已挂起。
- 预期：若 `TOUCH error` 从 `-54` 变为 `-30`/`i2c>0`，说明复位修复在这条基线上同样生效、且停在触摸芯片未上电（pm8998 l6/l28 未在 iOS 运行时开启）。同时验证触摸改动不会改变显示分支（`[fb events]` 仍应出现 `s8 out=438/780` 与 `s4/s5` 成对）。
- 观测窗口：成功轮首个 `sel 4` 在 t≈370 s，建议跑满 ≥600 s。
- 已推送 `main` 至 `origin`（`35936aa..1d6cd0e` 之后的本轮提交以本地为准）。

## 2026-10-30：fb-diag 成功进入桌面（14 帧），事件环拿到首个成功样本

- 回收 `out/p21/persistent-logs/20261030T055325Z/`：boot ID `0x50c0cf75`，79 轮，写入错误 0；`DISPLAY clients=57 frames=14`、`SWAP check=14 transfer=14 rc=0`、`EXITS 696`；用户报告**成功进入桌面**。
- `[fb events]` 首个完整成功序列（`cntpct/cntfrq` 秒）：
  - `E00 t=182.17 p94 s8 out2=438/780 rc=0`（尺寸查询返回 1080×1920）
  - `E01 t=182.22 p56 s15 in2=0/10 out1=0 rc=e00002bc`、`E02 t=182.22 p56 s8 out2=438/780 rc=0`
  - `E03 t=213.56 p56 s6 in3=0/80000003/0 rc=0`、`E04 s50 in2=10000/0 rc=e00002c7`
  - 静默约 157 s 后 `E05 t=370.65 p56 s4 out1=1 rc=0`、`E06 s5 rc=0`，随后 `s6 (n,1)/(n,80000000)` + `s4` + `s5` 成组重复，`s5` 每次 rc=0，14 帧与 14 次 `s4/s5` 对应。
- 结论（已确认）：selector 8 返回正确尺寸；失败分支缺的不是尺寸或 selector 50，而是 `s6` 之后的配置与 `s4/s5` 提交。失败轮在 `sel 6` 首次查询后停下，成功轮在 t≈370 s 才出现首个 `s4`。下一轮可在失败日志里对照 `[fb events]` 缺失的位置定位等待点。
- 本轮无源码改动，仅诊断候选 `out/p21/fb-diag`（镜像 `bf6d86d7…`、内核 `29d3a758…`）被实机验证可进入桌面；该镜像作为新的对照基线保留。

## 2026-10-30：fb-diag 诊断候选（selector 事件环）已启动一次

- 依据 `[mfb clients]` 全量行与 `[display state]` 得到的新分界：成功轮 backboardd 的 `sel 6` 完成 `(n,0x80000001,0) → 0` 序列，把显示对象 `[5820]/[5824]` 累加到 `0xe`（d2）/`0x2b`（window2g），`(n,0x80000000,0)` 返回 `0xe00002d5`(Busy)，随后 `sel 4`/`sel 5` 各 14 次成功；失败轮 `sel 6` 只调用一次 `(0,0x80000003,0) → 0`，没有 4/5，`[display state]` 的 `5816/5820/5824/5828` 全 0，`[display]` 的 `Current Display` 为空。剩余未知量是 `sel 6`/`sel 8` 的 scalarOutput。
- 上游依据：sheng `package/MobileDisplayProviderProbe.cpp` 用 `selector 8 + scalarOutputCount=2` 读宽高，证明 selector 8 是尺寸查询；本机 `LegacyDisplayProbe.cpp` 的客户端拦截与 sheng 同源。
- 新增只读诊断（提交 `a6651e8`）：32 槽事件环记录 selector 4/5/6/8/15/50 的时间（cntpct/cntfrq）、pid、客户端代际、至多 4 个 scalarInput/scalarOutput、原始返回与丢弃数；报告新增 `[fb events]` 段（≤2560 字节）。不改变任何返回值，不新增睡眠/分配/锁。
- 候选 `out/p21/fb-diag`（自 d2 复制，ADT `775d411b…`、参数与 d2 逐字相同）：内核编译 `466280ad…` → 预链接 `7b19d6ab…` → panic-screen `29d3a758…`（125,698,048 bytes）；payload `b44f0ad3…`；固件 `d131c854…`；镜像 `out/p21/fb-diag/uefi/boot-p21-up1.img` `bf6d86d7…`（82,567,168 bytes）；pins 三项命中、门禁 PASS（644 字节）。目标 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区；监视器已挂起。
- 观测窗口：历史首帧 307/318/524 s，建议跑满 ≥600 s。回收后比较 `[fb events]` 里 `sel 8` 与首个 `sel 6` 的 out 值；若与成功轮相同，则排除“provider 返回不同”，转向通知/等待路径。

## 2026-10-30：黑屏分析勘误与旧镜像复跑

- 用户回到 Recovery 后回收 `out/p21/persistent-logs/20261030T052133Z/`，boot ID `0x4bd8fea6`、962 秒、192 轮、写入错误 0；最新已记录的启动是 touch-window2g `boot-once2.log`（Fastboot 接受；日志本身不证明镜像哈希）。本轮 `stage=10 clients=5 frames=0`、SWAP/prepare 均 0；backboardd 的 15 次调用中 selector 6/8/88 成功，12 等多个 selector 返回 `e00002c7`，15 返回 `e00002bc`。
- **更正下方及报告的 NotOpen 判断**：同版本 XNU `iokit/IOKit/IOReturn.h` 定义 `0xe00002c7` 为 `kIOReturnUnsupported`，`kIOReturnNotOpen` 实际是 `0xe00002cd`。不能由 `e00002c7` 判断 open 失败或发布时序竞态。
- 更关键：成功的 d2 日志 `20261029T172431Z` 的 backboardd selector 50 也返回 `e00002c7`（2/2 失败），同时 selector 4/5 均成功 14 次并显示 14 帧；所以 selector 50 的 unsupported 不是黑屏特有签名，更不能单独作为根因。上游 `.work/p21-official-framework/scripts/check-system-framebuffer-client.py` 也将其原版 frame-info 查询的 `e00002c7` 作为 unsupported 记录，但未证明本机 selector 50 的语义。
- 新旧黑屏对照仍支持问题发生在观察到的首帧提交之前；`stage=10`、`newUserClient` 成功和显示服务匹配不证明所有显示/用户态状态相同。采样仅最多保留 8 个客户端，不能从 0/0 推断进程从未通过其他对象调用。触摸、身份与时序均未被全面排除。下一步应分析 backboardd 为什么在初始查询后没有进入 selector 4/5，优先符号化已有线程栈、关联客户端调用与用户态依赖状态；不应盲改 open 返回码或屏蔽服务。设备已回到 Recovery，未重新启动或写入分区。

## 2026-10-30：黑屏分支的日志分析（framebuffer 客户端 open 失败）

- 失败轮的共同签名：某个显示客户端（backboardd / `IOMFB_bics_dae` / `IOMFB_FDR_Load`）最后一次外部方法调用是 **selector 50，返回 `0xe00002c7`（kIOReturnNotOpen）**，且 backboardd 的帧调用为 `in/out=0/0` 或极少；成功轮 backboardd 最后是 **selector 5（swap_submit）rc=0**，`in/out=77/77` 或 `194/194`。失败轮 `prepare calls=0`、`SWAP rc=ffffffffffffffff`，即一次帧都没提交。
- 内核侧显示发布两轮一致（`IOMobileGraphicsFamily` 注册时间 74 s vs 66 s、`NativeLegacyDisplay` 注册、`DISPLAY stage=10 meta=0 reg=1`），所以差异不在内核显示初始化，而在用户态客户端连接时序。
- 用户态侧差异：失败轮进程退出 599/618/678 次、SpringBoard 8 线程；成功轮 1088 次、17 线程。失败轮 msgbuf 里 datamigrator/语言服务/bluetoothd 反复重启。首帧历史：307 s、318 s、524 s；失败轮 881/977 s 仍为 0。
- 上游核对：官方 `docs/reference/troubleshooting.md` 明确写“Firmware stalls → Random Silicium stall → Start the same image again”，承认随机卡住需重试；官方 sheng（同为 iPadOS 17.7.11/21H461、iPad7,1）`board.toml` 与本机的差异：`offline-legacy-unit-scale=2`（本机 1）、`offline-mobile-graphics-client=1`、`sheng-disable-jobs=com.apple.identityservicesd,com.apple.locationd,com.apple.CommCenter,com.apple.bluetoothd`、`boot-ui-focus3/4`（仅诊断用途）、`debug=0x14e`、`kt_fixed_zones=120`、`-apfs_shared_datavolume`。上游没有“NotOpen 黑屏”的直接案例，也没有可直接合并的修复。
- 待验证的下一步：给 `wrappedExternal` 增加 selector 50 与 open/close 的一次性记录（pid/时间/rc/当时的 `native_legacy_stage`），确认失败轮是否发生在显示服务发布之前；若成立，再考虑让 provider 更早发布或让 open 路径返回可重试状态，而不是直接失败。

## 2026-10-30：同一张 d2 复现失败——镜像代码不是变量

- 回收 d2 复跑日志 `out/p21/persistent-logs/20261030T050326Z/`（boot ID `0x50e2b156`，176 轮，写入错误 0）。同一张 `1999371c…` 镜像：首次 `20261029T172431Z` 为 `clients=115 frames=14`、`EXITS 1088`、backboardd 打开 FB `77/77`；复跑为 `clients=5 frames=0`、`EXITS 599`、backboardd FB `0/0`，`FB0 IOMFB_FDR_Load 15/15 enter/leave=50/50 rc=e00002c7`（kIOReturnNotOpen）、`FB2 MobileGestaltH rc=e00002f0`。
- 因此“触摸改动导致黑屏”与“手机身份导致 frames=0”两个判断都被证伪：镜像、payload、ADT、参数都相同的 d2 会两种结果都出现。触摸代码只影响触摸。
- 时间线：三次出画面的 run 首帧分别在 t≈307 s（`164350Z`）、t≈318 s（`172431Z`）、t≈524 s（`161111Z`）；两次黑屏 run 等到 881 s / 977 s 仍 `frames=0`、`prepare calls=0`、`SWAP rc=ffffffffffffffff`（从未提交）。failure 端 `IOMobileFramebuffer` 客户端反复拿到 `kIOReturnNotOpen`，表明用户态与 framebuffer 的握手在按启动时序分支，而不是在像素路径上失败。
- 三份 msgbuf 对比：APFS 数据卷都完成挂载（disk0/disk1 命名不同）、kext 加载序列一致、SpringBoard 在两种结果下都以约 1390 次/秒 CPU 唤醒自旋；黑屏 run 只有 8 个线程、正常 run 17 个。
- 按用户要求再往前启动 `out/p21/touch-window2g/uefi/boot-p21-up1.img`（`29029186…`，iPad7,1，上次 39 帧），`out/p21/touch-window2g/boot-once2.log` Sending/Booting OKAY；观察窗口按首帧历史（≥300 s）放宽到 ≥480 s。

## 2026-10-30：touch-fix2 回滚到 d2，全链差异核对

- 用户选回 `out/p21/phone-ui-d2/uefi/boot-p21-up1.img`（`1999371c…`）并已临时启动（`out/p21/phone-ui-d2/boot-once2.log`，Sending/Booting OKAY）。此前 touch-fix2 轮日志由监视器回收：`out/p21/persistent-logs/20261030T044653Z/`，boot ID `0x4bfec417`，216 轮，写入错误 0。
- touch-fix2 触摸确有推进：`error=-58` → `error=-30`、`i2c=0/1`、`hw/clock` 已过；说明 mux 0 + 输出 bit1 的复位修复生效，I2C 已发起第一次传输但在 `cb_discover` 首读失败（`SynapticsRMI4.h` 返回 `-30`）。触摸芯片此时仍未上电（pm8998_l6 1.808 V / l28 3.008 V 在 iOS 运行时未确认开启），下一步应先在板级开启这两路供电再谈 HID。
- 显示仍未出帧：`DISPLAY clients=5 frames=0`、`SWAP check=0`、`prepare calls=0`；用户报告黑屏。
- **全链对比（d2 可用版 vs touch-fix2 黑屏版）**：
  - payload 内 `unixnu.toml`、`kernel.macho.adt`、`kernel.macho.trustcache`、`scratch-data.img` 哈希完全一致；启动参数 644 字节逐字一致；仅 `kernel.macho` 不同（`fe6be784…` vs `031cf674…`）。
  - 板级运行时源码 `package/BoardRuntime` 只差 `CheeseburgerTouch.cpp` 一个文件；显示映射（`kIOMapInhibitCache`）、`BootLog.c`、按键代码完全相同。
  - 构建方式同为“官方 framework kernel.build → `mach.development.bcm2837.unstripped.noctf` 预链接 → panic-screen → payload → 独立 overlay 全量固件 → pins → pack → 门禁”；仅 overlay 名不同（`unixnu-up1-phone-d2` vs `unixnu-up1-touch-fix2`）。内核 `__text` 尺寸 d2 `0x7fb534`、touch-fix2 `0x7fb564`，差 0x30，与只改触摸函数一致。
- 结论：**此前“手机身份导致 frames=0”的判断撤回**——d2 同为 iPhone10,3/D22 且有 14 帧。现有证据只把差异收敛到触摸代码；但黑屏轮的用户态签名与触摸改动之前的 `20261029T165823Z` 相同（clients=5、frames=0、FB2 15/15 rc=e00002c7、FB3 MobileGestaltH），且黑屏轮 msgbuf 显示 datamigrator/语言服务/蓝牙在持续运行与重启，说明用户态在推进但没有提交帧，不能仅凭日志断言触摸驱动直接导致黑屏。
- 下一步建议：显示工作以 d2 为基线；触摸改动以启动参数开关隔离（默认关闭），避免再次影响显示；开启前先补 pm8998 l6/l28 供电。

## 2026-10-30：touch-fix2（GPIO89 mux/输出位）与黑屏记录，回滚待选

- 用户反馈 touch-drive（`db1385a3…`）黑屏、进不了系统。回收 `out/p21/persistent-logs/20261030T040140Z/`：boot ID `0x4f5bb00c`，927 秒 / 185 轮 / 写入错误 0；`DISPLAY clients=5 frames=0`、`prepare calls=0`、`SWAP rc=ffffffffffffffff`、`TOUCH error=-58`。
- 该签名与**触摸改动之前**的手机身份首启 `20261029T165823Z` 逐项一致（clients=5、frames=0、`FB2 backboardd 15/15 enter/leave=50/50 rc=e00002c7`、`FB3 MobileGestaltH`、`FB0` 独占）；因此现有日志不能单独证明黑屏由触摸改动造成，此点保持未定论记录。
- 触摸根因（设备 FDT `ts_reset_active` + Linux pinctrl-msm）：gpio89 的复用表只有 `gpio/tsif0/phase_flag`，复位必须 mux 0，原代码写的是 mux 3（I2C 的 mux 3 只属于 gpio87/88）；IO 寄存器 bit1 才是输出电平、bit0 是输入回读，原代码写 bit0，pad 实际从未拉高，故 `-58`。
- 修复提交 `b42b68a`。候选 `out/p21/touch-fix2`（自 touch-drive 复制，ADT 保持手机身份 `775d411b…`）：内核 `031cf674…`、payload `3ac2df84…`、固件 `66868a03…`、镜像 `out/p21/touch-fix2/uefi/boot-p21-up1.img` `80c8b19e…`（82,583,552 bytes）；pins 三项在 P9Probe/DarwinBoot/P4RamPayload 命中，门禁 PASS（644 字节参数），目标 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区。
- 实机结果：用户报告仍未进入系统界面。该修复源码仅保留作比较，不进入回滚候选。

### 可回滚的“最近一次实机确认能进系统”候选（待用户选择）

1. `out/p21/phone-ui-d2/uefi/boot-p21-up1.img` — SHA-256 `1999371cccd22d6b5b36bea283cf596c0f04de63d91c527806da59cf88d25da3`（01:13）；手机身份 iPhone10,3/D22 + `/buttons`（hold/voldown/volup、home-button-type=2、thin-bezel=1）+ `BootLog.c` DISPLAY SCALE 诊断；用户确认进入系统画面（平板布局、触摸无效），日志 `20261029T172431Z` 有 14 帧。
2. `out/p21/touch-window2g/uefi/boot-p21-up1.img` — SHA-256 `290291868afdf7c325438602b3ed77feabc4ebde8c922fe0839216aebc12f59f`（00:33）；iPad7,1 身份 + P23 高内存窗口（`darwinboot-memory-range=0x100000000+0xbd8c0000`、`PcdSystemMemorySize=0x13D8C0000`）；用户确认显示原始 iPadOS 界面，日志 `20261029T164350Z` 有 39 帧。

## 2026-10-30：iPadOS 切换为 main

- `main` 合并保留原 iOS 分支历史，合并后的代码树以 `ipados-17.7.11` 的 `4dc3167` 为准；原 iOS 工作保留在 `ios-16.7.16`。README 调整主线说明及组件状态，硬件源码、参数与镜像未因此更改。
- 切换前 Git 完整 bundle 和未跟踪文件副本保存在 `out/repository-backups/20261030-before-ipados-main/`，bundle 校验通过；不包含被忽略的系统镜像和生成物，它们仍在原位置。设备和构建状态以其后记录为准。

## 2026-10-30：touch-drive 修正重建（复位脉冲 + 构建链修复）

- 触摸：GPIO89 改为厂商复位序列——mux 0、上拉、8 mA，先写锁存低→置 OE→2 ms→锁存高→50 ms→回读；仍低返回 `-58`。
- 构建根因：resolved 树的 `kits/cheeseburger` 软链接原先指向 **d2 的 package**，所以候选树里的触摸改动从未进入内核；`p21-build-board-runtime.py` 增加 `P21_BOARD_ROOT` 覆盖，驱动把它指向候选的 `source-tree`，并把 `kits/cheeseburger` 重指到候选 `package`。链接阶段的 build-report 源码哈希校验因此通过。
- 重新整链：内核 `6a01d1f2`（编译）→ unstripped 预链接 → panic-screen 后 `bbbb2099`；payload `0d4f0932`；固件 `f4c98f7e`（独立 overlay，增量）；三项 pins 通过；镜像 `db1385a3`，门禁 PASS（644 字节）。
- 尚未启动。

## 2026-10-30：touch-drive 候选（S3320 复位拉高）

- 修复：`package/BoardRuntime/CheeseburgerTouch.cpp` 在输入上拉仍读到低电平时，改为置 OE、驱动 GPIO89 高（ACTIVE_LOW 复位释放）并回读；仍低则回退并返回 `-58`（提交 `5f299f0`）。
- 构建（按本仓库流程）：候选树 `out/p21/touch-drive/source-tree`（自 d2 复制）内跑官方框架 `kernel.build`（`scripts/p21-compile-touch-drive.py`，内核 `db6e18f4…` 编译通过）→ 取 `mach.development.bcm2837.unstripped.noctf` 预链接（`d440a90c…`）→ `patch-xnu-entry-probe.py --panic-screen --no-reset`（最终内核 `9efd466e…`）。
- payload `571e414a…`；固件在**独立新 overlay** `/home/dinnerabc.guest/unixnu-up1-touch-drive`（lower 叠加 d2 及以下历史层）全量重编，FD `9d2fce81…`，三项 pins 在 P9Probe/DarwinBoot/P4RamPayload 中核对通过；镜像 `out/p21/touch-drive/uefi/boot-p21-up1.img` `55c694fb…`（82,595,840 bytes），P21 门禁 PASS（644 字节参数）。
- 尚未启动。下一轮重点：日志 `TOUCH error` 是否不再是 `-54`（期望 0 或 `-58`）、`reset=` 值、I2C/F01/F12/HID 是否开始计数；显示与 scale 应与 d2 相同（frames>0）。

## 2026-10-30：手机布局上游对照与本轮日志

- 设备回到 Recovery 后回收 `out/p21/persistent-logs/20261029T172431Z/`：新 boot ID `0x5a0ea281`，511 秒、102 次记录、写入错误 0；用户报告已进入系统但仍为平板布局、触摸不可用。
- 实际 `DISPLAY SCALE status=0 calls=1 chosen=1`，`frames=14`、`SWAP check=14 reject=0 transfer=14`；日志记录 `1242x2208 -> 1080x1920`。这是帧像素尺寸与运行时发布的属性，不是对 UIKit 实际 idiom 或逻辑尺寸的直接测量。触摸仍 error=-54、I2C=0/0。
- 官方 sheng 配置 `offline-legacy-unit-scale=2`，但为 iPadOS 平板配置；ios_sagit 使用 iPadOS、声明已到桌面，现有文字不足以确认其实际布局和 scale；vphone-cli 的 iPadOS 文档说明设备身份与 display-scale 共同影响呈现，不能把其虚拟设备参数直接当成本机配置。当前 phone 身份搭配 iPadOS 根和 chosen scale=1 均为需要进一步核对的差异，尚无证据证明任一项是唯一原因。
- 下一步优先读取实际 MobileGestalt idiom、有效 UIScreen scale 和逻辑 bounds，并对照同版本可复用的手机布局案例；scale=2 可以作为独立对照实验，不能当成已验证的手机布局修复。未经定位不清除 Data。设备保持 Recovery，本轮未启动或写入分区。

## 2026-10-30：手机身份诊断版画面反馈

- 用户随后报告 `out/p21/phone-ui-d2/uefi/boot-p21-up1.img` 已进入系统画面。用户进一步确认布局仍是平板样式且触摸不可用；按键是否可用尚无反馈，本轮新 boot ID 日志尚未回收。设备的实际连接状态应在下次操作时重新确认。

## 2026-10-30：后续开发交接

- 项目简介按官方设备仓库结构更新，后续 DeepSeek 接手入口为 `docs/DEEPSEEK_HANDOFF.md`，包含当前候选、已确认结果、日志和构建路径、手机界面、触摸、按键及启动限制。
- 交接时系统 ADB 服务为 `tcp:localhost:5037`，目标未在 ADB 或 Fastboot 枚举；手机实际画面未知。`phone-ui-d2` 本轮仍待用户反馈及新日志，不应以 Fastboot 成功推断进入系统。

## 2026-10-30：手机身份按键及显示 scale 诊断

- 基于 phone-ui 的 ADT 补充 `/buttons` 的 hold/voldown/volup、home-button-type=2 和 product thin-bezel=1，保留一加 5 已有电源/音量和双音量模拟 Home 处理；尚未验证物理交互或混合 iPadOS 根是否支持手机布局。
- `BootLog.c` 新增实际 `DISPLAY SCALE status/calls/chosen/rotation` 快照，其他显示、SWAP、线程栈和触摸诊断保留。固定官方设备代码会在 phase4 时按 `offline-legacy-unit-scale=1` 发布 chosen display-scale；静态 ADT 中缺失不代表运行时缺失。
- `out/p21/phone-ui-d2/uefi/boot-p21-up1.img` SHA256 `1999371cccd22d6b5b36bea283cf596c0f04de63d91c527806da59cf88d25da3`；重编内核、载荷、固件，编译时 pins、FV 模块与离线门禁通过，目标设备 3b25cc7c 临时 boot 一次 Sending/Booting OKAY，实际画面待用户观察；未写分区。上一轮日志已回收。
- Data 种子本地只读挂载被 hdiutil 拒绝，未确认缓存是否存在，未清理缓存或手机 Data；用户允许必要时清理，应在定位明确后限定目标操作。用户打算延长等待，返回 Recovery 后回收新 boot ID 日志并检查 DISPLAY SCALE / SWAP / 栈。触摸 error=-54 仍未修复。源码提交 `5a03785`、`7c4dfe6`。

## 2026-10-30：手机身份与官方规范复核

- 固定 UniXNU `16e592abee8a550dec8154ed1f38b5ebc4f16213` 的 `docs/guide/mobilegestalt.md` 要求 iPadOS 根配 pad、iOS 根配 phone；当前手机候选为用户要求的 phone 身份 + iPadOS 根实验，与该推荐不一致，不能据此断言成功或失败的具体原因。
- 实际 `out/p21/phone-ui/adt/kernel.macho.adt`：model iPhone10,3、target D22、subtype D22AP、product iPhone X、idiom phone、artwork subtype 2436、scale 3、fallback iPhone9,2；这些字段与本地 P23 ADT 一致，root compatible 保持 `device-tree\0qcom,msm8998\0`。候选无 `/buttons` 节点、无 `home-button-type` 和 chosen `display-scale`；官方全面屏示例需要无 menu 且 home-button-type=2，实际一加 5 物理按键不能直接套无 Home 机型。P23 有 buttons 但无 home-button-type。手机候选未清 MobileGestalt 缓存，实际系统读取值尚未验证。
- 上游 main 当前 iPadOS profile 增加了 product-id；本机固定版本及两个候选均没有。该值为 J120 特定，不能用作 iPhone 值。实际日志 579s SWAP=0/frames=0，未证明上述任何差异为根因；优先保持既有日志，查实际 MobileGestalt、用户栈和显示提交之前的等待。

## 2026-10-30：手机身份候选日志

- 用户反馈手机身份候选未出现系统画面。Recovery 回收 `out/p21/persistent-logs/20261029T165823Z/`，新 boot ID `0x4dd45577`，579 秒、116 记录、写入错误 0。`SpringBoard p27`、`backboardd p56` 在运行，八核在线，VM managed=2048MiB；显示 clients=5，帧、SWAP 检查及传输均为 0。UI 重点进程无已捕获致命退出。
- 相比之前 iPad 身份实机 39 帧，本轮未观察到任何提交，现有证据不足以归因屏幕缩放或判定 identity 为唯一原因；应分析用户态提交前等待、身份匹配及缓存，并保留日志里的线程栈。触摸仍 -54/reset=0/I2C=0，无新触摸修复。
- 设备当前 Recovery，未进行新启动；保留原先 `out/p21/touch-window2g` 可进入系统的候选。

## 2026-10-30：手机界面身份实验与触摸日志

- 用户确认 `out/p21/touch-window2g` 进入系统但触摸无响应。回收日志 `out/p21/persistent-logs/20261029T164350Z/`，boot ID `0x5c58198d`、94 rounds、0 写入错误；`online=8`，39 帧；触摸 `error=-54 reset=0 i2c=0/0`，F01/F12/HID 均未启动，说明上拉不足，尚未证明原因是电源还是复位线被拉低。Recovery 只读检查显示 pm8998_l6 1.808 V 和 pm8998_l28 3.008 V enabled；这仅证明 Recovery 状态。原厂 Recovery dmesg 保存在 `out/p21/phone-ui/evidence/recovery-touch-dmesg.txt`。
- 用户授权在原 iPadOS 上尝试手机界面：复制 P23 iPhone10,3/D22 的 model 和 product identity，保留 OnePlus compatible、原硬件按键、iPadOS root、8 CPU、2 GiB 和触摸内核；没有修改 Data 或清缓存。官方建议身份与系统系列一致，这次为有风险的界面实验，效果未证实。
- `out/p21/phone-ui/uefi/boot-p21-up1.img` SHA-256 `53b8f0dfd837f3d1903dbde20fe6fd3e6f9186097681194c8676799a0cac452d`；payload/UEFI 重编、pins 和离线门禁通过；目标设备临时 Fastboot boot 一次 Sending/Booting OKAY，运行结果待观察，无分区写入。源码 `rebuild/ipados-up1/phone-ui/` 提交 `e59a5a2`。

## 2026-10-30：恢复 P23 高内存窗口

- 用户报告上一触摸/8 核/2 GiB 候选疑似 `NO WINDOW`。Recovery 窗口 `out/p21/persistent-logs/20261029T163303Z/` 与前轮相同，boot ID `0x55da2956`，未出现本轮记录；这不能单独证明内核未执行。
- P23 使用 `darwinboot-memory-range=0x100000000+0xbd8c0000`、`PcdSystemMemorySize=0x13d8c0000` 及 DarwinBoot 范围分配支持；前版 iPadOS 只带了 2 GiB size，未带范围和固件支持。本机 FDT RAM bank 证据见 `docs/P4_ADT_BRINGUP.md`。恢复 P23 局部补丁，并强化 AllocateAnyPages 的范围与 2 MiB 对齐检查；仍只分配 UEFI 报告的可用页。范围 host test 通过，实机成功未证实。
- 候选 `out/p21/touch-window2g/uefi/boot-p21-up1.img` SHA-256 `290291868afdf7c325438602b3ed77feabc4ebde8c922fe0839216aebc12f59f`，沿用触摸及 8 核内核，重新打包 ADT、payload、固件并通过离线门禁。目标 `3b25cc7c` 仅临时 Fastboot boot 一次，Sending/Booting OKAY，未写分区；屏幕及 2 GiB/触摸结果待用户确认。
- 修复源码、host test 与边界说明见 `rebuild/ipados-up1/window-fix/`，提交 `949ce25`。Recovery 返回后回收新日志，不自动重复启动。

## 2026-10-30：UP1 触摸、八核和 2 GiB 候选

- 用户确认上一版 `out/p21/up1-clean-rebuild/uefi/boot-p21-up1.img` 已进入 iPadOS。其新日志 `out/p21/persistent-logs/20261029T161111Z/` boot ID `0x55da2956`，268 rounds、写入错误 0。手机端持久日志已工作；主机 `scripts/unattended/monitor.py` 未运行，读取并非自动执行。
- 按一加 5 上游设备树的 GPIO89 input pull-up，修复 S3320 reset 输入未配置上拉导致的潜在初始化失败；不改变电源或驱动复位脚，低电平仍报告 -54。P22 的 R6 曾记录 -54、未发生 I2C；触摸是否修复尚待本轮实机结果。宿主 RMI4 测试通过。
- 新候选 `out/p21/touch-up1/uefi/boot-p21-up1.img` SHA-256 `61562019dd4c8f523c6c17664ea6f68b99bfca0271eac86f62998cde55b8391d`，基于 UP1，仅导入 P22 触摸与 SMP 修改；ADT 的 CPU/PSCI/内存/vnode 来源为本地 iOS Stage C，8 核、2 GiB、33792 vnode。历史 2 GiB iPadOS 尝试出现过 NO WINDOW，本轮不预设成功。
- 本轮内核、payload、UEFI 已构建，EFI 编译 pins 与 FV 模块匹配，`out/p21/touch-up1/offline-gate.json` 通过。设备 `3b25cc7c` 临时 Fastboot boot 一次，Sending/Booting OKAY，未写分区；设备实际运行状态待用户报告。若 NO WINDOW，在内核之前失败，不能判断触摸；若进入系统请测试点击/滑动，回 Recovery 后读取新的日志。
- 本次源码和流程提交 `166b938`；构建与运行日志在 `out/p21/touch-up1/`，构建脚本 `rebuild/ipados-up1/touch-scripts/`，VM overlay lower 仍需要保留。

## 2026-10-29：iPadOS UP1 独立重建与临时启动

- 用户报告此前 `out/p21/upstream-a/uefi/boot-p21-up1.img` 已进入 iPadOS 界面。该轮 Recovery 持久日志位于 `out/p21/persistent-logs/20261029T153842Z/`，boot ID `0x59359b02`，190 次记录，写入错误为 0；先读回再启动新镜像。
- 新内核从提交 `f89c28c9f83ec171ba7ee43018b0cd20766d9698` 导出的独立源码目录 `out/p21/up1-clean-rebuild/source/` 编译；官方框架和参考源码使用本地固定输入。日志在 `out/p21/up1-clean-rebuild/kernel-build.log` 及其独立构建树的 `logs/`；预链接、panic 屏幕与 payload 构建日志也留在该目录。
- UEFI 使用 Linux VM 的 `/home/dinnerabc.guest/unixnu-up1-clean/source` 和空构建输出；源码 overlay 的 lower 层仍依赖原有 UP1 及此前本地 VM 层，不是独立下载的新上游源码。构建脚本保存在 `rebuild/ipados-up1/clean-scripts/`，VM 中的 overlay 层必须保留。
- `out/p21/up1-clean-rebuild/uefi/boot-p21-up1.img` SHA-256 `7aabe63b44d37972658de89ca934151d847d2e6a8d817b45527f87382a00b772`；载荷、内核、ADT 校验值在编译后的 EFI 模块中匹配，三项 EFI 模块与 `FVMAIN.Fv` 对应节一致；镜像内容往返校验通过。注意 FVMAIN 在 FD 中经封装，本轮未单独反向解包 FD 验证 FV 内容。
- 设备 `3b25cc7c` 已通过 `fastboot boot` 临时启动此新镜像一次，Sending 和 Booting 均 OKAY；实机是否出现画面尚待用户报告。未写任何分区；boot 输出在 `out/p21/up1-clean-rebuild/boot-once.log`。出现问题时先记录画面，返回 Recovery 后回收新 boot ID 日志，勿重复自动启动。

## 2026-10-29：恢复 iPadOS userdata

- 用户要求启动一次：已核对 `out/p21/upstream-a/uefi/boot-p21-up1.img` SHA256 `7cf2fef9dea9103e860db4358697b4d2e67d550928b5deb3afac67c5d4309323`，P21 离线门禁 PASS（`pins_log_checked=false`），设备 `3b25cc7c` 通过 `fastboot boot` 临时启动，Sending/Booting 均 OKAY；未修改分区或预置 misc。本次设备运行画面尚待用户观察，不能以传输成功当作 iPadOS 启动成功。回 Recovery 后读取日志再进行后续启动。

- 当前分支 `ipados-17.7.11`，HEAD `08543c9`；未重置工作区，保留未跟踪的 `package/BoardRuntime/NativeLegacyDisplay.cpp.p23-pre-canvas`。此前 iOS 线和生成物未删除。
- 用户授权恢复 iPadOS userdata。设备 `3b25cc7c`，root Recovery，`/dev/block/bootdevice/by-name/userdata` 指向 `/dev/block/sda14`，容量 47938797568 bytes。将此前 iPadOS 基线 `out/p12/reference/cheeseburger-p12-reference/userdata/userdata-root.img` 的 9382658048 bytes 写入该分区前缀；镜像写前 SHA-256 与设备回读均为 `fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093`。
- 写前回收 `out/p21/persistent-logs/20261029T150245Z`，boot ID `0x3e1b3491`；userdata 10 GiB 日志窗口写前写后 SHA-256 均为 `76445f9fa52bd266f442b1e0070bfb94e14d50002beac8eead2b60f4681cc98a`。未写其他分区，未临时启动；本次恢复尚无 iPadOS 运行验证。

## 2026-10-29：主线切回 ipados-17.7.11，日志系统已移植

- 分支：当前 `ipados-17.7.11` @ `2508a59`；iOS 16.7.16 线保存为分支 `ios-16.7.16` @ `c1508e6`，已推送到 origin（49 个提交）。`main` 仍指向同一 iOS 提交，两个分支都在。
- 已移植：`scripts/unattended/monitor.py`
  - 2 秒轮询，只使用已经监听的 ADB 端点（默认 5037/5038），绝不启动第二个 server；
  - 判定“root Recovery + ro.bootmode=recovery + cheeseburger”后立即调用 `scripts/logging/read-cheeseburger-log.py`；
  - 以窗口 SHA-256 对比 `out/unattended/state.json`，输出 `NEW_LOG` / `NO_NEW_LOG`。
- 已移植门禁：`scripts/unattended/preboot-gate-p21.py`
  - 校验 packaging → 镜像哈希 → P13LZ001 payload → 展开哈希/长度 → payload 内 kernel 与 `kernel/validation.json` 一致 → 固件哈希与打包一致 → 镜像内 gzip(FD)+DTB 与 uefi 文件一致 → 参数含 `sheng-log=1`、`keepsyms=1`；
  - 已验证：R17 D2、R14 通过；R4 因参数没有 `sheng-log=1` 被拒（R4 是唯一出过 UI 的历史镜像，但要诊断仍需带日志的候选）。
- iOS 线留档要点（详见 `ios-16.7.16` 分支的 HANDOFF）：
  - sandbox kext 移除成功，根因是宽松 MAC policy 必须在 `mac_policy_init` 早期注册（晚注册会写只读静态表 panic）；
  - `lsd` 的 `mobilegestalt` 拒绝从 11 次降到 0；
  - 显示仍 `frames=0`；swap 入口是 selector 5 → Legacy vtable 槽 178 `swap_submit`，selector 72（vsync 注册）从未出现。

## 设备与约束

- 目标设备 `3b25cc7c`，只做临时 `fastboot boot`，不写 boot/recovery/固件/GPT。
- userdata 唯一授权写入：相对 10 GiB、32 MiB 的持久日志窗口（`out/p21/r14/log-window.json`）。
- 每次启动前要有离线门禁；启动后必须能从 Recovery 回收日志（或证明窗口未变）。
