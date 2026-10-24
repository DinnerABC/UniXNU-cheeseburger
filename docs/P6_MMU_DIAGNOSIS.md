# P6 早期汇编与 MMU 定位

以下地址来自 P6-v1 最终链接内核，不是源码推算，也不是实机 PC 采样。完整反汇编与符号表在 `out/p6/kernel/`，入口裁剪、精确编译命令与常量在 `out/p6/execution-map/`。

| 位置 | 链接虚拟地址 |
|---|---|
| `__start` | `0xfffffff0073fc488` |
| `LowExceptionVectorBase` | `0xfffffff0073fd000` |
| `start_first_cpu` | `0xfffffff0073fe05c` |
| S01 插桩开始 | `0xfffffff0073fe0c0` |
| S03 插桩开始 | `0xfffffff0073fe330` |
| `common_start` | `0xfffffff0073fe3ac` |
| S05 插桩开始 | `0xfffffff0073fe45c` |
| `set_mmu_ttb` | `0xfffffff0074026b4` |
| `arm_init` | `0xfffffff0079ddac4` |
| `bootstrap_pagetables` | `0xfffffff007179000` |
| `p6_diag_record` | `0xfffffff007bf4000` |

入口保留 P5 品红 16 行标记；真正的冷启动路径随后建立早期 VBAR，解锁调试寄存器，读取 BootArgs，设置 SP_EL1 异常栈和 SP_EL0 中断栈的 KVA（此时尚不能据此认定栈已可访问），准备返回到 `arm_init` 的 LR，清空并构造 V=P/KVA 表。`common_start` 写入 TCR、TTBR0、TTBR1、MAIR，设置正常异常向量，再写 SCTLR、ISB，最后以 KVA BootArgs 返回 `arm_init`。

## 实际启用的常量

- `ARM_PGSHIFT=12`：4 KiB granule；L1/L2 shift 分别为 30 / 21，L2 block 为 2 MiB。
- `TCR_EL1_BOOT=0x22a51a251a`，与最终 `_sysreg_restore` 初始化值相同。TG0=0、TG1=2，T0SZ=T1SZ=26；L1 只有地址空间有效的 256 个索引，不能按 39 位空间直接使用 511 掩码。
- `SCTLR_EL1_DEFAULT=0x3454593d`。这是编译目标值；尚未收到实机切换后的读回值。
- `BOOTSTRAP_TABLE_BYTES=0x10000`，16 页池。既有上游 `native-4k-bootstrap-capacity` 已存在，保留分配前边界检查，没有重复叠加补丁。

DarwinBoot 按 `ALIGN_VALUE(conventional_start, 2MiB) + (Kernel.Base & (2MiB-1))` 分配已拥有内存窗口，保持物理与虚拟基址的大页偏移一致。主机测试已执行实际页表构造并验证此条件；显式错位负例会得到错误映射。P5 OCR 不能代替本轮精确装载地址记录。未发现需要猜测或硬编码物理装载地址的依据。

## 标记及异常覆盖边界

| 标记 | 像素行 | 含义 |
|---|---:|---|
| S00 品红 | 0–15 | `__start` 已执行原有绘制路径 |
| S01 黄 | 24–31 | 已读取 BootArgs；对齐、版本/修订号、非零内存大小检查通过 |
| S03 青 | 48–55 | 页表构造循环完成 |
| S05 白 | 72–79 | 即将写 SCTLR；**不能据此声称 MMU 已启用** |
| FAULT 红 | 96–103 | 进入新增早期 fault 路径，且当时 MMU 关闭、视频输入有效 |
| 三行数字 | 128、176、224 起 | 依次为 ESR、ELR、FAR，每行 16 个十六进制字符 |

只增加三个正常检查点。宏不使用栈、不调用函数，仅破坏 x9–x12；实际指令单测检查了其他寄存器保持。所有物理 framebuffer 绘制先检查 SCTLR.M，MMU 开启后跳过。S01 是有限输入检查，不是完整物理内存权限证明。

早期 16 个低异常向量跳转到无栈记录器，保存 128 字节：magic、stage、ESR、FAR、ELR、SPSR、SP0、SP1、LR、SCTLR、TCR、TTBR0、TTBR1、MAIR、BootArgs、前一 stage。SP 使用 SPSel 切换并读取，不解引用尚未建立的栈。记录在内核自己的 BOOTDATA，不依赖未经确认的保留内存。

**覆盖有明确限制**：上游 `common_start` 在 MMU 开启前会将 VBAR 改为正常异常向量 KVA。因此新增低向量主要覆盖此前阶段，不保证 MMU 切换窗口或后续正常异常处理仍进入该记录器。即使记录器发现 MMU 已开启，也只写记录并停止，不尝试访问未映射 framebuffer。页表本身无效时异常向量也可能无法执行；没有红条不能排除异常。

S06/S07/S08/S09/S10 尚未建立可靠实机可见标记。下一次源码修改应依据本轮最后可见条带，缩小范围；若到 S05，应先解决切换窗口的向量/诊断内存映射，再增加 S06 和 C 入口证据。不要一次插入大量会改变启动条件的输出代码。

Unicorn 测试只验证合成内存中的真实指令及映射结果。`probe-emulation-05` 的完整 MMU 夹具失败，之后的 SCTLR 输入注入仅验证禁止物理 FB 的分支；不得解释成 MMU 成功。

## 最新实机证据（P6-v1）

`evidence/p6/boot-v1-s05.jpg` 依次显示品红、黄、青、白诊断条及剩余加载器绿底，确认 S00/S01/S03/S05。照片中内核长度19277552、ADT长度13616及SHA256 PASS与该轮产物一致。没有红色fault条或ESR/ELR/FAR数字；S06及后续仍未证明。

因此当前定位范围是 S05 之后的 SCTLR/ISB、取指/地址切换、NEON/线程寄存器准备、返回C入口及尚无输出的后续代码。不能仅凭最后可见条断言故障恰好发生在SCTLR指令，也不能断言MMU已成功开启。
