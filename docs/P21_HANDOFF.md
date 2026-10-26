# 最新交接：P21-UP1 已出现原始界面，下一步触摸

2026-10-26，用户确认“已开机，进入了系统出现了时间”。UP1镜像SHA256 `7cf2fef9dea9103e860db4358697b4d2e67d550928b5deb3afac67c5d4309323`。这是物理界面定性进展；未回收本轮日志、未收到照片，不填写精确帧数或声称完整Home交互。

保留UP1作为当前实验基线：Pagani02c7ccd的identityservicesd/locationd开发态作业策略与原watchdog规则；AKS、R17 RAM Data、原始安全/图形组件、单CPU、CPU显示和32MiB日志窗口保持。不重启当前运行设备，不恢复旧R18，不为了触摸重做图形/根卷。

用户本轮优先要求提交推送并将README改为官方风格项目入口；随后优先触摸。触摸必须以一加5实际控制器、总线与电源/IRQ证据为基础；现有NativeHidInput支持digitizer上层，但无合格板级触摸帧输入。先检查现有FDT/Android/UEFI材料，避免借用sheng或小米6的控制器地址。

UP1返回Recovery时读取现有日志，补齐策略采用及栈/显示对比；不为此主动中断当前实验。最新实验与证据边界见P21_UPSTREAM_BOOT_COMPARISON.md。
