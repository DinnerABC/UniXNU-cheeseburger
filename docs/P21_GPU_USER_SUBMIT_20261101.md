# P21 用户态 GPU 提交（sheng ABI，2026-11-01）

目标：按 sheng（iPadOS 17.7.11 / 21H461）的 NativeAdreno 用户客户端标准，让用户态分配
BO、提交一个 IB 并等到 CP 写入的 fence；A540 的 CP/GPMU/ZAP/SMMU 继续使用本机已验证路径。
sagit 只作为 A540 硬件参考，不引入 iOS 18 ABI。

## 1. 已实现（源码）

| 文件 | 内容 |
|---|---|
| `package/BoardRuntime/NativeAdrenoABI.h` | sheng wire ABI v1 原样引入（固定记录、方法号、静态断言） |
| `package/BoardRuntime/CheeseburgerGpuStart.h` | `submit(IB 列表 + fence)`：CP_INDIRECT_BUFFER、CP_SET_SECURE_MODE(0)、CP_WAIT_MEM_WRITES、CACHE_FLUSH_TS；ring 32 KiB 回绕用 NOP 填充；以硬件 WPTR 为准并等待 RPTR 追平；失败 halt/abandon |
| `package/BoardRuntime/CheeseburgerGpuService.cpp` | 持久 `NativeAdrenoGpuService` + `AGXDeviceUserClient`：Query/DeviceInfo/BoAlloc/BoMap/BoFree/BoCpuMap/BoCpuUnmap/Submit/WaitFence/Timestamp/Status/SetMetadata/GetMetadata/CreateQueue/CloseQueue；BO 连续物理页经现有 SMMU 映射；CPU 视图返回同一 descriptor 并要求 `kIOMapInhibitCache`；服务在 fence 成功后发布 |
| `package/BoardRuntime/CheeseburgerGpuSmmu.cpp` | `cheeseburger_gpu_smmu_unmap` + TLB 同步 |
| `package/BoardRuntime/CheeseburgerGpuCp.cpp` | 引擎提升为持久全局；导出 submit/wait/window；`cheeseburger-gpu-service=1` 时发布服务 |
| `package/BoardRuntime/BootLog.c` | `GPSVC`（open/bo/map/sub/fence/err…）与 `GPSTXT`（daemon 回报行）、`GADDON`（addon 安装/挂载状态） |
| `package/Userland/adrenoprobed.cpp` | 用户态测试：打开服务、2 个 4 KiB BO、CPU 映射、IB 3×CP_MEM_WRITE、Submit、WaitFence、校验 4 个 dword、SetMetadata 回报 |
| `scripts/p21-build-adrenoprobed.py` | 4 KiB 页签名、entitlements、owner trust bundle（Apple 2 模块 + 自建模块）、launchd plists、NGPUADD2 addon |
| `scripts/p21-stage-adrenoprobe-seed.py` | 将 daemon/plist 离线写入 Data 种子 `mobile/ngpu`（hdiutil 需本地 /tmp） |
| `out/p21/atos-gate/patches/xnu/0009-…patch` | addon 安装/挂载拆分为 `sgai`/`sgam`，路径改到 `/private/var/mobile/ngpu`，新增 `sgam=2` 文件影子挂载 |

构建集成：`p21-build-board-runtime.py` 将 `CheeseburgerGpuService` 编入并在 `late` 集合中
（元类初始化从 `__mod_init_func` 移出，由 `publish` 显式调用）。

## 2. 宿主机验证

`scripts/run-gpu-start-model-test.py`（`out/p21/gpu-cp/host-tests/20261101T105856Z`）：
CP 启动、GPMU、submit 单 IB、ring 回绕 700 次、非法 IB、kick 失败全部 PASS。

## 3. 实机结果

| 轮次 | 镜像/内核 | 结果 |
|---|---|---|
| 对照组：新内核+服务，原始 trust、无 addon | `132b2ad1` / `6709fdf0` | boot `0x3db53bd3`：`fence OK seq=1`、`CB GPU service: publish rc=0`、400 s 自动回；UI 正常。证明服务与 ABI 路径可用 |
| owner trust（Apple 2 + 自建 1） | `d37a4f1c` / `6709fdf0` | boot `0x3dc2364e`：`loaded external trust cache module: 0/1/2`；服务发布；系统跑到 778 s。自建模块可用 |
| gpuaddon（原始 trust） | `e5f5a38a` / `6709fdf0` | 无新日志、约 2 分钟 watchdog 回；启动期安装卡死 |
| owner trust + gpuaddon（自建模块第 0） | `d736c869` / `6709fdf0` | 无新日志；第 0 模块按 static 加载，失败会 panic（`kern_trustcache.c`） |
| 种子预置 daemon + `sgam=1`（plist 目录挂载） | `9618d685` / `5d512094` | boot `0x3dbb858b`：GADDON `ext=1/2 daem=4/2`；launchd 仍执行原 `bluetoothd`，`GPSVC open=0`；日志线程约 105 s 停（seed identity 变化触发 reseed，`DATA state=3`） |
| 文件影子挂载 `/usr/sbin/bluetoothd`（`sgam=2`） | `79a9a130` / `d14cfb76` | boot `0x3db104c8`：GADDON `ext=2/20`（mount 返回 ENOTDIR）；`bluetoothd` 原二进制运行；`GPSVC open=0` |

## 4. 阻塞点（日志 + 源码审计）

1. **addon 启动期安装**：`native_gpu_addon_install` 在 Data 挂载内部写 4 个文件时卡死，
   持久日志线程尚未落盘；失败表现为无新日志、约 2 分钟 watchdog 复位。
2. **bindfs 不是 union**：`bsd/miscfs/bindfs/bind_vfsops.c` 没有 `MNT_UNION` 处理；
   目录挂载只会显示源目录。日志中 `lookup of /System/Library/LaunchDaemons/
   com.apple.backboardd.plist` 返回 ENOENT，说明该路径不是本机 launchd 的 job 来源
   （job 在 SystemOS cryptex 中），挂载点选错；launchd 仍运行缓存的原 job。
3. **文件挂载被拒**：bindfs 对文件挂载点返回 `ENOTDIR`（GADDON `ext=2/20`），
   无法用 shadow binary 方式注入。
4. **种子改动副作用**：新 seed identity 触发 Data reseed（`state=3`、3 万块写入），
   日志线程在约 105 s 停止；同一启动内无法读取 daemon 后续事件。
5. 自建 trust 模块放第 0 会被当作 static 并在失败时 panic；放最后会作为 engineering
   正常加载（已实测 0/1/2 全 loaded）。

## 5. 下一步（按可行性）

| 顺序 | 方案 | 关键改动 | 判据 |
|---|---|---|---|
| A | 找对 launchd 的 cryptex job 目录 | 在挂载前枚举 `/System/Cryptexes/OS/System/Library/LaunchDaemons` 或 launchd 实际读取的 vnode；`visible` 用存在的 plist 验证 | GADDON 挂载 stage 4，`GPSVC open=1` |
| B | 让 launchd 重新扫描 | 挂载后在 launchd 启动前完成（现有 Data 挂载点已早于 launchd？需确认），或触发 `launchctl load` 等价路径 | job 出现在进程表 |
| C | 实现 bindfs union / 启用 unionfs | 在 `bind_vnops` 中合并 lower 层 `lookup/readdir`，或在配置中启用 `union` | 目录挂载 stage 5，plist 生效 |
| D | 延迟安装 + 无沙箱 job | 把安装移出 Data 挂载（内核线程 + VFS mkdir），文件落到 mobile/ngpu；不使用 seed 改动 | 安装不再卡死，Data 不 reseed |
| E | Metal/AGX 前端的标准路径 | 发布 `IOAcceleratorES`、AdrenoMetal/Turnip a5xx 插件、显示交接 | 属于 P3，依赖 A–D 的用户态注入 |

P1 的“用户态 post + fence”目前只差 **launchd 注入**；内核 ABI、BO、submit、fence 已由对照组和
宿主机模型证明。没有 launchd 注入就不能宣称 P1 完成，也不能开始 Metal/UI 的 P3。

## 6. 约束

- 不改 boot/recovery/固件/GPT；userdata 与 misc 的操作仍按既有授权。
- 不关闭 AMFI/ZAP/PAS；owner trust 只增加自己的 CDHash。
- 每次启动前写 misc BCB；400 s 自动回；保留 10 GiB Data、P8 静默控制台、低池与 BIMC 时钟。
- boot args 总量（含 DarwinBoot 池参数）保持在约 700 B 以下；本候选 644 B + 池约 34 B。
