# P21 Adreno 540：以首个 GPU 任务为目标的实施方案

本方案以 OnePlus 5 / MSM8998、iPadOS 17.7.11 / 21H461 为目标。优先让 A540 命令处理器工作，再验证一次真实 GPU 内存任务，最终推进到系统图形合成。不要把写代码、寄存器回读或主机测试当成 GPU 已运行。

## 当前起点

- CPU 显示、桌面、触摸和持久 Data 已有实机进展；GPU 尚未完成 CP 启动，`RPTR=0`、`WPTR=9`，尚无实机成功的 GPMU/Metal。
- F 轮 `out/p21/persistent-logs/20261031T133223Z/`、boot `0x5121fa54`：指定几个基址和控制寄存器回读成功，但 TTBR 高字、S2CR 展示和 PTE 查询存在疑问。建表成功不代表 GPU DMA 成功。
- 不使用 `cheeseburger-gpu-trust-clear=1` 强制写 TrustControl：已有启动中断线索，真实安全状态及因果尚不明确。保持已验证的 `UNIXNU_GPU_RATE=0` 起点，不在首轮同时调频。
- F 轮镜像、gate 和私有固件在 `out/p21/`；后续构建生成独立候选和对应 hashes、gate、启动及日志收据，避免覆盖。D 轮旧 heartbeat 混入问题见 `docs/P21_GPU_CP_SECOND_REVIEW_20261031.md`。

## 直接执行：第一轮合并必要修正并测试

以 F 轮已运行代码为基线，合并以下同一目标的工作，一次构建、一次启动，不把每个打印拆成一轮测试：

1. **SMMU 地址与页表修复**：审计并采用当前 `8d457ec` 的叶子 PTE 查询修复，给不同层、缺失项和地址边界加宿主测试。对照 `docs/upstream/reference/sagit/drivers/bus/QcomSMMU/HardwareContext.h` 与本机设备树，核实 TTBR0 的真实写入宽度、对齐、寄存器偏移、物理根地址、TCR 输入位宽与 bank/stream 配置。修正已明确的实现错误；对有歧义的值分别输出原始 32 位高低字、根页物理地址、真实叶子 PTE、预期 PA、FSR/GFSR，不用 64 位格式拼接代替原始读回。额外 64 位写不是只读操作，只有对照证明访问方式及顺序后才改。
2. **避免误判导致无效重试**：修心跳多生产者共槽及旧 boot 混读；用共享序号和同步或分区槽位，读取端按当前 boot ID 过滤并记录丢失。覆盖并发、环绕、跨 boot、失败的宿主测试；保持现有 userdata 日志窗口边界。心跳与 UFS 共用驱动，因此能证明采样时刻该线程与 UFS 路径工作，不能证明系统每个部分存活。
3. **对照真正的初始化链**：以 `docs/upstream/reference/sagit/QcomGPU/AdrenoStartup.h`、`AdrenoSubmit.h`、`QcomGPUHardware.cpp` 为参考，核对并修复可确认的不一致：PM4/PFP 裸固件长度和装载偏移、ring 地址及 64 位寄存器顺序、cache clean/barrier、CP_ME_INIT 包和 WPTR、停流/解停顺序、错误与超时处理。检查实际版本，与当前板级 CPU cache 和 4 KiB 页面约束适配；不要直接套用 A740 固件或硬件地址。
4. **恢复路径**：对 `CheeseburgerGpuStart.h` 的 halt/abandon 和失败分支做有界宿主测试；当前策略是失败后保留停用状态，并不是已证明硬件恢复成功。若 MMIO 自身可能阻塞，日志要在进入该访问之前留下可分辨标记，不能通过取消必要检查伪造成功。
5. **批量输出一页关键结果**：镜像 ID/boot ID、时钟与安全状态、页表/stream、CP reset stage/rc、RPTR/WPTR、CP/SMMU fault、心跳写错数、显示帧；对每一步保留真实状态与时间。不上报未执行步骤为成功，不输出无用刷屏。

用 `docs/P21_BUILD_GUIDE.md` 的本版本链路重编 runtime→kernel→prelink→payload→独立 UEFI overlay→boot image；每一步使用本轮输入，核对真实固件编入的 hash。记录可复现命令和 gate 后，在当前授权范围内先回收上一轮日志，确认设备 `3b25cc7c` 和唯一 ADB 服务，再做一次 `fastboot boot`。不自动循环；用户指示 Recovery/设备状态为准。按现有授权保留 userdata 日志及 Data store，misc 仅已授权 BCB 命令字段，其他分区不写。收回本轮新 boot ID 日志。

## 根据第一轮结果直接推进

- **TTBR/PTE/stream 或固件装载有明确不一致**：按本机同 SoC 正式实现修复，重新构建验证；不要再重复收集相同日志。
- **RPTR 从 0 前进至 9**：记录对应提交及状态，进入 GPMU。按 sagit `AdrenoStartup.h` 的 firmware power、间接命令、CM3 启动与 `GPMUReady=0xbabeface` 路径逐项适配，保留超时与错误处理；随后运行最小 ring/fence DMA 并从 CPU 验证结果。
- **RPTR 仍为 0 且上下文确认正确**：同步核对 CP 的安全属性、固件来源和实际 ZAP/PAS 前提，参考 sagit `QcomGPUHardware.cpp` 的平台 lease、认证 ZAP 及 mapper handoff，而不是强制写 TrustControl。若正式链路需要的 SCM/PAS 服务在本机不存在，明确缺失依赖后实现适配，并以设备原始 firmware 和相应 ABI 为准；不要把 `trust=1` 当成已证明的唯一原因。
- **出现硬件 fault 或线程停滞**：按捕获到的第一个 CP/SMMU fault、失败点和当前 boot 心跳选择修复；没有该信息时标记证据不足。保留可启动 CPU 显示候选。

## CP 之后的交付

按顺序完成 GPMU ready → GPU 写 fence/缓冲并由 CPU 回读 → 用户客户端与资源生命周期 → 最小 2D/渲染任务 → 用户态图形栈/Metal 的 ABI、编译器和 IOSurface 集成 → 合成画面。每步说明实机可验证结果与仍缺少的能力；不以 Metal 类名注册、fake 成功或单次启动代替真实渲染。

工作中遵循固定官方文档 `.work/p21-official-framework/docs/porting/gpu.md` 和项目 `AGENTS.md`；有上游实现优先移植并逐项适配，不随意改变设备的 CPU、内存、Data、显示参数。每轮提交源码与 HANDOFF；不提交固件、镜像及私人日志。
