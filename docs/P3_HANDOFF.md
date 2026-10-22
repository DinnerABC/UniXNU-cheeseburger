# P3 续接检查点

更新时间：2026-10-22。P1/P2 未删除；全部新输出在 out/p3、backups/p3、.work 下。

- Platform Tools 37.0.1 的 ADB 在本机 USB 路径发生 libunwind 崩溃；固定 35.0.2 后已真实连接 OnePlus 5。
- 使用 scripts/p3-env.sh / scripts/p3run.py；隔离服务端 localhost:5038，项目内密钥。USB 操作需要沙箱外权限。
- 私有设备标识仅在 out/p3/private，备份在 backups/p3，均不提交 Git。
- Android 16，OnePlus5 / ONEPLUS A5000，MSM8998，8 核，MemTotal 5850116 kB。Android shell 非 root，无 su，原始分区读取被拒绝。
- 实际 Fastboot oem device-info：Device unlocked true，critical unlocked false；getvar unlocked yes。Android flash.locked=1 与其不一致，不能据 Android 属性认定未解锁。
- boot、recovery 各 0x4000000 字节。current-slot、has-slot:boot 的远端响应 FAILED，即使宿主 fastboot 退出 0 也不能标为支持。
- fastboot reboot recovery 返回 OKAY 却进入 Android；adb reboot recovery 已成功进入现有 root Recovery。
- **58个实际分区镜像及6个LUN双GPT已验证备份，见P3_BACKUP_MANIFEST.md。原版已通过 fastboot boot 真正进入 EFI Shell，用户已确认屏幕并成功返回 Android。集成版已临时启动两次，照片确认 UniXNU 0.1.0 图形菜单执行。无 boot entry，倒计时默认返回 firmware，之后 Shell 符合预期。**
- P2 两版镜像哈希、gzip/FV/DTB 回读验证结果见 out/p3/P2-image-reverification.json。真实设备 DTB 与镜像 model/compatible/SoC/board ID 已逐项匹配；见 dtb-comparison.json。
- scripts/p3-backup.py 仅在 root ADB 下读取实际 by-name 映射，并要求退出码、预期大小、设备前后 SHA256 和本地 SHA256 一致。本轮已用该脚本实际完成58个分区的校验备份；p3-capture-gpt.py 已备份六个 LUN 双 GPT。
- 独立 ext4 树 /home/dinnerabc.guest/unixnu-p3/diagnostic 的诊断版已完整编译、打包、回读校验并实机临时启动成功。P2 树不修改；完整日志 out/p3/logs/build-probe.log，构建脚本 scripts/p3-buildrun.py + scripts/p2-linux-build.sh。
- 用户屏幕OCR确认：EL1；GOP2、TextInput2、SimpleFS3、BlockIO79、FV2两句柄；DarwinBoot Loader安装成功1句柄；SerialIO不存在。GOP1080×1920/stride1080/FB0x9D400000/size0x7E9000。详见 out/p3/runtime-protocols.json。按键功能、完整EFI内存图和内核执行尚未证明。
- 当前手机已恢复 Android；最终ADB为OnePlus5、normal、sys.boot_completed=1、shell uid2000。VM已正常停止，无在途编译/备份。所有58个分区、12个GPT文件、三份boot镜像最终重算校验PASS：out/p3/final-integrity.json。
- 下一步最小目标：独立XNU通用ARM64/4KiB编译基础、MSM8998配置与合格ADT，然后受控测试DarwinBoot加载。完整Xcode/iphoneos SDK仍缺；上游默认MACHINE_CONFIGS=BCM2837不能当成MSM8998。禁止自动解锁、刷写或数据清除。

## 精确续接命令

从项目目录执行；设备操作用 scripts/p3-env.sh，Platform Tools35.0.2缓存和SHA在out/p3/platform-tools-35.json。不要切回会崩溃的37.0.1。

```sh
source scripts/p3-env.sh
adb devices -l
python3 scripts/p3-final-verify.py
bash scripts/p2-vm.sh start --tty=false p2
python3 scripts/p3-buildrun.py build-probe-next -- bash scripts/p2-vm.sh shell --workdir /project/UniXNU-cheeseburger p2 bash scripts/p2-linux-build.sh /home/dinnerabc.guest/unixnu-p3/diagnostic /project/UniXNU-cheeseburger/out/p3/diagnostic-next
```

构建必须沿用项目内VM ext4；不要在macOS大小写不敏感目录直接编译。诊断树已应用P3补丁，不要再运行prepare脚本。新编译用新输出目录/日志label，不覆盖成功证据。导出/验证入口为p2-export.py、p2-verify-images.py，实际完整调用记录在out/p3/logs/export-probe.json、verify-probe.json。

## 日志自动化方向

用户提出后续自动取回日志。P3Probe已是实际运行的注入诊断驱动，下一步可加入环形缓冲与导出。优先外接FAT或专用USB接口；不能盲写手机现有SimpleFS句柄，不能假设ADB能取回退出UEFI后的RAM，也不能假设SerialIO存在。跨重启读取须先验证保留RAM/ramoops；本轮尚未实现自动导出。
