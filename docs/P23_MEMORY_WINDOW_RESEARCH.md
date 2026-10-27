# P23 内存窗口扩到 2 GiB 或更高：调研与修改计划

日期：2026-10-27（Asia/Shanghai）。本轮只做调研和计划，不改内核、UEFI、ADT、board/config，也不发送新镜像。

## 1. 当前事实

| 项目 | 当前值 | 来源 |
| --- | --- | --- |
| 设备 | OnePlus 5 / MSM8998 / 6 GB | 固定设备 |
| kernel managed window | **1 GiB** | `board.toml [payload.adt] owned_memory_size = "0x40000000"` |
| ADT `darwinboot-memory-size` | 1 GiB | `out/p23/ios-16.7.11/adt/kernel.macho.adt` |
| ADT `dram-base` | `0x40000000` | 同上 |
| ADT `dram-size` | `0x17D8C0000` = 6,401,294,336 字节（6 GB 级） | 同上 |
| Recovery FDT 内存 | `0x40000000 + 3 GiB`、`0x100000000 + 3,180,068,864` | `out/p3/runtime-memory-regions.json` |
| 最新实机 | 1 GiB 下 SpringBoard/backboardd 已启动，但持续 jetsam；无桌面 | `out/p21/persistent-logs/20261027T035713Z` |

历史实验：

- P22 R2：`darwinboot-memory-size = 4 GiB`，UEFI 报 `NO WINDOW 100000000 BIG RUN`，未进入 XNU。
- P22 R3：`2 GiB`，P22 交接记录同样为 `NO WINDOW`，没有进入 XNU 的运行时结果。
- P22 R4：恢复 1 GiB、8 核后可启动，证明 2/4 GiB 失败点是 UEFI 窗口分配，而不是 ADT 字段本身。

## 2. 为什么 2 GiB 在 OnePlus 5 上会失败

官方 UEFI 的 6 GB 内存表在：

`upstream/edk2-msm8998/Silicon/QC/Msm8998/QcomPkg/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.c`

6 GB 分支（`Mem6G`）的低地址部分是：

```text
0x40000000 + 0x40000000   (1 GiB, Conv)
0xA0000000 + 0x60000000   (1.5 GiB, Conv)
0x100000000 + 0xBD8C0000  (3.18 GiB, Conv)
```

中间 `0x80000000..0x9FFFFFFF` 是 Hypervisor/MPSS/SMEM/TZ/Display/UEFI 等保留区。因此：

- 低地址端**没有一个 2 GiB 连续 `EfiConventionalMemory` run**；
- 唯一可能容纳 2 GiB 的是 **4 GiB 以上的高 bank**（3.18 GiB）；
- 当前 `DarwinBoot` 的 `AllocateWindow()` 从低到高扫描 conventional run，没有“高 bank 范围”约束；1 GiB 能落在低 bank，2 GiB 要么被低 bank 碎片挡住，要么在高 bank 的 `AllocatePages`/MMU/Pcd 边界失败。

关键代码：

- `drivers/DarwinBoot/DarwinBootPkg/Driver/DarwinBoot.c`
  - `AllocateWindow()`：只接受 `EfiConventionalMemory`，要求当前 run 足够大，再 `AllocatePages(AllocateAddress, EfiLoaderCode, ...)`；
  - 失败时屏幕打印 `NO WINDOW`、最大 run 和若干 `FREE` 行。
- `MemoryInitPeiLib.c`：按 FDT 总内存选择 `Mem4G/Mem6G/Mem8G`，把对应描述符加入 HOB/MMU 表。
- `PcdSystemMemorySize` 在 `Platforms/Msm8998Pkg/Msm8998.dsc` 是 `0xF0000000`（从 `PcdSystemMemoryBase=0x80000000` 起，覆盖到 `0x170000000`）。高 bank 尾部 `0x170000000..0x1BD8C0000` 超出该 Pcd 范围；2 GiB 高 bank 窗口（`0x100000000 + 2 GiB = 0x180000000`）也超过该 Pcd 末端。这可能是 2 GiB 失败的第二层原因，需用 UEFI 诊断确认，不能先假定。

结论边界：**2 GiB 不是简单把 ADT 数字改成 `0x80000000` 就能保证成功**；P22 两次 NO WINDOW 已证明这一点。必须同时解决“选高 bank”和“UEFI/MMU/Pcd 允许高 bank 分配”。

## 3. 上游与其他仓库案例

### 3.1 UniXNU-polaris：`darwinboot-memory-range`

这是与本问题最接近的上游实现：

- [WindowRange.h](https://github.com/luuu20309/UniXNU-polaris/blob/main/drivers/DarwinBoot/DarwinBootPkg/Driver/WindowRange.h)
- [make-polaris-adt.py](https://github.com/luuu20309/UniXNU-polaris/blob/main/scripts/make-polaris-adt.py)
- [board.toml](https://github.com/luuu20309/UniXNU-polaris/blob/main/board.toml)

做法：

- ADT 新增 `darwinboot-memory-range = (base, bytes)`；
- `DarwinBoot` 解析该属性，`AllocateWindow` 只在给定范围内找 conventional run；
- Polaris 6 GB 使用高 bank：
  - `DRAM_BASE = 0x100000000`
  - `DRAM_SIZE_6 = 0x7CC00000`（约 2 GiB，留出顶部保留区）
  - `dram-base = 0x100000000`，`dram-size = 0x7CC00000`
  - `darwinboot-memory-size` 仍可单独选择小于 bank 的窗口
- 其 `owned_memory_size` 默认 `0x40000000`，但窗口范围明确固定在高 bank，不会再去低 bank 碎片里碰运气。

这是“旧版 `AllocateWindow` + 明确范围约束”的最小上游案例，不需要整体替换 XNU 或 UEFI。

### 3.2 ios-topaz / ios-sailfish：高 bank + shim 补 ADT/boot_args

- [ios-topaz](https://github.com/turgayasuk-sketch/ios-topaz)
  - live FDT memory 节点包含 `<0x40000000 ...>`、`<0x80000000 ...>`、`<0x1_40000000 4G>`；
  - `tools/mkadt.py --highmem` 使用 `HIGH_PHYS_BASE=0x100000000`、4 GiB 高 bank；
  - shim 从 ABL `/memory` 重新取高 bank，并 patch ADT `dram-base`/`dram-size` 与 boot_args `memSize/memSizeActual`。
- [ios-sailfish](https://github.com/joranv1/ios-sailfish)
  - 使用 `0x100000000` 起的 upper DDR bank，2 GB SKU；
  - `mkadt.py --highmem` 设置 `dram-base/dram-size`，boot_args 由 shim 写入；
  - 启动参数使用 `launchd_unsecure_cache=1`。

这些仓库证明：**iOS16/XNU 的 boot_args 和 ADT 可以接受 4 GiB 以上的物理基址**；关键不是 XNU 不支持，而是启动链要给出一段真实、连续的 UEFI 可分配窗口。

### 3.3 UniXNU 官方约束

- `WindowSize.h` 允许 `0x10000000..0x200000000`（256 MiB..8 GiB，2 MiB 对齐）；
- `devicetree.py` 的 `owned_memory_size` 同样接受 256 MiB..8 GiB；
- 但注释明确：**“Allocation still requires one real conventional-memory run. This is a parser bound, never a declaration of available RAM or an allocation fallback.”**

所以不能仅改 board/ADT 数字，必须让 UEFI 真正找到并分配范围。

## 4. 推荐修改计划（先不执行）

### Phase 0：只读诊断，确认失败层

目标：不先改内核，先确认 2 GiB 失败在“扫描不到 run”还是“找到 run 但 AllocatePages/MMU/Pcd 拒绝”。

候选（单独诊断镜像，不进入 XNU，或仅在 `NO WINDOW` 屏上增加文字）：

1. 在 `AllocateWindow` 失败路径打印：
   - 所有 `EfiConventionalMemory` run 的 `(base,size,type)`；
   - 高 bank run 的候选地址、`AllocatePages` 返回的 `EFI_STATUS`；
   - 当前 `PcdSystemMemoryBase/Size`、`gDeviceMemoryDescriptorEx` 选择的 `Mem4G/6G/8G`。
2. 对比 `Mem6G` 表：确认高 bank `0x100000000 + 0xBD8C0000` 确实是 `EfiConventionalMemory`，不是 Reserved/LoaderData。
3. 检查 `PcdSystemMemorySize=0xF0000000` 是否真的限制 `AllocatePages` 或 MMU；用一次 2 GiB 高 bank 请求验证。

验收：屏幕/日志能明确回答 `NO WINDOW` 的 `largest_run_value` 和 `AllocatePages` 状态；不发送 XNU 运行候选。

### Phase 1：移植 Polaris 的 `darwinboot-memory-range`

以项目补丁形式加入，不改固定 upstream checkout：

- 新增 `WindowRange.h`（Polaris 原文或等价实现）；
- `DarwinBoot.c`：
  - `AdtNode` 增加 `darwinboot-memory-range` 解析（16 字节，4K 对齐，base+bytes 不溢出）；
  - `AllocateWindow` 增加 `DARWIN_WINDOW_RANGE` 参数；
  - 用 `DarwinWindowCandidate()` 替代当前 `Candidate=ALIGN(...) + kernelBase offset` 逻辑；
  - 保持无 range 时行为完全不变（1 GiB 基线可复现）。
- Host 测试：
  - range 为空、越界、未对齐、溢出、跨描述符、多个 run；
  - `AllocateAddress` 失败时继续下一个 run；
  - 无 range 的旧路径输出逐字节一致。

验收：host 测试通过；1 GiB 无 range 候选仍可打包/对比旧镜像。

### Phase 2：2 GiB 高 bank ADT + 配置

候选 A（推荐先做）：

- `darwinboot-memory-size = 0x80000000`（2 GiB）
- `darwinboot-memory-range = (0x100000000, 0xBD8C0000)`（高 bank 实际 3.18 GiB）
- `dram-base`/`dram-size` 先保持当前 `0x40000000` / `0x17D8C0000`，因为 `gPhysBase` 来自 boot_args 的 `PhysicalBase`；只改变窗口范围，减少对驱动假设的改动。
- `kern.maxvnodes` 按 2 GiB 计算：`2*1048576KiB/64KiB+1024 = 33792`。
- board.toml `owned_memory_size` 改为 `0x80000000`；P23 ADT 通过可重复 repack 脚本加 range/改 size，不手改二进制。

候选 B（仅当 A 因 `gDramBase` 不一致失败）：

- `dram-base = 0x100000000`，`dram-size = 0xBD8C0000`，与 Polaris 一致；
- 保留低地址 framebuffer/UFS 的 IOKit 映射，检查 `phystokv` 只覆盖高窗口是否影响驱动。

验收：2 GiB 窗口进入 XNU；`gPhysBase=0x100000000`（或实际分配基址）、`memSize=2 GiB`；`kern.maxvnodes=33792`；无 root/auth/Data 回退。

### Phase 3：若 Phase 0/2 仍 NO WINDOW，修 UEFI 分配边界

按证据顺序：

1. **Pcd/MMU 范围**：如果 `AllocatePages` 在高 bank 返回 `EFI_NOT_FOUND/OUT_OF_RESOURCES`，检查 `PcdSystemMemorySize` 是否需从 `0xF0000000` 扩到覆盖 `0x1BD8C0000`（base `0x80000000` 时 size `0x13D8C0000`），并确认 `MemoryInitPei`/MMU 表映射高 bank。
2. **分配时机/碎片**：`Boot()` 当前先读 kernel/ADT/trust/payload 到 pool，再 `AllocateWindow`；如果 2 GiB 高 run 被 UEFI pool 占用，改为先读少量头信息/先分配窗口，或把大块 payload 移到窗口之后；不得改变窗口语义。
3. **UEFI 内存表选择**：确认 FDT `MemoryTotal` 落在 `SIZE_MB_IN(5120,6656,6)`，`Mem6G` 高 bank 条目实际加入 HOB；不要用 `Mem4G` 表误判 6 GB。
4. **高 bank 保留区**：高 bank 3.18 GiB 顶部若有未在 Linux `/proc/iomem` 显示的保留区，range 要排除；先诊断，不要猜。

验收：2 GiB `AllocatePages` 返回成功；没有降低安全/跳过窗口检查。

### Phase 4：2 GiB 实机验证

- 仅临时 `fastboot boot`，不写 userdata/boot/recovery/固件；
- 保留 8 核、1 GiB Data seed、日志窗口；
- 一轮启动后读持久日志/屏幕，检查：
  - `P17 Data activation=0`、root/SystemOS/Data 未回退；
  - `gPhysBase`/`memSize` 与 ADT 一致；
  - jetsam/`System is unhealthy` 是否减少；
  - DISPLAY clients/frames 是否推进；
  - 首个新错误，不用传输成功代替启动成功。

### Phase 5：2.5/2.75/3 GiB 逐级

高 bank 实际 3.18 GiB，`base + window` 必须落在 bank 内，并给 kernel/ADT/ramdisk/trust 留空间：

- 2.5 GiB：`0xA0000000`；
- 2.75 GiB：`0xB0000000`；
- 3 GiB：`0xC0000000`，末端 `0x1C0000000` 超过 `0x1BD8C0000`，**不可行**；
- 理论上限约 `0xBD8C0000` 减 kernel 布局/保留区，实际最大约 **2.9 GiB**，不是 4 GiB；
- 逐级只改 `darwinboot-memory-size`/range，不叠加其他变量；每级先 host gate，再一次实机启动。
- 4 GiB 不可由高 bank 提供；若未来要 4 GiB，必须重新安排低 bank 保留区或增加第二窗口，当前 XNU/UniXNU 不支持多窗口 managed memory，暂不纳入。

## 5. 明确不改的内容

- 不整体回退/替换 XNU 10063；
- 不改 root UUID、owner 策略、trust bundle、Data seed、userdata 布局；
- 不改 framebuffer、UFS、PSCI、8 核；
- 不写 boot/recovery/firmware/GPT；
- 不使用 4 GiB 请求碰运气，不把 `NO WINDOW` 当内核失败；
- 不先改 `Pcd`/UEFI 内存表，除非 Phase 0 诊断证明必要。

## 6. 最小第一候选建议

如果只做一个最可能成功的候选：

1. 移植 `darwinboot-memory-range`；
2. ADT：2 GiB + range 高 bank `0x100000000 + 0xBD8C0000`；
3. board `owned_memory_size=0x80000000`，maxvnodes 33792；
4. `dram-base/dram-size` 先不变；
5. 先做 host gate + 一次临时启动；若 `NO WINDOW`，只根据新增诊断决定是否改 Pcd/内存表，不盲目再发。

该计划与上游 Polaris 的做法一致，且把“2 GiB 失败”拆成可证伪的两层：范围选择 vs UEFI 可分配边界。
