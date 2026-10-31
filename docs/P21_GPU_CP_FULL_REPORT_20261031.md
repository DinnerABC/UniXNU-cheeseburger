# P21 Adreno 540 / CP 完整现状报告（2026-10-31，供审计决策）

- 范围：OnePlus 5 `cheeseburger`（MSM8998）上的 iPadOS 17.7.11 / 21H461，A540 内核后端 CP 启动。
- 本篇为**完整、自包含**的阶段报告，合并此前 `docs/P21_GPU_CP_STATUS_REPORT.md`、
  `docs/P21_ADRENO540_REVIEW_20261031.md`、`docs/P21_GPU_CP_DIRECTION_20261031.md` 与最新 A–F 轮结果。
- 证据等级：**已确认**（有实机日志/回读）、**推测**（有线索未对照）、**未验证**（代码/镜像已备，设备未给结果）。
- 设备当前：Recovery（F 轮自动返回）。未写 boot/recovery/固件/GPT；userdata 仅日志窗口与持久 Data；
  misc 仅 BCB 命令字段并每轮备份/回读。

## 0. 审计结论先行（TL;DR）

1. **CP 仍未开始执行**：`RBRead=0`、`WPTR=9`，`InterruptStatus/CPInterruptStatus` 均为 0，
   `trust=1`（secure 模式未清除），SMMU/CP fault 均为 0。
2. **已定位并规避一个确定性卡死**：写/读 `RBBM_SECVID_TRUST_CNTL`（试图强制退出 secure 模式）
   会卡住 GPU 访问路径。D 轮心跳显示 reset 到 `cm3` 后无 `trust` 标记，而 tick 继续到 ~370 s；
   E 轮去掉该写后 full-start 完全安全（66–67 rounds、自动复位、无黑屏）。
3. **reset/configure/地址编程已验证**（GCP8）：ME/PFP/RB 基址高低字、RB_CNTL=0x800020c、
   CP_ADDR_MODE_CNTL=1、CP_PROTECT_CNTL=7 均按写入值回读。
4. **A/B/C 已排除两类假设**：begin 行长短、五映射+console+8 s 睡眠都不触发停住；
   此前“日志中断”更可能是 full-start 的 reset 写路径（后被 D 精确定位到 TRUST_CNTL）。
5. **待决策**：CP 为什么仍不取 ring。首要候选是 **secure 模式（ZAP/PAS 前提）**，但尚未证实；
   另有 TTBR0 高字回读异常、S2CR 打印异常、PTE 查询修正未实机验证等数据质量问题。
   本报告第 7 节给出 A/B/C/D 四条路线与推荐。

## 1. 目标与工作流

目标（按官方 /zh/porting/gpu 与同 SoC sagit 参考）：UEFI 上电/时钟 → XNU SMMU 上下文与页表 →
CP 固件（PM4/PFP）加载与 `CP_ME_INIT` → GPMU → ring/PM4 提交 → XNU 用户客户端 → Metal。

约束（AGENTS.md）：
- 仅 `fastboot boot` 临时启动；不写 boot/recovery/固件/GPT；userdata 仅授权的 32 MiB 日志窗口与持久 Data；
  misc 仅 BCB `command` 字段，写前备份、写后回读；每轮启动前重写（Recovery 会消费）。
- 每轮：离线门禁（结果写 `gate-<UTC>.json`）→ 临时启动 → 回 Recovery 回收日志 → 冻结不可覆盖证据。
- 不绕过 secure/ZAP 保护；不在无独立存活判据时重复 CP reset/kick。
- 不提交镜像、私有固件、设备日志。

构建链（串行）：`p21-compile-fb-qup3.py` → `prelink.py` → `patch-xnu-entry-probe.py --panic-screen --no-reset`
→ `build-payload.py` → `prepare-uefi.py` + `p2-linux-build.sh`（Lima VM）→ `pack.py` → `preboot-gate-p21.py`
→ misc BCB → `fastboot boot` → monitor/读取 → `p21-freeze-round.py`。

## 2. 当前状态

- 设备：Recovery（F 轮自动复位返回；`monitor` 已回收 `20261031T133223Z`）。
- 最新候选 F：镜像 `c7a64a40…`、内核 `ac8f24fe…`、payload `4ffc54db…`、固件 `02d004be…`，args 669，门禁 PASS。
  源码另有 PTE 查询修复（`8d457ec`）与文档提交（`7daf2e7`），**尚未重新打包/启动**。
- 代码：`main`，GPU 相关提交 `8fa92ac/44dbca9/1f51d22/54c919c/5b268c4/9875d6d/1d8573a/c4ce378/ac8f24f/8d457ec` 等。
- 证据：`out/p21/gpu-cp/rounds/`（11 个不可覆盖轮次目录 + `INDEX.md`）；每个门禁运行写 `gate-<UTC>.json`；
  旧 `out/p21/fb-qup3/offline-gate.json`（a4af3b18…）与 `pins.json`（f9bafc4d…）已标记失效。
- 私有输入（不提交）：`out/p21/gpu/firmware/`（a530_pfp/pm4、a540_gpmu.fw2、a540_zap.*）。

### 2.10 第一轮合并修正结果（2026-10-31，boot `0x51cd61ff`）

- 镜像 `1ad823ea…`、内核 `5b488ddb…`、payload `fc2a3198…`、固件 `58a2373f…`，66 rounds，自动复位、无黑屏。
- 心跳分区生效：step 记录 `cm3/ready/cfg/addr/kick/fail` 与 tick 分槽；读取端 `foreign=1`（旧 boot 槽被正确过滤）、
  `empty=0`、各 tag 序号缺口为 0。
- `GCP10 ttbr=204a7000/1 smr=80000000 s2cr=0 pte=6000010a23ce43 root=1204a7000 ringpa=10a23c000 fsr=400 gfsr=0`：
  - **TTBR hi=1 是正确值**：root PA=`0x1204a7000`（>4 GiB），因此高字应为 1；此前“异常”是推断错误。
  - **S2CR=0**（`%x` 原始读回）：此前的 `fffffff7…` 是 64 位打印伪影。
  - 叶子 PTE=`0x6000010a23ce43`，地址部分=`0x10a23c000`=`ringpa`，属性位与既有设置一致。
- `GCP8` 仍全部正确；`GCP5 rc=-4 r=0 w=9`；`GCP6 trust=1`、CP/SMMU fault 均 0；`KEY` 单页汇总行正常。
- **结论**：reset/configure/地址/页表/stream 已确认自洽；`RPTR` 仍不前进，且无 fault。

### 2.11 ZAP/PAS 依赖核对（2026-10-31，离线）

- Android DTB 含 `qcom,kgsl-3d0@5000000`、`qcom,zap-shader`、`ga540_zap`；vendor 分区已取得
  `a540_zap.mdt/.elf/.b00/.b01/.b02`（.mdt/.elf/.b00 均为 ELF；.b01/.b02 为段）。
- 同 SoC 参考 sagit 在 XNU 内实现 `QcomSCM`（`SCMPAS.h` 的 SMCCC PAS 传输：service 2 cmd 1/2/5/6，
  service 1 cmd 10 resume，service 6 availability probe）与 `QcomGPUSecure`；其 GPU 启动前置为
  “platform lease → VBIF 停流 → **PAS/ZAP 认证 resume** → SMMU/CP”。ZAP 的 init/setup/auth 由 pre-XNU EFI loader
  或 QcomSCM 客户端完成，GPU 只负责 resume。
- 本机现状：UEFI（sheng）与 XNU 均**没有** PAS/ZAP 装载链路；`TrustControl=1`，强制写该寄存器会卡死。
  因此“CP 不取 ring”的**首要缺失依赖已明确为 secure world 的 ZAP/PAS 认证**（仍未证明是唯一原因）。
- 可选实现路线：
  1. **XNU 内 PAS 端口**（推荐）：移植 sagit `SCMPAS.h`/`SMCCC.h` 的 Transport/PAS，新增最小 ZAP loader
     （私有固件块），依次 PAS init→setup/auth→resume，再做 CP reset/start。sagit 已证明 XNU 可发 SCM/PAS；
     需要核准 ZAP 段装载地址/是否可重定位与 carveout 语义。
  2. **UEFI pre-XNU loader**：更贴近 sagit 架构（EFI 侧完成 ZAP 认证后交给 XNU），但需确认 sheng EFIScm 可用、
     把 ZAP 纳入 payload/ESP 并做 handoff；改动面更大。
- 约束：不写 `TRUST_CNTL` 绕过；使用设备原始 ZAP 与 PAS ABI；PAS 失败/超时按 QcomSecure 语义隔离，不伪造成功。

### 2.12 ZAP 固件格式与 PAS 装载设计（2026-10-31，离线）

- `a540_zap.mdt` / `.elf` / `.b00` 共享 ELF32 头与 3 个 program header（0x94 = 52 + 3×32）：
  - ph1：`p_flags=0x2200000`（type hash 0x2000000）、`p_paddr=0x7000`、filesz `0x1988`、memsz `0x2000`；
  - ph2：`p_flags=0x8000007`（含 RELOCATABLE 0x8000000）、`p_paddr=0x5000`、filesz/memsz `0x1334`。
- 按 Linux `mdt_loader.c`：镜像可重定位 → `PAS init`（metadata = ELF 头 + hash 段）→
  `PAS mem_setup(region_pa, max-min = 0x4000)` → 把段相对 `min_addr(0x5000)` 装入 region
  （ph2 在 +0，ph1 在 +0x2000）→ `PAS auth_and_reset(5)`；ZAP 执行后 GPU 应离开 secure 模式。
- 计划：私有嵌入 `.mdt/.elf`，分配 16 KiB 物理连续非缓存区，按上述顺序调用 PAS，随后只读确认
  `TrustControl==0` 再恢复 CP start。先运行**只读 availability 探针**（本轮），不改 PAS 状态。

### 2.13 SIP/PAS availability 探针结果（2026-10-31，boot `0x52c594dc`）

- 镜像 `8061ebaf…`、内核 `4b1fa291…`，66 rounds，自动复位、无黑屏。
- `PAS0 st=0 smc64=1 scm=1 resume=1 init=1 setup=1 auth=1 down=1 sup7=1 sup=0`：
  - EL1 的 64-bit SMCCC 可用；SCM availability、PAS resume/init/setup/auth/shutdown 全部存在；
  - `sup`（service 2 cmd 7 的返回值）为 0，但逐命令 availability 均为 1，按可用处理。
- 其余状态不变：`GCP5 rc=-4 r=0 w=9`、`trust=1`、CP/SMMU fault 0；`GCP10` 同前（TTBR hi=1 由 root>4 GiB 解释）。
- **结论：本机 XNU 可以直接实现 PAS/ZAP 装载**，不需要 UEFI pre-XNU loader。
- ZAP 装载设计（已按 Linux `mdt_loader` 核对）：
  - metadata = `a540_zap.mdt` 原样（ELF 头/phdrs 0x94 + hash 段 0x1988 = **0x1A1C = 6684 字节**），
    物理连续、4 KiB 对齐缓冲；
  - 区域大小 `0x4000`（16 KiB）；装载：ph2（file off `0x3000`, len `0x1334`）→ region+0；
    ph1（off `0x1000`, len `0x1988`）→ region+`0x2000`；
  - 顺序：PAS init(metadata_pa) → mem_setup(region_pa, 0x4000) → auth_and_reset(5)；
    之后只读检查 `TrustControl` 是否变为 0，再执行 CP reset/start；
  - 缓冲在 auth 后保持存活（TZ 引用），失败时不启动 CP、保持 halt 并记录阶段。

## 3. 轮次表（全部为实机日志）

| 轮次 | boot ID | 镜像/内核（前 12/12） | 关键配置 | 结果 |
|---|---|---|---|---|
| CP-state | `0x50b0c185` | `570365647142`/`aeb7b438ddc1` | 五映射+状态，不 reset | 5/5 映射 `r=0 tlb=0`；冷状态 `me=303 st=800001c1 trust=1`；稳定 400 s |
| CP-start | `0x5121380b` | `64fdf3d6b527`/`e2e767c03dbb` | 首次 reset/ME_INIT | `stage=3 rc=-4 r=0 w=9`；reset/configure 通过；kick 超时 |
| Secure | `0x50993220` | `adb12b453090`/`3b76b685d068` | SCTLR 0x1027 | `rc=-2`（idle 前置检查失败），未进 reset |
| Power | `0x5230df48` | `b15bb427eb1d`/`d771e2ecd273` | 提前到 ~152 s | 黑屏、手动；日志停在 begin 后 ~5.6 s |
| Early | `0x510ea570` | `aea9a9fa18d4`/`6ccfb681c17d` | 提前到 ~99 s | 黑屏、手动；日志停在 begin（98.7 s） |
| A | `0x52b799d2` | `4e83eccf696b`/`dbd69e17fd99` | 短行 log-only，无环 | 66 rounds、自动回；`smc` 心跳 ~398 s |
| B | `0x512997ee` | `d6f5f9964c6e`/`90596895a064` | 短行 log-only + 环 | 66 rounds、自动回；tick 28–308、mark 329、smc 333 |
| C | `0x511b5457` | `d8e47238b93a`/`90596895a064` | 长行 log-only + 环 | 与 B 完全同型 → 行长无关 |
| D | `0x51e9e4e1` | `27d3bea335ae`/`520659f68973` | full-start + 每步心跳 | 黑屏、手动；`halt/ack/idle/swrst/cm3` 后**无 `trust`**；tick 到 ~370 s |
| E | `0x50edef20` | `8e159f8dcbc3`/`8f5453bb6979` | full-start，不写 TRUST_CNTL | 67 rounds、自动回、无黑屏；`rc=-4 r=0 w=9 trust=1` |
| F | `0x5121fa54` | `c7a64a40526c`/`ac8f24fec98a` | E + GCP8/9 回读 | 66 rounds、自动回；GCP8 正确；GCP9 见 §4.4 |

日志目录：`out/p21/persistent-logs/{103113Z,104613Z,110117Z,111210Z,112909Z,122149Z,123537Z,124642Z,130216Z,131811Z,133223Z}`。

## 4. 已验证结论

### 4.1 五 buffer 映射与冷状态（已确认）
- ring/PM4/PFP/GPMU/fence 全部 `r=0`、`MAPD ret=0 tlb=0`；GPU VA `0x2000000…0x2015000`。
- 冷状态 `me=0x303`、`rb=0/0/0/0`、`irq=0`、`trust=1`、`sec=0`；GPMU `ready=0/err=0/cm3rst=1`。
- `Status` 在不同轮次为 `0x800001c1` 或 `0x80000101`（bit6/7 差异），**不是逐 bit 相同**。

### 4.2 映射≠DMA（已确认的限制）
- 软件建表与 TLB 同步报告成功；**未验证** stream 路由、PTE 属性、缓存一致性和设备侧 DMA 可达性。

### 4.3 reset/configure/地址（已确认，GCP8）
- `me=2008000/0 pfp=200d000/0 rb=2000000/0 ctl=800020c am=1 prot=7`：
  ME/PFP/RB 基址高低字、`CP_RB_CNTL`、`CP_ADDR_MODE_CNTL`、`CP_PROTECT_CNTL` 全部正确回读。
- `CP_ME_INIT` 已写入 ring（`WPTR=9`）；`CP_HW_FAULT/CP_PROTECT_STATUS/CP_AHB_FAULT` 均为 0；
  SMMU `CB0_FSR=0x400`（AArch64 格式位）、`GFSR=0`。

### 4.4 SMMU 只读回读（GCP9，部分待核）
- `SCTLR=0x1027`、`TCR=0x802018`、`SMR0=0x80000000`、TTBR0 低字 `0x201e9000` 符合预期。
- **异常 1**：TTBR0 高字回读 `1`（期望 0）。
- **异常 2**：S2CR0 打印 `fffffff700000000`（期望 0）；同现象在更早 SSETUP 行也有 `fffffff6…` 前缀，
  疑似 BootLog 长行/64-bit 打印伪影，需 `%x` 分高低字重测。
- **异常 3**：PTE 查询原返回 `0x14102f003`（L3 表描述符，因软件 walker 少下潜一级）；
  **源码已修**（`8d457ec`），尚未实机复验真实叶子 PTE。

### 4.5 黑屏/停住与 TRUST_CNTL（已确认）
- D 轮：心跳 `halt/ack/idle/swrst/cm3` 后无 `trust`，tick 继续到 ~370 s；console/msgbuf 在 begin 后停止。
- E 轮：不写 `TRUST_CNTL`，full-start 安全（67 rounds、自动回、无黑屏）。
- **结论**：该寄存器写/回读是 D 的卡死源；强制退出 secure 模式在本机不安全。默认已关闭，仅
  `cheeseburger-gpu-trust-clear=1` 显式开启（未再验证）。

### 4.6 已排除的假设（A/B/C 与审计）
- **PMRD 窗口**：99 s 轮不在 PMRD 窗口也中断；PMRD 活动在 69–72 s 已结束 → 排除。
- **begin 行长**：B（短）与 C（长）心跳/控制台同型 → 排除。
- **console/msgbuf 通用故障**：A/B/C/E/F 的 console 均到 ~400 s，心跳正常 → 不是通用故障；
  D 的 console 停止与 TRUST_CNTL 卡死同源。
- **五映射/睡眠本身**：A/B/C/E/F 均健康 → 不触发停住。

## 5. 遇到的问题与解决记录

| # | 问题 | 解决/结论 | 状态 |
|---|---|---|---|
| 1 | UEFI 直接读 GPUCC 触发 Synchronous Exception | 移除 UEFI MMIO 读；XNU 用自有映射 | 已解决 |
| 2 | 整屏黑（早期） | A/B 指向 `SetClockFreqHz(gfx3d,257MHz)`；保持 rate=0 | 已解决（升频待做） |
| 3 | 第 4 个映射后停住（早期） | SMMU 窗口整轮只映射一次；状态前置；证据落盘 | 未复现 |
| 4 | 旧环形日志行混入 | BootLog 行带 boot ID；按当前 ID 过滤 | 已解决 |
| 5 | 门禁 pin 扫描无命中 | FD 是压缩容器；改扫 VM 构建树 `FVMAIN.Fv/FFS/.efi` | 已解决 |
| 6 | gate/pins 文件过期 | gate 每次落盘 `gate-<UTC>.json`；每轮 `p21-freeze-round.py` 冻结；旧文件标记失效 | 已解决（P0） |
| 7 | reset 失败路径无清理 | 模板化状态机；失败保持 halt/abandoned、绝不 unhalt；host 9/9 | 已解决（P1，host） |
| 8 | 日志中断 vs 内核停住无法区分 | CBHB 心跳环（31 块、10 s、独立线程直写）+ auto-reset `mark/smc` + reset 每步标记 | 已解决（P2） |
| 9 | full-start 卡死 | D 定位到 `RBBM_SECVID_TRUST_CNTL`；E 证明去掉后安全；默认关闭 | 已解决（规避） |
| 10 | CP 不取 ring | `RBRead=0`、无 fault、`trust=1`；候选根因见 §6 | **未解决** |
| 11 | TTBR0 高字 / S2CR 打印异常、PTE 查询错误 | PTE 查询已修源码；前两者待分高低字复测 | 待验证 |

## 6. 核心未决问题：CP 为什么不取 ring

已知：`RBRead=0`、`WPTR=9`、无 CP/SMMU fault、`trust=1`、地址/configure 正确。

候选根因（按当前证据排序）：
1. **secure 模式阻止非安全 CP 微码/ring**（推测）：`trust=1`，且强制写 `TRUST_CNTL=0` 会卡死 GPU 访问，
   说明该寄存器/GPU 处于安全世界控制。非安全 ring 可能被 CP 忽略；无 fault 也符合“没有发起取指”。
2. **SMMU 翻译/路由问题**（未验证）：TTBR0 高字回读 1、S2CR 打印异常；若 TTBR0 真的高位非零，
   取指会走错误根表。但没有任何 SMMU fault，说明 CP 可能根本没取指。
3. **CP 微码/加载语义**（未验证）：PFP/PM4 地址正确、ME_INIT 包与 sagit 一致，但 ring 发布顺序
   （unhalt/clean/RBWrite）与参考存在细节差异；也可能微码在 secure 模式下不执行。
4. **其他**：GPU 电源/时钟在 reset 瞬间的状态、CP 的 stream ID 与安全属性、CP 需要 GPMU 先运行等。

## 7. 决策选项（供审计选择）

### 选项 A（低风险，推荐先做）：数据质量复测
- 用 `%x` 分高低字打印 TTBR0/S2CR；尝试 sagit 式 64 位 TTBR0 写并回读；用修正后的 walker 读真实叶子 PTE。
- 现行 full-start 已安全、有心跳；一次启动即可。
- 价值：排除“TTBR0 高位/打印伪影”这一变量；确认页表与上下文确实正确。

### 选项 B（低风险）：SCTLR 单变量 A/B
- 在安全 full-start 下，仅切 `0x1027` vs `0x1167`（差值 `0x140`：CFIE bit6、HUPCF bit8），
  用心跳与 `GCP5/GCP6` 判读；不同时改 TrustControl/时序。
- 价值：确认 SMMU fault 配置是否影响 CP 取指（虽然目前无 fault）。

### 选项 C（大工程，安全敏感）：ZAP/PAS 正式路径
- 仓库无 PAS-13/SCM 驱动；ZAP 固件（`a540_zap.*`）仅为私有输入。
- 需要实现 SCM/PAS 认证与 ZAP 装载，或在安全世界配合下进入非安全 GPU 模式；工作量大、风险高，
  且必须遵守“不得绕过安全保护”。**需要用户/审计明确授权与设计**。

### 选项 D（替代调查）：确认 secure 流/取指路径
- 只读核对 SMMU 的安全 stream/context 配置、GPU 的 SID/安全属性、CP 取指是否走 secure bank；
- 可在不写安全寄存器的前提下判断“secure 模式是否确实阻止取指”。
- 价值：为选项 C 提供是否必要的证据；风险低。

**推荐顺序：A → B → D；若 A/B/D 均不能解释 `RBRead=0`，再评估 C（并先取得授权）。**

## 8. 风险与安全约束

- 不写 boot/recovery/固件/GPT；userdata 仅授权窗口；misc 仅 BCB 字段（备份+回读）。
- 不强制写 `RBBM_SECVID_TRUST_CNTL`（默认关闭）；不绕过 ZAP/PAS；不尝试降低安全启动。
- 每轮启动前重写 BCB、跑门禁；回 Recovery 后先回收日志再下一次启动。
- 心跳与日志 writer 共用 UFS 驱动：只能区分“console/msgbuf 路径停”与“内核/UFS 整体停”，
  不能证明 UFS 独立存活；普通 RAM 标记不作证据。
- 私有固件/镜像不提交；`out/` 为 gitignore，证据留在仓库卷。

## 9. 复现与证据索引

- 门禁：`python3.13 scripts/unattended/preboot-gate-p21.py out/p21/fb-qup3` → `gate-<UTC>.json`。
- 冻结：`python3.13 scripts/p21-freeze-round.py <tag> --boot-log … --monitor-log … --retrieval …`；
  历史轮 `--reconstruct`；索引 `out/p21/gpu-cp/rounds/INDEX.md`。
- 心跳：`scripts/logging/heartbeat_record.py`；读取端输出 `retrieval.json.heartbeat.records` 与
  `decoded/<boot>/heartbeat.json`；codec 测试 `tests/test_heartbeat_codec.py`（8/8）。
- Host 状态机：`tests/gpu_start_model.cpp` + `scripts/run-gpu-start-model-test.py`（9/9）。
- 关键代码：`package/BoardRuntime/CheeseburgerGpuStart.h`（模板状态机/失败清理/每步标记）、
  `CheeseburgerGpuCp.cpp`（探针/接线/模式 1/2/3）、`CheeseburgerGpuSmmu.cpp`（上下文/页表/PTE 查询）、
  `BootLog.c`（CBHB 心跳环）、`CheeseburgerAutoReset.cpp`（mark/smc）。
- 私有输入：`out/p21/gpu/firmware/`（不提交）。

## 10. 审计核对清单

1. 轮次表与冻结目录/`retrieval.json` 的 boot ID、镜像/内核/payload/固件哈希是否一一对应；
   是否引用了旧的 `offline-gate.json`/`pins.json`（应禁止）。
2. D→TRUST_CNTL 结论：心跳最后标记、tick 延续时间、E 的对照是否支持；是否夸大为“整机停”。
3. GCP8/GCP9 数值：TTBR0 高字=1、S2CR 打印异常、PTE=0x14102f003 的解释是否被正确处理为“待核”。
4. 是否把“映射成功”当成 DMA 成功、把“configure 子集回读”当成整套验证。
5. 是否在任何轮次强制写 `TRUST_CNTL`（当前默认关闭；仅显式 arg）。
6. 分区写入边界与 misc/userdata 授权是否逐项符合 AGENTS.md。
7. secure/ZAP 路线是否在获得授权前被实施（当前未实施）。
8. 证据冻结、门禁落盘、AGENTS 纪律同步是否完整。

## 11. ZAP loader 实现与布局更正（2026-10-31 晚）

本节补充并**更正** 2.12/2.13 的装载布局；以实际固件 program header 与本机上游实现为准。

### 11.1 实际布局（由 `a540_zap.mdt` ELF 动态解析）

- 磁盘文件：`a540_zap.mdt` 6684 字节，`.elf/.b00` 同头，`.b01` 6536 字节，`.b02` 4916 字节。
- 3 个 program header（0x94 = 52 + 3×32）：
  - ph0：`p_type=0`、`p_offset=0`、`p_filesz=0x94`（ELF 头 + phdrs，元数据头）；
  - ph1：`p_type=0`、`p_flags=0x2200000`（bits 24..26 = 2，hash 段）、`p_offset=0x1000`、
    `p_filesz=0x1988`、`p_paddr=0x7000`、`p_memsz=0x2000`；
  - ph2：`p_type=1`、`p_flags=0x8000007`（bit 27 可重定位）、`p_offset=0x3000`、
    `p_filesz=p_memsz=0x1334`、`p_paddr=0x5000`、`p_align=0x100000`。
- 元数据 = ph0 头（0x94）紧跟 hash 段（0x1988）= **0x1A1C**；`mdt[0x94:]` 与 `.b01` 逐字节相同。
- 唯一可装载段是 ph2（`PT_LOAD`、非 hash）；因 `p_offset+p_filesz > mdt.size`，其数据来自
  `.b02`，装入 region 的 **+0**（`p_paddr - min = 0x5000 - 0x5000`）。
- 因此 Linux `qcom_mdt_get_size()` 给出的 region = `ALIGN(0x5000+0x1334) - 0x5000` = **0x2000**
  （不是此前 2.12 写的 0x4000；0x4000 把 hash 段的 `p_memsz` 误算进了可装载范围）。
  `PAS mem_setup(region_pa, 0x2000)`；可重定位目标按 `p_align=1 MiB` 对齐。
- `.b00/.b01` 不按代码装载；hash 段只作为元数据的一部分交给 `PAS init`。

### 11.2 调用顺序与依据

- Android `drm/msm` `zap_shader_load_mdt()`：`qcom_mdt_load()`（内部 `PAS init` → 可重定位时
  `mem_setup` → 装载段）→ `qcom_scm_pas_auth_and_reset(13)`；首次装载后**不**调用
  `BOOT_SET_REMOTE_STATE`。`qcom_scm_set_remote_state(0, 13)` 只在映像已加载、GPU 再次初始化时使用
  （`a5xx_zap_shader_resume`，A506 除外）。本轮因此不额外 resume。
- `IS_SUPPORTED`（service 2 cmd 7）在本机返回 0，但逐命令 availability（init/setup/auth）均为 1；
  sagit loader 明确记录并注明“IS_SUPPORTED 不是授权结果”，本轮同样记录、不用它做门禁。
- 只有 `init`、`mem_setup`、`authenticate` 均返回成功才继续 CP reset/start；失败即停用 GPU，
  保留 CPU 显示路径。认证后 metadata/image 页不再读、清、释放或做 cache 操作。

### 11.3 实现与诊断

- 新文件：`package/BoardRuntime/CheeseburgerZap.h`（QZAP 容器 + ELF32 MDT 解析，纯宿主可测）、
  `scripts/p21-zap-firmware-header.py`（私有固件打包为生成头，不入库）、
  `tests/qcom_zap_model.cpp`（59 项：合法/非法布局、段重叠、溢出、对齐、split 段、真实固件）。
- `CheeseburgerGpuPas.cpp`：分配 4 KiB 对齐 metadata 与 1 MiB 对齐 region（优先 32 位物理地址掩码，
  40 位回退并记录），依次 `init(metadata_pa)` → `setup(region_pa, 0x2000)` → 拷贝 `.b02` →
  `dsb`/cache clean → `authenticate()`；每步记录 `Status`、原始 `x0`、`x1`。
- 心跳阶段：`zapinit / zapsetup / zapauth / zapdone / zapfail`；BootLog 新行
  `ZAP0 rc md img al m i s a t sup bid`。`cheeseburger-gpu-start=1` 时 ZAP 为强制前置，无绕过开关。
- 认证后只读记录 `TrustControl`（before/after）以及既有 CP/SMMU/RPTR 诊断；仍不写 `TRUST_CNTL`。

### 11.4 未验证

- 本机安全监视器是否接受该元数据/物理区域（签名、PAS-13 平台依赖、GPU 电源租约）；
- 认证成功是否足以让 CP 取 ring（`RPTR` 前进）；GPMU/fence 尚未开始。

### 11.5 首轮 ZAP 实机结果（boot `0x79802690`，镜像 `395384a0…`）

- 日志 `out/p21/persistent-logs/20261031T155808Z/`：59 rounds、写入错误 0、心跳 27 条、400 s 自动回 Recovery。
- `ZAP0 rc=ffffffffffffff90(-112) md=10a257000 img=111300000 al=100000 m=40 i=0/0 s=0/0 a=deadbeef t=1 sup=0`；
  控制台 `CB GPU ZAP: image copy rejected 8192/8192 pa=111300000`。
- **已确认**：SCM probe 成功；PAS `init(metadata_pa=0x10a257000)` 与 `setup(region_pa=0x111300000, 0x2000)`
  均返回 `x0=0`，即安全监视器接受 **>4 GiB 的 40 位物理地址**。`sup=0` 与 sagit 的“非授权结果”一致。
- **本地缺陷**：`Image::imageInto` 沿用了 sagit 装载器的 32 位目的地址上限，把本机 40 位 region 拒绝了；
  不是 PAS/硬件失败。已改为 40 位上限并在宿主测试加入 40 位接受/超界拒绝。
- `allocBuf` 的物理掩码只是放置请求，返回的高地址必须复核；现在 32 位尝试越界即释放并回退 40 位，
  并记录真实宽度（`mm/rm`）。
- 失败按约定停用 GPU：`GCP5 stage=8`、CP 未 reset，`frames=4`，CPU 显示路径正常；`t=1` 未变（未写 TRUST_CNTL）。
- **未验证**：`authenticate()` 与 CP 取 ring；下一轮只需完成镜像拷贝、cache clean 与 auth。

### 11.6 第二轮：ZAP 认证成功（boot `0x523473ba`，镜像 `830a3f29…`）

- 日志 `out/p21/persistent-logs/20261031T161549Z/`：59 rounds、写入错误 0、心跳 27 条、400 s 自动回。
- `ZAP0 rc=0 md=10a258000 img=111100000 al=100000 m=40 i=0/0 s=0/0 a=0/0 t=1->1 sup=0`；
  控制台 `CB GPU ZAP: ok ... init=0/0 setup=0/0 auth=0/0 trust=1->1 sup=0`。
- **已确认**：PAS `init`、`mem_setup`、`authenticate` 三次 SMC 原始 `x0` 均为 0，签名被安全监视器接受；
  metadata 与 image 都在 40 位物理区。这是“认证成功”的设备证据，不是 CP 执行证据。
- 认证后 `RBBM_SECVID_TRUST_CNTL` 仍为 1。对照 sagit `AdrenoSubmit.h`：提交前的安全条件允许
  `trust==1`，但要求 `authenticatedZAP`；只有 ZAP 不可用时 Android 才写 `TRUST_CNTL=0`。因此
  “认证成功 ≠ TrustControl=0”，不能把 t=1 单独当作失败。
- 同一轮 CP reset rc=0、kick rc=-4、`RPTR=0 WPTR=9`、`CP_HW_FAULT/PROTECT/AHB=0`、`CB0 FSR=0x400`、
  `GFSR=0`；显示 frames=4。即认证与上下文都成功但 CP 仍未取 ring。
- **下一假设**：GPU 的 RBCCU/SP 核心电源域由 `a5xx_pm_resume` 单独打开（0xa887/0xa881 + 状态位 20），
  它们不属于 GPUCC 时钟投票；若未打开，RBBM 可读但 CP 微引擎不运行。第三轮加入上电与回读，并记录
  SID2/S2CR2 与 CB1/CB2 fault（secure stream 归属）。

### 11.7 第三轮：RBCCU/SP 上电（boot `0x51f37561`，镜像 `58cd0feb…`）

- `CB GPU power: before rbccu=778001/1 sp=778001/1 after=140000/140001`：**RBCCU 与 SP 核心域的
  status bit20 原本为 0（未上电）**；按 `a5xx_pm_resume` 写 0xA887/0xA881=0x778000 后两域均为
  `0x00140000`（bit20=1）。ZAP 认证后仍为 `0x140000`。这是本轮新增的、此前从未执行的电源步骤。
- 认证仍成功（`i=0/0 s=0/0 a=0/0`），CP reset rc=0、kick rc=-4、`RPTR=0 WPTR=9`、无 CP/SMMU fault；
  `SID2 SMR=0 S2CR=0x200ff`（stream 2 为 fault/未映射），CB1/CB2 FSR=0。显示 frames=5、400 s 自动回。
- 结论：核心域上电是必要但不足的条件；CP 仍未取 ring，且没有可见的 SMMU/CP fault。
- 第四轮改动：改用设备 vendor 驱动的 ring 配置（`CP_RB_CNTL=0x20C`，不使用 NO_UPDATE；把
  `CP_RB_RPTR_ADDR` 指向已映射的 fence 页），kick 失败后先重发一次 doorbell，并记录 kick 之后的
  **live MEControl/VBIFHalted、scratch RPTR、CP_CNTL、ME ucode debug**，用于区分“引擎已停”与
  “引擎在跑但 ring 不前进”。宿主模型同步更新并通过。
