# MSM8998 UEFI 差距与最小集成路径

## 1. 结论

edk2-msm8998 明确列出 OnePlus 5 / cheeseburger，并具备专属配置、DSC/FDF、Android/Linux DTB、ACPI 以及部分 Qualcomm 二进制驱动。优先复用这套现有 UEFI，不应从 sheng 的 SM8550 Mu-Silicium 内存与驱动配置起步。

但是现有工程既没有 UniXNU/DarwinBoot 集成，也不满足 `unixnu build firmware --uefi` 所硬编码检查的 Mu-Silicium 目录合同。它的 Windows/ACPI 支持声明不代表 XNU/Apple ADT 支持；本次只读源码审计，没有运行或启动该固件。

审计固定提交：`ace27110b9b9b6c591387d50cf748ec646402b4a`。来源：[支持清单与依赖](https://github.com/edk2-porting/edk2-msm8998/blob/ace27110b9b9b6c591387d50cf748ec646402b4a/README.md)。

## 2. 已找到的 cheeseburger 路径

| 内容 | 相对 edk2-msm8998 根目录 |
| --- | --- |
| 原构建入口 | `build.sh --device cheeseburger`，默认 RELEASE，可选择 DEBUG |
| 设备配置 | `Config/cheeseburger.conf`、`Config/default.conf` |
| 设备 DSC | `Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/cheeseburger.dsc` |
| 设备 FDF | 同目录 `cheeseburger.fdf` |
| 共享 DSC | `Platforms/Msm8998Pkg/Msm8998.dsc`、`Platforms/Msm8998FamilyPkg/Msm8998FamilyPkg.dsc.inc` |
| Android DTB | `.../cheeseburger/DeviceTreeBlob/Android/Android-cheeseburger.dtb` |
| Linux DTB | `.../cheeseburger/DeviceTreeBlob/Linux/Linux-cheeseburger.dtb` |
| ACPI | `Platforms/Msm8998ACPI/oneplus/cheeseburger/`，含 ASL 和 dsdt.aml |
| 设备二进制索引 | `Platforms/Msm8998Binaries/cheeseburger/dxe.inc`、`APRIORI.inc` |
| 共享二进制 | `Platforms/Msm8998Binaries/common/Binaries/`，包括 UFSDxe、Clock、Pmic 等 |
| 原始 FD 输出 | `workspace/Build/cheeseburger/RELEASE_GCC5/FV/MSM8998PKG_UEFI.fd`，DEBUG 改用 DEBUG_GCC5 |
| 原镜像输出 | 原脚本输出目录中的 `boot-cheeseburger.img`，不是 README 中下划线拼法 |

路径及行为依据：[build.sh](https://github.com/edk2-porting/edk2-msm8998/blob/ace27110b9b9b6c591387d50cf748ec646402b4a/build.sh)、[cheeseburger.dsc](https://github.com/edk2-porting/edk2-msm8998/blob/ace27110b9b9b6c591387d50cf748ec646402b4a/Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/cheeseburger.dsc)、[cheeseburger.fdf](https://github.com/edk2-porting/edk2-msm8998/blob/ace27110b9b9b6c591387d50cf748ec646402b4a/Platforms/Msm8998Pkg/Device/oneplus/cheeseburger/cheeseburger.fdf)。

原工程固定的子模块：

| 子模块 | gitlink SHA |
| --- | --- |
| Common/edk2 | `e7aac7fc137e247edad22f7ee53b9a1fba227397` |
| Common/edk2-platforms | `35a84b1838baacf0d470e76fb68a6ade30413450` |
| GPLDrivers/Library/simple-init | `29e062805776db5ab7a61d07ef040d43aed71161` |

不要直接将上述旧 EDK2 换成 Mu_Basecore。先确认原工程的干净构建基线，再在旁路集成通用 EFI。

## 3. 原构建脚本的主机差异

上游列出 Debian 11 / Ubuntu 20.04 测试环境及 gcc-aarch64-linux-gnu、iasl、nasm、GNU 工具、Python 图像/生成依赖。脚本使用 GNU getopt、nproc、realpath、带 GNU 参数的 rm、GCC5_AARCH64_PREFIX，并生成 simple-init rootfs/翻译资源；可用原有 skip-rootfs 选项，但仍需要对应源与依赖。

流程是 BaseTools → GCC5 AARCH64 固件 → gzip FD → 附 Android DTB → 顶层 mkbootimg.py。打包参数由原脚本给出，不是 UniXNU 的 Mu bootimg 路径。本机为 macOS，没有经过验证的 Linux/GCC5 构建环境；因此本次没有把调用原脚本的快速依赖错误当成 UEFI 编译尝试或成功。

本次实际通过的 Mu_Basecore BaseTools，只证明 UniXNU package 工具链可用，不能替代 MSM8998 所锁旧 EDK2 的完整构建。

## 4. EFI 协议逐项核对

UniXNU 的需求来自其 [UEFI 文档](https://github.com/UniXNU/UniXNU/blob/b6ae99c17194e6a18fd819d3148e69fe405e91fc/docs/porting/uefi-firmware.md)，并与 package INF/C 和 DarwinBoot 源码交叉核对。

| 协议/服务 | 现有 MSM8998 源码证据 | 差距与验收 |
| --- | --- | --- |
| EFI_GRAPHICS_OUTPUT_PROTOCOL | FDF 包含 SimpleFbDxe；其 INF 标明 PRODUCES | 仅是静态候选；须读出运行时 framebuffer、像素格式、stride，确认画面持续有效 |
| EFI_SIMPLE_TEXT_IN_PROTOCOL | FDF 包含 ConSplitter、VirtualKeyboard；设备 ButtonsDxe 二进制，源码 KeypadDxe 也有相关声明 | 实际绑定链未验证，不能仅凭有按键 blob 声称菜单可操作；用运行时输入事件验收 |
| EFI_SIMPLE_FILE_SYSTEM_PROTOCOL | FDF 包含 DiskIo、PartitionDxe、FatPkg/EnhancedFatDxe | 需实际枚举到可读 FAT volume；内部文件存在不代表原生 UFS 上的目标卷可读 |
| EFI_BLOCK_IO_PROTOCOL | 设备 dxe.inc 包含共享 UFSDxe 二进制及 USB mass storage 相关驱动 | UFSDxe 的实现不是开源，本次只核对存在/哈希；运行时只读枚举 block size、media、handle |
| EFI_SERIAL_IO_PROTOCOL | 共享 DSC 的 SerialPortLib 实际选用 FrameBufferSerialPortLib | **不能把 SerialPortLib 当作 SerialIo 协议或物理 UART**；需额外确认/实现日志通路，无证据不填 UART 地址 |
| EFI_ACPI_TABLE_PROTOCOL | FDF 包含 AcpiTableDxe / AcpiPlatformDxe，已有设备 ACPI | 可供可选表注入；ACPI 表存在不提供 Apple ADT 或 MSM8998 XNU 驱动 |
| EFI_FIRMWARE_VOLUME2_PROTOCOL | 使用标准 DXE core 和 FV 布局 | 源码结构符合预期，但需运行时枚举；UniXNUDxe 只有嵌入 FV/注册启动项路径才依赖它 |
| Boot Services / memory map / ExitBootServices | 标准 UEFI 启动链基础 | 仍需校验描述符、分配区间和退出一致性；保留区/MMU/EL 不能由构建成功推断 |
| **gUniXNULoaderProtocolGuid** | 原 MSM8998 树未集成 DarwinBoot | **明确缺少**；只启动 UniXNU.efi 可以看菜单，但 XNU entry 会报告无 loader 并拒绝 |

特别依据：UniXNU [Boot.c](https://github.com/UniXNU/UniXNU/blob/b6ae99c17194e6a18fd819d3148e69fe405e91fc/package/UniXNUPkg/Library/UniXNULib/Boot.c) 用 LocateProtocol 获取自定义 loader，检查 revision 1/2 与 Boot 回调；[UniXNULoader.h](https://github.com/UniXNU/UniXNU/blob/b6ae99c17194e6a18fd819d3148e69fe405e91fc/package/UniXNUPkg/Include/Protocol/UniXNULoader.h) 定义接口；[DarwinBoot.inf](https://github.com/UniXNU/DarwinBoot/blob/85332b317ee80898fc78939ea64e6ca52d3837a3/DarwinBootPkg/Driver/DarwinBoot.inf) 明确 PRODUCES 该协议。普通 GOP/UFS/ACPI 齐全仍不能替代此协议。

## 5. Mu-Silicium 与现有 EDK2 的结构差别

| 项 | UniXNU + Mu-Silicium 约定 | 现有 MSM8998 |
| --- | --- | --- |
| 主机工具链 | EDK2 AARCH64 CLANGPDB，Mu_Basecore | Linux AARCH64 GCC5，独立旧 edk2 |
| 配置入口 | Resources/Configs/<device>.toml | Config/<device>.conf shell 变量 |
| DSC 路径 | Platforms/<Vendor>/<device>Pkg | Platforms/Msm8998Pkg/Device/oneplus/cheeseburger |
| 共享树 | Common/Mu、Silicon/Silicium、Silicon/Qualcomm 等 | Common/edk2、Common/edk2-platforms、Silicon/QC、GPLDrivers/simple-init |
| 启动打包 | 独立 BootShim.S + 原始 FD，Resources/Scripts/mkbootimg.py | FDF 自带 ARM64 Linux header，顶层 mkbootimg.py |
| FD 名称 | 通常 SILICIUM_UEFI.fd | MSM8998PKG_UEFI.fd |
| UniXNU 与 DarwinBoot | sheng DSC/FDF 已加入对应 INF | 原树不存在，需要单独集成 |
| 板级参数 | Mu TOML/DSC 定义 | 原设备 FDF/DSC/配置定义，必须对照当前手机证据 |

`unixnu` 的 need_uefi 会检查 `Resources/Scripts/mkbootimg.py`，firmware/bootimg 还需要 Mu TOML 和 BootShim。仅把 DSC/FD 字符串改成 cheeseburger 不足以满足合同。

原 cheeseburger FDF **已有 ARM64 镜像头以及从 header 跳入固件的布局**。直接让 Mu 的 bootimg 再拼一个 BootShim 会改变入口和装载关系，不能用“编译成功”掩盖双重包装问题。FDF 中已有 base/size 仅作为特定提交的源码事实，未复制到 board.toml，也未将其当成本手机测得的参数。

## 6. 最小集成路径

1. **先复现原 UEFI 的离线编译**：在固定 Linux 环境、固定 edk2/gitlink 上构建 cheeseburger，保存 FD/boot image/工具链/命令/退出码/哈希。只在构建目录生成文件，不操作手机。若失败，先解决原基线而不是引入 XNU。
2. **独立通用 EFI**：本次已完成 UniXNU.efi 及附属模块的 macOS 编译。下一步将其当作外部 EFI 应用，保留原平台启动链；先验证菜单或无磁盘写入的协议探针。需要明确只读来源与加载方式后，才规划硬件实验。
3. **仅在需要时嵌入 FV**：将 UniXNU application 与 UniXNUDxe INF 加进原 DSC/FDF，梳理 MdePkg/BaseFdtLib、库类、编译器差异和 PACKAGES_PATH。若旧 EDK2 不兼容，先采用预编译 AArch64 EFI 旁路方案；不一次性替换平台 edk2。
4. **再加入 DarwinBoot**：固定源码，与同一 UniXNULoader.h 接口编译，确认协议安装及 revision。需要独立的 DarwinBoot 二进制，package 的五个产物里不含它。确认当前异常级、内存图、4 KiB 转换能力、缓存和 ExitBootServices 路径，再准备 XNU handoff。
5. **最后做打包适配**：保留原 MSM8998 的 FD/header 方案，或者设计新的入口，但必须单独审查；不能为了通过 need_uefi 而伪造 Mu 目录和地址。原始 stock boot/DTB、布局和尺寸需只读核对，解包回读比对后才算镜像格式验证通过。

优先采用第 1、2 步；不要求为此移植整套 Mu-Silicium，也不要求制作 userdata。

## 7. 专有输入与未知项

本工程 `evidence/msm8998-binary-inventory.json` 列出 common 与 cheeseburger 目录中实际存在的 EFI 二进制的大小/SHA256。它们已经在上游仓库中，不应误报为全部缺失；但 blob 的设备适配、运行时状态、来源及再分发条件仍需审查。清单只记录哈希，本设备仓库没有复制这些 vendor 二进制。

还缺本手机的只读证据：具体 RAM 变体、SMEM/保留内存、TZ/固件兼容、PSCI、GIC/CPU 拓扑、framebuffer/面板保持、bootloader 接受的 header/DTB，以及后续存储阶段的 GPT 和 root volume 策略。Adreno 540 不能继承 sheng 的 Adreno 740 配置；Snapdragon 835 的启动/内存路径也不能从 SM8550 推断。

**完成 UEFI 编译、看到 UEFI 菜单、找到 loader 协议、到达 XNU entry 和运行用户态，是五个不同验收点。** 本次只完成通用 EFI 编译；其余均未获得实机证据。
