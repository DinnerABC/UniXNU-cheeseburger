# P3 原版 UEFI 实机测试

结果：**已经真实进入 UEFI Shell**。来源为设备命令输出加用户屏幕观察，不仅是 Fastboot OKAY。

- 输入：out/p2/baseline/boot-cheeseburger.img，6070272 字节。
- SHA256：c52e0ab0100a8bfb46c6d66c888455147d28634b6b88ab1d93599d7b87ca6433。
- 预检：Android header v1 / page2048；gzip 解压精确还原 6MiB FD；ARM64 header；附加 DTB 与构建输入相同；ramdisk 是上游原有单个换行；设备真实 SoC/board ID 匹配。见 out/p3/temporary-boot-preflight.json。
- 已校验关键分区备份及六 LUN 双 GPT 后，在已解锁 Bootloader 执行 fastboot boot out/p2/baseline/boot-cheeseburger.img。
- 实际输出：Sending 5928KB OKAY，Booting OKAY，耗时10.249秒，退出0。原始日志 baseline-temporary-boot.log/json。
- 用户观察原文：“进入了boot menu，然后加载了命令，现在进入并停在了shell命令行”。
- 用户随后长按/菜单重启回 Android，ADB 再次报告 OnePlus5 / normal，恢复路径已实际验证。

尚未证明：按键菜单移动、GOP 模式、Block I/O、Simple File System、EFI 内存图或 DarwinBoot。没有键盘输入/协议探针日志，不能因 Shell 可见推断全部驱动正常。没有执行 fastboot flash。

注意 fastboot reboot recovery 虽返回 OKAY，但实机进入 Android；实际成功的进入方式是 Android 下 adb reboot recovery。
