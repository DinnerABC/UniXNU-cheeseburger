# P11 实现

## 最终状态：已停止（2026-10-23）

用户要求记录并报告后停止，未继续编译或测试。v5 最终照片已确认 6 个只读 LUN 服务和 LUN0 的四次 READ10 响应传输成功；根匹配阶段仍 panic，未证明 IOMedia/GPT/APFS、根卷或 launchd。先前文中的“待验证”属于历史过程，最终证据以本节和 P11_BOOT_RESULT.md 为准。

最终读路径：完成全部容量查询后发布服务，读取由 IOLock 串行化；有界 READ10 只允许 32 位 LBA 和非零 16 位计数，越界拒绝，保持完整长度、OCS、状态、任务/LUN、sense 门禁，不忽略任何设备错误。固定 EDK2 UfsPeimRead10 作为字段布局参考，host wire/boundary/sanitizer 和真实编译均通过，最终 Mach-O 反汇编确认实际 opcode=0x28。

修复了三项实际代码缺陷/兼容问题：REPORT LUNS 的合法短响应被拒绝、枚举与上层读取共享 DMA 缓冲区的并发风险、当前设备拒绝 READ16 时仍固定使用该命令。原始 PMGR/安全组件代码保持，PMGR 的 Qualcomm 硬件电源后端仍未实现。

当前不能证明所有 LUN、所有块或上层目标缓冲区都稳定可读；照片日志在 writeBytes 之前，只证明已接受的 UFS 响应传输。不能把 root-match panic 解释成已确认 APFS 驱动故障或声称完整 iOS 引导完成。未生成新的完整 iOS 系统镜像，未改任何分区。

照片/哈希/逐项转录：out/p11/evidence/v5-photos/；最终 manifest、STOPPED.json、冻结候选和接续记录已保存。所有历史产物保留，无后台自动重试，构建 VM 已停止。

## 最新候选 v5 READ10

- 内核 SHA256 `40a9449209885028c0b0c09d728aa8d79f2db1826a55948a61be8747d18760dd`，51544064 字节，普通 ARM64。
- 镜像 `out/p11/uefi/boot-p11-xnu.img`，57753600 字节，SHA256 `57ce685b94469091fff85a4a8c782bd9c47b30561b69b150677ac4c157f894fe`。
- 固定 prelink-v5；所有构建/绑定/ABI/解析/打包/回读门禁已真实通过，详细命令与退出码在 out/p11/logs/*v5*。最终 read() 反汇编包含实际 opcode 0x28 构造，证据 disassemble-read-v5-01.log。
- 冻结二进制和源文件：out/p11/milestones/v5-read10/。P1–P10 和 P11 v1–v4 均保留。
- 用户已确认 Android；run `20261022T184732Z-8356a95a` 正执行本版一次临时启动流程。实机结果待收集，尚不能宣称 READ10 或根卷成功。

## v4 实机结论与 v5

v4 照片归档于 `out/p11/evidence/v4-photos/`。READ(16) 的 rx_lun 与请求一致、tag=1、cs=0、ehs=0、flags=0，但设备 response=1、status=2、sense key=5、ASC/ASCQ=24/00，accepted=0；因此不能把全部失败归因于旧并发缺陷。最终仍等待 Apple_HFS IOMedia，报 root mount panic（Caller `0xfffffff007a07b6c`，Last bootstrap bsd_init）。没有可靠块读取或挂载成功证据。

v5 根据固定 EDK2 `MdeModulePkg/Bus/Ufs/UfsBlockIoPei/UfsBlockIoPei.c` 的 UfsPeimRead10 与容量分支，实现严格有界 READ(10)：32 位 LBA、非零 16 位块数，拒绝末端越界，不截断大地址/计数。UPIU 中其余 CDB 字节清零，保留先枚举后发布、OCS/响应状态/完整长度门禁和只读限制。高地址请求显式不支持，不尝试未经验证的大容量读取路径。不添加硬件复位或写命令，也不忽略 sense。

READ10 CDB 字节布局、边界/跨界/零块数/65536 截断拒绝及原有响应/写命令拒绝测试真实通过（sanitizer）；AArch64 编译通过，正在增量链接。v5 是否可读仍需实机证据。

## PMGR 兼容服务

独立设备源码 `drivers/CheeseburgerPlatformPower/`，使用真实 XNU 编译记录生成普通 ARM64 archive，加入 KERNEL_EXTRA_ARCHIVES。保留固定原始 ApplePMGR/ApplePMGRNub、安全组件和 pthread；不是空壳类或硬编码虚表地址替身。

- 固定组件与当前 C++ ABI 审计：ApplePMGR 对象大小/340 槽及原始基类绑定；原始 Nub 工厂、类型和生命周期；运行时元类与大小门禁。
- 延迟原生 C++ 注册，保证原始父类已加载。独立 IORegistryEntry 作描述，真正的设备 IOService 作 Provider，不再转移平台 DT 根关联；创建前后检查根子关系。
- KernelConfigTables 以不同匹配类别注册设备专用 personality，避免覆盖 ArmDTPlatform，也不请求不存在的外部 kext。
- 实际最终 BL 顺序：PE_init_iokit → cheeseburger_ufs_register → cheeseburger_pmgr_startup → mac_policy_initmach。
- AMFI 使用的 AON 兼容 READ 调用原始“不支持”实现，返回 ffffffff；OR/WRITE 使用原始无硬件写语义，记录真实 caller。这些不是 Qualcomm 硬件锁存状态，也不是声称验证通过的安全寄存器。
- 其他尚不支持的电源操作明确拒绝，未实现 PMIC/RPM/SCM、CPU 电源、时钟或完整电源管理后端，没有新增未经核实的电源 MMIO 写。

v2/v3 实机已输出兼容 READ/WRITE 和原始 AMFI PMGRAON 消息，并继续进入 BSD，No PMGR 已越过。AMFI 的开发模式消息来自原始组件的 protected data 不可用路径，没有伪造 protected data 或改变原始安全代码。

## NativeUfs 恢复

设备源码 `drivers/p11/NativeUfs/`；当前构建脚本直接使用该 canonical 输入，避免每次覆盖回 P8。

绑定原始 IOStorageFamily，不链接第二份 IOBlockStorageDevice/存储类；190 槽静态 ABI 核对和真实运行时元类/大小门禁，原始父类加载后才注册自定义类。

复用捕获的 cheeseburger FDT 和固定 OnePlus 内核来源的控制器/时钟范围。要求暖启动链路、时钟可用、控制器和队列空闲；分配独占、对齐、物理连续的 DMA 区，验证地址能力。仅接管临时运行队列，没有磁盘写命令；超时/致命状态隔离，拒绝重试或释放控制器仍可能拥有的内存，不盲目复位。

所有 SCSI 请求经过 ReadPolicy；拒绝 WRITE、FORMAT、UNMAP、MODE SELECT、SECURITY OUT 等操作，块服务报告只读。没有添加 raw 分区写入。

### v3 短响应修复（已实机确认）

REPORT LUNS 分配 256 字节，并不保证返回满 256 字节。依据 [Linux v6.12 UFS 定义](https://github.com/torvalds/linux/blob/v6.12/include/ufs/ufs.h) 及 [完成响应处理](https://github.com/torvalds/linux/blob/v6.12/drivers/ufs/core/ufshcd.c#L4998)，只对 REPORT LUNS 接受有界 residual；列表必须完整落在实际接收长度内。普通块读取仍要求足额，OCS、状态、任务标识、LUN、sense、EHS 和溢出检查不绕过。

真实 sanitizer 测试覆盖有效短列表、截断/异常/溢出、CHECK CONDITION、短块读取拒绝和全部 outbound opcode 拒绝。AArch64 编译通过。

v3 照片证明 received=56、residual=200、6 个条目；发布 LUN0、LUN3 只读服务。此前 REPORT LUNS 失败已突破。

### v4 枚举并发修复（仍有 READ16 设备错误）

v3 在枚举尚未完成时发布首个 LUN，存储层可能同时 read()，而枚举未持有 read() 的锁，造成共享单槽 DMA/响应缓冲并发风险。v4 先完成全部容量查询并保存有效结果，再统一发布子服务；发布阶段不再发枚举命令，后续所有读取保持 IOLock 串行。

补全响应 LUN/tag/command-set/EHS 元数据，并限制成功读取日志至前四次；只记录范围、长度和状态，不输出磁盘内容或唯一身份。保留所有错误和拒绝路径，不因 ASC=24/00 就盲目添加命令回退。

## 构建、证据与边界

唯一 XNU 工作树、固定 UEFI 环境、XNU -j2；先停止 VM 编译 XNU，完成后才启动 VM 构建 UEFI。官方 NativePrelink 保留原始安全组件代码及依赖，选定 ABI 和绑定逐项核验；原生 XNU 与 donor 并非完全相同 patch 版本，不声称整个 iOS 用户空间 ABI 已验证。

每版保留独立 prelink-vN、milestones、命令/退出码日志、manifest 和哈希。通过内核/ADT、payload、DarwinBoot 解析、UEFI 编译/FV 与 PE、boot image 回读后，才允许新候选测试。控制器验证设备、关键备份和镜像 SHA256，仅 fastboot boot，一次 run 一个镜像；失联不重试。

v3 实机照片：`out/p11/evidence/v3-photos/`。v4 SHA/运行编号和观察结果见 P11_BOOT_RESULT.md、P11_HANDOFF.md。完整电子内核日志回收尚未实现，照片不冒充电子日志。

尚无可靠 READ/GPT/IOMedia/APFS 或根卷成功证据；当前默认等待 Apple_HFS 并不对应经过核验的 iOS 根卷。没有写入手机分区，没有虚构根 UUID、刷写镜像或绕过真实性校验。暂不开发非启动必需外设。
