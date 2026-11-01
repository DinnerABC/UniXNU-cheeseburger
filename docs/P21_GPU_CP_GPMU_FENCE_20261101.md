# P21 GPU 里程碑：CP、GPMU 与 fence（2026-11-01）

本文件记录 A540 图形后端到“CPU 验证 GPU DMA”的实际结果、证据边界和下一步。
设备 `3b25cc7c` 已回 Recovery；本文件不需要设备。

## 1. 当前已确认的链路

```text
GPU power/clocks
  -> SMMU 上下文 + 低地址页表（UEFI Reserved pool）
  -> 硬件 ATOS 成功（PAR 页 = 期望 PA）
  -> CP reset/configure + CP_ME_INIT（RPTR 0 -> 9）
  -> GPMU 固件上传（GPMUReady = 0xbabeface）
  -> ZAP/PAS auth（rc=0）
  -> CP_SET_SECURE_MODE(0) + CP_CACHE_FLUSH_TS
  -> CPU 读到 fence 序列号 1
```

### 1.1 BIMC GFX 时钟是决定性变量

- DTS `arm,smmu-kgsl@5040000` 的 clocks 包含 `gcc_gpu_cfg_ahb_clk`、
  `gcc_bimc_gfx_clk`、`gcc_gpu_bimc_gfx_clk`。
- UEFI 的 GPU 电源表只加了 ClockDxe 名称，实际没有开启 `gcc_bimc_gfx_clk`
  （GCC 0x46040）；读回为 `0x80000000`（halted）。
- 内核 GPU 探针直接写 CBCR bit0 后读回 `1`。
- 同轮 ATOS 从 `rc=-2` 变为 `rc=0`，PAR 页与期望 PA 一致；随后 CP 首次读取 ring。
- 证据：boot `0x3e8f27de`，日志 `out/p21/persistent-logs/20261101T084226Z/`。

### 1.2 低地址 SMMU 表池

- UEFI `DarwinBoot` 在 ExitBootServices 前用
  `AllocatePages(AllocateMaxAddress, EfiReservedMemoryType, 16 pages)` 预留 64 KiB，
  只追加一个 `cheeseburger-smmu-pool=<pa>` boot arg。
- XNU 用 `IOMemoryDescriptor::withPhysicalAddress` +
  `kIOMapAnywhere | kIOMapCopybackCache` 映射；用 `IODeviceMemory` 默认映射会让
  `bzero` 的 `dc zva` 在 Device 内存触发 unaligned data abort。
- 实机 `SMPOOL pa=fff40000 bytes=65536 used=12288`、`SMICRO lt=3 ht=0`。
- 低池本身**没有**让 ATOS 成功；它排除了高地址页表假设。决定性变量仍是 BIMC GFX 时钟。
- 证据：copyback boot `0x3f3758ae`，日志 `20261101T074815Z`；panic 符号化见
  `20261101T070951Z`/`20261101T080842Z` 与 HANDOFF。

### 1.3 kick 完成判据

设备在 CP 已消费 ring 时返回 `Status=0x80000101`。旧判断
`!(Status & ~1)` 只允许 bit0，错误地把完成当成超时并 halt；改为
`idleStatus(Status)`（允许 reset 使用的 `0x800001c1` idle 位）并保留
`Status3` bit24 检查。宿主模型新增 `Status=0x80000101` 成功用例。

证据：kick boot `0x3df0a447`，`GCP5 stage=3 rc=0 r=9 w=9`、`GCP12 rptr=9`、
`ZAP0 rc=0`；日志 `20261101T093608Z`；提交 `8fbd910`。

### 1.4 GPMU

按 sagit 顺序在 `CP_ME_INIT` 后、ZAP 前上传 GPMU 命令流：

- `CB GPU start: gpmu OK ready=babeface err=0 rptr=11`
- `GCP12 rptr=11`、`GCP13 rw=11/11`、ZAP `rc=0`、`trust=1`
- 日志 `20261101T100445Z`，boot `0x3ce1812b`；提交 `f50d417`

### 1.5 fence（当前最高证据）

在 GPMU/ZAP 后追加：

```text
CP_SET_SECURE_MODE(0)  (packet 0x66)
CP_WAIT_MEM_WRITES     (packet 0x26)
CP_CACHE_FLUSH_TS      (packet 0x46, fence IOVA, sequence)
```

实机：

- `CB GPU start: fence OK seq=1`
- `post-auth trust=0 rptr=9->19 wptr=19`
- `GCP13 rw=19/19`
- ZAP `rc=0`，GPMU `ready=babeface`
- 日志 `out/p21/persistent-logs/20261101T102647Z/`，boot `0x3d6b6baf`
- 镜像 `3af1fd5b…`、内核 `10223927…`、firmware `80f81754…`；提交 `841925e`

fence 页由 CP 写入，CPU flush 后读到 1。这证明 **CP 执行命令并经 SMMU 完成 DMA**，
不是只前进 RPTR。

## 2. 证据边界

### 已实机验证

- BIMC GFX 时钟使能后 SMMU/ATOS 可用。
- 低地址池被真实使用（`lt=3 ht=0`）。
- CP kick 成功，RPTR 前进。
- GPMUReady=0xbabeface。
- ZAP/PAS init/setup/auth 全 0。
- fence 序列号由 GPU 写回、CPU 读到。
- 400 s 自动返回在正常轮和带 panic 的轮次均实际发生。

### 未验证 / 仍需解决

- **CP AHB fault**：最近 fence 轮 `GCP6 cphb=900009c`（ME write），未阻断 fence；
  更早 GPMU 轮 `9024da8`。需要解码具体 ME 访问，确认是否影响用户提交。
- **SID2 secure stream**：`GCP11/GCP13` 为 `smr=0 s2cr=200ff`（FAULT）；
  当前用户 ring 走 SID0，未证明 secure 内容路径。
- **GPU 渲染/Metal**：没有用户客户端、IOSurface 资源生命周期、渲染任务或合成器。
- **流畅度**：显示仍是现有 GOP/CPU 路径（本轮 frames=0，属于已知间歇），
  未使用 GPU 合成；不能把 fence 成功等同于 UI 流畅。
- **PosterBoard/sandbox**：按用户决定暂缓，不作为 GPU 前置条件。
- **panic 文本持久化**：panic screen 只画屏；下次早期 panic 需要屏幕文本或
  单独的持久化路径。

## 3. 接下来的步骤

| 顺序 | 阶段 | 目标 | 入口/改动 | 通过判据 |
|---|---|---|---|---|
| P0 | 文档冻结 | 固定本轮证据和边界 | 本文件 + HANDOFF + AGENTS | 文档哈希/提交完成 |
| P1 | GPU 用户提交 | 从用户态提交一个 batch 并收到 fence | 移植 sagit `QcomGPU` 的 `AdrenoSubmit`/`QcomGPUHardware`；先复用现有 ring/fence/GPMU | 用户态 post + fence 往返成功 |
| P2 | 资源与 IOSurface | 分配 GPU buffer/IOSurface，跑最小 2D/清屏 | 资源生命周期、GPU VA/SMMU 映射、CPU 校验 | CPU 读到预期像素/值 |
| P3 | Metal/合成 | 接 AGXAccelerator/Metal 与显示合成，替换 CPU 缩放 | 用户客户端、IOSurface 展示、帧同步 | 连续帧、掉帧/延迟可测；UI 不靠 CPU 缩放 |
| P4 | 稳定性 | 处理 CP AHB/SID2/GPMU 功耗、回归 | 串行单变量、失败 fail-closed | 不阻断 P1–P3，设备可自动回 Recovery |
| P5 | 流畅度 | 目标可用的 UI 帧率/交互 | 与 P3 联调，测量 swap/帧间隔 | 用户可感知流畅，且日志有数据 |

### P1 的技术前提

当前 CP/GPMU/fence 由 `CheeseburgerGpuCp` 的探针 worker 持有；用户态 GPU 需要：

1. 一个持久的 GPU 服务/IOKit 对象，拥有 ring、fence、GPMU、ZAP 状态；
2. 用户客户端接口，至少支持提交 indirect buffer 和等待 fence；
3. 与现有 `CheeseburgerGpuStart`（ME_INIT/GPMU/fence）和 SMMU 映射复用，
   不能另建一套绕过 ATOS/ZAP 的路径；
4. 失败仍然 halt/abandon，用户提交不能在后端已 abandoned 时继续。

## 4. 构建与证据纪律

- 每轮仍是一次构建、一次 `fastboot boot`、preboot gate、misc BCB、回收日志。
- 保留 10 GiB Data、P8 静默控制台、400 s 自动回、低池与 BIMC 时钟。
- **不**写 `RBBM_SECVID_TRUST_CNTL`，**不**做 SID0 bypass，**不**关闭 ZAP/PAS。
- boot args 总量保持 <约 700 B；池只传一个 PA 参数。
- 不以 RPTR、GPMUReady 或 fence 单独宣称 UI/渲染完成；用户态证据另列。
- panic/日志停止不是内核停止；auto-reset 可能被 console 锁影响，设备级自动返回以实际
  Recovery 为准。
