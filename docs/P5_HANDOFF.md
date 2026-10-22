# P5 续接：首次真实 XNU 入口已获得屏幕标记证据

项目所有历史成果保留。未刷写任何分区。

- P5内核：out/p5/kernel/kernel.macho，19261032字节，SHA256 a849889308cb67fafe26f1002be4d5957c6b2dd5dc1f2810eb80611a9b3f53b7；入口0xfffffff0073fc488；未解析符号0。
- P5 ADT：out/p5/adt/kernel.macho.adt，13616字节，SHA256 cec5013d014ea9a5c659a388691ab6446e889a43a4e0111e24a5f0e0075f0369。平台匹配和空易失CHRP proxy均已修复。
- P5镜像：out/p5/uefi/boot-p5-xnu.img，25411584字节，SHA256 06af9d6c08e8e7308d81a6a07095553aa6692e816919faa5ac24090e7c9f51b7。
- 首次临时启动 run_id=20261022T011901Z-228b8fa4。Fastboot接受镜像；240秒观察后无ADB/Fastboot，状态机退出2并停止。未盲目重试。
- 用户实机OCR确认 kernel/ADT读取SHA256 PASS，Mach-O解析，Stage3准备完成。原文 out/p5/screen-ocr-stage3.txt，错误字符不猜填配置。
- 用户实际观察“紫色细条，很细的黄色条，宽绿条依次，然后”。紫红16行是唯一在真实XNU __start生成的标记；宽绿条来自EBS后的DarwinEnter。结合实际入口反汇编，**确认执行XNU入口（人工屏幕观察级别，不是电子PC跟踪）**。黄色没有代码阶段对应，不解释为额外进度。
- 已确认Stage0–5；Stage6后续启动路径、Stage7 CPU/VM/platform、Stage8日志仍未确认。尚无POST运行自检、根卷或launchd证据。

当前手机停在上述页面且无通信，保留现场。下一轮实机测试前需要用户一次硬件恢复；不要再测试相同已失联镜像。可先进一步审查bootstrap/arm_init路径并设计更细的诊断，不能按猜测改硬件参数。

P5 UEFI在现有VM项目磁盘中使用overlayfs：/home/dinnerabc.guest/unixnu-p5/source，下层P3/P4只读，upper/work/build独立。脚本 scripts/p5-guest-overlay.sh 可重新挂载。不要修改原P3目录。XNU唯一工作树保持.work/p4-unixnu/.cache/xnu-work，重编XNU时先停止VM，-j2。当前已停止设备重试；构建日志均在out/p5/logs。

交付：P5_BOOT_REPORT、P5_XNU_ENTRY、P5_NEXT_BLOCKERS；结构化结果out/p5/stages.json。后续重点是入口之后的CPU/MMU/early C诊断与可靠异常/屏幕日志，而不是重新验证已过的整个P3/P4。
