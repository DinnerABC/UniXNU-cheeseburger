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
