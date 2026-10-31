# P21 GPU/CP 最新修改方向（依据 2026-10-31 审计）

> 完整现状、A–F 轮证据与审计决策选项（A/B/C/D）见 `docs/P21_GPU_CP_FULL_REPORT_20261031.md`。

- 依据：`docs/P21_ADRENO540_REVIEW_20261031.md`（审计提交 `340d8c8`）、`docs/P21_GPU_CP_STATUS_REPORT.md`（状态报告）
- 原则：先证据与可观察性，后硬件动作；单变量；不伪造成功；未验证前不重复 CP reset。
- 当前：设备已由用户手动返回 Recovery；**暂停中，不再启动**。

## P0 冻结证据与构建对应关系（离线，先做）

问题（审计发现 1/10）：候选目录每轮被覆盖；`out/p21/fb-qup3/offline-gate.json` 仍指向 `a4af3b18…`、
`pins.json` 的 payload 仍是 `f9bafc4d…`，均不能证明当前镜像 `aea9a9fa…`。

改动：
1. 新建不可覆盖的每轮目录 `out/p21/gpu-cp/rounds/<UTC>-<tag>/`，保存：
   - `manifest.json`：image / kernel / ADT / payload / firmware 的 SHA-256、args 全文与字节数、
     gate 结果 JSON、pins 扫描结果、git HEAD、工作区是否干净、构建时间；
   - `boot.img`（约 80 MB）与 `kernel.macho`（约 122 MB）副本（当前卷余量约 446 GiB，足够多轮）；
   - `boot.log`（fastboot 输出）、`monitor.log`、`retrieval.json` 的副本或路径。
2. 修改 `scripts/unattended/preboot-gate-p21.py`：把结果写入候选目录
   `gate-<UTC>.json`（不再只打印），并核对 `uefi/packaging.json` 的 `sha256` 与当前镜像一致；
   pins 扫描输出带时间戳落盘。
3. 把 `out/p21/fb-qup3/offline-gate.json`、`out/p21/fb-qup3/pins.json` 标记为历史记录，禁止引用。
4. 为已发生的轮次补 frozen manifest（`a4af3b18` / `57036564` / `64fdf3d6` / `adb12b45` / `b15bb427` / `aea9a9fa`），
   每项注明哈希来源（终端输出重建或日志）。
5. 验收：任一 manifest 能独立重算出对应镜像/内核/固件哈希；gate 记录与镜像一一对应。

### P0 执行状态（2026-10-31 已完成，离线）

- `scripts/unattended/preboot-gate-p21.py` 现在把每次结果写为候选目录下的 `gate-<UTC>.json`，
  并新增“packaging.json 必须指向本候选 boot 镜像”的检查；本次已对当前镜像 `aea9a9fa…` 重新运行门禁，
  记录为 `out/p21/fb-qup3/gate-20261031T114312Z.json`（PASS，args 669，pins 命中）。
- 新增 `scripts/p21-freeze-round.py`：冻结当前轮的镜像/内核/ADT/payload/固件/配置副本与全部哈希、
  gate/pins、boot/monitor/retrieval 引用、git 状态；历史轮次可用 `--reconstruct` 以哈希方式补录。
- 最新轮已冻结：`out/p21/gpu-cp/rounds/20261031T114334Z-cp-early2/`（约 392 MB，含产物副本，
  boot `0x510ea570`）。5 个历史轮次已补录为 `reconstructed`（仅哈希，产物已被覆盖），索引见
  `out/p21/gpu-cp/rounds/INDEX.md`。
- `out/p21/fb-qup3/offline-gate.json`、`pins.json` 已用 `STALE-EVIDENCE.md` 标记为旧记录，禁止引用。
- 注意：`out/` 在 `.gitignore` 中，证据副本只在本机/仓库卷；提交的是脚本与文档。

## P1 reset 失败路径与执行上下文审计（离线）

问题（审计发现 2）：`gpuReset` 先写 `VBIFHalt=15`，ack 超时、idle 失败或后续检查失败时直接返回，
没有恢复/停用定义，可能遗留未知停流状态。

改动：
1. 把 `gpuReset()/gpuStart()` 的状态机抽成模板（注入寄存器访问器），kernel 实例用 MMIO accessor，
   host 用 mock；参考 `docs/upstream/reference/sagit/QcomGPU/AdrenoStartup.h` 的 `template<class Registers>` 结构。
2. 失败语义表（每个失败分支都要有明确结果）：
   - VBIF halt ack 超时 → 保持 halt，标记 `abandoned`，不 kick；
   - idle 检查失败（含重试后）→ 同上，记录 `Status/MEControl/VBIFHalted` 原值；
   - MEControl 非 halt / SW reset 回读失败 / CM3 失败 → 同上；
   - 任何失败后**绝不** unhalt 或写 ring；统一 exit 先写 `cb_cp` 行（不依赖 console）。
3. host 模拟：对每个失败点注入故障，断言寄存器写入序列与最终状态（保持 halt）符合设计。
4. 执行上下文：确认 `gpuReset` 所在 kernel thread、是否可能持有 IOLog/console 锁、`IOSleep` 与
   `CheeseburgerAutoReset` 线程的关系；关键路径移除 IOLog，仅用 `cb_cp` 行。
5. 验收：失败分支均可在 host 测试复现；源码中不存在“失败后继续 unhalt/kick”的路径。

### P1 执行状态（2026-10-31 已完成，host-verified）

- 新增 `package/BoardRuntime/CheeseburgerGpuStart.h`：把 `reset/configure/start` 抽成模板（寄存器访问器注入），
  `CheeseburgerGpuCp.cpp` 改为 `Startup<Mmio>`；失败统一进入 `fail()`：记录原因、标记 abandoned、
  写 `MEControl=Halt`/`VBIFHalt=15`，**绝不 unhalt 或再次提交**；kick 超时也会重新 halt（相对旧实现的行为变化）。
- 新增 `tests/gpu_start_model.cpp` + `scripts/run-gpu-start-model-test.py`：host 寄存器模型覆盖 8 个用例
  （成功路径、VBIF ack 超时、idle 失败、MEControl 非 halt、SW reset 回读失败、CM3 失败、kick 超时、
  未 reset 直接 start）。结果 **HOST_MODEL_PASS 8/8**，记录
  `out/p21/gpu-cp/host-tests/20261031T115416Z/result.json`（含源码/头文件/二进制哈希与编译器版本）。
- 内核已用新模板重编通过（`native/kernel.macho` `fb78e5c4…`，**未打包、未启动**；候选镜像仍为冻结的
  `aea9a9fa…`，未受影响）。
- 未做：真机验证（依赖 P2 的独立存活判据）。

## P2 可观察性：独立存活判据（需用户确认通道与授权）

问题（审计发现 7）：auto-reset 在 SMC 前调用 `IOLog`，console 锁卡住也会阻止复位；
普通 RAM 标记没有独立读取通道；持久日志 writer 与被怀疑的 console/msgbuf 路径相关。

候选通道（按优先级评估）：
1. 屏幕（用户可观察，弱证据）；
2. iOS userland ADB（本项目当前无 userland USB stack，预计不可用；先只做一次验证）；
3. UART（本机未引出，排除）；
4. **独立心跳线程写现有持久日志窗口的专用 slot**：不经 `copy_msgbuf`/console，使用
   `native_ufs_log_write` 直接写一个独立 slot，每 10 s 递增；Recovery 用现有读取脚本读取。
   仍在已授权的 32 MiB 日志窗口内，不新增窗口外写入；与被怀疑的 console/msgbuf 路径独立，
   但仍与 UFS 驱动同源。
5. 持久 Data store 心跳页（需要单独授权；已有跨重启保持证据，但属于新增写入模式）。

流程：
- 先在正常启动证明通道 4 可读、节拍正确；再在故障启动中观察其停止时刻。
- 受控 A/B：只改变 `begin` 行长度（短行 vs 长行），**关闭 CP 写**（不 reset、不 kick），
  其余不变；同时记录心跳、console、auto-reset 状态。每次只改一个变量。
- 验收：一次故障后能明确回答“98.7 s 之后内核是否仍在运行”，不依赖 console 与 log writer。

### P2 执行状态（2026-10-31，实现完成，A/B 结果待回收）

- **心跳块**：`BootLog.c` 在日志窗口**最后一个空闲块**（`window_blocks-1`，不落在 header/msg/slot 区域）
  直接用 `native_ufs_log_write`+`sync` 写 `CBHB0001 boot=… seq=… sec=… tag=… state=… rounds=…`，
  独立线程每 10 s 一次，**不经过 msgbuf/console**。`CheeseburgerAutoReset` 在最终 `IOLog` 之前写 `tag=mark`、
  在 PSCI SMC 之前写 `tag=smc`。
- **读取**：`scripts/logging/heartbeat_record.py` 解析；`read-cheeseburger-log.py` 把结果写入
  `retrieval.json.heartbeat` 与 `decoded/<boot>/heartbeat.json`。codec 自测 `tests/test_heartbeat_codec.py` 7/7。
- **受控模式**：`cheeseburger-gpu-start=1` 完整启动；`=2` 只打印长 begin 行后返回（无 GPU 写）；
  `=3` 只打印短 begin 行后返回。A/B 只改行长度、关闭 CP 写。
- 已构建候选 A（`start=3`，短行）：内核 `dbd69e17…`（native `6133c8d5…`）；本轮结果待回收。
- **限制**：心跳与日志 writer 共用 UFS 驱动，只能区分“console/msgbuf 路径停住”与“内核/UFS 整体停住”；
  普通 RAM 标记仍不作为证据；不使用窗口外写入。

### P2 结果与后续（2026-10-31）

- A/B：长/短 begin 行、五映射、console、8 s 睡眠均健康到 400 s（A `0x52b799d2`、B `0x512997ee`、C `0x511b5457`）。
- D（full-start + 逐步心跳，boot `0x51e9e4e1`）：reset 到 `cm3` 后**卡在 `RBBM_SECVID_TRUST_CNTL`**，
  心跳继续到 ~370 s，说明内核/UFS 活着、console 路径受阻。**强制退出 secure 模式在本机会卡死 GPU 访问，
  已改为默认关闭（需显式 `cheeseburger-gpu-trust-clear=1`）。**
- E（下一步）：full-start、不写 TRUST_CNTL，观察 `ready/cfg/addr/kick` 与 `RBRead`；
  若 kick 仍不前进，再回到 SMMU/取指与 secure 世界（ZAP/PAS）的前提核查。

### E 轮结果（2026-10-31）

- full-start 不写 `TRUST_CNTL`：boot `0x50edef20`，67 rounds，自动复位，无黑屏；
  `GCP5 rc=-4 r=0 w=9`（kick 超时），`GCP6 trust=1`，无 SMMU/CP fault。
- **卡死源已定位并规避**：`RBBM_SECVID_TRUST_CNTL` 的写/回读在 D 轮卡住 GPU 访问路径；
  现在默认不写、仅显式 opt-in。
- 下一步：在安全路径下补 GCP8/GCP9 只读回读（ME/PFP/RB 高低字、CP_PROTECT/ADDR_MODE、TCR/TTBR/SMR/S2CR、
  PTE 抽样），并核对 ZAP/PAS 前提；之后再评估 SCTLR/secure 世界对 CP 取指的影响。

### F 轮结果（2026-10-31，只读回读）

- `GCP8` 证明 ME/PFP/RB 基址、RB_CNTL=0x800020c、CP_ADDR_MODE=1、CP_PROTECT_CNTL=7 全部正确回读。
- `GCP9`：SCTLR=0x1027、TCR=0x802018、SMR=0x80000000、TTBR0 低字=0x201e9000 正确；
  **待核**：TTBR0 高字回读 1（期望 0）、S2CR 打印 `fffffff700000000`（期望 0），疑似 64-bit 打印伪影；
  PTE 查询少下潜一级（已修源码），下一轮验证叶子 PTE。
- CP 仍 `RBRead=0`、trust=1、无 fault；secure 模式/ZAP/PAS 是下一个决策点。

## P3 SMMU 与 CP 前置验证（硬件，待 P2 判据可用）

问题（审计发现 4/5/9）：五映射只证明软件建表/TLB 报告成功；configure 只回读关键子集；
SCTLR 位语义未核定。

改动：
1. 新增只读核对行 `GCP8/GCP9`：ME/PFP/RB 基址高低字、`CP_PROTECT_CNTL`、`CP_ADDR_MODE_CNTL`、
   `TCR/TTBR`、`SMR/S2CR`、页表 PTE 抽样（iova→pte→pa）。
2. `SCTLR` 单变量 A/B：固定其它变量，只切 `0x1027` / `0x1167`（差值 `0x140`：CFIE bit6、
   HUPCF bit8）；逐 bit 解码以本机 SMMU 实现为准，不写未验证语义。
3. ring 发布顺序对齐 sagit（unhalt、填充、clean、`RBWrite` 的顺序），并记录与当前实现的差异。
4. 小范围可控 DMA 验证优先于 kick；在 P2 判据下确认电源与访问权限。
5. 验收：每项有独立读回证据；不使用“映射成功”推断 DMA 成功。

## P4 恢复 CP 分阶段推进

- `RBRead` 前进（唯一 kick 成功判据）→ GPMU 固件（AGC 0x9800、`CM3_SYSRESET=0`、
  `GPMUReady==0xbabeface`）→ 最小 fence/提交 → 用户客户端 → Metal/合成。
- 每阶段：可复现输出、回滚入口、真实实机证据；初始化成功不等于 Metal 可用。

## 明确不做

- 不引用旧 `offline-gate.json` / `pins.json` 作为证据；
- 未验证前不重复 CP reset/kick；
- 不新增分区或日志窗口外写入，不随意改 MMIO 做诊断；
- 不用普通 RAM 计数或同一 log writer 当“独立存活证据”；
- 不把 257 MHz 黑屏、PMRD、reset 失败等单点/小样本结论扩展为通用根因（以审计与 HANDOFF 勘误为准）。

## 当前状态

- 设备：Recovery（用户手动返回；400 s 自动复位未生效）。
- 最新镜像：`aea9a9fa…`（内核 `6ccfb681…`、payload `61666a13…`、固件 `b51ed096…`，args 669）；
  日志 `20261031T112909Z`（boot `0x510ea570`，8 rounds），停在 `begin` 打印后。
- 下一步：等待确认后从 P0（离线、不触设备）开始；P1 同为离线；P2 需用户确认通道与授权后再启动设备。
