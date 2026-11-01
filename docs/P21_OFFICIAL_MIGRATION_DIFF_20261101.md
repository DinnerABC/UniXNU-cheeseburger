# P21 官方迁移差异清单（一页，2026-11-01）

本页只记录“当前实际输入 ↔ 官方期望入口 ↔ 本机必要覆盖”，不用于替换已验证的
UEFI、内核、Data、时钟和硬件参数。官方框架固定提交：`16e592abee8a550dec8154ed1f38b5ebc4f16213`
（`.work/p21-official-framework`，已核对）。未与在线新版本做替换；如需比较，只记录版本差异。

## 1. 当前实际输入

| 输入 | 值/来源 |
|---|---|
| OS profile | `ipados-17.7.11-21H461`；Darwin 23.6.0；XNU 10063.141.1 revision `d8b80295…`；ABI：IOGPUFamily / IOHIDFamily-2031.120.4 / IOMobileGraphicsFamily |
| 官方框架 | `16e592ab…`；`docs/porting/{gpu,display,uefi-firmware,migration}.md` |
| donor kernelcache | `f18b4df8…`（`out/p12/inputs/A/kernelcache.macho`） |
| XNU 构建树 | `out/p21/atos-gate/source-tree/.../tree`（官方 recipe + 板级 archive） |
| verified Data seed | `owner-data-10g.dbr`，10 GiB，encoded `b41a46e4…`（`validation-10g.json`） |
| 板级运行时 | `package/BoardRuntime/`（NativeUfs/NativeRamBlock、GopFramebuffer/NativeLegacyDisplay、GPU A540、Touch、AutoReset、BootLog） |
| 板级 XNU 补丁 | `patches/xnu/0008-fb-console-switch`、`0009-addon-install-mount-switches`（后者为 bring-up 开关，不是主线） |
| 上一轮启动镜像 | `79a9a130…`，内核 `d14cfb76…`，firmware `31724bc9…`，payload `6e7319f0…`，boot `0x3db104c8` |
| 本轮最小修正候选 | 镜像 `2d1b4d3e…`、内核 `80474673…`、firmware `84fbbe2c…`、payload `067f0b07…`、args 612 B；boot `0x3db45895` |

## 2. 官方期望入口（对照）

| 能力 | 官方入口 | 本机状态 |
|---|---|---|
| 固件扫描/菜单/DarwinBoot | Mu-Silicium + `DarwinBootPkg`；`uefi-firmware.md` 的 DSC/FDF/服务清单 | 已有可启动 FD；未替换 |
| 显示后端 | GopFramebuffer `NativeLegacyDisplay/NativeGOPPresenter/NativeGOPWire`；`display.md` 先验证扫描、swap 完成与 IOSurface 生命周期 | 复用已验证 CPU 路径；GPU 呈现未验证 |
| GPU 用户态 | sheng `Adreno740`（接口/集成参考）+ `AdrenoMetal`（Metal 前端） | 只采用 wire ABI 与 service/client 形状；A740 寄存器/命令/电源/固件未使用 |
| A540 硬件 | 小米 6 `ios_sagit` 的 A540 CP/GPMU/ZAP/SMMU 顺序 | 作为 MSM8998/A540 参考；已用本机 fence 验证 |
| GPU addon/seed/trust | `zzy-native-gpu-addon-mount.patch`、`make-gpu-addon.py`、`add-gpu-plugin-data.py`、`make-owner-trust-bundle.py` | 已审计；本机挂载/launchd 路径存在差异，见第 4 节 |
| Metal | AdrenoMetal + Turnip + metal2vulkan + IOGPU 发布 | 未开始；依赖用户态注入和显示交接 |

## 3. 板级必要覆盖

- A540：CP ME_INIT/configure/reset、GPMU 上传、ZAP/PAS、SMMU 4 KiB 表、BIMC GFX 时钟、
  `cheeseburger-smmu-pool` 低池；均为本机寄存器/固件/地址，不取自 A740。
- 存储/Data：NativeUfs + NativeRamBlock + 10 GiB seed；日志窗口 10 GiB/32 MiB。
- 显示：GOP 扫描缓冲 + NativeLegacyDisplay/NativeGOPPresenter；P8 控制台可关闭画屏。
- 输入：CheeseburgerTouch QUP5/RMI4 + NativeHidInput。
- 诊断：BootLog 快照 + CBHB 心跳；两者独立，快照停止不等于内核停止。

## 4. 已知未对齐/未验证

| 项 | 证据 | 结论 |
|---|---|---|
| gpuaddon 启动期安装 | 无新日志、约 2 分钟 watchdog；安装发生在 Data 挂载内 | 未解决；本机需延后/换路径，未采用种子改动绕行 |
| launchd job 目录 | `/System/Library/LaunchDaemons/com.apple.backboardd.plist` 查找 ENOENT；job 在 SystemOS cryptex | 挂载点需重新确认；本轮加 `GJOBS` 路径探针 |
| bindfs union | `bind_vfsops.c` 无 MNT_UNION；文件挂载返回 ENOTDIR | 不能按 sheng 的目录/文件影子直接复用；需最小差异 |
| 用户态进程 | `GPSVC open=0`，无 GPSTXT；无 launchd 注入 | 用户态提交未验证，不能宣称 P1 完成 |
| 快照覆盖 | 本轮 snapshot rounds=17（约 85 s），CBHB 到 350 s tag=smc | 诊断覆盖有限；快照停止原因单独查 |
| 显示/触摸/crash | 用户报告刷屏覆盖、触摸无响应、crash | 独立问题；需按镜像/boot ID 对齐后查第一处失败，不归因 GPU |
| Metal/像素 | 无 | 未验证 |

## 5. 构建与启动顺序（保持不变）

1. 板级源码/补丁 → `p21-build-board-runtime.py`（宿主可编译）。
2. `p21-compile-atos-gate.py`（官方 tree + 板级 archive）→ unstripped 内核。
3. `out/p21/atos-gate/scripts/prelink.py` → `patch-xnu-entry-probe.py --panic-screen --no-reset`。
4. payload（verified 10 GiB seed、原始 trust、无 addon）→ UEFI 固件（pins 重扫）→ pack。
5. `preboot-gate-p21.py` → misc BCB → 一次 `fastboot boot` → 回收日志。
6. 分项报告：主机测试 / Fastboot 接受 / 内核 fence / 用户态提交 / 实际像素 / Metal / 系统显示。
