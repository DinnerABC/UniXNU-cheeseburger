# P5 后续阻塞与门禁

已解除：KernelConfigTables 平台匹配缺项；最小 ADT 缺 CHRP NVRAM proxy；宿主 DarwinBoot 对候选内核/ADT 的解析验证。

已解除的实机门禁：用户OCR确认内核/ADT读取SHA256 PASS、Mach-O解析及Stage3；用户颜色观察确认EBS后的绿条和实际XNU入口品红细条。对应源代码、反汇编、镜像SHA及run_id已绑定保存。

当前最关键阻塞：真实XNU入口之后停机，既无后续初始化日志，也无可用ADB/Fastboot；尚不能定位是早期系统寄存器访问、bootstrap页表、MMU切换、C初始化还是panic。下一轮应只增加这一小段路径的诊断与可靠异常记录，不能凭屏幕不动就断定某一具体原因。设备需先经一次人工硬件恢复。没有可靠USB gadget/SerialIO日志通道，不能把 USB Host/MassStorage 当成 USB Device 日志通道。

如果入口之后停机，先保存屏幕和真实设备状态，再以 PC/ESR/FAR/ELR 或早期日志定位；缺这些输入时不按猜测修改页表、内存地址或中断资源。遇到无法自动恢复状态停止同镜像重试。

目前尚无 CPU/VM/platform 完成、POST 自检完成、根卷挂载或 launchd 证据。没有资格进入实机刷写阶段。进一步需要核验：UFS驱动和真实资源、匹配的IPSW/kernelcache与APFS根卷、Trust Cache、Root UUID、必要用户空间驱动和恢复路径。本轮不准备虚假身份、借用sheng哈希或绕过签名校验。未进入稳定早期初始化前不下载大体积IPSW或改userdata布局。
