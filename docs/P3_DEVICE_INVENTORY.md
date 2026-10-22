# P3 实际设备清单

采集来源：Android 只读 ADB、实际 Fastboot、原有 Recovery root ADB。原始输出/命令/退出码保存在 out/p3/private。硬件标识不放公开日志。

| 项目 | 观测结果 |
| --- | --- |
| 设备 | Android OnePlus5 / ONEPLUS A5000；Recovery cheeseburger |
| SoC | MSM8998，运行时 DT model 为 MSM 8998 v2.1 MTP |
| CPU | 8 核，implementer 0x51，part 0x801/0x800 |
| Android | 16 / API 36，kernel 4.4.302-perf+ |
| 内存 | Android MemTotal 5850116 kB；不是完整 DRAM 物理容量证明 |
| 屏幕 | wm 1080×1920，density 420；物理 framebuffer 参数待运行时探针 |
| Bootloader | oem device-info unlocked=true / critical unlocked=false，getvar unlocked=yes |
| Android 锁属性 | flash.locked=1、verifiedbootstate=green，与实际 Fastboot 不一致 |
| 分区布局 | ro.build.ab_update=false，slot_suffix 为空，实际 GPT 名称无 A/B；按 A-only 处理 |
| Fastboot slot 查询 | current-slot、has-slot:boot 远端 FAILED，宿主工具却退出 0；不当成成功 |
| boot/recovery | 均 67108864 字节，已完整备份 |
| UFS | bootdevice 1da4000.ufshc，6 LUN：sda–sdf；逻辑扇区 4096 字节 |
| Android 权限 | shell uid2000，无 su，原始块设备和 iomem 等读取受限 |
| Recovery 权限 | uid0，ro.bootmode=recovery，具备 blockdev/sha256sum |

真实 FDT 434048 字节，位于 out/p3/private/recovery-device.dtb。原始 iomem、命令行、分区表、挂载表见 recovery-inventory.log / recovery-memory-map.log；inventory 命令中途 USB 断开退出255，后续独立读取与所有备份均重新验证，不把截断日志标作完整。

运行时 FDT 与 P2 DTB 的 model/compatible/qcom,msm-id/qcom,board-id 全部相同（msm-id=[292,131073]，board-id=[8,0,16859,23]）。二进制哈希不同符合 runtime fixup/不同树的可能性，不声称两份 DTB 逐字一致。节点摘要保留在 private/*-dt-nodes.json，尚未转写成 Apple ADT。

实际映射中为 abl/ablbak、xbl/xblbak，没有套用 aboot 或 dtbo 名称。完整实际映射与哈希清单见 P3_BACKUP_MANIFEST.md。后续验证进展如下。

后续 root Recovery 读取已补充：触控 input 名称 synaptics,s3320，指纹 fpc1020，另有 qpnp_pon/gpio-keys/tri-state-key。Recovery framebuffer virtual_size=1080,3840，32bpp，stride=4352；这是 Recovery 的缓冲布局，不等于 UEFI GOP stride。UFS 六 LUN 的 model=KLUCG4J1ED-B0C1，vendor=SAMSUNG，rev=0200。Recovery build display id 为 fox_OP5x5T-eng，pstore 目录为空，没有获得历史 UEFI 日志。

P3 运行时探针已确认 EL1、GOP 1080×1920、stride1080、framebuffer base0x9D400000/size0x7E9000，DarwinBoot Loader 1句柄、SimpleFS3、BlockIO79、SerialIO0。来源是用户屏幕OCR，见 runtime-protocols.json；句柄存在不等于所有接口功能已测试。MPIDR、GIC/计时器实测和完整EFI内存图仍待验证。

FDT /memory/reg 使用实际2个地址cell和2个大小cell解析出两个区域，见 out/p3/runtime-memory-regions.json。该数据仅是Linux运行时树的观测，不直接视为Apple ADT或可任意分配的RAM。

ADB 37.0.1 在本机出现 libunwind 崩溃；35.0.2 原生 ARM64 已实际可用。固定官方安装包及摘要在 .work/downloads 和 out/p3/platform-tools-35.json，隔离服务端为 localhost:5038，项目内密钥。不需要 Rosetta。
