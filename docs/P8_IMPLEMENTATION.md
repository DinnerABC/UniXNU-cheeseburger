# P8 实现与构建

本轮实现了真实存储模块和一次集中实机启动；实机进入 IOKit 设备树处理后重复输出 `unexpected`，尚未证明 UFS 读盘、根卷或 BSD。针对该故障的修正版已继续离线开发。P1–P7 产物保留，P7 冻结哈希通过复查。

## 内核、IOKit 与日志

- 继续使用唯一 XNU 工作树 `.work/p4-unixnu/.cache/xnu-work`，版本 10063.141.1 / Darwin 23.6.0 / 普通 ARM64，增量构建 `MAKEJOBS=-j2`；编译内核时 VM 已停止。
- 保留 P7 已验证的 MMU、KVA、栈、VM 和 PE 实现。移除正常路径中的逐点色条调用，没有增加新的 Sxx 探针。
- `p6_diag_record` 从会释放的 `__BOOTDATA` 迁到 `__DATA,__data`，普通文字控制台使用 VM 后 `ml_io_map` 得到的 framebuffer VA 和 XNU 自有 `iso_font`。32 KiB 环形日志存于内核 DATA，无分配、磁盘写入或重启操作。
- 控制台支持正常输出及 unbuffered panic 路径。`startup_debug=1` 和 kprintf 输出选项启用现有 bootstrap 日志；未禁用调度器、IPC、VM、IOKit、BSD、断言、签名或 Crypto POST。
- 这实现了持续文字显示，**没有实现跨重启的电子日志自动回收**；内存环不能保证长按重启后保留。当前实机证据仍为用户 OCR，不能编造 panic 调用栈。

真实 IOStorageFamily 固定为 Apple `e45fbe30a7879e0b16e26dddb78538f1b00e0bff`。已编译 IOStorage、IOMedia、IOBlockStorageDevice、IOBlockStorageDriver、IOPartitionScheme、IOGUIDPartitionScheme、IOFDiskPartitionScheme、IOFilterScheme、IOMediaBSDClient，并与实际内核 ABI 一同链接，最终无未解析符号。适配只排除本目标不用的 DriverKit 用户态块设备 perf-control 分支，保留原始 native provider 路径；没有使用虚假存储类或闭源 ABI 偏移占位。

KernelConfigTables 的标准存储 personality 从该固定源码 Info.plist 提取。NativeUfs 使用独立项目 bundle 命名和精确 `qcom,ufshc` 匹配，ArmDTPlatform 发布真实 arm-io 设备节点。ArmCpu 兼容额外的有界总线 range，原 GIC range 保留。

## MSM8998 NativeUfs

项目源码在 `drivers/p8/NativeUfs/`，复用固定 UniXNU NativeUfs 的 descriptor 布局和有界 completion policy，独立实现 MSM8998 adapter。上游固定 SHA 和硬件输入哈希见 `out/p8/input-pins.json`；OnePlus Linux 固定提交 `e34b770a6350cab6bfd51d7b4c106b95b40b3dc5`。

| 项目 | 实现及边界 |
|---|---|
| 控制器 | 实机 FDT：0x01da4000 / 0x2500；PHY 0x01da7000 / 0xda8 |
| 时钟 | 实机 GCC 0x00100000 / 0xb0000；CBCR 偏移取自固定一加源码。先只读检查，不照搬 SM8550 时钟或复位 |
| 链路 | 仅支持固件已建立、空闲且 HCE/HCS 就绪的 warm-link；未就绪则拒绝，不假装完成冷启动 PHY/UniPro/电源域初始化 |
| DMA | 自有连续、对齐内存，检查地址能力和长度、cache clean/invalidate、屏障；不重用固件 request list 内存 |
| SMMU | 捕获的 UFS 节点没有 IOMMU 绑定，使用该描述下的物理 DMA；没有复制 stream 0x60、SM8550 SMMU 基址或执行全局 bypass。实际 DMA 可达性仍待硬件验证 |
| 请求队列 | 空闲检查、有界停队列、安装自有队列，单槽轮询；超时/fatal 后隔离并保留可能仍归硬件所有的内存，不盲目取消/重置/重提 |
| 命令 | NOP、REPORT LUNS、READ CAPACITY(16)、READ(16) 及严格读取白名单；最底层拒绝 outbound data、WRITE、UNMAP、FORMAT、MODE SELECT、缓存 flush 等 |
| 块接口 | 按实际 LUN 的容量及块大小发布 IOBlockStorageDevice，经真实 IOBlockStorageDriver → IOMedia → GPT/BSD client；reportWriteProtection 为 true |
| 中断 | 当前为轮询传输；保存真实 INTID 297 元数据，没有冒称实现通用 GIC SPI controller |

以上均已真实编译、链接；尚无实机成功 NOP、LUN、容量、READ 或 IOMedia 证据。它是最小 warm-link 适配，不是完整 MSM8998 冷启动 UFS 驱动。SHA/退出码和离线安全策略测试保存在 `out/p8/native-ufs/`、`out/p8/logs/`。

## 实机故障修复

用户观察持续输出 `unexpected .../IODeviceTreeSupport.cpp:6...`。实际源码第 610 行在缺少 `#interrupt-cells` 时打印此信息；旧 ADT 的 timer/UFS 有通用 `interrupts`，父 arm-io 却没有完整中断 controller/map。

实际 `IODTMapOneInterrupt` 在该描述下不推进 parent，因此持续打印。`scripts/p8-test-interrupt-contract.py` 提取并执行固定源码的真实映射函数：有界复现旧树的重复 warning，完整 synthetic controller 对照组正常退出；修正版检查通过。OCR 没有完整行号，故手机 PC 的精确行号仍是源码关联推断，宿主复现不能冒充手机 trace。

修正版将原始硬件参数表示为驱动私有的 `p8,timer-intid=19` / `p8,spi-intid=297`；ArmCpu 配对读取前者，保留 IOCPUInterruptController、GIC dispatch 和 `rtclock_intr`。UFS 继续轮询。没有禁用 IOKit 核心映射器，没有伪造一个不存在的中断服务。修正后是否能通过该处，仍需设备恢复后的针对性实测。

## iPhone、APFS 和根卷

已直接 Range 读取 Apple iPhone11,8（XR）iOS 17.6 / 21G80 的 BuildManifest、kernelcache、DeviceTree、StaticTrustCache，后三者 SHA384 与 manifest 一致。没有声称完成 Apple 签名链验证。完整 7.62 GB IPSW 与多 GB OS 没有下载。来源及成员路径见 `out/p8/ios/apple-manifest.json`。

真实解压 kernelcache 为 **arm64e，Darwin 23.6.0 / xnu-10063.142.1 / T8020**，与当前普通 ARM64 / 10063.141.1 不同；同为 Darwin 23.6 并不能证明闭源驱动 ABI 相容。SystemOS 的真实 component 名为 `Cryptex1,SystemOS`。没有把这些 iPhone kext、设备身份、Trust Cache 或偏移混入 P8 payload。

`NativeRamBlock` 固定真实源码已编译为 ARM64 archive，未提供 Data seed、未激活，也未谎称形成 APFS Data 卷。P8 NativeUfs 不导出任何 persistent store 写接口。

`scripts/p8-build-root.py` 复用 SHNGROOT header/window 格式，先验证 P3 GPT 文件 SHA、header/entry CRC，再使用一加 5 的真实 userdata 几何：sda，4096 字节 LBA，2441472–14145279，47938797568 字节。构建入口要求同一 Apple identity 的真实镜像、SHA 和当前内核的 ABI 验证记录；只读验证/挂载本地 DMG，检查容量、写入项目内镜像并完整回读窗口哈希，不生成手机刷写命令。

本轮 geometry 路径实际通过；根卷构建因缺兼容且验证过的 APFS/OS 输入明确失败，未生成 `userdata-root.img`。本地完整 DMG 构建路径还未跑通实证。未具备 Root UUID、APFS/AMFI 预链接、VFS 根设备选择、launchd 所需可信运行环境；没有通过跳过签名或核心服务来填平缺项。

当前 UFS 发布的是完整 LUN 及标准 GPT 路径，尚未移植将 userdata 内 SHNGROOT windows 发布为 APFS 子 IOMedia 的过滤层。不能认为镜像格式工具存在，就已具备根卷挂载能力。

## 复现和证据

- `scripts/p8-build.sh`：完整本地流程，遇错停止；不含手机操作。读取现有固定源码，不升级依赖。
- `scripts/p8run.py`：每次唯一 label，命令、stdout/stderr、退出码和时长均入项目日志；中断而无退出状态的执行单独标注，不当作通过。
- `scripts/p8-check-integration.py`：最终 DATA 生命周期、真实存储类、零 unresolved symbol 和设备地址转换核对。
- `scripts/p8-pack-image.py`：FV/PE/DTB/ramdisk 回读，并核对实际 DarwinBoot PE 中内嵌 kernel/ADT 哈希。防止编译失败后把旧固件与新 payload 误打包。
- VM 重启后必须重挂 `p8-guest-overlay.sh`；主构建脚本已包含此步骤。曾产生的一份旧固件误混包已隔离到 `out/p8/invalid/`，从未用于手机。

工具链实际输出在 `out/p8/toolchain.json`。本轮禁止并未使用 GitHub Actions、分区刷写、擦除、解锁或 EDL。
