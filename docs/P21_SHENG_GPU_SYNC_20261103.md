# P21 sheng GPU 代码完整同步（2026-11-03）

## 1. 结论摘要

本轮把官方 sheng/UniXNU 的 GPU 内容（**除本机 A540 板级驱动之外**）一次性纳入仓库：

- `drivers/Adreno740` 全部文件 → `package/BoardRuntime/sheng/Adreno740/`（含 `tests/`、`Info.plist`、`README.md`、`AGENTS.md`；tests 不编译）。
- GPU kits（`AdrenoPM4`、`ArmSmmu4K`、`AdrenoFirmware`、`AirNir`）→ `package/BoardRuntime/sheng/kits/<名>/`。
- `drivers/AdrenoMetal` 全部文件 → `package/BoardRuntime/sheng/frontend/AdrenoMetal/`（含 `NativeOfflineAccelerator.cpp`、`OfflineMetalWire.h`、`bridge/`、`shaders/`、`tools/`）。
- GPU/Metal 相关构建、审计、探针脚本 → `package/BoardRuntime/sheng/tools/`（只 vendor，不执行）。
- 旧的 `package/BoardRuntime/adreno740/` 在逐文件确认与新结构一致后删除；`CheeseburgerGpuSmmu4K.cpp` 与本轮构建脚本的 include 已改到新路径。

构建验证只编译“硬件无关 + A540 可用”的 22 个模块（9 个 Adreno740 硬件无关源文件 + ArmSmmu4K 8 个 + AdrenoPM4 5 个）。`batch18` 内核源码构建通过：`OFFICIAL_KERNEL_SOURCE_BUILD_PASS`，内核 `out/p21/gpu-dev-batch18/native/kernel.macho`，SHA-256 `028187e8b198ab542f1247ba460d53e20edd86a4741cf4c61876c23c7913988f`。

A740 硬件模块、AdrenoMetal 前端、AirNir/AdrenoFirmware kit 和 tools 只 vendor，未编译、未接入板级调用路径。**本轮没有接触设备，也没有生成可启动镜像。**

## 2. 来源与 revision

| 内容 | 来源 | revision | 说明 |
| --- | --- | --- | --- |
| UniXNU 框架（固定副本） | `.work/p21-official-framework` | `16e592abee8a550dec8154ed1f38b5ebc4f16213` | 本机固定框架树，含各子模块 |
| Adreno740 驱动 | `.work/p21-official-framework/drivers/Adreno740` | `4e0314d8ad29d52f6fa3f9383a540adcd618b87` | 框架子模块；本轮 clone 上游失败后按任务约定使用固定副本 |
| AdrenoMetal 前端 | `upstream/AdrenoMetal`（`git clone https://github.com/UniXNU/AdrenoMetal.git`） | `243d63cf43305a85c8119b66b49cb7df4b30b019` | 本轮新 clone 的最新 main |
| AdrenoPM4 kit | `.work/p21-official-framework/kits/AdrenoPM4` | `282606b528b2595bc4274ea7490b62b323f35a5f` | 框架子模块 |
| ArmSmmu4K kit | `.work/p21-official-framework/kits/ArmSmmu4K` | `8bb168ad058c28d5c04f179ba407875eacba7ef3` | 框架子模块 |
| AdrenoFirmware kit | `.work/p21-official-framework/kits/AdrenoFirmware` | `20ad3d7aca14bd46aecab6335c780f5413fa054b` | 框架子模块 |
| AirNir kit | `.work/p21-official-framework/kits/AirNir` | `b356371a74cd158c2902f4886f373ebb0f833a36` | 框架子模块 |
| sheng 设备适配（参考） | `upstream/UniXNU-sheng` | `7cff07e` | 只用于核对 GPU 组件选择和集成方式，未复制 SM8550 板级胶水 |
| UniXNU 框架上游（参考） | `upstream/UniXNU` | `b6ae99c` | 本机 checkout，仅作文件对照 |

来源说明：

1. 任务要求优先 `git clone` 最新 `UniXNU/Adreno740`。本轮 clone 未成功（网络/权限），按任务约定改用 `.work/p21-official-framework` 的固定副本（revision `4e0314d`），并在本文记录。因此**无法确认上游 main 在 `4e0314d` 之后是否还有 Adreno740 提交**；后续如取得更新版本，应按同一流程重新同步并更新本表。
2. `UniXNU/AdrenoMetal` clone 成功，使用的是最新 main `243d63c`。它比框架固定副本（`454ff2c`）多出 `NativeGpuLatency.h`、`NativeGpuTrace.h`、`NativeTurnipSwapTest.h`、`QueuePriority.h`、`SharedShaderCache.h`、`bridge/RustPacBoundary.c`、`bridge/coreimage_host.mm`、`bridge/interface_regressions_host.mm`、`bridge/queue_priority_host.cpp`、`bridge/scanout_host.mm`、`bridge/wallpaper_freeze_host.mm`、`shaders/scanout_convert.ll`、`shaders/smoke_swap.vert`、`shaders/smoke_swap_fetch.frag` 及一组新的 `bridge/patches/`；旧的 0005–0007 补丁编号内容也已变化。本轮以 clone 版本为准。
3. kit 的 revision 取自框架子模块当前 HEAD；框架自身固定为 `16e592ab`。

## 3. 目录结构

```text
package/BoardRuntime/sheng/
├── Adreno740/                 # 官方 Adreno740 驱动（全部文件，tests 不编译）
│   ├── *.cpp / *.h / Info.plist / README.md / AGENTS.md / LICENSE
│   └── tests/                 # 宿主/设备测试，只 vendor
├── kits/
│   ├── AdrenoPM4/             # PM4 打包/常量/着色器/VPC/光栅（编译）
│   ├── ArmSmmu4K/             # SMMU 页表/上下文/流/审计/live（编译）
│   ├── AdrenoFirmware/        # A740 GMU/HFI 固件协议（仅 vendor）
│   └── AirNir/                # AIR 前端库（仅 vendor）
├── frontend/
│   └── AdrenoMetal/           # 官方 Metal 前端/插件/离线加速器/桥接/着色器/工具
└── tools/                     # GPU/Metal 构建、审计、探针脚本（只 vendor）
```

`package/BoardRuntime/sheng/` 共 367 个文件（不含生成物）：Adreno740 92、kits 81、AdrenoMetal 122、tools 102。旧 `package/BoardRuntime/adreno740/` 已删除。

## 4. 构建集成改动

### 4.1 `scripts/p21-build-board-runtime.py`

- 全局 include 根从 `S/adreno740`、`S/adreno740/ArmSmmu4K`、`S/adreno740/AdrenoPM4` 改为：
  - `S/sheng/Adreno740`
  - `S/sheng/kits/ArmSmmu4K`
  - `S/sheng/kits/AdrenoPM4`
- `A740` 变量改为 `S/'sheng/Adreno740'`，新增 `KITS=S/'sheng/kits'`；kit 源文件改从 `KITS/'ArmSmmu4K'` 和 `KITS/'AdrenoPM4'` 枚举。
- 编译清单保持不变（只改路径和注释），仍为：
  - Adreno740：`NativeAdrenoMemory`、`NativeAdrenoVm`、`NativeAdrenoSmmuContext`、`NativeAdrenoSmmuMemory`、`NativeAdrenoRing`、`NativeAdrenoSubmit`、`NativeAdrenoCpu`、`NativeAdrenoSha256`、`NativeAdrenoTimer`；
  - ArmSmmu4K：`ArmSmmu4K`、`ArmSmmu4KAudit`、`ArmSmmu4KContext`、`ArmSmmu4KLive`、`ArmSmmu4KMmio`、`ArmSmmu4KProgram`、`ArmSmmu4KStreams`、`ArmSmmu4KTableMemory`；
  - AdrenoPM4：`AdrenoConstants`、`AdrenoPM4`、`AdrenoRaster`、`AdrenoShader`、`AdrenoVPC`。
- `test_*.c` 仍由脚本排除；`tests/` 目录完全不进入编译。

### 4.2 `package/BoardRuntime/CheeseburgerGpuSmmu4K.cpp`

只改 include，不改逻辑：

```cpp
#include "sheng/Adreno740/NativeAdrenoMemory.h"
#include "sheng/Adreno740/NativeAdrenoSmmuContext.h"
#include "sheng/Adreno740/NativeAdrenoSmmuMemory.h"
#include "sheng/kits/ArmSmmu4K/ArmSmmu4K.h"
```

Adreno740 源文件内部的 include 是扁平名称（`ArmSmmu4KStreams.h`、`AdrenoPM4.h`、`ArmSmmu4KMmio.h` 等），由上面的 `-I` 根解析，文件内容无需改写。

### 4.3 其他

- 仓库内没有其他源码引用旧 `adreno740` 路径（见第 8 节审计）。
- 没有新增 prelink、payload、UEFI 或 boot 镜像步骤；本轮只验证内核源码构建。

## 5. 完整文件清单

状态说明：`已编译` = 本轮由 `p21-build-board-runtime.py` 编译进 `libCheeseburgerRuntime.a`；`仅vendor` = 已同步但未编译；`用户态` = AdrenoMetal 前端/插件/桥接/工具资源，未编译；`工具` = 构建/审计/探针脚本，只 vendor 不执行。头文件不单独编译，标为 `仅vendor` 或 `用户态`，随包含它的单元使用。

| 上游路径 | 仓库路径 | 状态 | 说明 |
| --- | --- | --- | --- |
| `drivers/Adreno740/AGENTS.md` | `package/BoardRuntime/sheng/Adreno740/AGENTS.md` | 仅vendor | 文档/工程文件 |
| `drivers/Adreno740/Info.plist` | `package/BoardRuntime/sheng/Adreno740/Info.plist` | 仅vendor | 文档/工程文件 |
| `drivers/Adreno740/LICENSE` | `package/BoardRuntime/sheng/Adreno740/LICENSE` | 仅vendor | 文档/工程文件 |
| `drivers/Adreno740/NativeAdrenoA740HwInit.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoA740HwInit.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoABI.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoABI.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoAccelerator.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoAccelerator.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoBoard.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoBoard.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoBoard.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoBoard.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoClient.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoClient.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoClient.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoClient.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoCpInit.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoCpInit.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoCpInit.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoCpInit.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoCpu.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoCpu.cpp` | 已编译 | 硬件无关 VM/BO/环形缓冲/提交/CPU/SHA-256/定时器模块 |
| `drivers/Adreno740/NativeAdrenoFirmware.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoFirmware.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoFirmware.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoFirmware.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoFirmwareDefaults.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoFirmwareDefaults.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoGmuConfig.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGmuConfig.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoGmuConfig.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGmuConfig.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoGmuContext.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGmuContext.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoGmuContext.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGmuContext.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoGmuMemory.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGmuMemory.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoGmuMemory.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGmuMemory.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoGmuPlatform.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGmuPlatform.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoGmuPlatform.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGmuPlatform.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoGmuService.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGmuService.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoGmuService.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGmuService.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoGpu.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGpu.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoGpu.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGpu.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoGpuContext.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGpuContext.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoGpuContext.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoGpuContext.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoIrq.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoIrq.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoIrq.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoIrq.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoKernelTime.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoKernelTime.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoKernelTime.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoKernelTime.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoMemory.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoMemory.cpp` | 已编译 | 硬件无关 VM/BO/环形缓冲/提交/CPU/SHA-256/定时器模块 |
| `drivers/Adreno740/NativeAdrenoMemory.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoMemory.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoPublish.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoPublish.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoPwrup.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoPwrup.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoPwrup.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoPwrup.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoPwrupRegs.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoPwrupRegs.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoRing.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoRing.cpp` | 已编译 | 硬件无关 VM/BO/环形缓冲/提交/CPU/SHA-256/定时器模块 |
| `drivers/Adreno740/NativeAdrenoRing.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoRing.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoService.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoService.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoSha256.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoSha256.cpp` | 已编译 | 硬件无关 VM/BO/环形缓冲/提交/CPU/SHA-256/定时器模块 |
| `drivers/Adreno740/NativeAdrenoSha256.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoSha256.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoSmmuContext.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoSmmuContext.cpp` | 已编译 | 硬件无关 VM/BO/环形缓冲/提交/CPU/SHA-256/定时器模块 |
| `drivers/Adreno740/NativeAdrenoSmmuContext.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoSmmuContext.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoSmmuMemory.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoSmmuMemory.cpp` | 已编译 | 硬件无关 VM/BO/环形缓冲/提交/CPU/SHA-256/定时器模块 |
| `drivers/Adreno740/NativeAdrenoSmmuMemory.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoSmmuMemory.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoSmmuService.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoSmmuService.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoSmmuService.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoSmmuService.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoStatus.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoStatus.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoSubmit.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoSubmit.cpp` | 已编译 | 硬件无关 VM/BO/环形缓冲/提交/CPU/SHA-256/定时器模块 |
| `drivers/Adreno740/NativeAdrenoSubmit.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoSubmit.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoTimer.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoTimer.cpp` | 已编译 | 硬件无关 VM/BO/环形缓冲/提交/CPU/SHA-256/定时器模块 |
| `drivers/Adreno740/NativeAdrenoTimer.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoTimer.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoUserClient.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoUserClient.cpp` | 仅vendor | A740 硬件或服务/用户客户端代码，不编译 |
| `drivers/Adreno740/NativeAdrenoUserClient.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoUserClient.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoVm.cpp` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoVm.cpp` | 已编译 | 硬件无关 VM/BO/环形缓冲/提交/CPU/SHA-256/定时器模块 |
| `drivers/Adreno740/NativeAdrenoVm.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoVm.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/NativeAdrenoVmAlias.h` | `package/BoardRuntime/sheng/Adreno740/NativeAdrenoVmAlias.h` | 仅vendor | 头文件，随包含它的单元使用；A740 硬件/服务头不编译 |
| `drivers/Adreno740/README.md` | `package/BoardRuntime/sheng/Adreno740/README.md` | 仅vendor | 文档/工程文件 |
| `drivers/Adreno740/tests/XnuMemoryFixture.h` | `package/BoardRuntime/sheng/Adreno740/tests/XnuMemoryFixture.h` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/hostsim/builtin_cache.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/hostsim/builtin_cache.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/hostsim/deferred_bo.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/hostsim/deferred_bo.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/hostsim/host_gpu_service.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/hostsim/host_gpu_service.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/hostsim/host_iokit_shim.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/hostsim/host_iokit_shim.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/hostsim/pipeline_cache.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/hostsim/pipeline_cache.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/hostsim/pipeline_cache_ios.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/hostsim/pipeline_cache_ios.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/hostsim/plugin_smoke.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/hostsim/plugin_smoke.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/hostsim/vulkan_smoke.c` | `package/BoardRuntime/sheng/Adreno740/tests/hostsim/vulkan_smoke.c` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_board.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_board.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_client.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_client.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_cpinit.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_cpinit.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_gmuconfig.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_gmuconfig.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_gmuctx.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_gmuctx.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_gmumap.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_gmumap.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_gmumem.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_gmumem.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_gmuplatform.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_gmuplatform.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_gpu.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_gpu.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_irq.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_irq.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_memory.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_memory.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_pwrup.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_pwrup.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_ring.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_ring.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_sha256.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_sha256.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_smmu_memory.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_smmu_memory.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_smmuctx.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_smmuctx.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_submit.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_submit.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_timer.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_timer.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_userclient.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_userclient.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_vm.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_vm.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `drivers/Adreno740/tests/test_vm_alias.cpp` | `package/BoardRuntime/sheng/Adreno740/tests/test_vm_alias.cpp` | 仅vendor | 宿主/设备测试，本轮不编译 |
| `kits/AdrenoPM4/AGENTS.md` | `package/BoardRuntime/sheng/kits/AdrenoPM4/AGENTS.md` | 仅vendor | 文档/工程文件 |
| `kits/AdrenoPM4/AdrenoConstants.c` | `package/BoardRuntime/sheng/kits/AdrenoPM4/AdrenoConstants.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/AdrenoPM4/AdrenoConstants.h` | `package/BoardRuntime/sheng/kits/AdrenoPM4/AdrenoConstants.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/AdrenoPM4/AdrenoPM4.c` | `package/BoardRuntime/sheng/kits/AdrenoPM4/AdrenoPM4.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/AdrenoPM4/AdrenoPM4.h` | `package/BoardRuntime/sheng/kits/AdrenoPM4/AdrenoPM4.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/AdrenoPM4/AdrenoRaster.c` | `package/BoardRuntime/sheng/kits/AdrenoPM4/AdrenoRaster.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/AdrenoPM4/AdrenoRaster.h` | `package/BoardRuntime/sheng/kits/AdrenoPM4/AdrenoRaster.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/AdrenoPM4/AdrenoShader.c` | `package/BoardRuntime/sheng/kits/AdrenoPM4/AdrenoShader.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/AdrenoPM4/AdrenoShader.h` | `package/BoardRuntime/sheng/kits/AdrenoPM4/AdrenoShader.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/AdrenoPM4/AdrenoVPC.c` | `package/BoardRuntime/sheng/kits/AdrenoPM4/AdrenoVPC.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/AdrenoPM4/AdrenoVPC.h` | `package/BoardRuntime/sheng/kits/AdrenoPM4/AdrenoVPC.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/AdrenoPM4/LICENSE` | `package/BoardRuntime/sheng/kits/AdrenoPM4/LICENSE` | 仅vendor | 文档/工程文件 |
| `kits/AdrenoPM4/README.md` | `package/BoardRuntime/sheng/kits/AdrenoPM4/README.md` | 仅vendor | 文档/工程文件 |
| `kits/AdrenoPM4/test_constants.c` | `package/BoardRuntime/sheng/kits/AdrenoPM4/test_constants.c` | 仅vendor | 测试，本轮不编译 |
| `kits/AdrenoPM4/test_pm4.c` | `package/BoardRuntime/sheng/kits/AdrenoPM4/test_pm4.c` | 仅vendor | 测试，本轮不编译 |
| `kits/AdrenoPM4/test_raster.c` | `package/BoardRuntime/sheng/kits/AdrenoPM4/test_raster.c` | 仅vendor | 测试，本轮不编译 |
| `kits/AdrenoPM4/test_shader.c` | `package/BoardRuntime/sheng/kits/AdrenoPM4/test_shader.c` | 仅vendor | 测试，本轮不编译 |
| `kits/AdrenoPM4/test_vpc.c` | `package/BoardRuntime/sheng/kits/AdrenoPM4/test_vpc.c` | 仅vendor | 测试，本轮不编译 |
| `kits/ArmSmmu4K/AGENTS.md` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/AGENTS.md` | 仅vendor | 文档/工程文件 |
| `kits/ArmSmmu4K/ArmSmmu4K.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4K.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/ArmSmmu4K/ArmSmmu4K.h` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4K.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KAudit.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KAudit.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KAudit.h` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KAudit.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KContext.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KContext.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KContext.h` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KContext.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KLive.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KLive.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KLive.h` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KLive.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KMmio.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KMmio.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KMmio.h` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KMmio.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KProgram.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KProgram.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KProgram.h` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KProgram.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KStreams.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KStreams.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KStreams.h` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KStreams.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KTableMemory.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KTableMemory.c` | 已编译 | ArmSmmu4K/AdrenoPM4 kit 源文件 |
| `kits/ArmSmmu4K/ArmSmmu4KTableMemory.h` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/ArmSmmu4KTableMemory.h` | 仅vendor | 头文件/未编译源文件 |
| `kits/ArmSmmu4K/LICENSE` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/LICENSE` | 仅vendor | 文档/工程文件 |
| `kits/ArmSmmu4K/README.md` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/README.md` | 仅vendor | 文档/工程文件 |
| `kits/ArmSmmu4K/test_alias.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/test_alias.c` | 仅vendor | 测试，本轮不编译 |
| `kits/ArmSmmu4K/test_audit.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/test_audit.c` | 仅vendor | 测试，本轮不编译 |
| `kits/ArmSmmu4K/test_context.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/test_context.c` | 仅vendor | 测试，本轮不编译 |
| `kits/ArmSmmu4K/test_live.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/test_live.c` | 仅vendor | 测试，本轮不编译 |
| `kits/ArmSmmu4K/test_permissions.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/test_permissions.c` | 仅vendor | 测试，本轮不编译 |
| `kits/ArmSmmu4K/test_program.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/test_program.c` | 仅vendor | 测试，本轮不编译 |
| `kits/ArmSmmu4K/test_streams.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/test_streams.c` | 仅vendor | 测试，本轮不编译 |
| `kits/ArmSmmu4K/test_tables.c` | `package/BoardRuntime/sheng/kits/ArmSmmu4K/test_tables.c` | 仅vendor | 测试，本轮不编译 |
| `kits/AdrenoFirmware/AGENTS.md` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/AGENTS.md` | 仅vendor | A740 GMU/HFI 固件协议，不编译 |
| `kits/AdrenoFirmware/AdrenoFirmware.c` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/AdrenoFirmware.c` | 仅vendor | A740 GMU/HFI 固件协议，不编译 |
| `kits/AdrenoFirmware/AdrenoFirmware.h` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/AdrenoFirmware.h` | 仅vendor | A740 GMU/HFI 固件协议，不编译 |
| `kits/AdrenoFirmware/AdrenoHfi.c` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/AdrenoHfi.c` | 仅vendor | A740 GMU/HFI 固件协议，不编译 |
| `kits/AdrenoFirmware/AdrenoHfi.h` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/AdrenoHfi.h` | 仅vendor | A740 GMU/HFI 固件协议，不编译 |
| `kits/AdrenoFirmware/AdrenoHfiStartup.c` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/AdrenoHfiStartup.c` | 仅vendor | A740 GMU/HFI 固件协议，不编译 |
| `kits/AdrenoFirmware/AdrenoHfiStartup.h` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/AdrenoHfiStartup.h` | 仅vendor | A740 GMU/HFI 固件协议，不编译 |
| `kits/AdrenoFirmware/AdrenoHfiVotes.c` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/AdrenoHfiVotes.c` | 仅vendor | A740 GMU/HFI 固件协议，不编译 |
| `kits/AdrenoFirmware/AdrenoHfiVotes.h` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/AdrenoHfiVotes.h` | 仅vendor | A740 GMU/HFI 固件协议，不编译 |
| `kits/AdrenoFirmware/LICENSE` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/LICENSE` | 仅vendor | A740 GMU/HFI 固件协议，不编译 |
| `kits/AdrenoFirmware/README.md` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/README.md` | 仅vendor | A740 GMU/HFI 固件协议，不编译 |
| `kits/AdrenoFirmware/test_gmu.c` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/test_gmu.c` | 仅vendor | 测试，本轮不编译 |
| `kits/AdrenoFirmware/test_hfi.c` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/test_hfi.c` | 仅vendor | 测试，本轮不编译 |
| `kits/AdrenoFirmware/test_startup.c` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/test_startup.c` | 仅vendor | 测试，本轮不编译 |
| `kits/AdrenoFirmware/test_votes.c` | `package/BoardRuntime/sheng/kits/AdrenoFirmware/test_votes.c` | 仅vendor | 测试，本轮不编译 |
| `kits/AirNir/.clang-format` | `package/BoardRuntime/sheng/kits/AirNir/.clang-format` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/AGENTS.md` | `package/BoardRuntime/sheng/kits/AirNir/AGENTS.md` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/AirNir.h` | `package/BoardRuntime/sheng/kits/AirNir/AirNir.h` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/BackendABI.h` | `package/BoardRuntime/sheng/kits/AirNir/BackendABI.h` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/ConstantUploads.c` | `package/BoardRuntime/sheng/kits/AirNir/ConstantUploads.c` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/ConstantUploads.h` | `package/BoardRuntime/sheng/kits/AirNir/ConstantUploads.h` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/Frontend.cpp` | `package/BoardRuntime/sheng/kits/AirNir/Frontend.cpp` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/FusedF32.h` | `package/BoardRuntime/sheng/kits/AirNir/FusedF32.h` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/LICENSE` | `package/BoardRuntime/sheng/kits/AirNir/LICENSE` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/Library.c` | `package/BoardRuntime/sheng/kits/AirNir/Library.c` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/Library.h` | `package/BoardRuntime/sheng/kits/AirNir/Library.h` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/MesaBackend.c` | `package/BoardRuntime/sheng/kits/AirNir/MesaBackend.c` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/README.md` | `package/BoardRuntime/sheng/kits/AirNir/README.md` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/licenses/SoftFloat-3e.txt` | `package/BoardRuntime/sheng/kits/AirNir/licenses/SoftFloat-3e.txt` | 仅vendor | AIR 前端库，A540 用户态阶段使用，暂不编译 |
| `kits/AirNir/tests/check_coefficients.py` | `package/BoardRuntime/sheng/kits/AirNir/tests/check_coefficients.py` | 仅vendor | 测试，本轮不编译 |
| `kits/AirNir/tests/check_fma.py` | `package/BoardRuntime/sheng/kits/AirNir/tests/check_fma.py` | 仅vendor | 测试，本轮不编译 |
| `kits/AirNir/tests/check_math_flags.py` | `package/BoardRuntime/sheng/kits/AirNir/tests/check_math_flags.py` | 仅vendor | 测试，本轮不编译 |
| `kits/AirNir/tests/constant_cpp_client.cpp` | `package/BoardRuntime/sheng/kits/AirNir/tests/constant_cpp_client.cpp` | 仅vendor | 测试，本轮不编译 |
| `kits/AirNir/tests/fma_vertex.ll` | `package/BoardRuntime/sheng/kits/AirNir/tests/fma_vertex.ll` | 仅vendor | 测试，本轮不编译 |
| `kits/AirNir/tests/test_constant_uploads.c` | `package/BoardRuntime/sheng/kits/AirNir/tests/test_constant_uploads.c` | 仅vendor | 测试，本轮不编译 |
| `kits/AirNir/tests/test_library.c` | `package/BoardRuntime/sheng/kits/AirNir/tests/test_library.c` | 仅vendor | 测试，本轮不编译 |
| `drivers/AdrenoMetal/AGENTS.md` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/AGENTS.md` | 用户态 | 文档/工程文件 |
| `drivers/AdrenoMetal/CacheExport.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/CacheExport.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/ExceptionDiag.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/ExceptionDiag.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/GpuScanoutClient.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/GpuScanoutClient.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/GpuScanoutPresenter.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/GpuScanoutPresenter.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/HostMetalCommandServer.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/HostMetalCommandServer.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/HostMetalCommandServer.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/HostMetalCommandServer.mm` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/LICENSE` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/LICENSE` | 用户态 | 文档/工程文件 |
| `drivers/AdrenoMetal/NativeAdrenoMetalPlugin.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeAdrenoMetalPlugin.mm` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/NativeAdrenoProbeDaemon.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeAdrenoProbeDaemon.cpp` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/NativeAdrenoUserTest.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeAdrenoUserTest.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/NativeBridgeMetalFrontend.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeBridgeMetalFrontend.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/NativeBridgeMetalFrontend.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeBridgeMetalFrontend.mm` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/NativeGpuLatency.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeGpuLatency.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/NativeGpuTrace.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeGpuTrace.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/NativeIOGPUProviderProbe.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeIOGPUProviderProbe.cpp` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/NativeMetalBridgePhase.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeMetalBridgePhase.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/NativeOfflineAccelerator.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeOfflineAccelerator.cpp` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/NativeOfflineGPU.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeOfflineGPU.cpp` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/NativeOsLogExport.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeOsLogExport.mm` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/NativeTurnipSmoke.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeTurnipSmoke.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/NativeTurnipSwapTest.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/NativeTurnipSwapTest.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OfflineMetalCommandWire.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalCommandWire.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OfflineMetalDepthStencilWire.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalDepthStencilWire.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OfflineMetalEventWire.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalEventWire.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OfflineMetalFenceWire.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalFenceWire.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OfflineMetalFrontend.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalFrontend.mm` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/OfflineMetalFunctionWire.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalFunctionWire.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OfflineMetalPlugin.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalPlugin.mm` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/OfflineMetalQueueWire.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalQueueWire.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OfflineMetalSessionWire.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalSessionWire.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OfflineMetalSharedEventWire.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalSharedEventWire.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OfflineMetalTextureWire.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalTextureWire.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OfflineMetalVertexWire.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalVertexWire.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OfflineMetalWire.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OfflineMetalWire.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OriginalAcceleratorABI.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OriginalAcceleratorABI.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/OriginalGPUProviderABI.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/OriginalGPUProviderABI.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/PrimeShaders.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/PrimeShaders.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/QueuePriority.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/QueuePriority.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/README.md` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/README.md` | 用户态 | 文档/工程文件 |
| `drivers/AdrenoMetal/SharedShaderCache.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/SharedShaderCache.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/bridge/FrontendChecks.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/FrontendChecks.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/bridge/NativeMetalBridge.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/NativeMetalBridge.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/bridge/NativeMetalBridge.rs` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/NativeMetalBridge.rs` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/NativeMetalObjects.h` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/NativeMetalObjects.h` | 用户态 | 前端/插件头文件 |
| `drivers/AdrenoMetal/bridge/NativeMetalObjects.rs` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/NativeMetalObjects.rs` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/RustPacBoundary.c` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/RustPacBoundary.c` | 用户态 | 文档/工程文件 |
| `drivers/AdrenoMetal/bridge/archive_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/archive_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/bridge_smoke.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/bridge_smoke.cpp` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/bridge/cache_export_host.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/cache_export_host.cpp` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/carenderer_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/carenderer_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/compute_archive_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/compute_archive_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/coreimage_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/coreimage_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/driver_cache_host.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/driver_cache_host.cpp` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/frontend_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/frontend_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/graphics_cache_loader.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/graphics_cache_loader.cpp` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/bridge/import_count_loader.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/import_count_loader.cpp` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/bridge/interface_regressions_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/interface_regressions_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/nil_args_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/nil_args_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/no_copy_import_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/no_copy_import_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/patches/0001-optional-constant-output-members.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0001-optional-constant-output-members.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0002-resource-wait-storage-dependencies.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0002-resource-wait-storage-dependencies.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0003-cache-device-entry-point-priming.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0003-cache-device-entry-point-priming.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0004-fold-constant-global-field-loads.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0004-fold-constant-global-field-loads.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0005-skip-draws-without-a-pipeline.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0005-skip-draws-without-a-pipeline.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0006-batch-barriers-and-key-hash-neutral-maps.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0006-batch-barriers-and-key-hash-neutral-maps.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0007-optional-shader-cache-directory.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0007-optional-shader-cache-directory.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0008-llvm-bitreverse.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0008-llvm-bitreverse.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0009-patch-with-no-control-points.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0009-patch-with-no-control-points.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0010-core-image-kernel-libraries.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0010-core-image-kernel-libraries.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0011-two-plane-shadow-as-memory-lies.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0011-two-plane-shadow-as-memory-lies.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0012-srgb-extended-range-blend-rounded-model.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0012-srgb-extended-range-blend-rounded-model.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0013-growing-memory-blocks.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0013-growing-memory-blocks.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0014-in-place-written-textures.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0014-in-place-written-textures.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0015-in-place-written-host-page-textures.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0015-in-place-written-host-page-textures.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0016-refuse-rebinds.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0016-refuse-rebinds.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0017-heap-texture-size-a-multiple-of-its-alignment.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0017-heap-texture-size-a-multiple-of-its-alignment.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0018-memory-census-bytes.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0018-memory-census-bytes.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0019-allocation-sites.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0019-allocation-sites.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0020-written-host-page-textures-in-place-at-any-size.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0020-written-host-page-textures-in-place-at-any-size.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0021-spare-image-blocks-and-room-keeper.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0021-spare-image-blocks-and-room-keeper.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0022-command-buffer-stage-trace.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0022-command-buffer-stage-trace.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0023-quality-of-service-on-ios.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0023-quality-of-service-on-ios.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0024-diagnostics-switch-and-submit-steps.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0024-diagnostics-switch-and-submit-steps.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0025-specialized-links-shared-constants-and-allocation-size.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0025-specialized-links-shared-constants-and-allocation-size.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0026-frame-diagnostic-links.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0026-frame-diagnostic-links.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0027-bounded-image-reuse-and-retired-release.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0027-bounded-image-reuse-and-retired-release.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0028-guard-memory-release-test-worker.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0028-guard-memory-release-test-worker.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0029-latency-stage-selection.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0029-latency-stage-selection.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/patches/0030-spare-image-blocks-give-room.patch` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/patches/0030-spare-image-blocks-give-room.patch` | 用户态 | 桥接/工具/着色器输入 |
| `drivers/AdrenoMetal/bridge/pipeline_timing_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/pipeline_timing_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/plugin_sequence_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/plugin_sequence_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/present_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/present_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/priming_cache_host.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/priming_cache_host.cpp` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/priming_cache_ios.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/priming_cache_ios.cpp` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/bridge/priming_cache_loader.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/priming_cache_loader.cpp` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/bridge/quartzcore_blur_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/quartzcore_blur_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/quartzcore_cache.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/quartzcore_cache.mm` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/bridge/quartzcore_cache_ios.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/quartzcore_cache_ios.cpp` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/bridge/quartzcore_cache_provisioned_ios.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/quartzcore_cache_provisioned_ios.cpp` | 用户态 | AdrenoMetal 用户态前端/插件，本轮不编译 |
| `drivers/AdrenoMetal/bridge/queue_priority_host.cpp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/queue_priority_host.cpp` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/register_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/register_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/resource_lifetime_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/resource_lifetime_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/resource_wait_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/resource_wait_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/scanout_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/scanout_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/scheduled_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/scheduled_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/selector_coverage_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/selector_coverage_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/surface_flush_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/surface_flush_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/surface_import_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/surface_import_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/telemetry_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/telemetry_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/uber_diff_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/uber_diff_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/bridge/wallpaper_freeze_host.mm` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/bridge/wallpaper_freeze_host.mm` | 仅vendor | 宿主桥接/测试工具（用户态），不编译 |
| `drivers/AdrenoMetal/shaders/scanout_convert.ll` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/shaders/scanout_convert.ll` | 用户态 | 插件着色器资源 |
| `drivers/AdrenoMetal/shaders/smoke_fill.comp` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/shaders/smoke_fill.comp` | 用户态 | 插件着色器资源 |
| `drivers/AdrenoMetal/shaders/smoke_swap.vert` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/shaders/smoke_swap.vert` | 用户态 | 插件着色器资源 |
| `drivers/AdrenoMetal/shaders/smoke_swap_fetch.frag` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/shaders/smoke_swap_fetch.frag` | 用户态 | 插件着色器资源 |
| `drivers/AdrenoMetal/shaders/smoke_triangle.frag` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/shaders/smoke_triangle.frag` | 用户态 | 插件着色器资源 |
| `drivers/AdrenoMetal/shaders/smoke_triangle.vert` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/shaders/smoke_triangle.vert` | 用户态 | 插件着色器资源 |
| `drivers/AdrenoMetal/tools/MetalVulkanProbe.rs` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/tools/MetalVulkanProbe.rs` | 用户态 | Metal/Vulkan 探针数据与工具资源 |
| `drivers/AdrenoMetal/tools/metal-vulkan-probe.lock` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/tools/metal-vulkan-probe.lock` | 用户态 | Metal/Vulkan 探针数据与工具资源 |
| `drivers/AdrenoMetal/tools/probe-coverage-attachment.json` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/tools/probe-coverage-attachment.json` | 用户态 | Metal/Vulkan 探针数据与工具资源 |
| `drivers/AdrenoMetal/tools/probe-vertex-layout.json` | `package/BoardRuntime/sheng/frontend/AdrenoMetal/tools/probe-vertex-layout.json` | 用户态 | Metal/Vulkan 探针数据与工具资源 |
| `scripts/add-gpu-plugin-data.py` | `package/BoardRuntime/sheng/tools/add-gpu-plugin-data.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-metal-api-surface.py` | `package/BoardRuntime/sheng/tools/audit-metal-api-surface.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-mobile-graphics-legacy.py` | `package/BoardRuntime/sheng/tools/audit-mobile-graphics-legacy.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-mobile-graphics-userclient.py` | `package/BoardRuntime/sheng/tools/audit-mobile-graphics-userclient.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-accelerator-base.py` | `package/BoardRuntime/sheng/tools/audit-target-accelerator-base.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-air-inputs.py` | `package/BoardRuntime/sheng/tools/audit-target-air-inputs.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-cache-leaf.py` | `package/BoardRuntime/sheng/tools/audit-target-cache-leaf.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-display-query.py` | `package/BoardRuntime/sheng/tools/audit-target-display-query.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-dyld-cache.py` | `package/BoardRuntime/sheng/tools/audit-target-dyld-cache.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-iogpu-client-contract.py` | `package/BoardRuntime/sheng/tools/audit-target-iogpu-client-contract.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-iogpu-provider.py` | `package/BoardRuntime/sheng/tools/audit-target-iogpu-provider.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-iogpu-start-contract.py` | `package/BoardRuntime/sheng/tools/audit-target-iogpu-start-contract.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-iogpu-userclient.py` | `package/BoardRuntime/sheng/tools/audit-target-iogpu-userclient.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-kext-bindings.py` | `package/BoardRuntime/sheng/tools/audit-target-kext-bindings.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-libm-inline-data.py` | `package/BoardRuntime/sheng/tools/audit-target-libm-inline-data.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-metal-constants.py` | `package/BoardRuntime/sheng/tools/audit-target-metal-constants.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-metal-device.py` | `package/BoardRuntime/sheng/tools/audit-target-metal-device.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-metal-discovery.py` | `package/BoardRuntime/sheng/tools/audit-target-metal-discovery.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-metal-shared-events.py` | `package/BoardRuntime/sheng/tools/audit-target-metal-shared-events.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-renderbox-device.py` | `package/BoardRuntime/sheng/tools/audit-target-renderbox-device.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-root-hashes.py` | `package/BoardRuntime/sheng/tools/audit-target-root-hashes.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-sandbox-client-class.py` | `package/BoardRuntime/sheng/tools/audit-target-sandbox-client-class.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-userspace.py` | `package/BoardRuntime/sheng/tools/audit-target-userspace.py` | 工具 | 仅 vendor，不执行 |
| `scripts/audit-target-xpc-endpoints.py` | `package/BoardRuntime/sheng/tools/audit-target-xpc-endpoints.py` | 工具 | 仅 vendor，不执行 |
| `scripts/build-adreno-metal-bridge.py` | `package/BoardRuntime/sheng/tools/build-adreno-metal-bridge.py` | 工具 | 仅 vendor，不执行 |
| `scripts/build-adreno-metal-plugin.py` | `package/BoardRuntime/sheng/tools/build-adreno-metal-plugin.py` | 工具 | 仅 vendor，不执行 |
| `scripts/build-adreno740-accelerator.py` | `package/BoardRuntime/sheng/tools/build-adreno740-accelerator.py` | 工具 | 仅 vendor，不执行 |
| `scripts/build-air-nir-host.py` | `package/BoardRuntime/sheng/tools/build-air-nir-host.py` | 工具 | 仅 vendor，不执行 |
| `scripts/build-air-nir-ios.py` | `package/BoardRuntime/sheng/tools/build-air-nir-ios.py` | 工具 | 仅 vendor，不执行 |
| `scripts/build-arm-driver.py` | `package/BoardRuntime/sheng/tools/build-arm-driver.py` | 工具 | 仅 vendor，不执行 |
| `scripts/build-metal-vulkan-probe.py` | `package/BoardRuntime/sheng/tools/build-metal-vulkan-probe.py` | 工具 | 仅 vendor，不执行 |
| `scripts/build-turnip-darwin.py` | `package/BoardRuntime/sheng/tools/build-turnip-darwin.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-accelerator-abi.py` | `package/BoardRuntime/sheng/tools/check-accelerator-abi.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-adreno-constant-commands.py` | `package/BoardRuntime/sheng/tools/check-adreno-constant-commands.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-adreno-constant-uploads.py` | `package/BoardRuntime/sheng/tools/check-adreno-constant-uploads.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-adreno-firmware-checkpoints.py` | `package/BoardRuntime/sheng/tools/check-adreno-firmware-checkpoints.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-adreno-firmware.py` | `package/BoardRuntime/sheng/tools/check-adreno-firmware.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-adreno-hfi-startup.py` | `package/BoardRuntime/sheng/tools/check-adreno-hfi-startup.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-adreno-hfi-votes.py` | `package/BoardRuntime/sheng/tools/check-adreno-hfi-votes.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-adreno-hfi.py` | `package/BoardRuntime/sheng/tools/check-adreno-hfi.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-adreno-pm4-checkpoints.py` | `package/BoardRuntime/sheng/tools/check-adreno-pm4-checkpoints.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-adreno-pm4.py` | `package/BoardRuntime/sheng/tools/check-adreno-pm4.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-adreno-raster.py` | `package/BoardRuntime/sheng/tools/check-adreno-raster.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-adreno-shader-bindings.py` | `package/BoardRuntime/sheng/tools/check-adreno-shader-bindings.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-air-fragment-inputs.py` | `package/BoardRuntime/sheng/tools/check-air-fragment-inputs.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-air-fragment-outputs.py` | `package/BoardRuntime/sheng/tools/check-air-fragment-outputs.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-air-library-compiler.py` | `package/BoardRuntime/sheng/tools/check-air-library-compiler.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-air-library-inputs.py` | `package/BoardRuntime/sheng/tools/check-air-library-inputs.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-air-resource-inputs.py` | `package/BoardRuntime/sheng/tools/check-air-resource-inputs.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-arm-smmu-4k.py` | `package/BoardRuntime/sheng/tools/check-arm-smmu-4k.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-host-metal-async.py` | `package/BoardRuntime/sheng/tools/check-host-metal-async.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-host-metal-fences.py` | `package/BoardRuntime/sheng/tools/check-host-metal-fences.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-host-metal-shared-events.py` | `package/BoardRuntime/sheng/tools/check-host-metal-shared-events.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-host-metal-vertex-inputs.py` | `package/BoardRuntime/sheng/tools/check-host-metal-vertex-inputs.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-iogpu-checkpoints.py` | `package/BoardRuntime/sheng/tools/check-iogpu-checkpoints.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-metal-vulkan-probe.py` | `package/BoardRuntime/sheng/tools/check-metal-vulkan-probe.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-mobile-graphics-checkpoints.py` | `package/BoardRuntime/sheng/tools/check-mobile-graphics-checkpoints.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-mobile-graphics-client-checkpoints.py` | `package/BoardRuntime/sheng/tools/check-mobile-graphics-client-checkpoints.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-native-metal-frontend-host.py` | `package/BoardRuntime/sheng/tools/check-native-metal-frontend-host.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-native-metal-vertex-inputs.py` | `package/BoardRuntime/sheng/tools/check-native-metal-vertex-inputs.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-offline-metal-addon.py` | `package/BoardRuntime/sheng/tools/check-offline-metal-addon.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-offline-metal-sharing.py` | `package/BoardRuntime/sheng/tools/check-offline-metal-sharing.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-original-gpu-attach-only.py` | `package/BoardRuntime/sheng/tools/check-original-gpu-attach-only.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-original-gpu-attach.py` | `package/BoardRuntime/sheng/tools/check-original-gpu-attach.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-original-gpu-client-abi.py` | `package/BoardRuntime/sheng/tools/check-original-gpu-client-abi.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-original-gpu-client-factory.py` | `package/BoardRuntime/sheng/tools/check-original-gpu-client-factory.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-original-gpu-descriptor-provision.py` | `package/BoardRuntime/sheng/tools/check-original-gpu-descriptor-provision.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-original-gpu-descriptor-refusal.py` | `package/BoardRuntime/sheng/tools/check-original-gpu-descriptor-refusal.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-original-gpu-provider.py` | `package/BoardRuntime/sheng/tools/check-original-gpu-provider.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-priming-cache-host.py` | `package/BoardRuntime/sheng/tools/check-priming-cache-host.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-renderbox-drawing-boundary.py` | `package/BoardRuntime/sheng/tools/check-renderbox-drawing-boundary.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-renderbox-function-constants.py` | `package/BoardRuntime/sheng/tools/check-renderbox-function-constants.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-renderbox-pipeline-boundary.py` | `package/BoardRuntime/sheng/tools/check-renderbox-pipeline-boundary.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-renderbox-queue-priority.py` | `package/BoardRuntime/sheng/tools/check-renderbox-queue-priority.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-smmu4k-audit.py` | `package/BoardRuntime/sheng/tools/check-smmu4k-audit.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-smmu4k-checkpoints.py` | `package/BoardRuntime/sheng/tools/check-smmu4k-checkpoints.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-smmu4k-context.py` | `package/BoardRuntime/sheng/tools/check-smmu4k-context.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-smmu4k-live.py` | `package/BoardRuntime/sheng/tools/check-smmu4k-live.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-smmu4k-permissions.py` | `package/BoardRuntime/sheng/tools/check-smmu4k-permissions.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-smmu4k-program.py` | `package/BoardRuntime/sheng/tools/check-smmu4k-program.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-smmu4k-streams.py` | `package/BoardRuntime/sheng/tools/check-smmu4k-streams.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-turnip-darwin-host.py` | `package/BoardRuntime/sheng/tools/check-turnip-darwin-host.py` | 工具 | 仅 vendor，不执行 |
| `scripts/check-turnip-pipeline-cache-host.py` | `package/BoardRuntime/sheng/tools/check-turnip-pipeline-cache-host.py` | 工具 | 仅 vendor，不执行 |
| `scripts/derive-a740-hwinit.py` | `package/BoardRuntime/sheng/tools/derive-a740-hwinit.py` | 工具 | 仅 vendor，不执行 |
| `scripts/derive-a740-pwrup.py` | `package/BoardRuntime/sheng/tools/derive-a740-pwrup.py` | 工具 | 仅 vendor，不执行 |
| `scripts/fetch-adreno-inputs.py` | `package/BoardRuntime/sheng/tools/fetch-adreno-inputs.py` | 工具 | 仅 vendor，不执行 |
| `scripts/gen-turnip-replay.py` | `package/BoardRuntime/sheng/tools/gen-turnip-replay.py` | 工具 | 仅 vendor，不执行 |
| `scripts/gen-turnip-smoke-shaders.py` | `package/BoardRuntime/sheng/tools/gen-turnip-smoke-shaders.py` | 工具 | 仅 vendor，不执行 |
| `scripts/make-gpu-addon.py` | `package/BoardRuntime/sheng/tools/make-gpu-addon.py` | 工具 | 仅 vendor，不执行 |
| `scripts/prepare-air-nir-diagnostic.py` | `package/BoardRuntime/sheng/tools/prepare-air-nir-diagnostic.py` | 工具 | 仅 vendor，不执行 |
| `scripts/prepare-offline-gpu-candidate.py` | `package/BoardRuntime/sheng/tools/prepare-offline-gpu-candidate.py` | 工具 | 仅 vendor，不执行 |
| `scripts/probe-accelerator-abi.py` | `package/BoardRuntime/sheng/tools/probe-accelerator-abi.py` | 工具 | 仅 vendor，不执行 |
| `scripts/probe-air-nir-native.py` | `package/BoardRuntime/sheng/tools/probe-air-nir-native.py` | 工具 | 仅 vendor，不执行 |
| `scripts/probe-metal-vulkan-host.py` | `package/BoardRuntime/sheng/tools/probe-metal-vulkan-host.py` | 工具 | 仅 vendor，不执行 |
| `scripts/probe-offline-gpu-transport.py` | `package/BoardRuntime/sheng/tools/probe-offline-gpu-transport.py` | 工具 | 仅 vendor，不执行 |
| `scripts/probe-original-gpu-client-abi.py` | `package/BoardRuntime/sheng/tools/probe-original-gpu-client-abi.py` | 工具 | 仅 vendor，不执行 |
| `scripts/probe-original-gpu-provider.py` | `package/BoardRuntime/sheng/tools/probe-original-gpu-provider.py` | 工具 | 仅 vendor，不执行 |
| `scripts/probe-turnip-internal-compiler-host.py` | `package/BoardRuntime/sheng/tools/probe-turnip-internal-compiler-host.py` | 工具 | 仅 vendor，不执行 |
| `scripts/requirements-adreno-compiler.txt` | `package/BoardRuntime/sheng/tools/requirements-adreno-compiler.txt` | 工具 | 仅 vendor，不执行 |
| `scripts/save-turnip-builtin-patch.py` | `package/BoardRuntime/sheng/tools/save-turnip-builtin-patch.py` | 工具 | 仅 vendor，不执行 |
| `scripts/save-turnip-darwin-patch.py` | `package/BoardRuntime/sheng/tools/save-turnip-darwin-patch.py` | 工具 | 仅 vendor，不执行 |
| `scripts/turnip_compiler_lldb.py` | `package/BoardRuntime/sheng/tools/turnip_compiler_lldb.py` | 工具 | 仅 vendor，不执行 |

## 6. 与 A540 板级的边界

**未改动**的 A540 板级文件（保持原样，未引入任何 A740 寄存器、固件或命令序列）：

- `package/BoardRuntime/Cheeseburger*.cpp` / `Cheeseburger*.h`（唯一例外是 `CheeseburgerGpuSmmu4K.cpp` 的 4 行 include 路径）；
- `package/BoardRuntime/BootLog.c`、`Boot*Probe.c`、`BootWatchdogPolicy.c`、`CompactObserver.c`、`DisplayObservation.c` 等板级/日志源码；
- 所有 `*-original-bindings.S`、ABI 头、策略头与 `GuardedPMGRSlots.inc`。

边界说明：

1. `CheeseburgerGpuSmmu4K.cpp` 仍然负责板级部分：SMMU 全局配置、vendor ACTLR hazard 位、micro-MMU resume、ATOS 门和失败收敛；ArmSmmu4K kit 只提供页表、上下文、stream 和 live 更新接口。
2. A740 的 `NativeAdrenoA740HwInit.h`、`NativeAdrenoPwrup*`、`NativeAdrenoCpInit*`、`NativeAdrenoGmu*`、`NativeAdrenoIrq*`、`NativeAdrenoGpu*`、`NativeAdrenoFirmware*`、`NativeAdrenoBoard*` 以及 Sm8550/GMU platform 代码只 vendor，不参与 A540 编译，也没有被任何 `Cheeseburger*` 文件包含。
3. AdrenoMetal 前端和 AirNir/AdrenoFirmware kit 没有接入本轮内核构建；A540 的 GPU 服务发布/用户态提交路径保持原状。
4. 没有把 A740 的 SID、GMU 固件地址、ring 初始化序列、寄存器偏移或 SM8550 电源投票复制到 A540 路径。

## 7. 未编译清单

### 7.1 Adreno740（已 vendor，未编译）

- A740 硬件/固件/初始化：`NativeAdrenoA740HwInit.h`、`NativeAdrenoPwrup.cpp/.h`、`NativeAdrenoPwrupRegs.h`、`NativeAdrenoCpInit.cpp/.h`、`NativeAdrenoFirmware.cpp/.h`、`NativeAdrenoFirmwareDefaults.cpp`。
- GMU/SM8550 platform：`NativeAdrenoGmuConfig.cpp/.h`、`NativeAdrenoGmuContext.cpp/.h`、`NativeAdrenoGmuMemory.cpp/.h`、`NativeAdrenoGmuPlatform.cpp/.h`、`NativeAdrenoGmuService.cpp/.h`。
- GPU/IRQ/服务/用户客户端：`NativeAdrenoGpu.cpp/.h`、`NativeAdrenoGpuContext.cpp/.h`、`NativeAdrenoIrq.cpp/.h`、`NativeAdrenoBoard.cpp/.h`、`NativeAdrenoKernelTime.cpp/.h`、`NativeAdrenoService.cpp`、`NativeAdrenoPublish.cpp`、`NativeAdrenoAccelerator.cpp`、`NativeAdrenoClient.cpp/.h`、`NativeAdrenoUserClient.cpp/.h`。
- `tests/` 全部宿主/设备测试（`test_*.cpp`、`tests/hostsim/*`、`XnuMemoryFixture.h`），不计入内核编译。

### 7.2 kits

- `kits/AdrenoFirmware/`：`AdrenoFirmware.c`、`AdrenoHfi.c`、`AdrenoHfiStartup.c`、`AdrenoHfiVotes.c`、`test_*.c`（仅 vendor）。
- `kits/AirNir/`：`Frontend.cpp`、`Library.c`、`MesaBackend.c`、`ConstantUploads.c`、`tests/*`（仅 vendor）。
- `kits/ArmSmmu4K`、`kits/AdrenoPM4` 的 `test_*.c` 与 `tests/*` 不编译。

### 7.3 前端与工具

- `frontend/AdrenoMetal/` 全部文件：`NativeAdrenoMetalPlugin.mm`、`NativeOfflineAccelerator.cpp`、`NativeOfflineGPU.cpp`、`OfflineMetalFrontend.mm`、`OfflineMetalPlugin.mm`、`OfflineMetal*Wire.h`、`bridge/*`、`shaders/*`、`tools/*`。属于用户态/宿主桥接，本轮没有构建或打包。
- `tools/` 全部 102 个 GPU/Metal 脚本与 `requirements-adreno-compiler.txt`：只 vendor，本轮未执行。

## 8. 审计清单

1. **旧文件一致性**：删除 `package/BoardRuntime/adreno740/` 前，逐文件与 `sheng/Adreno740` 及 `sheng/kits/{ArmSmmu4K,AdrenoPM4}` 比较；57 个旧文件全部字节一致；kit 目录只有 `LICENSE`、`README.md`、`AGENTS.md` 三种文档是新增文件。
2. **旧路径清零**：`package/BoardRuntime` 下已无 `adreno740` 引用；`scripts/` 下无 `adreno740` 引用。`sheng/tools/` 内的 `build/adreno740` 字样是上游脚本自己的输出目录名，不是本仓库 include 路径，脚本未执行。
3. **A540 文件差异**：`git diff --stat` 显示板级源码只有 `CheeseburgerGpuSmmu4K.cpp` 的 4 行 include 变化（4 insertions / 4 deletions）；其他 `Cheeseburger*`、`BootLog.c` 等无改动。
4. **编译覆盖**：`runtime-build/commands.json` 中来自 `package/BoardRuntime/sheng/` 的源文件恰好 22 个，与第 4.1 节清单一致；22 个编译日志无 `error:`，共 92 条 warning（provider 记录中的 C99 designator、字段顺序、类型转换等上游风格告警，`-Wno-error` 下不阻断）。
5. **归档**：`libCheeseburgerRuntime.a` 重建成功，`objects=66`，`archive_sha256=de98b7ca215a1817f1e1e21881df215b3883aff8e7bbda05707bc185ae18e87f`，`old_objects_reused=false`。
6. **batch18 内核构建**：`out/p21/gpu-dev-batch18` 由 batch17 复制 `adt/patches/userland/scripts/source/config`，`dev-seed.dbr` 指向 `../gpu-smmu-tlb/dev-seed.dbr`，scripts 内 `batch17` 全部改为 `batch18`，然后执行 `P21_CANDIDATE="$PWD/out/p21/gpu-dev-batch18" /opt/homebrew/bin/python3.13 scripts/p21-compile-atos-gate.py`；结果 `OFFICIAL_KERNEL_SOURCE_BUILD_PASS`，用时 91.5 s，exit 0。
7. **设备状态**：本轮未运行 fastboot/adb，未写分区，未启动镜像。

## 9. 已知差异与未同步项

1. **Adreno740 版本可能落后**：clone 失败后使用固定副本 `4e0314d`；上游 `UniXNU/Adreno740` main 的更新情况未知，未同步项无法逐条列出。文档已记录来源；下一次取得网络后应重新 clone、比对并更新本节。
2. **AdrenoMetal 以最新 main 为准**：`243d63c` 比框架固定副本 `454ff2c` 多出若干头文件、桥接源、着色器和 0026–0030 等补丁；本轮没有尝试编译这些新增桥接代码，无法评估其与 A540/本机 OS ABI 的兼容性。
3. **sheng 板级 GPU 胶水未同步**：`upstream/UniXNU-sheng` 的 `package/GpuPowerProbe.cpp`、`GpuPowerBoard.*`、`AdrenoCompilerProbe.c`、`AdrenoFirmwareProbe.c`、`AdrenoPM4Probe.c`、`ArmSmmu4KProbe.c`、`AirNirDiagnostic.cpp`、`AirLibraryDiagnostic.cpp`、`MetalRuntimeDiagnostic.cpp`、`Offline*` 系列以及 `scripts/prepare-sheng-gmu.py`、`prepare-sheng-gmu-votes.py`、`prepare-sheng-gpu-smmu.py`、`build-adreno740-kits.py`、`build-offline-gpu-*.py`、`pack27-gpu.sh` 等未复制。原因：这些是 sheng（SM8550/A740）板级投票、固件生成和诊断胶水，直接搬到 OnePlus 5 会引入 A740 硬件前提；A540 对应能力由现有 `Cheeseburger*` 板级代码承担。
4. **A540 驱动未替换**：按任务边界，`package/BoardRuntime/Cheeseburger*.cpp/.h` 保持为本机 A540 实现；官方 Adreno740 中面向 A740 的服务/加速器/用户客户端没有接入。
5. **前端与 tools 未接入构建**：AdrenoMetal 插件没有 bundle 进 payload，`Info.plist` 没有安装，`make-gpu-addon.py`/`add-gpu-plugin-data.py` 等工具没有执行；本轮不产生用户态包。
6. **未生成 prelink/UEFI/payload/boot 镜像**：`batch18` 只有内核源码构建产物，没有做 prelink、payload 哈希、UEFI 重编或启动镜像；因此不能用于设备启动验证。
7. **测试未运行**：Adreno740 `tests/`、ArmSmmu4K `test_*.c`、AdrenoPM4 `test_*.c`、AirNir `tests/` 均未编译或执行。

## 10. 未在设备验证声明

**本轮全部结论仅来自源码同步、静态审计和主机侧内核源码构建。没有在 OnePlus 5（`cheeseburger`）上启动、没有接触 Recovery/Fastboot、没有写 userdata/misc/GPT，也没有回收日志。** `batch18` 的 `OFFICIAL_KERNEL_SOURCE_BUILD_PASS` 只证明本轮内核源码可在本机工具链下链接通过，不证明 A540 渲染、SMMU 上下文、用户态 Metal 或任何硬件路径可用；这些仍需后续独立候选和实机验证。
