# P7 最终实机报告

2026-10-22（北京时间）收尾。**实机已越过 MMU 切换，进入 arm_init，并完成早期 VM 初始化及两阶段 PE 调用。** 按用户要求停止增加诊断轮次，下一轮转向 kernel bootstrap、IOKit 和 UFS 根设备。

证据为用户逐轮明确确认新增色带，且与对应镜像哈希、最终指令位置绑定。没有将主机模拟、Fastboot OKAY 或 USB 消失当成执行证据；尚无 P7 原始照片或电子 PC trace。

## 三轮真实测试

| 版本 / run_id | 用户实机确认 | 控制器与恢复 |
|---|---|---|
| v1 / `20261022T090944Z-3b815c31` | “四条新增色带都出现了”：S06 MMU开启后执行、S07 KVA、S08栈读写、S09首段C | 观察超时退出2，停止重试，随后用户恢复Android |
| v2 / `20261022T092933Z-906e4a08` | “七条都出现了”：pre-sign、重定位、BootArgs/CPU数据、早期PE、CPU topology、cpu_bootstrap、arm_vm_init前 | 观察超时退出2，停止重试，随后用户恢复Android |
| v3 / `20261022T094235Z-19c8a3c4` | “八条新增色带都出现了”：VM内部构造、TTBR1切换、VM返回后IO映射显示、PE(TRUE)返回 | 控制器观察到RETURNED_ANDROID_ADB；用户报告已恢复安卓，不能称为自动内核恢复 |

最后一次只读 `detect-final-01` 退出0，确认 Android ADB 在线。VM已停止，没有后台构建或待执行的启动重试；不再启动第四轮。

## 最终版本与保留的里程碑

| 版本 | kernel SHA256 | boot image SHA256 |
|---|---|---|
| v1 | `a01604c6a0aa01833f2a5050c0ea07bc77c9fa226bef43a366ee9eba95539e52` | `00826ff1a0512efc81017873ef56abfbc8341d66dabaf67cfb802ba6fc7ed78f` |
| v2 | `b640185f1e1fc040f9ec067760081faecb2ca135715e89f53f0e9655ae1be542` | `aff38b4380dc758f7b84c941097032de064e518d619d7b44af894fb47e6c0382` |
| v3（最终） | `9ab8952643ec08de5464c0887ab1b2b616976e88c0e22bb10148167915195aaf` | `5171c19816486446326b1d5bfb0c50bc9d11963b74ae4719111318932e7da455` |

当前 `out/p7/kernel/kernel.macho` 为v3，19277576字节；`out/p7/uefi/boot-p7-xnu.img` 为v3。最终 ADT 13704字节，SHA256 `2e3d21c3335dd10023917959736acea54a7b018e5dab7c5faa3b4fe8ffa8f1d4`。

完整版本分别在 `out/p7/milestones/mmu-c-v1/`、`early-pe-cpu-v2/`、`vm-pe-v3/`，含各自内核、ADT、UEFI、镜像、payload、配置和源码。不能把同长度的不同内核当成同一版本。P6 S05基线和原 start.o 已另存只读副本，P1–P6保留。

## 实际开发与验证

- BCM2837遗留MMIO在真实P6汇编对象及最终内核中均不存在，排除该候选；未重复修复。
- 新增严格限定范围的4KiB Device-nGnRnE framebuffer映射、MMU窗口低异常向量、开启前寄存器核对、开启后执行标记。
- 后续诊断只增加C/VM检查点，保留断言、正常TTBR0失效和初始化调用；VM返回后通过标准 `ml_io_map` 获取有效IO VA，未盲写旧物理别名。
- 主机使用真实ADT复现缺少 `/chosen/dram-base` 的panic。按已采集、彼此连续的FDT内存区域补齐dram-base/size，通过解析器和主机执行；没有把该主机panic冒称手机日志。
- 固定工具链、唯一XNU工作树、-j2增量链接；三轮最终内核链接均退出0，约23–26秒。VM在内核编译时停止。
- 对应UEFI真实编译、FV/PE对照、原生镜像回读、RAM payload打包、内核/ADT哈希门禁均通过。最终修正ADT后只重编匹配哈希的UEFI，不重编无源码变化的内核。
- Unicorn执行最终机器指令，验证页表、邻接页不映射、设备属性、MMU/KVA/栈/C路径，限定为主机证据。所有失败及修复日志保留。

完整命令、stdout/stderr、退出码、耗时在 `out/p7/logs/`；阶段证据在 `stages-v1.json`、`stages-v2.json`、`stages-v3.json`；最终产物清单 `artifacts.json` / `artifacts-v3.json`。设备私有原始记录在各run的private目录。

## 最终边界

已确认：MMU、高地址执行、栈、C、VM返回、PE(FALSE/TRUE)返回。**未确认：IOKit启动/匹配、ArmCpu的GIC与计时器中断、Crypto POST通过、UFS根块设备、APFS、BSD/launchd。** PE返回不能替代IOKit平台驱动成功。

没有自动回收真实XNU电子日志，没有证明EBS后可自动恢复。三轮均仅临时启动，未执行flash、erase、解锁、GPT或校准分区修改。
