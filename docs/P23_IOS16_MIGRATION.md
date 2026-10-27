# P23：iPhone iOS 16.7.11 系统内容准备与兼容性边界

> **执行更新（2026-10-27）**：本文件的早期静态结论已被实际执行取代。
> Data 所有权已完成；根 UUID、内核策略、trust bundle、Data seed 已进入最终镜像；
> 第一次启动在 `rootvp not authenticated` 停止并已定位修复，第二次启动已发送。
> 详见 [P23_EXECUTION_RESULT.md](P23_EXECUTION_RESULT.md)。

更新：2026-10-27
设备：OnePlus 5 / cheeseburger / MSM8998，6 GiB，1080×1920
目标：在**只改 userdata**、保留现有内核/启动固件/参数/日志系统的前提下，把系统内容从
iPadOS 17.7.11 换成真实 iPhone X 的 iOS 16.7.11。

## 结论摘要

**已完成**：真实 IPSW 校验与提取、OS/SystemOS 原始磁盘校验、iOS 16.7.11 userdata
`SHNGROOT` 镜像、与 owner key 匹配的逐块签名策略、20H360 trust cache bundle、
20H360 skip-setup Data seed、日志窗口/备份边界核对。

**未执行**：没有向手机写入任何分区，没有临时启动，没有回收 R7 的持久日志。
2026-10-27 02:14 和 08:27（CST）检查时 ADB 与 fastboot 设备列表均为空；USB 上能枚举到
`OnePlus A5010`、序列号 `3b25cc7c`，但没有 ADB 接口。设备可能仍停留在上一次 XNU
实验状态，按项目规则不能擅自重启或打断。

**核心判断**：只替换 userdata **不足以**让这套 16.7.11 用户空间在冻结的 17.7.11
启动栈上启动。至少有四类启动输入与 17.7.11 镜像绑定：`/chosen/root-matching`、
内核内嵌的逐块根卷清单、payload trust cache、Data 所有权/身份/文本修补。具体冲突和
最小变更见下文；本轮没有擅自修改这些冻结输入。

## 1. 真实系统版本与来源

| 项目 | 实际值 |
| --- | --- |
| 系统 | iPhone OS **16.7.11** |
| Build | **20H360** |
| 机型 | iPhone10,3 / iPhone10,6（`d22ap`） |
| IPSW | `out/p23/ios-16.7.11/iPhone10,3,iPhone10,6_16.7.11_20H360_Restore.ipsw` |
| 字节 | 6,171,152,202 |
| SHA256 | `af698420578a3eb6f9c157831a20d15075e9c14abc853acb2aca5e7b22865411` |
| OS 组件 | `087-86698-034.dmg`，SHA256 `a43fb001...b2797c`，SHA384 与 BuildManifest 相符 |
| SystemOS 组件 | `087-86593-037.dmg`，SHA256 `0450687c...d682c`，SHA384 与 BuildManifest 相符 |
| StaticTrustCache | `Firmware/087-86698-034.dmg.trustcache`，SHA256 `3194ad50...0f867a` |
| SystemTrustCache | `Firmware/087-86593-037.dmg.trustcache`，SHA256 `9bdda0b8...c57e37a` |

提取记录：`out/p23/ios-16.7.11/extracted/extraction.json`（11 个条目全部通过
BuildManifest SHA-384 校验）。IPSW 的 SHA256 在提取前独立重算并匹配。

宿主挂载读出的真实卷身份：

| 卷 | UUID | 用途 |
| --- | --- | --- |
| OS | `E652776C-E75D-4712-850F-AB4D81C3D836` | APFS `System`，未 sealed |
| SystemOS | `6B06A0EF-7B90-4C3B-B25A-D30E696BAE3C` | APFS `SystemCryptex` |

`launchd`、`dyld` 都是 **arm64**（`LC_BUILD_VERSION minos 16.7, sdk 16.7.3`），
与当前 plain ARM64 内核在指令集层面不冲突；这不是“没有版本问题”的证明。
SystemOS 的 `dyld_shared_cache_arm64*` 位于
`/System/Library/Caches/com.apple.dyld/`，OS 卷自身没有 `System/Library/dyld`。

## 2. 已生成的 userdata 产物

`out/p23/ios-16.7.11/userdata/userdata-root.img`

- 大小 **8,378,122,240** 字节（7990 MiB）
- SHA256 `f8bf26f4dda809a38f1fbd8e99bd1082fa045119ba3cb09afd5675c77744314b`
- `SHNGROOT` v1，2 个窗口，1 MiB header，窗口按 1 MiB 对齐：

| 窗口 | 偏移（字节） | 大小（字节） | SHA256 |
| --- | ---: | ---: | --- |
| OS | 1,048,576 | 5,632,984,576 | `4f283ab0780eb23151c91b92210b324294bf7e34059c7ae404a15b92cefa46f3` |
| SystemOS | 5,635,047,424 | 2,743,074,816 | `0450687c8488f0bf7deb6b467db3d5501a078936459019bd2e012cf812ed682c` |

IPSW 另含 `Cryptex1,AppOS`（`087-86715-037.dmg`，10,485,760 字节）和
`Cryptex1,AppTrustCache`；它们已提取并通过 BuildManifest SHA-384，但**没有**放进
userdata。当前 `SHNGROOT`、owner policy 库和 NativeUfs 发布计划都是两个窗口；
AppOS 主要服务 App Store/按需 cryptex，首屏/根挂载通常不依赖它，但本轮未验证。
若后续需要，必须同时增加第三窗口、第三份 policy 和内核策略输入。

两个窗口是 IPSW 原始整盘字节，不是重新封装的 APFS，也没有改 UUID。完成后对镜像
逐窗口重新 seek + SHA-256，结果与 header 和源盘一致：
`out/p23/ios-16.7.11/userdata/userdata-root.json`。
这是**文件准备与回读证据**，不是手机写入或启动证据。

### owner 根策略

`out/p23/ios-16.7.11/policies/`

- `OS/manifest.bin`：44,008,256 字节，1,375,241 块，UUID `E652776C...`
- `SystemOS/manifest.bin`：21,430,816 字节，669,696 块，UUID `6B06A0EF...`
- 使用项目原 owner RSA-3072 key（`out/p12/policies/private/owner.pem`）签名
- 原始 C 验证器对每个块实际校验通过；`libVerifiedRootPolicy.a` 已生成，
  SHA256 `e6de3b3beb1a5bfe75802bccd92f5a9856814d7a0c4a381f7d838cdbce76923e`
- **没有链接进当前内核**：当前 R7 内核里的清单仍描述 17.7.11 的
  1,483,785 / 806,400 块。

## 3. 兼容性实测与阻塞

机器可读记录：`out/p23/ios-16.7.11/analysis/compatibility.json`。

### 3.1 硬阻塞：`/chosen/root-matching` UUID

`package/BoardRuntime/NativeUfs.cpp:863`：

```cpp
if (!strcmp(gPendingDevice[i]->windowLabel(),"OS") &&
    !p12RootMatchingEquals(gPendingUuid[i])) return false;
```

新 OS 策略 UUID 是 `E652776C-E75D-4712-850F-AB4D81C3D836`，冻结 ADT 仍是
`04769D26-2CA0-4825-B443-E2E5F88300F2`。两者不等，OS 窗口不会被发布。
只把策略 UUID 改成旧值也不行：`mediaWithUuidExists()` 需要真实 APFS 卷 UUID 与策略
一致。最小变更只能是更新 payload ADT 的 `root-matching`，或改写 Apple APFS 卷 UUID
（后者会改动原始系统身份，未采用）。

### 3.2 硬阻塞：内核内嵌的逐块根清单

`_verified_root_os_manifest` / `_verified_root_systemos_manifest` 编译在
`out/p22/r7/kernel/kernel.macho` 内，覆盖 17.7.11 窗口的每个 4 KiB 块。新窗口
块数不同、哈希不同，冻结内核无法验证新窗口。最小变更：用新
`libVerifiedRootPolicy.a` 重新链接内核（或仅原位替换两个清单与 policy size 字段，
不改代码），再重新 prelink/payload。当前 R7 镜像未改。

### 3.3 硬阻塞：payload trust cache

当前 payload 的 trust bundle（`out/p12/trust/original-static-system.bundle`）有
2,478 + 76 条记录，**不包含** 20H360 的 `launchd`
`b65c36c9f5b287a6a8f5ca83d9427fb55ef04f4e` 和 `dyld`
`684d847c40e6e873eeaec693011d4d997a869d2a`。
新 bundle `out/p23/ios-16.7.11/trust/ios-16.7.11-static-system.bundle`
（67,764 字节，SHA256 `7a68b36c...dc680a`）有 2,760 + 61 条，并确认覆盖两者。
最小变更：替换 payload trust bundle 并重打 payload。

### 3.4 版本配对：17.7.11 内核 + 16.7.11 用户空间

- 当前内核：Darwin 23.6.0 / XNU 10063.141.1，`LC_BUILD_VERSION minos 17.0`
- IPSW 内核：Darwin 22.6.0 / `xnu-8796.142.1.703.8~1`，`RELEASE_ARM64_T8015`
- 用户空间：16.7.11，`launchd`/`dyld` minos 16.7

arm64/arm64 匹配，没有 iPhone X 的 arm64e/PAC 问题；但“新内核 + 旧系统用户空间”
不是 Apple 支持的组合。IOKit user client、AMFI、APFS、cryptex、KeyStore 的具体
ABI 没有静态证明，必须实机验证。最小选择只有两个：保留 17.7.11 用户空间，或把
板级支持移植到 20H360 内核并重建启动栈。不能靠改版本号或产品名绕过。

### 3.5 文本修补：4/4 不适用

冻结 ADT 的 `text-fixups` 针对 21H461 dyld cache/BTAudio，20H360 实际字节全部不同：

| 文件@偏移 | 17.7.11 期望 | 20H360 实际 |
| --- | --- | --- |
| `dyld_shared_cache_arm64.15@421e8bc` | `28ad1bb0008544f9` | `9402005209000014` |
| `dyld_shared_cache_arm64.13@1b05bec` | `44008052` | `420b8052` |
| `dyld_shared_cache_arm64.13@1b05b08` | `44008052` | `080100b5` |
| `BTAudioHALPlugin@38bec` | `ffc300d1f44f01a9` | `142040a91f0114eb` |

不能套用旧偏移。`out/p23/ios-16.7.11/adt/kernel.macho.adt.ios16-uuid-only`
只是把 UUID 改新并移除旧 text-fixups 的**差异记录**，明确标注
`ADT_DELTA_PREPARED_NOT_A_BOOT_INPUT`，不是可启动参数集。

### 3.6 身份与显示

冻结 ADT 的 `model=iPad7,1`、`target-type=J120`、pad idiom，是为 iPadOS 17.7.11
donor 选的。iPhone X 的 16.7.11 系统期望 D22/iPhone10,3 的 traits。只改字符串
不是适配：MobileGestalt、idiom、1080×1920 下的缩放/布局都要重新验证。本轮没有
把 iPad 身份改成 iPhone 来“伪装”成 iOS。

## 4. Data seed

`out/p23/ios-16.7.11/data-seed/`

- `owner-data-1g.cdr`：1 GiB raw APFS，来自 16.7.11 OS 卷的 `/private/var`，
  排除 `staged_system_apps`；`fsck_apfs -n` 通过
- `owner-data-1g.dbr`：DBRDISK1 打包，979,056 字节
- `skip-setup-1g-d242.dbr`（最终）：1,245,280 字节，SHA256
  `ccb66c7aeecd815e9ec4d2ef860268e72595898d7fcc13b9a675384b7f06f0e0`
- 已写入 `LastSystemVersion=20H360`、`DMUserDataDisposition.buildVersion=20H360`、
  purplebuddy 完成、SpringBoard `BKNoWatchdogs` 与 `--no-auto-dim` 三项
- **已消除 Data UUID 冲突**：把 seed 的 APFS 卷 UUID 从 `5237E48D...` 改成冻结内核
  编译期期望的 `D2420479-F58E-44C5-AB79-1815EC32C686`。只改 5 个 APSB 检查点的
  UUID 与 Fletcher 校验字，文件内容/权限/prefs 不变；`fsck_apfs -n` 退出 0，只读
  挂载显示 D242 UUID。记录在 `skip-setup-1g-d242.json`。
- 前身 `skip-setup-1g.dbr`（UUID `5237E48D...`，SHA256 `beb7c7...be4a9`）保留，
  作为 UUID 修改前后的对照，不再作为最终输入。

**已知限制**：没有管理员授权，宿主以 `501:20` 写入，无法恢复原 `0:0/501:501/
service uid`。记录在 `skip-setup-1g.json` / `skip-setup-1g-d242.json` 的
`ownership_preserved=false`。
这是部署前必须补做的一步，不把“文件存在”当成可启动 Data。

已生成源卷所有权清单 `data-seed/ownership.tsv`（9,609 条，含 19 个 0700
目录无法向下遍历但目录本身已记录）和需要管理员执行的
`data-seed/fix-ownership.sh`；它只对 seed 中实际存在的路径执行 `chown -h`/`chmod`。

## 5. userdata 部署范围、备份与日志

现有 userdata（P12/P13 写入的原镜像）与日志窗口：

| 项目 | 值 |
| --- | ---: |
| userdata 分区 | 47,938,797,568 字节（LBA 2441472..14145279，4096） |
| 旧根镜像 | 9,382,658,048 字节 = 8948 MiB |
| 旧 OS 窗口 | offset 1,048,576，6,077,580,800 |
| 旧 SystemOS 窗口 | offset 6,079,643,648，3,303,014,400 |
| 持久日志窗口 | userdata 相对 10 GiB = 10,737,418,240，32 MiB |
| 新镜像 | 8,378,122,240 字节 = 7990 MiB |
| 新镜像结束 | 8,378,122,240，距日志窗口 2250 MiB |
| 覆盖范围 | `[0, 8,378,122,240)`，不含日志窗口 |
| 建议备份范围 | `[0, 9,382,658,048)`（旧根全长，8948 MiB） |

日志系统源码、32 MiB 窗口、Recovery 读取工具均未改动。现有本地日志
`out/p21/persistent-logs/`（含 `20261026T180059Z` 八核证据）与窗口备份
`out/p21/r14/private/window-backup/window-before.bin`（SHA256
`83ee4724...c4302`）保持原样。

R7 的 `20261026T180807Z` 运行日志仍在设备窗口里，**尚未回收**；部署前必须先
`scripts/logging/read-cheeseburger-log.py`。设备当前没有 ADB，无法执行。

### 建议的部署顺序（未执行）

1. 人工把设备切到 root Recovery，确认序列号 `3b25cc7c` 和分区几何；
2. 运行 `read-cheeseburger-log.py` 回收 R7 日志；
3. 备份 `[0, 9,382,658,048)`，双读校验；
4. 只写 `[0, 8,378,122,240)`，`conv=fsync`，不碰 10 GiB 日志窗口；
5. 回读 SHA256 与 `f8bf26f4...` 比较；
6. 只有完成内核策略/ADT/trustcache/Data 所有权四组变更并获得明确授权后，才谈临时启动。

## 6. 修改范围

- 新增 `out/p23/ios-16.7.11/` 下的提取、分析、userdata、policies、trust、data-seed，
  以及本文件和兼容性记录。
- **没有**修改 `package/`、`patches/`、`drivers/`、`board.toml`、`dts/`、`configs/`；
  `git status` 为空（out 被忽略）。
- **没有**写手机分区、GPT、boot/recovery/固件；`partition_writes=0`、`boot_attempts=0`。
- 现有 R7 内核、ADT、payload、日志窗口和 P12/P13 历史产物均保留。

## 7. 结论分级

| 阶段 | 结果 |
| --- | --- |
| IPSW 文件校验 | 通过（SHA256 + BuildManifest SHA-384） |
| 系统内容提取 | 通过（OS/SystemOS/trustcache/root_hash/mtree/kernelcache） |
| userdata 镜像生成 | 通过（8,378,122,240 字节，窗口回读哈希一致） |
| owner 逐块签名 | 通过（1,375,241 + 669,696 块，原始 C 验证器） |
| 20H360 trust bundle | 通过（覆盖 launchd/dyld） |
| Data seed 生成 | 通过（20H360 skip-setup，D242 UUID，fsck 0；所有权待管理员修复） |
| 手机传输 | 未执行（无 ADB/fastboot） |
| 内核启动 | 未执行 |
| 用户空间 | 未执行 |
| 原始 iPhone 界面 | 未验证 |

**不能把“IPSW 完整”或“镜像生成”写成“iOS 已运行”。** 当前最接近可启动的下一步
是补做四组冻结启动输入的变更并获得授权；在完成之前，这套 userdata 内容不应直接
覆盖手机。
