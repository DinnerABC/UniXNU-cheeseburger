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
| launchd job 路径 | boot `0x3db45895` `GJOBS ld=0 ldb=0`：`/System/Library/LaunchDaemons` 及 `com.apple.backboardd.plist` 存在；`cx=2 cxb=2` cryptex 路径不存在 | **路径存在**已证明；“launchd 实际读取”与“我们的 job 被加载”仍未证明。此前 ENOENT 是非 union 挂载盖住下层后的可见性检查，不是路径错误 |
| bindfs union | 官方 patch 以 `MNT_RDONLY\|MNT_UNION` 挂载并检查下层/自有 plist；目标 XNU 的 `bind_vfsops.c` 无 `MNT_UNION` 处理，`bindfs_mount` 置 `MNT_RDONLY`、单层视图；文件挂载返回 ENOTDIR | 官方 union 前提在本机不成立。挂载会替换目录；launchd 仍运行原 job（启动列表/缓存），但“何时读取 plist”未证明。先补 `MNT_UNION` 的完整前提对照，不实现完整 unionfs、不再覆盖 bluetoothd |
| 用户态观测 | 只有 `GPSVC ready=1/open=0`、`markers=0`；没有同 boot 的 exec/连接记录 | 表述为“**未观察到用户态 GPU 服务连接**”；不能由 `markers=0` 推断进程未启动。下一受控启动用 `GPROBE exec`、marker open、`IOServiceOpen` 返回值区分 launchd 加载/exec/找到服务/open |
| 快照覆盖 | 本轮 snapshot rounds=17（约 85 s），CBHB 到 350 s tag=smc | 诊断覆盖有限；快照停止原因单独查 |
| 显示/触摸/crash | boot `0x3db45895`：DISPLAY `frames=4`、SWAP `transfer=4`；TOUCH `stage=4 error=0 HID=0`；PosterBoard 多次 `st=6/ns=2/code=6` | `frames=4/transfer=4` 只证明发生过 CPU 显示传输，不证明 GPU 合成；`HID=0` 不能单独证明控制器故障或没有触摸操作；PosterBoard 需另查首个 abort 原因。三项均不归因 GPU |
| Metal/像素 | 无 | 未验证 |

## 5. 构建与启动顺序（保持不变）

1. 板级源码/补丁 → `p21-build-board-runtime.py`（宿主可编译）。
2. `p21-compile-atos-gate.py`（官方 tree + 板级 archive）→ unstripped 内核。
3. `out/p21/atos-gate/scripts/prelink.py` → `patch-xnu-entry-probe.py --panic-screen --no-reset`。
4. payload（verified 10 GiB seed、原始 trust、无 addon）→ UEFI 固件（pins 重扫）→ pack。
5. `preboot-gate-p21.py` → misc BCB → 一次 `fastboot boot` → 回收日志。
6. 分项报告：主机测试 / Fastboot 接受 / 内核 fence / 用户态提交 / 实际像素 / Metal / 系统显示。


## 6. MNT_UNION 前提对照（2026-11-01 补）

- 目标内核确实编入了 `CONFIG_UNION_MOUNTS`（构建的 `CONFIG_DEFINES` 含
  `-DCONFIG_UNION_MOUNTS`），VFS 实现存在：
  - `bsd/vfs/vfs_lookup.c`：`VNOP_LOOKUP` 返回 ENOENT 且 `dp->v_mount->mnt_flag` 有
    `MNT_UNION` 时，调用 `lookup_traverse_union()`；挂载根（VROOT）走
    `mnt_vnodecovered`，即被覆盖的下层目录。
  - `bsd/vfs/vfs_attrlist.c`：`getattrlistbulk` 在上层 EOF 后切到下层 union 目录。
- `bindfs_mount` 不实现 union，但保留挂载标志中的 `MNT_UNION`，并强制
  `MNT_DONTBROWSE|MNT_MULTILABEL|MNT_NOSUID|MNT_RDONLY`。因此前提是：
  1. `mount()` 后该 bindfs mount 的 `vfs_flags` 仍含 `MNT_UNION`（需运行时记录）；
  2. 挂载点的 vnode 是 VROOT，且 `mnt_vnodecovered` 正是路径解析实际使用的目录
     （firmlink/cryptex 可能让路径解析绕过该挂载点）；
  3. 下层查找通过 `mnt_vnodecovered` 能到达原目录（`lookup_traverse_union` 的 path 重建）。
- 当前 `GADDON daem=4/2` 只说明 mount() 成功、`visible` 直接查找失败；它既可能是
  `MNT_UNION` 未保留，也可能是挂载点/被覆盖 vnode 与 launchd 实际路径不一致。
  在未记录 mount flags、mnt_vnodecovered 路径和 union lookup 结果前，不能判定原因，
  也不应再覆盖 bluetoothd 或实现完整 unionfs。

### 三级区分（launchd）

| 问题 | 当前证据 | 结论 |
|---|---|---|
| 底层路径存在 | `GJOBS ld=0 ldb=0` | 已证明 |
| launchd 实际读取我们的 plist | 原 `bluetoothd` 仍执行；无 exec 观测 | 未证明 |
| 我们的 job 已加载/已 exec | boot `0x3db7cb89`：`GPROBE exec=0`，无 P21PROBE 行 | 未加载/未 exec；union 挂载与双查找本身通过（`daem=5/0`、`GADDON2 flags=4101029 orig=0 own=0`）。下一步记录挂载后 uid/gid/mode 与 boot-mount 开关（`GADDON3`）；501:501 只是观察差异，未确认根因。内核 readdir 接口不兼容，launchd 枚举以 `GPROBE exec` 判断 |
