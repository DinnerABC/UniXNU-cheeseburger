# P5 入口证据分级

| Stage | 必须观察到的证据 | 当前证据状态 |
|---|---|---|
| 0 | 真实UEFI运行 | 用户实机DarwinBoot页面确认 |
| 1 | P5Probe或DarwinBoot从EFI文件系统实际读取，大小及SHA256 PASS | 实机OCR确认内核19261032字节、ADT13616字节，均SHA256 PASS |
| 2 | 实机 DarwinBoot Mach-O parsed 与入口地址 | 实机OCR确认；由后续Stage3和入口标记补充装载证据 |
| 3 | 实机 Stage3 ADT/BootArgs/memory-map ready | 实机OCR确认 |
| 4 | EBS成功后的DarwinEnter绿条 | 用户观察到宽绿条；仅说明加载器交接，独立于XNU入口证据 |
| 5 | 实际XNU `__start` 绘制品红色顶条 | 用户观察到紫色细条，符合真实入口的16行品红标记；入口反汇编与镜像SHA绑定。人工屏幕观察级别 |
| 6 | XNU启动后续代码日志或进一步独立标记 | 未证实 |
| 7 | CPU、VM、platform初始化证据 | 未证实 |
| 8 | 可追溯真实XNU日志/panic | 未证实 |

加载器先从指定卷读取 kernel.macho，验证完整 SHA256，再调用实际 ParseKernel；ADT sidecar 同样验证 SHA256 后实际解析。所有条件满足后才分配并填充真实内存窗口、BootArgs 和 EFI memory map，随后尝试 ExitBootServices 和跳转。任何输入错误都保留固件并返回错误，不用占位数据硬过门禁。

XNU入口条使用 BootArgs 0x28处Video base、0x38处rowbytes、0x48处height、0x50处depth，来自 Darwin23实际ABI及加载器静态断言。仅在32bpp、非零基址、height>=16、有效rowbytes范围内画前16行。Loader 绿条和 XNU 品红条分别在不同映像的实际执行路径，不能混淆。

屏幕颜色只有被真实观察/拍照才构成运行证据。没有看到颜色时不能从 Fastboot OKAY、USB消失、编译成功或自动返回推断 XNU 已进入。

本次用户原文：“紫色细条，很细的黄色条，宽绿条依次，然后”。紫/品红细条与宽绿条分别对应上表Stage5/4；黄色条没有阶段归属。未收到原始照片或电子寄存器跟踪，不扩大证据范围。完整机器可读状态见 `out/p5/stages.json`。

## 异常与恢复边界

本次没有加入未经验证的跨MMU异常向量或内存保留假设。上游早期异常向量仍有保留寄存器并自旋的路径，但尚无可自动提取 PC/ESR/FAR/ELR 的可靠电子通道。P4 易失环形缓冲未证明跨重启保留，不能作为自动日志回收。

BootServices定时返回只在EBS前有效；EBS后内核异常可能需要一次硬件重启。状态机观察超时会保存现场、停止该路径，而不执行未知固件复位、EDL或分区操作。
