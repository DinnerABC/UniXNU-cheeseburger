# P7 MMU 定位：证实与待证实项

## BCM2837 候选已排除

已阅读上游 `architectural-timer-entry.patch` 和 `check-native-arch-timer-checkpoints.py`。后者依赖上游自己的候选内核、QEMU/launchd fixture 和固定哈希，不能直接当作本设备已通过的测试。本工程复用其完整遗留指令字节序列，在**实际 P6 start.o 和最终 kernel.macho** 中检查，计数均为 0；并核对目标真实预处理结果及汇编对象反汇编。

- `ARM64_BOARD_CONFIG_MSM8998=1`；BCM2837 与 ARM64_BOARD_CONFIG_BCM2837 均未定义。
- `XNU_NATIVE_ARCH_TIMER` 未定义；该专门 patch 未应用，但原始 `#if defined(BCM2837)` 分支已因平台选择被排除。
- S05 在源码中位于旧路由块之后；最终 common_start 中没有该旧 MMIO 块。
- P4 ArmCpu PPI19 补丁没有被当成此处的排除依据。

证据 `out/p7/preflight/timer.json`、`macros.txt`、`start-object-disassembly.log`。P6 start.o 哈希 `4c95b81aaf4e2605493efe0e06b7fc4d116f4d9d2450253d1389fe603192d2f0`，只读副本在 `out/p7/baseline/.work/.../start.o`。当前共享缓存对象会随 P7 编译改变，复核 P6 必须用该副本。

因此没有为了“修复”不存在的 BCM2837 指令而改 defconfig 或重复打 patch。**P6 S05 后第一处真实故障原因仍未确定；最后可见 S05 不等于 SCTLR 指令发生故障。**

## 本轮最小诊断变更

没有改变 TCR/SCTLR 的既有配置，没有改 CPU、IOKit 或 UFS。修改限于启动诊断及其必要映射：

1. 以 BootArgs 提供的 framebuffer 基址、rowbytes、height 验证并计算页对齐范围。限制为32bpp、基址4KiB对齐、高度300–4096、rowbytes至多64KiB、总范围至多16MiB、同一L1槽、不得覆盖内核拥有窗口或已有RAM映射。
2. 从已经清零、已有容量检查的16页 bootstrap pool 中分配 L2/L3，仅映射该范围的4KiB页。使用目标实际 `CACHE_ATTRINDX_DISABLE=3`，MAIR byte3=0（Device-nGnRnE），AF、OuterShareable、PXN/UXN；没有将整个MMIO区域映射为普通缓存内存。
3. 开启前检查 TTBR0/1 与两个实际页表根一致，TCR 等于实际编译值，设备属性对应 MAIR byte 为0。保存 TCR/TTBR/MAIR/SCTLR/VBAR/当前PC到内核自有记录区。
4. 冷启动切换窗口保持指向 V=P 可执行低异常向量；MMU 开启后仍可通过同一身份映射执行记录器。在 `arm_init` 首段 C 标记后恢复正常 ExceptionVectorsBase。没有改变 warm CPU 路径。
5. S06 仅在 `msr SCTLR`、ISB、读回 M=1 之后绘制；S07 在通过原 LR 到达的 KVA wrapper；S08 在真实栈存取之后；S09 是 `arm_init` 编译器 prologue 之后的首个 C 内联汇编语句。

原始 P6 BOOTDATA 记录区扩为256字节，额外保存已映射 framebuffer、大小、rowbytes及开启前快照。若异常发生时有可靠映射，红条下三行仍分别为 ESR、ELR、FAR；SPSR/SP/LR等保存在易失记录区。目前没有跨重启自动回收通道。

## 最终 P7 主机证据

内核 `a01604c6a0aa01833f2a5050c0ea07bc77c9fa226bef43a366ee9eba95539e52`。`out/p7/execution-map/` 包含最终指令与常量；TCR `0x22a51a251a`、SCTLR `0x3454593d`、4KiB granule、38位 VA、40位 IPS、2MiB RAM block、16页池。

`bootstrap-03` 执行实际页表构造指令，验证 V=P、KVA 和 framebuffer 的每个页描述符；检查邻接页不映射、设备 AttrIndx、XN 以及错位负例。`switch-host-05` 在 Unicorn2.1.4 的合成 CPU/内存中实际启用 MMU，执行最终内核的 S06/S07/S08、C prologue 与 S09，并恢复正常 VBAR。没有用回调伪造这些检查点，也没有跳过 SCTLR。

此前完整切换夹具在 ISB 前后失败。修正模拟器 SCR_EL3.NS/RW、HCR_EL2.RW 的 AArch64 EL1 环境后通过；这是**主机夹具修正，不是对手机 EL2/EL3 的写入或硬件根因结论**。[Unicorn 官方说明](https://github.com/unicorn-engine/unicorn/blob/master/docs/FAQ.md)提供其系统寄存器环境及 MMU 能力背景。全部失败日志保留。

主机测试使用合成 BootArgs 和内存，不能代替真实 CPU 的 PARange/granule 能力读回、缓存一致性或设备执行证据。P7 UEFI 保留只读 MMFR0/ISAR1 输出，但尚未得到本轮该页面的实机数值。

## 实机当前结论

P7-v1 run_id `20261022T090944Z-3b815c31`。用户明确确认“**四条新增色带都出现了**”，证实 S06/S07/S08/S09：MMU开启后执行、KVA代码、栈读写和 arm_init 首段C语句均已实机运行。这是用户观察级别，尚无照片或电子PC跟踪。证据见 `out/p7/stages-v1.json` 和 `evidence/p7/user-confirmation-v1.txt`。

这证明新增诊断可以观测这些阶段，**不能倒推出 P6 一定在 MMU 开启时失败，也不能宣称修复了尚未定位的 MMU 配置错误**。本轮没有修改TCR/SCTLR。

用户UEFI OCR报告MMFR0=0x1122：若识别值准确，PARange=2（40bit）、TGran4=0（支持4KiB）与当前配置相符。该值是内核前能力读数，不能当成XNU寄存器快照。

## 后续突破与本轮收尾

用户随后确认v2七条C标记、v3八条VM标记全部出现。现在实机已确认：CPU topology解析、cpu_bootstrap、VM页表构造、TTBR1切换、arm_vm_init返回，以及PE_init_platform(FALSE/TRUE)均返回。run与完整二进制见 `P7_REAL_BOOT_REPORT.md`，不再停留于S05/S09。

VM重建后正常使TTBR0失效，旧物理FB别名不可继续输出；v3在VM返回后通过标准ml_io_map获取IO VA，保留正常切换与保护，成功继续显示。这修复的是跨VM的诊断通道，不能将此前标记缺失都解释成VM失败。

主机使用真实ADT在后续arm_init复现明确panic：`Unable to find 'dram-base' entry in the 'chosen' DT node`，同一源码也要求dram-size。记录 `out/p7/host-dram-missing-before.json`。补充的两个属性来自P3已捕获FDT连续区域：base `0x40000000`、观察边界长度 `0x17d8c0000`；实际DarwinBoot解析器通过，主机不再触发该缺项panic。它是保守观察边界，**不是完整物理容量或EFI空闲映射**，内核拥有的512MiB窗口不变。真实手机没有提供该panic文字，不能冒称已实机复现此异常。

最终最后确认位置为PE(TRUE)返回，后续kernel bootstrap、IOKit和根设备仍未知。按用户要求在这里结束本轮，不启动第四轮。下阶段先验证这些路径及日志；继续前必须把诊断记录迁出会被arm_vm_prot_finalize释放的BOOTDATA，而不是为了探针跳过正常释放。
