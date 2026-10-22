# P2 UniXNU 与 MSM8998 集成结果

## 已实现的层级

已在独立 MSM8998 副本中完整构建并生成集成固件/boot image：

- **UniXNU.efi**：本轮用固定 Mu_Basecore / LLVM 在 macOS 从源编译，再以 FILE APPLICATION / PE32 嵌入原 FV。不是用旧 EDK2 从源重新编译整个 UniXNU 应用。
- **UniXNUDxe.efi**：在 MSM8998 原 EDK2 / GCC11 构建中从源编译，作为 DXE driver 加入 DSC/FDF。
- **DarwinBoot.efi**：在同一原 EDK2 / GCC11 构建中从源编译，作为 UEFI driver 加入 DSC/FDF；只做一个汇编头版本兼容补丁。

`p2-export.py` 从构建产生的 FVMAIN.Fv 解析 FFS 与 PE32 sections，分别按下面 GUID 找到三个实际组件，并比较 section 内容 SHA256 与输入/源编译 .efi 一致：

| 组件 | FV GUID | 来源 |
| --- | --- | --- |
| UniXNU | 9B665204-A133-47A2-9396-9E2B370A4E25 | 本轮独立编译的应用 |
| UniXNUDxe | 29B950CF-586D-49EA-9D71-F88645CCDAF6 | 旧 EDK2 原位源编译 |
| DarwinBoot | 1B64AF87-8415-4B57-9B16-680D70913B9F | 旧 EDK2 原位源编译 |

结果在 `out/p2/integrated/compiled-efi-manifest.json`，integration_verified=true；不是仅在源码中 grep 到名称。实际集成 FD 与 boot image、52 个 AArch64 PE 的清单都已导出。没有设备启动或实际 LoadImage/StartImage 执行证据。

## 改动与复现

`patches/p2/unixnu-fv.patch` 修改 cheeseburger 专属 DSC/FDF：引入 `P2Integration/Integration.dsc.inc`，把 UniXNU 应用和两个驱动放入原 FV。新 include 内容在 `config/p2/Integration.dsc.inc`。

`scripts/p2-integrate.py` 负责从锁定源树拷贝 UniXNUPkg / DarwinBootPkg、复制真实应用、生成 FV 补丁和输入哈希，并调用兼容脚本。应只对新的独立工作副本执行；它拒绝覆盖已存在的 package。已有集成副本直接调用构建入口，不要重复初始化。

`patches/p2/darwinboot-old-edk2-asm.patch` 只将 Entry.S 的 AsmMacroLib.h 换成旧树提供的 AsmMacroIoLibV8.h，并添加 INF 的 ArmPkg/ArmPkg.dec。旧树 ARM 库也使用这一 ASM_FUNC 宏；没有抄入新 Mu 整套头文件，没有改变 handoff 指令。

没有新加 BootShim，没有改变 FD base/size、CPU、显示、保留区、分区或 Trust Cache 数据。原始 FD 已含 ARM64 header，继续采用原来的 gzip(FD)+Android DTB 打包方案。集成版 FD 仍为 6291456 字节。

## 启动路径及尚未证明的部分

UniXNUDxe 源码会在同 FV/其他 FV 找到应用 GUID，构造启动项，并调整 BootOrder，供平台 BDS 启动该 UEFI application。固件提供 Variable/VariableWrite 架构协议及 FV2 仍需运行时核对。这个注册动作在编译阶段不会发生；本轮未运行驱动、未写手机 NVRAM。

DarwinBoot 源码负责安装 gUniXNULoaderProtocolGuid，供 UniXNU 的 XNU entry 调用。它现已编译并嵌入，但 DXE 调度成功、协议实际安装、菜单显示和输入、文件系统可读均未在设备验证。编译成功不能证明其 EL1/内存/ExitBootServices 前提成立。

静态候选协议仍是：SimpleFbDxe 的 GOP、ConSplitter/VirtualKeyboard/Buttons 的输入链、Fat/DiskIo/Partition 的文件系统、UFSDxe 的块设备、ACPI table 与 FV2。特别是 FrameBufferSerialPortLib **不等于** EFI_SERIAL_IO_PROTOCOL 或已知 UART。本轮没有凭静态 INF 声明宣称这些在本手机上可用。

## 距离 XNU 内核启动还缺什么

1. 完整 Xcode 和 iphoneos SDK；当前本机检查仍失败。即使 CI 有 SDK，也缺已资格验证的 MSM8998 kernel archives、defconfig 和 prelink 配置。
2. 独立选择、取得并校验的 IPSW / kernelcache、Trust Cache 策略。没有继承 sheng 的 Apple 平台身份、缓存哈希或私有归档。
3. MSM8998 的合格 Apple ADT 生成器与只读设备参数：RAM/保留区、CPU/MPIDR、GIC/PSCI、显示、异常级与电源等。不能从 sheng 的 SM8550/Adreno 740 推断 MSM8998/Adreno 540。
4. 对原 UEFI（包括源码原有 CPU 编译选项）在目标 CPU/固件组合上的运行资格验证。原项目配置本身也不能因为编译通过就当作硬件事实。
5. 最小运行时协议探针、日志通路与分阶段 handoff 证据，再谈 kernel entry。后续存储/root 还需要准确 GPT、UUID 和卷策略；本轮没有制作 userdata。

`board.toml` 的 example=true 与空研究 defconfig 保留；通用 UEFI 已能独立构建，不应因此解除 XNU 板级门禁。`out/p2/xnu/readiness.json` 是实际检测得到的 BLOCKED 报告，不是编译产物。

本轮可交付的是**真实完成的离线固件集成**，不是“已经能在手机加载 UniXNU”或“已经启动 iOS”。
