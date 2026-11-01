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
| 文件影子挂载 `/usr/sbin/bluetoothd`（`sgam=2`） | `79a9a130` / `d14cfb76` | boot `0x3db104c8`：GADDON `ext=2/20`（`mount()` 在 stage 2 返回 ENOTDIR）；`bluetoothd[80]` 原二进制运行；`GPSVC open=0` |

### 3.1 诊断覆盖（boot `0x3db104c8`）

- 快照 `rounds=17`（约 85 s），但 CBHB 心跳到 `sec=350 tag=smc`；心跳tag 含
  `zapdone/submit/submitdone/fencedone`（sec 52），说明内核 fence 确实执行，
  快照停止不等于内核停止。
- `GADDON ext=2/20` 的实际调用点：`sgam=2` 分支调用
  `native_gpu_addon_mount(source=/private/var/mobile/ngpu/bin/adrenoprobed,
  point=/usr/sbin/bluetoothd)`；函数在 stage 2 调用 `mount()`，返回 ENOTDIR。
- 同一轮 `lookup of /System/Library/LaunchDaemons/com.apple.backboardd.plist`
  不存在，说明 launchd 的 job 目录不是该路径（SystemOS cryptex），因此目录挂载
  也选错了目标；`bluetoothd` 仍按启动时的 plist 执行。

## 4. 阻塞点（日志 + 源码审计）

1. **addon 启动期安装**：`native_gpu_addon_install` 在 Data 挂载内部写 4 个文件时卡死，
   持久日志线程尚未落盘；失败表现为无新日志、约 2 分钟 watchdog 复位。
2. **bindfs 不是 union**：`bsd/miscfs/bindfs/bind_vfsops.c` 没有 `MNT_UNION` 处理；
   目录挂载只会显示源目录。boot `0x3db45895` 的 `GJOBS` 已确认
   `/System/Library/LaunchDaemons` 存在且 `com.apple.backboardd.plist` 存在
   （`ld=0 ldb=0`），SystemOS cryptex 路径不存在（`cx=2 cxb=2`）。因此之前的
   ENOENT 是**非 union 挂载盖住下层**后的可见性检查结果，不是路径选错；launchd
   仍运行原 job，说明挂载对其 job 列表/缓存未生效。最小差异应查官方 bindfs 的
   union 支持与挂载时机，而不是更换路径。
3. **文件挂载被拒**：bindfs 对文件挂载点返回 `ENOTDIR`（GADDON `ext=2/20`），
   无法用 shadow binary 方式注入。
4. **种子改动副作用**：新 seed identity 触发 Data reseed（`state=3`、3 万块写入），
   日志线程在约 105 s 停止；同一启动内无法读取 daemon 后续事件。
5. 自建 trust 模块放第 0 会被当作 static 并在失败时 panic；放最后会作为 engineering
   正常加载（已实测 0/1/2 全 loaded）。

## 5. 下一步（按可行性）

| 顺序 | 方案 | 关键改动 | 判据 |
|---|---|---|---|
| A | 确认 launchd 路径/时机（已完成一半） | `GJOBS` 已证明路径是 `/System/Library/LaunchDaemons`；下一步查挂载是否先于 launchd 的 job 读取，以及官方 bindfs 的 union 支持 | 挂载后 launchd 读到我们的 plist（`GPSVC open=1`） |
| B | 让 launchd 重新扫描 | 挂载后在 launchd 启动前完成（现有 Data 挂载点已早于 launchd？需确认），或触发 `launchctl load` 等价路径 | job 出现在进程表 |
| C | 实现 bindfs union / 启用 unionfs | 在 `bind_vnops` 中合并 lower 层 `lookup/readdir`，或在配置中启用 `union` | 目录挂载 stage 5，plist 生效 |
| D | 延迟安装 + 无沙箱 job | 把安装移出 Data 挂载（内核线程 + VFS mkdir），文件落到 mobile/ngpu；不使用 seed 改动 | 安装不再卡死，Data 不 reseed |
| E | Metal/AGX 前端的标准路径 | 发布 `IOAcceleratorES`、AdrenoMetal/Turnip a5xx 插件、显示交接 | 属于 P3，依赖 A–D 的用户态注入 |

内核 ABI、BO、submit、fence 已由宿主机模型和对照组启动证明；**用户态提交仍未验证**
（`GPSVC open=0/sub=0/fence=0`，未观察到用户态 GPU 服务连接；不能由 `markers=0` 推断进程未启动）。launchd 注入是必要条件，但注入路径尚未成立，
因此不能把 P1 描述为“只差一次启动”。P1 的成功链必须逐步记录：进程启动 → 找到服务 →
`IOServiceOpen` 成功 → 两个 BO 分配/映射 → 提交 → fence → CPU 读回四个值 → 安全清理，
且每步带同一 boot ID 和真实返回值。

## 6. 约束

- 不改 boot/recovery/固件/GPT；userdata 与 misc 的操作仍按既有授权。
- 不关闭 AMFI/ZAP/PAS；owner trust 只增加自己的 CDHash。
- 每次启动前写 misc BCB；400 s 自动回；保留 10 GiB Data、P8 静默控制台、低池与 BIMC 时钟。
- boot args 总量（含 DarwinBoot 池参数）保持在约 700 B 以下；本候选 644 B + 池约 34 B。

## 7. 最小修正候选（2026-11-01 晚）

- 镜像 `2d1b4d3e…`，内核 `80474673…`，firmware `84fbbe2c…`，payload `067f0b07…`，args 612 B。
- 输入：已验证 10 GiB seed `b41a46e4…`、原始 Apple trust（无 owner 模块）、无 gpuaddon、
  `cheeseburger-gpu-service=1`；未改 UEFI、时钟、硬件参数。
- 修正：
  - `CheeseburgerGpuWait.h`：fence 轮询统一为 ns 预算，`IODelay(50)` 对应 50000 ns；
    宿主 `gpu_wait_model` 覆盖立即成功、轮询后成功、超时和零轮询。
  - 服务单客户端；BO map 拒绝 IOVA 重叠；页表更新在客户端锁内串行；`BoFree` 先要求
    `cheeseburger_gpu_engine_idle()`（RPTR==WPTR + idle status），SMMU unmap/TLB 成功
    才释放；失败保留对象。
  - `GPSV2`：marker 计数/最后类型、CPU map options、被拒绝的 open。
  - `GJOBS`：在 GPU 探针中只读查找 `/System/Library/LaunchDaemons`、
    `.../com.apple.backboardd.plist`、`/System/Cryptexes/OS/...`、Extensions 和
    `/private/preboot`，记录 errno；用于确定 launchd 实际 job 目录。
  - 用户态探针：每个阶段写 `/dev/console`（独立于 GPU 连接），并在服务找到后先发
    0x41470001 marker open；真实 open 失败也有记录。`/dev/console` 是否在该进程的
    sandbox 下可写、以及是否进入可回收日志，尚未验证，留待受控启动。
  - 关闭/异常路径：`releaseMappedBuffer` 统一“引擎运行→保留、unmap/TLB 失败→保留”；
    `ClientGate` 用 generation 防止旧连接 close/free 清掉新连接；宿主
    `gpu_bo_policy_model` 覆盖关闭交错、旧代释放、unmap 失败。
  - 内核 `GPROBE`：`exec`（成功 exec `adrenoprobed` 的次数，`set_proc_name` 弱引用）、
    marker、refused、open、calls，与同 boot 的 `GPSVC/GPSV2` 对照，用于区分
    launchd 加载/exec/找到服务/open。
- 本轮实机（boot `0x3db45895`，镜像 `2d1b4d3e…`）：快照 70 rounds 到 400 s；CBHB 含
  `fencedone`；`fence OK seq=1`、`publish rc=0`、`AUTORESET issued=1`；
  `GJOBS ld=0 ldb=0 cx=2 cxb=2 ext=0 extx=2 pre=0`；`GPSVC open=0`、`GPSV2 markers=0`；
  `DATA state=3`（从 staging seed 退回已验证 10 GiB seed 的一次 reseed，writes=150585）。
  DISPLAY stage=10 frames=4 clients=25 SWAP=4；TOUCH stage=4 error=0 HID=0（触摸无响应仍是
  独立问题）；PosterBoard crash 循环仍在。**用户态提交仍未验证**。
- 本轮不作为：bluetoothd 替换、文件影子挂载、完整 unionfs、改种子绕安装。

## 8. 关闭策略与 launchd 观测（无启动轮，2026-11-01）

- `CheeseburgerGpuBoPolicy.h`：`releaseMappedBuffer` 统一规则——引擎运行（idle()==-2）
  或 SMMU unmap 失败时保留 BO；`ClientGate` 用 generation 防止旧连接 close/free 清掉
  新连接。`clientClose`、`free`、显式 `BoFree` 都走 `freeBo` 的同一规则。
- 宿主 `gpu_bo_policy_model`：关闭交错、旧代释放、unmap 失败、运行中保留；`gpu_wait_model`
  覆盖等待成功/超时。6 组宿主模型全部 PASS（`out/p21/gpu-cp/host-tests/`）。
- 内核新增 `GPROBE`（`native_adreno_probe_exec` + marker/refused/open/calls）。`exec` 来自
  `set_proc_name` 的成功 exec 计数；与 `GPSVC/GPSV2` 一起用于区分 launchd 加载/exec/
  找到服务/open。本轮**未启动**，`GPROBE` 尚无设备值。
- 本轮内核构建 `402e9197…`（仅构建，未打包/未启动）；上一次实机仍是 `2d1b4d3e…` /
  boot `0x3db45895`。
- `/dev/console` 写入与日志捕获：探针未运行，**未验证**；必须在探针真正 exec 后确认。
- 受控启动的判定条件（满足后再做一轮）：
  1. 记录 daemons 挂载后的 `vfs_flags`（MNT_UNION 是否保留）、`mnt_vnodecovered` 和
     union lookup 结果；
  2. 在不改 seed identity 的前提下放置 daemon/plist（延迟安装），或明确接受一次记录的
     reseed；
  3. 同 boot 采集 `GPROBE exec/marker`、`GPSVC open/refused`、探针 `/dev/console` 行和
     `IOServiceOpen` 返回值。

## 9. launchd 候选（2026-11-01，用户授权一次 reseed）

- 输入保持：A540 初始化、内核源码/参数、UEFI、21H461、时钟、显示配置不变；只改
  Data seed（用户已授权一次 reseed）与 addon 挂载校验。
- Data seed：在 10 GiB seed 副本中只放
  `mobile/ngpu/bin/adrenoprobed` 与 `mobile/ngpu/daemons/org.cheeseburger.adrenoprobed.plist`；
  不包含任何系统 job 的 shadow。encoded `9f8c45f8…`；daemon CDHash `728fe415…`；
  owner trust 模块顺序保持 Apple×2 + own（已实测可加载）。
- 启动参数：`cheeseburger-gpu-service=1 sheng-gpu-addon=1 sgai=0 sgam=1`（644 B）。
  `sgai=0` 跳过启动期安装（避免 Data 挂载内写入卡死）；`sgam=1` 走官方
  `bindfs + MNT_RDONLY|MNT_UNION` 挂载到 `/System/Library/LaunchDaemons`。
- 挂载后一次性校验（`GADDON2`）：实际 `vfs_flags`（MNT_UNION 是否保留）、
  原 `com.apple.backboardd.plist` 查找、`org.cheeseburger.adrenoprobed.plist` 查找；
  任一失败立即 `dounmount` 回退，保证 launchd 仍看到原目录，不留半成品挂载。
- 释放判定：`engine_idle()` 三态——0 运行且空闲、1 reset 回读确认 halt、-1 未知/
  abandoned 未确认、-2 运行中；只有 0/1 且 SMMU unmap/TLB 成功才释放 BO。
- 同 boot 记录：`GADDON/GADDON2`、`GPROBE exec/markers/refused/open`、`GPSVC/GPSV2`、
  探针 `/dev/console` 行、CPU 读回四个 dword、首个失败阶段与返回值。
- 失败分支：job 与原项可见但 `GPROBE exec=0` → 查 launchd 读取/plist 接受/签名；
  exec 有记录但 open 失败 → 查服务匹配/sandbox/权限/ABI；open 成功但 BO/map/submit
  失败 → 修首个失败调用；提交完成但读回错 → 查 CPU 映射属性、PTE、缓存维护、IB 与
  fence；四值与 fence 均通过 → 进入已知像素与 IOSurface 生命周期，暂不宣称 Metal。

## 10. launchd 候选第一次结果（boot `0x3db7cb89`）

- 镜像 `50cfe172…`，内核 `08e4c81a…`，firmware `bcad6a5c…`，payload `b5f5d6a5…`，args 644 B，
  Data 预置种子 `9f8c45f8…`（授权 reseed）。
- **挂载/可见性通过**：`GADDON ext=1/2 daem=5/0 wp=1/2`（daemons 到 stage 5，status 0）；
  `GADDON2 flags=4101029 orig=0 own=0 rb=-1`——`vfs_flags` 含 `MNT_UNION`（0x20），
  原 `com.apple.backboardd.plist` 与自有 `org.cheeseburger.adrenoprobed.plist` 都能查到，
  未发生回退。这证明 union 挂载本身在本机可用。
- **launchd 未执行**：`GPROBE exec=0 markers=0 refused=0 open=0 calls=0`，
  `GPSVC open=0`，msgbuf 无 `P21PROBE`/adrenoprobed 行。原系统 job 正常运行。
- 结论边界：路径存在、挂载 flags/双查找通过已证明；“launchd 实际读取该目录”与
  “我们的 job 被加载/exec”仍未证明。没有把它写成用户态提交或成功。
- 观察到但**未确认根因**的差异：种子预置文件 uid/gid 为 `501:501`，系统 plist 为
  root:wheel。官方 patch 的 `BINDM_FORCE_ROOT` 会在 bindfs `getattr` 报告 root:wheel；
  本机构建树有该实现。不能把 `501:501` 直接定为 launchd 拒绝原因，也不做内核批量
  chown。下一候选记录：Data 原路径与 `/System/Library/LaunchDaemons` 挂载路径的
  实际 uid/gid/mode、`native_bindfs_boot_mount`（BINDM_FORCE_ROOT 的开关）以及
  目录枚举结果（`GADDON3`），并保留 `LimitLoadToSessionType=System` 作为待验证变量。
  `GADDON3` 本轮只记录 uid/gid/mode 与 boot-mount 开关；内核 `VNOP_READDIR` 与
  目标 XNU 的 `uio_resid_64`/六参数接口不兼容，readdir 诊断不并入本轮。launchd 的
  实际枚举仍以 `GPROBE exec` 和 launchd 行为判断，不把 VNOP_READDIR 结果当作同一路径。
  若挂载后 plist 显示 0:0，则保留 Data 所有权，转查 launchd 枚举/接受/签名/加载；
  若仍为 501:501，再定位 FORCE_ROOT 未生效的具体原因。
- 本轮快照 70 rounds、`fence OK`、`publish rc=0`、`AUTORESET issued=1`；Data reseed
  `state=3`（writes=133446）。DISPLAY/TOUCH 证据仍按独立问题保留。

## 11. 所有权观测结果（boot `0x3f3e3c8f`，本轮第二次受控启动）

- 镜像 `dbd33465…`，内核 `a39b12a3…`，firmware `6f4d6c96…`，payload `5cb8080f…`，
  Data 预置种子 `46d03a5a…`（授权 reseed），args 644 B。
- `GADDON3 data=1f5/1f5/81a4 mnt=0/0/81a4 orig=0/0/81a4 boot=1`：
  Data 原路径为 `501:501 0644`；挂载后的自有 plist 与原 `backboardd.plist` 都是
  `0:0 0644`。`boot=1` 表示挂载走 `native_bindfs_boot_mount`，bindfs 的
  `BINDM_FORCE_ROOT` 生效；**未在 Data 上 chown**。
- 挂载/可见性仍通过：`GADDON daem=5/0`、`GADDON2 flags=4101029 orig=0 own=0 rb=-1`。
- 但 `GPROBE exec=0`、`GPSVC open=0`、无 `P21PROBE` 行；`LimitLoadToSessionType=System`
  已加入 plist，仍不足以触发 exec。
- 结论：所有权差异与 FORCE_ROOT 不是 launchd 未加载/未执行的原因（mounted 已显示 0:0，
  按用户分支保留 Data 所有权、不再 chown）。剩余待查：launchd 对本目录的实际枚举
  （getattrlistbulk/缓存/扫描时机）、plist 接受与签名、job 加载和 exec。原系统 job 始终
  可见并正常运行；未覆盖 bluetoothd 或任何系统 job。
- 本轮分项：主机模型 6/6 PASS；Fastboot 接受；内核 `fence OK seq=1`、`publish rc=0`；
  用户态提交未观察；像素/Metal/GPU 合成未验证；显示/触摸/crash 证据独立保留。
