# P3 UniXNU 集成版实机测试

结果：**用户照片确认 UniXNU.efi 已在 OnePlus 5 真正执行并绘制图形菜单**。照片保存在 out/p3/photos/unixnu-menu.jpg，大小、SHA256 和来源见同目录 manifest.json。

输入 out/p2/integrated/boot-cheeseburger.img（6113280字节），SHA256 dc599569ca8a9edd5f1d6c3a6edf37bc61e8e5b1636220b4b3c00e73a87d12e8。P2 包含源编译 UniXNUDxe/DarwinBoot，UniXNU 应用由 Mu 从源编译后嵌入；不混淆源编译位置。两版独立保存。

预检通过后临时启动两次，首次传输5970KB及启动均OKAY，退出0，耗时10.251秒。日志 integrated-temporary-boot 和 integrated-temporary-boot-repeat 保留原始输出。没有执行刷写。

## 照片与控制流

照片明确显示 UniXNU logo、version 0.1.0、built-in configuration、Return to firmware / Restart 两个动作，以及 Booting Return to firmware in 9 s。底部 no boot entry found，drivers 0 loaded / 0 failed、kits 0、acpi 0。

这说明此前闪过 UniXNU 后进入 Shell，并非已证实的崩溃。Main.c 自动执行默认动作；内建配置没有内核启动项，默认 Return to firmware 返回成功，应用的 firmware host 模式将其转换成 EFI_ABORTED，BDS 随后继续到 Shell。照片与该控制流一致。没有伪造 kernel entry 消除提示。

图形应用执行与显示输出已有实机证据。应用动态注入统计 drivers 0 不能理解为 UEFI 没有 DXE 驱动，也不能据此断言 DarwinBoot 未安装。

## 运行时诊断

config/p3/Probe 是已源码编译、集成并实机运行的只读 DXE 探针：首次 ReadyToBoot 显示协议结果并停留25秒，再继续启动链；不写文件或变量，不调用内核入口。独立 ext4 工作树为 /home/dinnerabc.guest/unixnu-p3/diagnostic，P2 两棵树保留。

完整构建退出0、耗时96.108秒。新 boot image 位于 out/p3/diagnostic/boot-cheeseburger.img，6115328字节，SHA256 84cbf7983ee9a24ac8658dd1ec645a2b0d5f7cb0cd9eefa5bed97eb4b700b4d5；FD 为6291456字节，SHA256 c8c1d55c2f0a0affd6de9315a51287f15d62f3fe862a628601a8712101214517。诊断 PE 为 AArch64，并与固件内对应 PE section 哈希匹配。构建/导出/验证日志见 out/p3/logs，探针校验见 diagnostic/probe-verification.json。

构建环境复用项目内 Ubuntu22.04 ARM64 VM：aarch64-linux-gnu-gcc11.4.0（目标aarch64-linux-gnu）、Python3.10.12、IASL20200925、NASM2.15.05；版本原始证据保留在 out/p2/logs/linux-setup.log，本轮未升级VM依赖。锁定源提交见 sources.lock.json，诊断副本提交及新增源码摘要见 out/p3/probe-inputs.json。本机 ADB/Fastboot、macOS/CPU、Python/Clang 的新检测在 out/p3/host-tool-versions.json。EDK2工具链标签GCC5不是实际GCC版本号；宿主ARM64、VM ARM64与交叉编译目标已区分。

临时启动传输5972KB及 Booting 均OKAY，退出0，耗时10.243秒。用户回传屏幕OCR文字如下，记录来源为人工转录而非直接电子串口日志，见 out/p3/runtime-protocols.json。

| 项目 | 实际观测 |
| --- | --- |
| 异常级 | CurrentEL=1 |
| GOP | Success，2 handles |
| TextInput | Success，2 handles；按键功能未单独验证 |
| SimpleFS | Success，3 handles；不代表所有文件系统可用 |
| BlockIO | Success，79 handles；不是79个物理磁盘 |
| SerialIO | Not Found，0 handles |
| FV2 | Success，2 handles（OCR中的 FU2 按源码标签核正） |
| DarwinBoot Loader | Success，1 handle |
| GOP 模式 | 1080×1920，format=1，stride=1080 |
| Framebuffer | base=0x9D400000，bytes=0x7E9000 |
| BootCurrent | Success，0000 |

DarwinBootEntry 在安装 gUniXNULoaderProtocolGuid 后才提供该 Loader，因此已有协议安装的真实执行证据；尚未调用 BootXnu/ExitBootServices，不能声称 XNU 已进入。源码 handoff 要求 CurrentEL 的原始值为4（EL1），本轮观测满足这一项条件，仍不代表内存/ADT等条件满足。

下一步最小目标是独立配置与最小内核/ADT输入，获得受控内核加载或明确错误日志。当前未执行 XNU，也没有 iOS 启动证明。

## 自动取回日志

本轮 P3Probe 已证明可以注入诊断逻辑。后续可将屏幕打印抽象为内存环形日志，并增加导出通道。优先考虑显式选中的外接 FAT 介质或独立 USB 日志接口；不要向现有三个 SimpleFS 句柄盲目写文件，其中可能是固件分区。SerialIO 不存在，不能把 framebuffer 调试输出当成串口；ADB 只在 Android/Recovery 可用，不能自动读取已退出 UEFI 的普通内存。跨重启回读须先验证实际保留区域/ramoops生命周期，不能猜地址或覆盖校准/GPT。探针尚未实现自动持久化/USB回传，当前结果仍来自屏幕。
