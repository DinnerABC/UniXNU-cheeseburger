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
| 1 | prelink 加入 `com.apple.iokit.IOGPUFamily`（及其依赖 IOSurface/IOKit），核对 load 顺序在 owner GPU 服务发布之前 | 启动日志出现该组件加载；无 panic |
| 2 | 把现有 `CheeseburgerGpuService` 改造成 `AGXAccelerator : IOAcceleratorES`（参照 `NativeAdrenoAccelerator.cpp`），发布 `MetalPluginName=NativeAdrenoMetal`、`MetalPluginClassName`、`NativeSandboxGrantProcess=backboardd`；保留我们的 A540 CP/GPMU/ZAP/SMMU/fence 后端与单客户端/BO 策略 | 注册表中出现 IOAcceleratorES 节点及属性 |
| 3 | 把官方 `zzw-native-adreno-sandbox-grant.patch` 纳入 XNU series（目标 `IOUserClient.cpp` 的 `is_io_service_open_extended`） | 宿主编译通过；打开路径仅在命名进程放行 |
| 4 | 构建最小 owner 插件 bundle（`NativeAdrenoMetal`，`NSPrincipalClass` + `initWithAcceleratorPort:`），在 backboardd 内跑 P1：Query → BoAlloc → BoMap → 填 IB → Submit → WaitFence → CPU 回读；stage 到 Data `/private/var/ngpu/addons`，由现有 Extensions union 挂载呈现；bundle cdhash 加入 owner trust module | 发现加载、open、BO/submit/fence 计数、回读值 |
| 5 | 之后才讨论 `--publish`（渲染服务器合成）与 `--clients`（逐进程设备），再进入 IOSurface/Metal 前端 | 首帧/合成日志与像素证据 |

风险与未决：IOGPUFamily 能否在本机 prelink/加载；加速器基类 ABI 与 vtable 审计必须针对 21H392；backboardd 沙箱与 grant 的进程名匹配；插件签名/信任与加载路径；A540 无 Turnip/metal2vulkan 等价物，P1 阶段只用我们的 ABI 测试，不声称 Metal。

## 5. 本轮不建议继续的方向

- 不再尝试新的 launchd plist/路径/缓存覆盖；同样会被导入规则或 launch constraint 拒绝。
- 不启用 `sgac` / `launchd_unsecure_cache`，不替换 bluetoothd 或系统 plist。
- 不在 `/Developer` 或 Cryptexes 路径继续投入；保留相关补丁仅作为“已验证被拒”的证据。
