# P21 / Adreno 540 GPU 现状报告（供审计）

- 日期：2026-10-31（UTC 日志；本地 UTC+8）
- 设备：OnePlus 5 `cheeseburger`（MSM8998 / Snapdragon 835），序列号 `3b25cc7c`
- 分支/HEAD：`main`，代码提交 `5b268c4`（GPU 相关；此前 `1f51d22`、`54c919c`、`44dbca9`、`8fa92ac`）
- 候选目录：`out/p21/fb-qup3/`（本地构建树 `/Users/dinnerabc/unixnu-p21/fb-qup3/source-tree`，Lima VM `p2` 编 UEFI）
- 报告目的：供另一个 agent 审计“当前状态是否如本报告所述”，不冒充硬件成功

## 0. 审计者须知（证据等级约定）

- **已确认**：有本轮实机日志/回读哈希支持。
- **推测**：有日志线索但未做对照验证。
- **未验证**：代码已写/已打包，但设备尚未给出结果。
- 所有 boot ID 均为 `BootLog` 的 `cb_boot_id`；环形窗口里有旧 boot 的行，必须按 boot ID 过滤。
- 启动一律 `fastboot boot` 临时启动，未写 boot/recovery/固件/GPT。userdata 只写官方持久日志窗口
  （`out/p21/r14/log-window.json`：userdata 相对 10 GiB、32 MiB）与已授权的持久 Data store。
- misc 只写 `/dev/block/sda4` 第一个 4 KiB 的 BCB `command` 字段；每轮启动前重写并回读，
  最近回读 SHA-256 均为 `8698abf44b69b8040dd109feefc23706f070a26932c47f2b44a835efb2b9c801`
  （`out/p21/misc/misc-readback*.bin`）；BCB 会被 Recovery 消费清零。

## 1. 当前在处理什么

在 iPadOS 17.7.11（21H461）已能进桌面、持久 Data、触摸方向修复、400 s 自动回 Recovery 的基础上，
按官方 GPU 移植手册与同 SoC 参考（`docs/upstream/reference/sagit/`，pin `898998c`）实现 **Adreno 540
（A540）内核后端**。当前阶段是 **CP（Command Processor）固件启动**：

1. 用 UEFI `EFI_CLOCK_PROTOCOL` 给 GPU 上电/开时钟（`rebuild/ipados-up1/gpu-clock/UniXNuGpuClock.c`，
   `UNIXNU_GPU_POWER=1`、`UNIXNU_GPU_RATE=0`）。
2. XNU 侧（`package/BoardRuntime/CheeseburgerGpuProbe.cpp`）只读清点 GCC/GPUCC/GPU/SMMU。
3. `CheeseburgerGpuSmmu.cpp`：按 sagit 非安全 bank0/SID0 建立 AArch64 stage-1 上下文与页表。
4. `CheeseburgerGpuCp.cpp`：解析私有 QGFW 固件容器、分配 ring/PM4/PFP/GPMU/fence、经 SMMU 映射、
   读取 CP/GPMU 冷状态；`cheeseburger-gpu-start=1` 时执行 CP `reset()→configure()→start()`（CP_ME_INIT）。
5. 目标顺序：CP 启动成功（RBRead 前进）→ GPMU 固件上传（AGC 0x9800、`GPMUReady==0xbabeface`）→
   ring/PM4 提交 → XNU 用户客户端 → Metal 前端。

## 2. 已验证事实（按时间顺序，均为实机日志）

### 2.1 背景（此前已完成，本轮未改动）

- 持久 Data：`20261031T044135Z`（boot `0x4e1d0ef9`）mode2 `state=3 writes=161233 err=0`；
  复启 `20261031T044943Z`（boot `0x4fa89bdb`）`state=2 loaded=111 err=0`。已确认跨重启成立。
- 400 s 自动回 Recovery：内核线程写 BCB 前等 4 s 落盘后发 PSCI `SYSTEM_RESET`；配合预置 misc BCB
  可自动回 Recovery（如 `20261031T053946Z`）。黑屏手动返回不算自动成功。
- 触摸：面板倒装，`transform=0` 后用户确认方向正常。

### 2.2 GPU 电源/时钟/寄存器（已确认）

- UEFI 直接读 GPUCC/GCC MMIO 会触发 `Synchronous Exception`（UEFI 自身映射不可用），已移除；
  上电/开时钟本身成功，capture 以 `P:done` 结束。
- XNU 读取（如 `20261031T062615Z`，boot `0x5048a3db`）：`GPU0 stage=3 flags=f`；
  `RBBM_ID=0x00010042`、rev `0xb5f00000`、`MEControl=0x303`（CP halted）、GPMU 未加载。
- 整屏黑 A/B（用户确认）：`29dd9fdc`（含 `SetClockFreqHz(gpucc_gfx3d_clk,257MHz)`）整屏黑；
  `ca8d45f0`（仅去掉该调频，rate=0）正常进系统并自动回 Recovery。
  **结论（已确认）：整屏黑由 gfx3d 257 MHz 调频引入，不是 CP 探针本身。**

### 2.3 SMMU 与五 buffer 映射（已确认）

- `20261031T103113Z`（boot `0x50b0c185`，镜像 `57036564…`、内核 `aeb7b438…`）：
  - ring(8 页)/PM4(5)/PFP(5)/GPMU(3)/fence(1) 全部 `r=0`、`MAPD ret=0 tlb=0`；
    GPU VA = `0x2000000/0x2008000/0x200d000/0x2012000/0x2015000`。
  - `GCP2 me=303 vbif=0/f0000 rb=0/0/0/0 st=800001c1 irq=0 trust=1 sec=0`：
    `Status=0x800001c1` 与 sagit `haltedPipelineIdle` 掩码 `0x800001c1` 完全一致。
  - `GCP4 gready=0 gerr=0 cm3rst=1 prot=0 swrst=0 ahb=1`（GPMU 未加载、CM3 复位）。
  - 同轮显示正常（`DISPLAY clients=129 frames=6`、`SWAP transfer=6`），内核跑满 400 s 自动回 Recovery。
- 之前“第 4 个映射后整机停住”的问题**未再复现**；本轮差异：SMMU 寄存器窗口整轮只映射一次
  （此前每次 buffer 映射都新建 IOMemoryMap），且 CP 状态读取提前到映射之前。

### 2.4 CP reset/configure/start（已执行，未成功）

- `20261031T104613Z`（boot `0x5121380b`，镜像 `64fdf3d6…`、内核 `e2e767c0…`）：
  `GCP5 stage=3 rc=-4 vbif=f000f me=303 r=0 w=9 irq=0 cpirq=0 st=c00003c1 st3=100000`。
  - reset() 通过（否则 rc=-1..-6）、configure 回读通过（否则 rc=-2）、`CP_ME_INIT` 已写入 ring（WPTR=9）；
  - **RBRead 始终 0**，`InterruptStatus/CPInterruptStatus` 全 0；`Status` 仅多 bit30（CP 已唤醒）。
  - 结论：CP 微码没有执行，不是 reset/configure 失败。
- `20261031T110117Z`（boot `0x50993220`，镜像 `adb12b45…`、内核 `3b76b685…`）：
  将 CB0 `SCTLR` 由 `0x1167` 改为 sagit 实测的 `0x1027`、并准备在 reset 后写
  `RBBM_SECVID_TRUST_CNTL=0`；但 **reset 在 idle 前置检查失败**（`GCP5 stage=1 rc=-2`，
  `VBIFHalted` 已 ack=`0xf000f`），未进入 SW reset/TrustControl 清除/kick，`GCP6` 全 `deadbeef`。
- `20261031T111210Z`（boot `0x5230df48`，镜像 `b15bb427…`、内核 `d771e2ec…`）：
  `GCP7 cx=80001fe0 gx=a0222000 gfx=621`（启动时 CX/GX/gfx3d 均在线），
  `CB GPU start: begin` 时间戳 **151.73 s**，最后一条内核消息 **157.33 s**，整屏黑、手动回 Recovery。
  **冻结发生在 begin 之后的 8 s 等待中、尚未执行任何 GPU 写操作**；日志显示 150–157 s 正是系统自身
  **PMRD**（`PowerChangeOverride`、`idle sleep timer`）电源状态切换窗口。前两轮 start 在 ~188 s
  （PMRD 之后）均未冻结。→ **推测：冻结与 PMRD 电源切换窗口相关，而非某条 GPU 寄存器写。**

### 2.5 本轮结果（已回收，boot `0x510ea570`）

- 镜像 `aea9a9fa18d420614246ce29f027ea784729556a2f9f3d2742d15b5001570565`，
  内核 `6ccfb681c17dff0cdaacc6e7626952d64d1d465c9a5400badabb7bf6f9db2dfd`，
  payload `61666a13b3ead3ff1783789f2e6d446434c4712cdbe2aa62f90fba3046ed8d41`，
  固件 `b51ed09655a95360c98dfc3b5c2f84ec877704458cbe9150ca11b1049d9a855a`，args 669，门禁 PASS。
- 日志 `out/p21/persistent-logs/20261031T112909Z/`：boot `0x510ea570`，**8 rounds**，0 写入错误，state=2；
  writer 相对运行时间 `uptime_s=35`（约等于内核 ~102 s 写最后一轮）。
- 时间线（内核时间戳）：
  - 76.71 s `CB GPU SMMU:` `cb=1027`、`fsr=400`（无 fault）；CP 冷状态与 2.3 完全一致
    （`me=303 vbif=0/f0000 rb=0/0/0/0 st=800001c1 trust=1`）。
  - 86.7–98.7 s：ring/PM4/PFP/GPMU/fence 依次 `r=0 tlb=0`（5/5）。
  - 98.725 s `CB GPU CP:` 汇总；98.7258 s `CB GPU start: begin … cx=80001fe0 gx=a0222000 gfx=621`。
  - **98.7258 s 是整轮最后一条内核消息**；之后没有 userland 消息（正常每 ~1 s 有 CoreAnalyticsHub 行），
    也没有 `reset rc=` 行。writer 在 ~102 s 写最后一轮后停止。
- **本轮没有执行任何 GPU 写操作**（begin 之后先 `IOSleep(8000)`，reset 在 ~106.7 s 才开始）。
- **设备最终回到了 Recovery**：监视器 `polls=245`（≈自启动 503 s）在 Recovery 检测到设备并回收日志，
  时间上符合“400 s 自动复位 + Recovery 启动/ADB 枚举”；但用户尚未确认这是自动返回还是手动返回，
  **该点未验证**（`misc` BCB 已被消费，但手动进 Recovery 也会被清空，不能区分）。
- 两种解释（需下一轮/用户确认）：
  - (a) **内核在 ~99 s 停住**：那么 400 s 自动复位不可能执行，设备只能是手动返回。
  - (b) **仅控制台/日志捕获路径停住**（例如某线程在 IOLog/console 路径持锁或卡住），
    userland 与 auto-reset 线程仍在跑，所以 400 s 自动复位成功、设备自行返回。
  - 现象差异：本轮 begin 后 3 s 内没有任何 userland 控制台行，更像 (a)；但设备最终回到 Recovery 又更像 (b)。
    本轮日志不足以区分，需在下一轮加“不依赖控制台”的存活标记。

## 3. 遇到的问题与解决方式

| # | 问题 | 解决/结论 | 状态 |
|---|------|-----------|------|
| 1 | UEFI 内读 GPUCC/GCC 触发 `Synchronous Exception` | 移除 UEFI MMIO 读取，改由 XNU 用自有映射读取；上电仅走 EFIClock | 已解决 |
| 2 | 整屏黑（连屏幕日志都不显示） | A/B 定位到 `SetClockFreqHz(gpucc_gfx3d_clk,257MHz)`；`UNIXNU_GPU_RATE=0`；用户确认 257MHz 版本黑、rate=0 正常 | 已解决（升频待做） |
| 3 | CP 第 4 个 buffer 映射后整机停住 | SMMU 寄存器窗口整轮只映射一次（不再每次新建 IOMemoryMap）；CP 状态读取前置；每步落盘间隔 | 已解决（五映射全通、400 s 稳定） |
| 4 | 环形日志旧行混入、无法区分本轮 | `BootLog` 行写 `cb_boot_id`；分析时按当前 boot ID 过滤 | 已解决 |
| 5 | 门禁“firmware pin scan has no hit” | FD 是压缩容器，直接扫 FD 找不到 hash；改为扫 VM 构建树的 `FV/FVMAIN.Fv`/FFS/`.efi`（`out/p21/fb-qup3/scan-pins.py`） | 已解决 |
| 6 | CP `rbRead=0`、无 CP 错误中断 | 已对比 sagit/Linux：`RB_CNTL`、`ME_INIT` 包、instr base 一致；下一假设 secure 模式（`TrustControl=1`）与 CB0 `SCTLR`（`0x1167` 有 CFIE/HUPCF，`0x1027` 对齐 sagit） | **未解决** |
| 7 | reset 前 idle 检查失败（rc=-2） | VBIF 停流 ack 后 `Status` 非 idle；已加 20×10 ms 重试并记录 `Status/MEControl/VBIFHalted` 原值（`GCP7`） | 部分缓解，根因未定 |
| 8 | `CB GPU start: begin` 之后日志中断（151 s 轮黑屏手动回；99 s 轮自行回 Recovery） | 两次都发生在 begin 后的 8 s 等待内、**均无 GPU 写**；99 s 轮不在 PMRD 窗口，说明 PMRD 不是必要条件。共同点是“5 次 SMMU 映射完成后立即 begin 打印”。现无法区分“内核停住”与“仅控制台/日志捕获停住” | **未解决**，需下一轮加不依赖控制台的存活标记 |
| 9 | 持久日志 writer 在关键打印后停止 | 最后一条恰是我们的 IOLog；writer 在下一轮（~5 s 后）也没写。可能是一致性/控制台锁问题，也可能是 SoC 停住 | **未解决** |

## 4. 当前状态（重点）

### 4.1 设备

- **设备当前在 Recovery**（ADB 枚举正常，`ro.bootmode=recovery`）；本轮日志已回收：
  `out/p21/persistent-logs/20261031T112909Z/`（boot `0x510ea570`，8 rounds）。
- 本轮设备在约 **503 s**（自启动）被监视器发现于 Recovery；是否符合“400 s 自动复位”路径**尚无用户确认**
  （用户未像前几轮那样报告黑屏/手动返回）。misc BCB 已消费（`misc-now7.bin` 全零），但手动进 Recovery 也会被清空，不能作为判据。
- 上一轮：`out/p21/persistent-logs/20261031T111210Z/`（boot `0x5230df48`，黑屏、用户手动回）。
- 本轮 misc BCB 已预置并回读（`misc-readback29.bin`）；下一次启动前需重新写入。

### 4.2 代码/镜像

- 已实现且可编译：GPU 只读探针、SMMU 上下文+页表（五 buffer 映射）、CP 全状态读取、
  CP `reset/configure/start`（CP_ME_INIT）、TrustControl 清除、CP/SMMU fault 捕获、PMRD 避开时序。
- 硬件进度：**GPU 上电/时钟、SMMU 上下文、五 buffer DMA 映射、CP 冷状态**均已在实机取得；
  **CP 固件尚未开始执行（RBRead=0）**；GPMU 固件尚未上传；尚无 Metal/用户客户端。
- 私有输入（不随仓库分发）：`out/p21/gpu/firmware/`（a530_pfp.fw / a530_pm4.fw / a540_gpmu.fw2，
  哈希见 `out/p21/gpu/firmware.sha256`）；内核构建时由 `scripts/p21-gpu-firmware-header.py` 生成
  `CheeseburgerGpuFirmware.h` 到本地构建树，不入库。

### 4.3 未决问题（按优先级）

1. **“日志在 begin 后停止”到底是内核停住还是仅日志/控制台停住**：两次都发生在 begin 后的 8 s 等待内、
   无 GPU 写；99 s 轮设备最终回到 Recovery。下一轮需要**不依赖控制台与 UFS writer 的存活标记**
   （例如由独立内核线程在固定时间改一个会被 photo/录像以外的路径读到的内存，或用 auto-reset 标记落盘），
   否则无法区分 (a) 内核停住 / (b) 仅 console+writer 停住。
2. **CP 为什么不取 ring（RBRead=0）**：只有 188 s 那两轮完整执行了 reset/kick，`GCP6` 仍未取到
   （一轮 reset idle 失败，一轮日志中断）。需要先解决第 1 项与 idle 检查，才能拿到 `CP_HW_FAULT /
   CP_PROTECT_STATUS / CP_AHB_FAULT / CB0_FSR / GFSR`。
3. **reset idle 前置检查不稳**：`Status` 在 VBIF 停流后有时非 idle；需要判断是 drain 瞬时 busy、
   GPU 域掉电，还是 PMRD 介入。`GCP7` 已能记录 `cx/gx/gfx/st/me/vbif/ry`，但关键轮次日志中断。
4. **PMRD 窗口**：151 s 轮与 PMRD 切换重合，但 99 s 轮不在该窗口也发生中断，所以 **PMRD 不是必要条件**
   （此前归因需降级为“可能相关”）。
5. **XNU 侧 GPU 电源/时钟 vote**：目前只有 UEFI EFIClock 上电；XNU 没有 RPM/RSC vote。
   `CheeseburgerPlatformPower.cpp` 只是 Apple ABI 兼容壳（不是 RPM 驱动）。是否需要补 vote 尚无证据。
6. **升频**：`gfx3d` 仍保持 rate=0；CP/GPMU 跑通后再按 sagit root clock/PLL 顺序安全升频。

## 5. 给审计 agent 的核对点

1. **寄存器/序列**：对照 `docs/upstream/reference/sagit/QcomGPU/AdrenoStartup.h`、
   `AdrenoSubmit.h` 与本仓库 `CheeseburgerGpuSmmu.cpp`/`CheeseburgerGpuCp.cpp`：
   - `CP_ME_INIT` 包头（`packet7(0x48,8)` + 8 dwords）；
   - `CP_RB_CNTL = 12 | (2<<8) | (1<<27)` 与 Linux `MSM_GPU_RB_CNTL_DEFAULT|NO_UPDATE` 的字段位置；
   - `CP_ME_INSTR_BASE_LO/HI`(0x838/0x839)、`CP_PFP_INSTR_BASE_LO/HI`(0x835/0x836)、
     `CP_RB_BASE`(0x800/0x801)、`CP_RB_RPTR/WPTR`(0x806/0x807)；
   - CB0 `SCTLR=0x1027` 的位定义（M|TRE|AFE|CFRE|S1_ASIDPNE，CFCFG=0 为 stall）与 `0x1167`
     （额外 CFIE/HUPCF）的差异是否被正确理解；
   - 40-bit IA + 4 KiB granule 的起始层级（`firstLevel=0`，shift=39/30/21/12）。
2. **日志证据是否被如实引用**：抽查 boot ID（`0x50b0c185`、`0x5121380b`、`0x50993220`、`0x5230df48`）
   与 `out/p21/persistent-logs/<UTC>/retrieval.json` 是否一致；本轮日志是否已回收。
3. **是否越权写分区**：核对 misc 只有第一块 BCB 命令字段、有备份/回读；userdata 只有日志窗口与持久 Data；
   未写 boot/recovery/固件/GPT。
4. **是否把推测当结论**：本报告已标注“已确认/推测/未验证”。若发现把 `PMRD` 归因、`reset rc=-2`
   或本轮冻结写成已确认，请指出。
5. **构建一致性**：`out/p21/fb-qup3/scripts/{prelink,build-payload,prepare-uefi,pack}.py` 的硬编码路径；
   pins 扫 `FVMAIN.Fv`；args 669 字节（< ~700 截断阈值）；`gate` 输出与镜像哈希是否对应。
6. **隐私/仓库卫生**：vendor 固件、设备日志、镜像未提交；`out/` 为 gitignore；确认 `git status` 干净。

## 6. 下一步（待审计后执行，不在本报告内启动）

> 按用户要求，本报告完成后**暂停**，不继续构建或启动；以下仅为候选方案。

- 先做**存活判据**：在 CP 启动阶段由一个独立内核线程/定时器写“与 console、UFS writer 无关”的标记
  （例如固定时刻改 `cb_cp`/专用页并让下一次 boot 的 Data 或固定 RAM 位置可读，或让 auto-reset 在 PSCI 前
  将标记写入 UFS store），用于区分“内核停住”与“仅日志停住”。
- 在无法区分前，**不再把“日志中断”直接当成“内核冻结”**；此前 PMRD 归因已降级为“可能相关”。
- 减少关键路径上的控制台 IOLog（改用 `cb_cp` 行 + BootLog 轮询），避免 console 锁参与。
- 拿到稳定 `GCP6` 后：按 `smfsr/smg`（SMMU 取指 fault）或 `cphw/cpprot`（CP 内部 fault）分别处理；
  `GCP5 r=9` 才是 CP 真正执行 ME_INIT。
- CP 起来后再做 GPMU（AGC 0x9800、`CP_INDIRECT_BUFFER`、`CM3_SYSRESET=0`、`GPMUReady==0xbabeface`）。
