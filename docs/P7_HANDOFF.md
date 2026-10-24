# P7 已收尾：下一轮转 kernel bootstrap / IOKit / UFS

项目 `/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/`。最终系统目标为iPhone iOS。用户要求本轮结束出报告，不再增加诊断轮次；没有计划或启动第四轮测试。

## 已有实机证据，不要重复MMU定位

用户依次确认v1新增4条、v2新增7条、v3新增8条。对应最终指令语义证明：

1. MMU开启后执行、KVA代码、真实栈读写、arm_init首段C。
2. 重定位、BootArgs/CPU数据、PE(FALSE)、CPU topology、cpu_bootstrap、VM调用前。
3. VM页表构造、TTBR1切换、arm_vm_init返回并通过有效IO VA继续显示、PE(TRUE)返回。

证据级别为用户观察，没有电子PC trace或P7原始照片。见out/p7/stages-v1/2/3.json及evidence/p7/user-confirmation-v1/2/3.txt。不能把以上结果扩展为IOKit、GIC中断或根设备成功。

手机最后经detect-final-01只读确认ANDROID_ADB在线；用户已恢复Android。Linux VM停止；所有构建及三轮观察均结束，没有后台重试。v3控制器RETURNED_ANDROID_ADB与人工恢复相符，不能冒称自动恢复内核挂死。

## 从这个成功版本继续

当前out/p7为最终v3：

- kernel/kernel.macho，19277576字节，SHA `9ab8952643ec08de5464c0887ab1b2b616976e88c0e22bb10148167915195aaf`
- adt/kernel.macho.adt，13704字节，SHA `2e3d21c3335dd10023917959736acea54a7b018e5dab7c5faa3b4fe8ffa8f1d4`
- uefi/boot-p7-xnu.img，SHA `5171c19816486446326b1d5bfb0c50bc9d11963b74ae4719111318932e7da455`
- 实测run `20261022T094235Z-19c8a3c4`。

完整成功版本分别保存在out/p7/milestones/mmu-c-v1、early-pe-cpu-v2、vm-pe-v3。P6只读基线在out/p7/baseline。当前主目录会随下一轮构建变化，回归时必须用milestone和对应manifest，不按文件大小认版本。

## 修复与尚未确定的根因

BCM2837旧MMIO已通过真实P6 start.o及最终Mach-O排除。XNU_NATIVE_ARCH_TIMER未定义，但BCM本身也未定义，旧分支不编译；不能用PPI19驱动补丁替代此证明。

没有证明P6恰好死在SCTLR；本轮没有修改TCR/SCTLR，而是完善观测通道后取得MMU/C/VM执行证据。主机复现了真实ADT缺少dram-base/size的panic，已按P3 FDT的连续观察区间补齐，原512MiB加载器拥有窗口保持不变。该数据是保守观察边界，不声称完整物理容量或EFI空闲内存；不能把主机panic当作手机panic。

接下来的未知范围在PE(TRUE)返回之后至kernel bootstrap / IOKit / BSD路径。当前没有原始panic文本可定位第一条实机故障指令。

## 下一轮最小目标

优先验证machine_startup、kernel_bootstrap、vm_mem_bootstrap、PE_init_iokit的进入/返回，读取现有ArmDTPlatform/ArmCpu观测计数，建立可靠panic/日志证据，再定位UFS块设备。不要重做P1–P6审计、重复测试旧失败镜像或立即准备刷写。

**后续诊断记录必须先迁出__BOOTDATA**：当前p6_diag_record仍在那里，arm_vm_prot_finalize会释放此段；继续到kernel bootstrap/IOKit时应改用可长期存活的内核自有DATA区并验证权限，不能继续依赖已释放内存。不要为保留探针跳过正常释放或关闭保护。

VM后日志必须使用ml_io_map建立的有效VA；源码arm_vm_init会正常切换TTBR1并使TTBR0无效，原物理FB别名不可继续使用。P7-v3已实现返回后的映射，别退回旧写法。

尚未实机证明IOKit匹配、ArmCpu计时器/GIC中断、Crypto POST、UFS根设备、APFS、BSD或launchd。iPhone IPSW未选定/下载，不混用sheng的iPad身份、Trust Cache或prelink输入。

## 复用工程

唯一XNU工作树.work/p4-unixnu/.cache/xnu-work；固定Xcode/LLVM和P4库，-j2，内核编译前停止VM。patches/p7依次为mmu-diag-v1、c-checkpoints-v2、vm-checkpoints-v3；ADT修复脚本p7-fix-dram-adt.py。准确已链接源码在evidence/p7/*-linked-v3.*。

scripts/p7run.py保存每个全新label的完整命令/输出/退出码。tools/p7/device_loop.py复用冻结P5控制器，保留身份、备份、DTB、SHA、一次镜像一次尝试及失联停止门禁。没有改写状态机。

scripts/p7-test-probes.py、p7-test-bootstrap.py、p7-test-switch.py、p7-host-early-c.py执行主机真实指令。依赖已有Python3.12和.work/p6/pythondeps/Unicorn2.1.4，需授权JIT；SCR/HCR设置仅用于主机夹具，禁止照搬为手机EL2/EL3修改。

UEFI guest独立P7 overlay/build：/home/dinnerabc.guest/unixnu-p7/source；Lima磁盘在项目内。kernel或ADT变化后需重新生成payload/Expected.h，再同步源码并真实编译UEFI、导出、回读、打包和验证。只临时fastboot boot，不flash/erase/解锁或修改分区。
