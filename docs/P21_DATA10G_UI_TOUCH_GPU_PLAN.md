# P21 · 10 GiB Data、UI、触摸与 GPU 修复计划

日期：2026-11-01。用户授权把持久 Data 扩到约 10 GiB，并依次修复后续问题。
每轮仍遵守：先冻结旧轮、一次构建、一次 `fastboot boot`、跑 preboot gate、写 misc BCB、
回收持久日志后再进入下一轮；不写 boot/recovery/固件/GPT，不清理 Data，不绕过 ZAP/PAS。

## 当前已完成（离线，P0）

- 官方 `seed-data-grow.py --gib 10` 从 `owner-data-1g.dbr` 生成
  `out/p21/atos-gate/data-seed/owner-data-10g.dbr`，原 1 GiB 种子未改。
- 新种子：ADFS 容器 10 GiB（10,737,418,240 B），packed 1,781,872 B，
  encoded SHA-256 `b41a46e44dbbb15a79b3765e27fcb8370c5babb9c5d96566443e324041d2b07f`。
- 卷 UUID 保持 `D2420479-F58E-44C5-AB79-1815EC32C686`；五个 skip-setup 偏好文件哈希与
  1 GiB 种子逐项一致；`fsck_apfs -n` 退出 0。收据：`data-seed/validation-10g.json`，
  fsck 日志：`data-seed/fsck-10g.log`。
- 10 GiB store 布局（4 KiB 逻辑块）：pages=2,621,440，bitmap=80 blocks，window=2,621,521 blocks，
  first LBA=11,261,615，last=13,883,135；日志窗口 5,062,912–5,071,103 不重叠；
  store 之后保留 1 GiB；store 之前给 root image 的余量 8,820,143 blocks（约 33.6 GiB）。
- **注意**：种子大小改变后 seed identity 改变，旧的 1 GiB 持久 Data 不会被恢复；扩容后的
  Data 从新种子重新开始。种子内的文件与 skip-setup 偏好保持不变。
- 本轮候选（尚未启动）：镜像 `out/p21/atos-gate/uefi/boot-p21-up1.img`
  SHA-256 `d107ac79afa7a516a1f658eddeb69d8e57bb52f254b7aa72d9a4b8f9654106ae`，
  firmware `5cad6653…`，expanded payload `609c3007…`，args 635 B，kernel `afb7d180…`
  （与上一轮相同）。boot args 已去掉 `-v`、`debug=0x8`、`-enable_kprintf_spam`，
  auto-reset 改为 600 s，保留 `sheng-log=1`、CBHB、panic-screen。

## P1 首轮结果（2026-11-01，boot `0x51875b27`）

- 10 GiB Data 生效：`pages=2621440`、`first=11261615`、`state=3`、`err=0`；ENOSPC 为 0。
- 触摸正常：`TOUCH stage=4 error=0 publish/sink/send/rep=31`，用户确认触摸可用。
- 显示出帧 7 次（第一帧约 188 s）；但日志仍覆盖 UI。
- 根因是板级 P8 framebuffer console 直接把每个 kprintf 写进同一 GOP 扫描缓冲，
  与 `-v/debug` 无关。已新增 `patches/xnu/0008-cheeseburger-fb-console-switch.patch`
  和 `p8-fb-console=0`：保留日志环/持久日志，不画屏；auto-reset 恢复 400 s。
- 该候选已启动：boot `0x4338e6da`，用户确认进入系统，触摸 stage=4；10 GiB Data 续用
  `state=2 pages=2621440 first=11261615 err=0`；ENOSPC 0；PosterBoard corpse 29。
- 本轮仍是用户手动重启（心跳最后 sec=208，`AUTORESET requested=400 issued=0`）；**400 s 自动返回尚未验证**，后续保持 400 s 且不手动干预。
- 仍不能把本轮当作 UI 日志完全消失的自动证据：屏幕以用户观察为准；日志侧
  `[visible-console-tail]` 仍正常采集。

## 2026-11-01 GPU 里程碑（已实机验证）

- P4 已完成：BIMC GFX 时钟使能后 ATOS `rc=0`；低池 `lt=3 ht=0`；CP kick `rc=0`、RPTR 前进。
- P5 已完成：GPMU `ready=babeface`；ZAP/PAS auth 全 0；fence `seq=1` 由 CPU 读到。
- P6 进行中：下一步移植 sagit `QcomGPU` 提交/资源，建立持久 GPU 服务与用户态 batch + fence；详细证据和边界见 `docs/P21_GPU_CP_GPMU_FENCE_20261101.md`。
- P2（触摸 3 次读失败恢复）仍是潜在缺陷；P3（PosterBoard/sandbox）按用户决定暂缓。
- 已知未解决：`CP_AHB_FAULT=900009c`（ME write）不阻断 fence，但需在用户态提交前解码；SID2 secure stream 仍 `0x200ff` FAULT；`DISPLAY frames=0` 是已知间歇，不能归因 GPU。

## 计划表

| 顺序 | 阶段 | 目标 | 具体改动 | 通过判据 | 风险/边界 |
|---|---|---|---|---|---|
| P0 | Data 种子 | 扩到 10 GiB 并证明内容未坏 | 官方 grow 工具；UUID/偏好/fsck/布局/哈希 | 容器 10 GiB；五偏好哈希一致；fsck=0；不压日志窗口 | 旧 1 GiB Data 重新开始；不覆盖原种子 |
| P1 | Data + 静默控制台 | 让系统用上 10 GiB，日志不再盖 UI | 候选 payload 换 10 GiB 种子；去掉 `-v/debug=0x8/-enable_kprintf_spam`；600 s 自动回 | `DATA pages=2621440`；ENOSPC 消失或显著减少；屏幕 UI 可见；自动回 Recovery | 两个独立变量同轮；若 UI 仍被盖，单独查 console/display 所有权 |
| P2 | 触摸恢复 | 触摸 worker 不因 3 次读失败永久退出 | 失败时 toggle GCC BLSP1_QUP5 BCR、重开 CBCR、重新 `qupInit`；记录失败时 GCC/QUP 状态；host 检查 | `TOUCH stage=4`；sink/send/rep 随触摸增长；HID reports 增长 | 先按上游 i2c-qup 恢复路径；不在 IRQ 上下文做重初始化 |
| P3 | PosterBoard / sandbox | 降低崩溃循环与属性拒绝 | 先看 10 GiB Data 后是否缓解；再核对 canvas-width/height 属性来源与 sandbox profile | corpse 次数下降；不再持续 `canvas-width` deny | 不整体放宽 sandbox；不伪造属性 |
| P4 | GPU ATOS | 让硬件 ATOS 完成 | UEFI `AllocatePages(AllocateMaxAddress, EfiReservedMemoryType)` 预留 64 KiB 低池，经单参数 `cheeseburger-smmu-pool` 给 XNU；XNU 用 CopybackCache 映射；直接使能 `gcc_bimc_gfx_clk` | ATOS `rc=0`、PAR 匹配；CP kick 后 RPTR 前进 | 保持 2 GiB 内存、ZAP/PAS；不 SID0 bypass、不写 trust |
| P5 | GPU 首个任务 | CP_ME_INIT、GPMU、fence | ME_INIT -> GPMU -> ZAP -> `CP_SET_SECURE_MODE(0)` + `CACHE_FLUSH_TS` | CP kick `rc=0`；GPMU ready=babeface；CPU 读到 fence seq | `CP_AHB_FAULT` 仍存在但未阻断 fence；失败 fail-closed |
| P6 | 用户态 GPU | IOSurface/Metal 客户端 | 在 P5 成功后移植 sagit `QcomGPU` 提交/资源，建立持久 GPU 服务与用户客户端 | 用户态 batch + fence 往返；2D/渲染内容可由 CPU 校验 | 不提前宣称 Metal 可用；不绕过 ATOS/ZAP |

## 证据纪律

- P1/P2 每轮都保存 gate、boot、monitor、retrieval 和冻结轮目录；按 boot ID 过滤旧行。
- `DATA pages` 必须等于 2,621,440 才说明新种子生效；仅看到 `state=2` 不代表扩容成功。
- ENOSPC 消失不等于 PosterBoard 崩溃消失；两者分别统计。
- UI 通过用户屏幕确认，日志/帧计数不能单独证明界面可见。
- ATOS 成功不等于 CP 执行；CP kick 成功不等于 fence 已写回。
