# P4 MSM8998 最小 ADT

输入为 P3 从实际 Recovery 读取的 FDT：`out/p3/private/recovery-device.dtb`，SHA256 `743b9d0fee2a7aedb9db32dc57ddfb8197378b78ea003d8c653f492b7e5a2448`。寄存器来自用户实机屏幕 OCR，保存于 `config/p4/observed-registers.json`，没有冒称电子日志或原始照片。

| 参数 | 实际证据 |
|---|---|
| EL | 1 |
| MPIDR / MIDR | 0x80000000 / 0x51AF8014 |
| CNTFRQ | 19200000 Hz，与 FDT 一致 |
| CPU affinity | 0,1,2,3,0x100,0x101,0x102,0x103 |
| GICv3 | distributor 0x17a00000/0x10000；redistributor 0x17b00000/0x100000；stride 0x20000 |
| PSCI | smc |
| 虚拟 timer | FDT PPI index3 → INTID19；不是上游 ArmCpu 的固定27 |
| RAM | 0x40000000/0xc0000000；0x100000000/0xbd8c0000；不代表全部可分配 |
| framebuffer | GOP 1080×1920，stride1080，格式1，0x9D400000/0x7E9000（P3 探针） |

`scripts/p4-make-adt.py --boot-mpidr 0x80000000` 生成 `out/cheeseburger/adt/kernel.macho.adt`。`scripts/p4-build-adt.py` 从观测记录传参并运行真实 DarwinBoot AdtNode C 函数的宿主验证，不能用宿主验证代替设备内核读取成功。

已生成树大小5208字节，详细来源和未知项见 `out/p4/adt-generation.json`。`validate-adt-02` 退出0。第一次验证失败来自宿主 shim 对 ScanMem8 参数顺序映射错误，已修正并保留失败日志。

`darwinboot-memory-size=512MiB` 是分配请求策略，实际物理窗口必须由 DarwinBoot 从 EFI ConventionalMemory 成功分配；不指定猜测的物理基址。random-seed 每次生成取主机随机数，属于启动输入而非硬件观测，因此再次生成 ADT 的字节哈希会变化。

已提供 cpus、memory、reserved-memory、chosen、defaults、options、product、arm-io/intc/timer。静态保留区域来自原 FDT，动态无 reg 节点不猜补地址。没有 sheng 硬件地址、Apple chip-id、Trust Cache 哈希或 Root UUID。

## 尚待运行验证

- ArmCpu 定时器补丁已编译成库，但 GIC 实际控制、中断派发、CPU 注册仍未在本机 XNU 执行。
- ArmDTPlatform 要求 chosen 下 CHRP NVRAM proxy，目前最小树尚未提供。可设计明确标注的空易失 NVRAM 仿真快照，但不可冒称手机真实 NVRAM。
- 仅计划先启动一个核，尚不启用 sheng-smp 或软件 SPI IPI 等上游实验参数。
- EFI 完整内存图、早期异常寄存器和 XNU panic 尚未回传；Framebuffer 可显示不等于已经建立 XNU 控制台。
