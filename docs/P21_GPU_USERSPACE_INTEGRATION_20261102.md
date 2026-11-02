# P21 GPU 用户态接入：上游机制与本机迁移（2026-11-02）

本文件记录 2026-11-02 对 UniXNU / UniXNU-sheng 最新代码的调研结论、本机 launchd 注入失败的实际证据，以及按上游方式迁移的工作计划。文中“已确认”均指有源码、反汇编或本轮设备日志依据；“未验证”不得当作成功。

## 1. 上游如何把 GPU 接入用户态（已确认，源码级）

官方 `docs/drivers/adreno740.md` / `docs/drivers/native-adreno.md` 与固定框架 `.work/p21-official-framework` 的源码一致，运行路径为：

```
应用 → QuartzCore/RenderBox → NativeMetal 插件
     → metal2vulkan → Turnip (NIR/IR3) → NativeAdreno 用户客户端
     → GMU / CP / SMMU / ring → GPU → scanout
```

关键点：**不是自建 launchd 作业**，而是把 owner 侧代码作为 Metal 插件放进**已有的系统进程**里运行：

1. 内核发布一个 `IOAcceleratorES` 服务（官方实现 `drivers/Adreno740/NativeAdrenoAccelerator.cpp`，类名 `AGXAccelerator`，无额外字段、无操作型 override），提供 `MetalPluginName` / `MetalPluginClassName` 等属性，并用 `NativeSandboxGrantProcess` 指明唯一被放行的进程名（渲染服务器 backboardd）。
2. 系统 Metal 发现按 `IOServiceMatching("IOAcceleratorES")` 找到该节点，读取 `MetalPluginName`，加载 `/System/Library/Extensions/<name>.bundle`，实例化 `NSPrincipalClass` 并调用 `initWithAcceleratorPort:`。
   - 目标 OS 的 `_MTLRegisterDevices` / `_getMetalPluginClassForService` 还会检查插件类 **必须是 `_MTLDevice` 的子类**（`isSubclassOfClass:`，Metal 私有基类）；普通 `NSObject` 子类即使选择子存在也会被跳过，表现是属性被读、accelerator 被 open/close，但插件代码从不执行。官方插件用本地 `@interface _MTLDevice : NSObject @end` 声明该祖先并链接 Metal 框架。
3. 插件是 owner 构建、owner trust cache 认可的 4 KiB CodeDirectory 签名 bundle。它在 backboardd 内打开用户客户端；`patches/xnu/zzw-native-adreno-sandbox-grant.patch` 让服务按其属性放行该进程的 `mac_iokit_check_open(_service)`，其它进程与其它服务不受影响。
4. 插件默认（stage 1）执行共享的用户态 GPU 测试（官方 `NativeAdrenoUserTest.h`）后返回 nil，**不注册设备**，原有软件渲染保持不变；放入 `NativeAdrenoMetal.publish` 标记后，backboardd 接受原生设备，CARenderServer 用 GPU 合成系统 UI；`NativeAdrenoMetal.clients` 列出可获得各自设备的进程（`*` 表示全部）。
5. 数据侧 staging：`scripts/add-gpu-plugin-data.py --publish [--clients ...] BASE.dbr OUT.dbr`，把 `NativeAdrenoMetal.bundle`、`adrenoprobed`、`daemons/*.plist`、标记文件写进 Data seed 的 `/private/var/ngpu/addons`；内核启动时的 addon union mount 把它以只读 union 呈现到 `/System/Library/Extensions`。

本机 ABI 与官方完全一致：`package/BoardRuntime/NativeAdrenoABI.h` 与
`drivers/Adreno740/NativeAdrenoABI.h` 均为 165 行、内容相同（method 1..16、固定记录）。因此差距不在 ABI，而在**服务发布形态、插件、sandbox grant 与 IOGPUFamily 前置**。

## 2. 本机 launchd 注入为什么失败（已确认，本轮设备证据）

两重硬墙，均与路径或参数无关：

1. **系统域只导入签名缓存的搜索路径。** launchd 的搜索路径含
   `/System/Library/LaunchDaemons`、`/System/Library/DeveloperModeLaunchDaemons` 等，**不含 `/Library/LaunchDaemons`**。签名缓存里唯一位于 `/Library` 的 `com.apple.factory.NFQRCoded` 从未被导入：boot `0x55a9e4ea` 的 launchd.log 对它零提及。
   `GDEV stage=5 status=0` 只证明 `/Developer` union 挂载和程序路径解析成功，不代表 launchd 会加载该作业。
2. **AMFI launch constraint 强制校验。** 换成缓存内会触发的 `/System/Library/LaunchDaemons/com.apple.WebBookmarks.webbookmarksd`（XPC activity 约 55 s 经 xpcproxy 启动，原本因程序缺失报 ENOENT）后，boot `0x3ee5d06f` 的日志给出：
   ```
   AMFI: Launch Constraint Violation (enforcing), error info: c[8]p[1]m[1]e[6], (Constraint not matched)
     launching proc[pid: 155]: /System/Cryptexes/App/usr/libexec/webbookmarksd, launch type 1
   ```
   进程被反复重试（pid 155/189/211/219）并成为 corpse。**owner trust cache 中的 cdhash 不能替代平台二进制身份**，因此任何“缓存作业 + 路径替换”都会在此被拒。
3. **`launchd_unsecure_cache=` 是禁用校验，不是路径参数。** 反汇编 `usr/libexec/launchd_cache_loader`（21H461）：该 boot-arg 由 `strstr` + `strtoul`（十进制）解析，非零即跳转到 0x100002828 直接 open/mmap 默认路径，**完全跳过 `SecStaticCodeCreateWithPath` / `SecCodeCheckValidity` / AMFI cdhash 检查**。按其设计会把改动过的缓存挂到默认路径，属于缓存签名绕过，禁止启用。

结论：在不削弱 AMFI/缓存签名的前提下，**launchd 作业路线不可用**；上游的插件路线不需要新作业，因此是正确迁移方向。

## 3. 附带取得的能力：读取 launchd 自身日志（取证用）

launchd.log 在 Data 卷 `/private/var/log/com.apple.xpc.launchd/launchd.log`。Data 在设备上是 seed + UFS overlay：

- 存储区：`/dev/block/sda` 绝对 LBA `11261615`（4 KiB 块，来自启动行 `CB Data store`），窗口共 `1 + 80 + 2621440` 块。
- 布局：块 0 头（`SHNGDATA`，含 seed SHA 与 generation）；块 1..80 位图（页 i 的位 = `bitmap[i>>3] >> (i&7) & 1`）；页区从块 81 开始，**页 i 位于块 81+i**。
- 主机恢复：用 `dev-seed.dbr` 经 `packed_ramdisk.regions` 展开为 10 GiB 稀疏镜像，再按位图把页写回；读写挂载（journal 重放）后才能挂载。注意容易踩的 81 块头/位图偏移。
- 本轮证据：`out/p21/atos-dev-job/private/overlay-20261101T181318Z/`（boot `0x55a9e4ea` 的 Data 重建、launchd.log、loader log 与摘录）。

## 4. 迁移计划（按上游方式，分步验证）

前置：`IOGPUFamily`（`IOAcceleratorES` 基类）必须在我们的 prelink 组件中，且其基类 ABI 要按**本机 21H392 kernelcache** 审计（官方脚本 `audit-target-accelerator-base.py` 生成 `ioaccelerator-base-abi.json`，不是直接套 A740 的地址）。

| 步骤 | 内容 | 验证 |
| --- | --- | --- |
| 1 | prelink 加入 `com.apple.iokit.IOGPUFamily`（官方 `profiles/os/ipados-17.7.11-21H461/profile.toml` 的 roots 本来就含它，我们的 `prelink.py` 漏了）；核对 load 顺序在 owner GPU 服务发布之前 | **宿主已验证**：同一 native 内核加该 root 后 prelink 成功（32 个组件，`IOGPUFamily` 有 entrypoint，输出 `/tmp/prelink-iogpu/kernel.macho`，仅“runtime unqualified”提示）；设备加载未验证 |
| 2 | 把现有 `CheeseburgerGpuService` 改造成 `AGXAccelerator : IOAcceleratorES`（参照 `NativeAdrenoAccelerator.cpp`），发布 `MetalPluginName=NativeAdrenoMetal`、`MetalPluginClassName`、`NativeSandboxGrantProcess=backboardd`；保留我们的 A540 CP/GPMU/ZAP/SMMU/fence 后端与单客户端/BO 策略 | 注册表中出现 IOAcceleratorES 节点及属性 |
| 3 | 把官方 `zzw-native-adreno-sandbox-grant.patch` 纳入 XNU series（目标 `IOUserClient.cpp` 的 `is_io_service_open_extended`） | 宿主编译通过；打开路径仅在命名进程放行 |
| 4 | 构建最小 owner 插件 bundle（`NativeAdrenoMetal`，`NSPrincipalClass` + `initWithAcceleratorPort:`），在 backboardd 内跑 P1：Query → BoAlloc → BoMap → 填 IB → Submit → WaitFence → CPU 回读；stage 到 Data `/private/var/ngpu/addons`，由现有 Extensions union 挂载呈现；bundle cdhash 加入 owner trust module | 发现加载、open、BO/submit/fence 计数、回读值 |
| 5 | 之后才讨论 `--publish`（渲染服务器合成）与 `--clients`（逐进程设备），再进入 IOSurface/Metal 前端 | 首帧/合成日志与像素证据 |

风险与未决：IOGPUFamily 能否在本机 prelink/加载；加速器基类 ABI 与 vtable 审计必须针对 21H392；backboardd 沙箱与 grant 的进程名匹配；插件签名/信任与加载路径；A540 无 Turnip/metal2vulkan 等价物，P1 阶段只用我们的 ABI 测试，不声称 Metal。

## 5. 本机实机进展（2026-11-02 晚）

按上游方式已完成到“插件在 backboardd 内执行并尝试连接”：

| boot | 结果 |
| --- | --- |
| `0x4089ad44` | `load IOGPUFamily rc=0`、`IOAcceleratorES class=1 size=136`、`modload rc=0`、`accelerator publish rc=0`；`GPLUG mpn=3 cls=3`（发现已读属性）；`GPSVC open=1 close=1 uid=1f5`；插件未执行（无 marker/无标记文件）。 |
| `0x3eddc234` | `GOPEN name=backboardd uid=1f5` 确认发现进程是 backboardd；`GPLUG 3/3`；插件仍未执行。 |
| `0x3ee780a4` | 修正插件祖先为 `_MTLDevice` 后：**插件开始执行**，`GPSV2 markers=442 last=3`（进入并反复尝试 open），但 `open=1 close=0`——发现的 type-1 客户端一直占着单客户端 ABI 门，插件的 0x4147 open 得到 Busy（`refused=439`）。 |

关键修正与经验：

- IOGPUFamily 只是被 prelink 并不会注册 `IOAcceleratorES`；必须像上游 `GpuPowerProbe` 一样调用 `OSKextLoadKextWithIdentifier("com.apple.iokit.IOGPUFamily")`，再用 `OSMetaClass::preModLoad` + 本类 `__GLOBAL__sub_I` + `postModLoad` 注册 owner 类，最后 attach 到 `IOService::getPlatform()` 并 `registerService()`。
- 加速器构造/析构/元类地址按本机 prelink 布局静态审计：`accelerator-original-bindings.S` 里的三个地址在多次重编 prelink 后保持不变（Apple 组件按 donor+slide 放置）。
- 发现是**逐进程**的：每个查询 Metal 的进程都会加载自己的插件副本并调用 `initWithAcceleratorPort:`。本机已观察到插件在 **mediaserverd（pid 29）** 中执行（`/private/var/tmp/p21-plugin.txt` 的 `enter pid=29`）；grant 名单必须包含这些宿主（已改为 `backboardd,mediaserverd,SpringBoard`，逗号列表由内核 grant 解析）。未列入的宿主 open 会被 sandbox 静默拒绝（内核看不到 marker）。
- 快照/持久写线程偶发在 80–105 s 卡住；`CheeseburgerAutoReset` 现在另起一个**不写日志/不碰 UFS 的守护线程**，在 `requested+10 s` 直接发 PSCI，避免日志锁阻止自动回 Recovery（主线程仍做 mark/smc 记录）。
- 发现**会自己 open 一次** accelerator（原始 `IOGPUDeviceCreateWithAPIProperty`，open type 1，API 值在 type 高 16 位），该连接可能一直保持；它不属于我们的 ABI。
- 金属发现要求 principal class 是 `_MTLDevice` 子类（私有基类），否则属性被读、accelerator 被 open/close，但 `initWithAcceleratorPort:` 从不调用。
- 单客户端门改为只约束 0x4147 ABI 客户端：其它类型的连接作为 observer 创建，但 `externalMethod` 与 `clientMemoryForType` 一律拒绝，环/SMMU/BO 仍只属于一个 ABI 客户端。

## 5.1 失败路径硬化（2026-11-02，仅构建）

- 用户提交失败后不再重试或读取 CP/VBIF/Interrupt/Status/MEControl：`kickTo()` 只保留第一次捕获的 irq/cpirq 和故障前快照，未采样的字段是 `0xffffffff`；`fail()` 先建立失败状态，再发 posted halt，不写日志。用户 `submit()` 在 doorbell 前采样锁存位：RBBM 陈旧位按 sagit 方式清除（`InterruptClear=OwnedIRQs`），锁存的 CP fault 直接拒绝（`-7`），失败后只读 CPU fence 页记录 `fenced`。
- 每次 boot 只允许一次用户提交；失败后新 ABI open 返回 Busy，`engine_idle=-1` 保留 BO；新增 retained-IOVA 表，teardown/unmap 未能同步的 IOVA 在本 boot 内拒绝任何新连接，避免覆盖旧叶 PTE。
- 插件只在 `mediaserverd` 内执行 P1，其它发现宿主立即返回 nil；open 重试上限 5 s。`CheeseburgerAutoReset` 主线程到点不写日志/UFS 直发 PSCI，guard 在 +10 s 只补发一次。
- 部署约束：本轮候选沿用当前 Data seed（`5446b3a2…`）与 trust（`e92415cc…`），不触发 reseed；新插件（`c6936b5a…`）只构建未写入 seed，部署需用户明确授权一次 seed 更新。
- 实机 `0x5d6a3728` 证明：新 reset 路径在提交卡死后仍于 400 s 返回 Recovery；但用户提交依旧停在门铃之后。上一轮 `e5612490`（IB 在 `CP_SET_SECURE_MODE(0)` 之前）能返回 `rc=-3`，本轮两个内核把切换移到 IB 之前后卡住；同轮探测的 ATOS + fence 又说明 ATOS/门铃本身可用。据此按 sagit 顺序在切换后、IB 前加入 `CP_WAIT_MEM_WRITES`，等待 secure-mode 转换完成。

## 6. 本轮不建议继续的方向

- 不再尝试新的 launchd plist/路径/缓存覆盖；同样会被导入规则或 launch constraint 拒绝。
- 不启用 `sgac` / `launchd_unsecure_cache`，不替换 bluetoothd 或系统 plist。
- 不在 `/Developer` 或 Cryptexes 路径继续投入；保留相关补丁仅作为“已验证被拒”的证据。
