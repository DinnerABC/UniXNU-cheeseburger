# P6 平台初始化状态

目前最深**已有实机证据**仍是 P5 的 `__start` 人工色条观察。P6-v1 已临时启动，但本轮屏幕证据待补，尚不能确认比 P5 更深。没有真实 panic、ESR/ELR/FAR 数值或 C 初始化日志。

| 项目 | 本地成果 | 实机执行状态 |
|---|---|---|
| ArmCpu / ArmDTPlatform | 复用已编译静态库，P5 matching 修复保留 | 未确认初始化 |
| POST Crypto Bootstrap | 保留真实源码组件及 HMAC sealing | 未确认 POST 运行/通过 |
| ADT | P5 独立 ADT 不变；实际 DarwinBoot 解析器复测通过 | P5 读取/哈希有证据，P6 待屏幕证据 |
| CHRP proxy | 8192 字节易失软件实现 | 不声称真实 Apple NVRAM |
| CPU 拓扑、计时器 | 沿用实采 MPIDR/CPU 拓扑、19.2MHz/PPI19 | 中断与调度未运行验证 |
| VM / Platform Expert | 内核包含相关代码 | 未确认进入 |
| IOKit / UFS / APFS | 未获得根设备枚举或挂载证据 | 未验证 |
| launchd | 无兼容 iOS 根卷输入 | 未进入 |

最终二进制的 C 入口是 `arm_init`。固定源码 `osfmk/arm/arm_init.c` 的后续链包含 `PE_init_platform(FALSE,args)`、CPU topology 解析、`cpu_bootstrap()`、`arm_vm_init()`、`PE_init_platform(TRUE,...)` 和 `machine_startup()`。这些是定位后续检查点的实际调用链，不能当成已经执行。

当前自动化能回收主机命令、USB 状态、Fastboot 结果和阶段分类。P6 异常记录是 RAM 中的易失数据；跨重启保持和电子读取均未证明。EBS 前的 P4 自动返回机制不能恢复 EBS 后的内核挂死。

继续推进前，先取得本轮顶部色条/异常行，再人工恢复 Android。不要因为观察超时就重复同一镜像，不增加无关驱动，也不进入持久化刷写准备。
