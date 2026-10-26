# P22 R6：PSCI 修复与 iOS / Metal 输入核对

2026-10-27

## CPU

本轮 Recovery 日志 `out/p21/persistent-logs/20261026T174210Z`：
boot_id `0x4ac5d73c`，315次提交、写入错误0；CPU1–7全部为
`step=3 PSCI=-1001 online=1`。

R5的实际ADT缺少`/psci`。官方ArmCpu的`psciConduit()`在找不到该节点时，
由`startCPU()`返回`-1001`，尚未调用PSCI CPU_ON，因此步骤保持3。
本机Recovery FDT实际包含`compatible=arm,psci-1.0; method=smc`。

R6仅补齐该节点，沿用R5内核、8核配置、1GiB和vnode33792。
其他既有ADT节点逐项保持一致；日志和CPU软件显示保留。
可重复修复脚本：`scripts/p22-add-psci.py`。

镜像：`out/p22/r6/uefi/boot-p22-r6-psci-8cpu-1g.img`

SHA256：`240497486a0c43333892ba368cd15bf6b475e1ffaaf8354d7a60624466efd87d`

启动记录：`out/p22/r6/boot-result.json`。以该记录和用户屏幕反馈区分
传输成功、PSCI成功、实际在线CPU及系统交互，不能将打包视为八核成功。

本轮R6实机日志已回收至`out/p21/persistent-logs/20261026T180059Z`：
boot_id `0x46b9d65f`，174次提交、写入错误0。873秒快照显示`online=8`，
CPU1–7均为`step=7 fail=0 PSCI=0`，全部核心有定时器计数；
显示`clients=7 frames=12`、SWAP `transfer=12 reject=0`。
这是八核在线和显示提交的实机证据，不代表完整IPI或长期稳定性验证。
触摸仍在初始化检查报`error=-54`，I²C尚未发生；本轮不混入触摸改动。

## iOS 完整 IPSW

用户同意改选有完整IPSW的版本。已查询到iPhone XS的iOS17.7/21H16完整包，
另选iPhone X的iOS16.7.11/20H360作为arm64候选并开始后台下载。
直接从Apple CDN包内读取BuildManifest已确认版本、机型iPhone10,3/iPhone10,6，
系统映像路径`087-86698-034.dmg`。来源及下载校验记录保存在
`out/p23/ios-16.7.11/`；下载完成之前保留`.partial`。

iOS17.7.2/21H221的iPhone XS目录只查到OTA包，本轮停止下载它。
系统尚未迁移；完整IPSW和arm64不代表与当前XNU10063.141.1、Apple组件、
root匹配和信任输入兼容。后续系统迁移仍限userdata，不执行苹果整机恢复流程。

## Metal

官方NativeAdreno当前说明是A740/SM8550实现，不能直接用于A540。
同芯片参考`MC-XiaoXiao/ios_sagit`已固定到
`898998cf1b74ea103e0c16d6dc0016822d8d644b`，源码及说明保存在
`out/p23/metal-review/`。

该仓库有QcomGPU内核后端、QcomMTLDriver.bundle、Mesa NIR/IR3编译链和
EFI GPU固件/电源/SMMU交接。`docs/gpu/native-iogpu.md`声明当前私有ABI输入为
iOS18.7 internal/22H368、SDK18.6；不能直接合入当前17.7.11或新16.7.11。
需要匹配所选OS的IOGPU ABI，并核对一加5实际RPM/电源、SMMU、GPU固件输入。
本轮R6未开启Metal，未替换现有CPU渲染路径。

## 后续源码：帧缓冲 Normal Non-Cacheable

按用户要求，帧缓冲的早期页表、VM初始化后的观察器映射、XNU视频控制台、
NativeLegacyDisplay和NativeGOPPresenter统一选择Normal NC。
当前ARM64源码中`kIOMapWriteCombineCache → VM_WIMG_WCOMB →
CACHE_ATTRINDX_WRITECOMB`，非XS路径的MAIR为`0x44`，即Inner/Outer均不缓存。
已有显示写入完成屏障保留；触摸/按键等外设MMIO仍为Device。

修改位于0003补丁、新增0008补丁及两个显示后端；未改固定上游检出目录。
这是R6启动之后的新源码变更，不能据此认定实机性能改善。

R7已完成XNU重编译、板级archive、Apple组件预链接和固件打包，并执行一次
身份绑定临时启动。记录`out/p22/r7/boot-result.json`为
`TRANSPORT_ACCEPTED_SCREEN_PENDING`，未刷写分区，实际画面与性能待用户反馈。
镜像`out/p22/r7/uefi/boot-p22-r7-normal-nc-8cpu-1g.img`，SHA256：
`93268d6b84862e2fc8d071e162f77c0ff0ce7d9e4b3b5961d09a6b666f8ce04a`。
保留R6的8核/1GiB、PSCI描述和日志；未迁移iOS或启用Metal。
