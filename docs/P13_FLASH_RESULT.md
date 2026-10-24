# P13 userdata 实验记录

**实际结果：P12 根卷已通过一次 Fastboot 命令写入 userdata。68/68 个分块的发送和写入均为 OKAY，真实退出码 0；没有传输或分区错误。**

用户本轮明确授权清除普通 Android 用户数据并将固定 P12 iPad7,1 / 21H461 镜像用于一次性 userdata 根卷工程实验。最终目标仍为 iPhone iOS。该授权不扩展到 boot、GPT、bootloader、radio、persist、modemst 或任何其他分区。

## P12 限制分类

| 类别 | 既有限制 | P13 处置 |
| --- | --- | --- |
| A 授权 | 仅限离线参考、禁止清除 Android 数据 | 本轮新授权解除；仅一次性工程实验，非正式 iPhone 系统 |
| B 几何 | 手机当前 GPT/容量未复核 | root Recovery 已读取主/备 GPT 并核验 CRC；userdata 实体 sda14/LUN0，首末 LBA、4096-byte 块和容量与 P12 一致；Fastboot 报告 0xB29600000，容量完全相同 |
| C 完整性 | 新根卷/VerifiedRoot/UUID 需重新验证 | 固定 P12 22 项文件重新哈希、C 验证器实际签名、UUID/ADT 均通过；写入任务在发送前再次全量核对根卷 SHA256 |
| D 恢复 | 尚无完整 Android userdata 回滚副本 | 用户随后明确取消 userdata 备份，任务已终止、3.20 GB 未完成副本已删除，不存在有效 userdata 备份。独立核验当前 Android boot/Recovery 与 P3 有效备份一致、system 保留、OrangeFox R11.1_7 提供 format data 和 mke2fs；恢复目标仅为空用户数据的 Android，未演练恢复 |
| E 系统 | 无 Data seed；APFS/根认证与更大 payload 尚无实机证据 | 仅合格根挂载实验，不声称 launchd/Data 已准备完毕。保留 Apple 根认证，无禁用/绕过；更大 boot image 仅允许临时 RAM 启动，禁止写 boot。Fastboot max-download-size=805306368，已覆盖131112960-byte 启动镜像和128MiB块 |

当前记录与命令见 `out/p13/device-geometry.json`、`private/`、`logs/`。缺少任何写入门禁结果时，状态保持未刷写。

不支持的可选变量 version、is-userspace、is-logical、has-slot 没有冒充成功。第一次严格查询遇到 version 不支持而退出，未写入；在同一次有记录的 Recovery→Bootloader 会话继续必需项核验通过。空 version-bootloader 记录为未知。

传输使用官方 Platform Tools 35.0.2 的 `-S 128M`，固定原始 userdata 文件不变。64 位偏移与 RAW/FILL/分块处理核验见 out/p13/fastboot-source/。协议背景：[AOSP Fastboot](https://android.googlesource.com/platform/system/core/+/master/fastboot/README.md)。

## 实际刷写结果

- run_id：`20261023T042351Z-b61dc3ac`。
- 原始镜像：`out/p12/reference/cheeseburger-p12-reference/userdata/userdata-root.img`，9,382,658,048 bytes。
- 发送前再次完整 SHA256：`fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093`，与 P12 manifest 完全相同，校验过程中文件未改变。
- 官方 Platform Tools 35.0.2，固定设备标识，`-S 128M flash userdata`；只有一次顶层 flash 命令，68次分块协议写入，不是68次重刷。
- Fastboot 总耗时499.611秒，68次发送、68次写入成功；完整输出 `out/p13/flash-stdout-stderr.log`，命令及返回码 `out/p13/flash-result.json`。
- 未执行 erase、-w、flashall、update、锁定、EDL，未刷 boot、GPT 或其他分区。
- 刷后在原 Recovery 对 sda14 前 9,382,658,048 bytes 做设备端完整 SHA256，结果与固定 P12 根卷镜像一致，见 `out/p13/readback.json`。这是已部署镜像前缀的完整校验，不是整个47.94GB分区校验，也不等于 XNU 的 VerifiedRoot 已执行。
- userdata 原 ext4 根结构被替换，原 Android 用户数据不再可用。未承诺对镜像范围之外的残余数据进行安全抹除。
