# P21 iPadOS 构建与临时启动指南

本指南记录本机已使用的 P21 构建链。项目根目录为 `/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger`，分支 `ipados-17.7.11`。先读根目录 `AGENTS.md`、`HANDOFF.md` 和 `docs/P21_OFFICIAL_WORKFLOW_AUDIT.md`；保留其他 agent 的改动及历史镜像。构建与设备操作遵循用户当次授权。

## 已知可启动基线

- 用户确认进入系统：`out/p21/phone-ui-d2/uefi/boot-p21-up1.img`，SHA-256 `1999371cccd22d6b5b36bea283cf596c0f04de63d91c527806da59cf88d25da3`。画面仍为平板布局，触摸未工作。
- 本轮日志：`out/p21/persistent-logs/20261029T172431Z/`，boot ID `0x5a0ea281`。Recovery 使用 `scripts/logging/read-cheeseburger-log.py` 回收，连接系统实际使用的唯一 ADB 服务。
- 官方框架固定源码 `.work/p21-official-framework`，运行时源码 `package/BoardRuntime/`。基线生成的独立源码位于 `out/p21/phone-ui-d2/source-tree/`；输入、配置、脚本、构建日志和产物在 `out/p21/phone-ui-d2/`。
- 固件运行于 Linux VM，基线源码 overlay 为 `/home/dinnerabc.guest/unixnu-up1-phone-d2/source`。其 lower 依赖历史 overlay，见 `out/p21/phone-ui-d2/scripts/guest-overlay.sh`；不要删除旧层或更新旧候选的 pin。

## 新候选流程

1. 创建独立候选目录。以基线的 `prepare-source.py` 为参考，从其指定的历史提交导出源码，复制当前需纳入的板级源码、补丁和脚本，并连接固定 feeds。路径在脚本中硬编码，先调整到新目录；它会拒绝覆盖现有源码。核对实际参与编译的源码哈希与当前提交一致。
2. 使用官方框架 API 准备树并编译板级运行时和 XNU。当前 `scripts/p21-compile-touch-drive.py` 指向 `out/p21/touch-drive/source-tree`，输出 `out/p21/touch-drive/native/kernel.macho` 和 `kernel-source-build.json`。执行前检查脚本内容和未提交改动，确认 tree、工具链、补丁和板级源码路径符合本轮；切勿改写固定上游 checkout。其调用入口示例：

   ```sh
   /opt/homebrew/bin/python3.13 scripts/p21-compile-touch-drive.py > out/p21/touch-drive/kernel-build.log 2>&1
   ```

   查看退出码、构建报告和内核哈希。此前尝试 `p22-compile-r5.py` 在 `tree/drivers/ArmCpu` 尚未准备时失败；不要将该失败后的中间输出当成功。必要时先通过官方流程完成组件解析，并将需修改的 ArmCpu 复制到候选独立树，避免修改上游软链接目标。
3. 用候选独立脚本调用官方 XnuPrelink，核对 donor kernelcache 哈希与段布局，输出 prelinked 内核；本机参考 `out/p21/phone-ui-d2/scripts/prelink.py` 和 `prelink.log`。再按该候选的构建流程执行早期入口及 panic-screen patch，保存最终内核哈希。入口与 prelink 需要匹配当前内核版本，不能仅复制旧内核。
4. 使用本轮内核、匹配的 ADT、启动配置、trust cache 和 Data seed 生成 payload；参考 `out/p21/phone-ui-d2/scripts/build-payload.py`。生成 `CompressedPayload.h` 和 `Expected.h`，记录 manifest、长度和哈希。镜像侧不要覆盖 userdata 的 10 GiB 起 32 MiB 持久日志窗口。
5. 在 VM 创建**独立** overlay 和 Build 目录，把本轮两个生成头文件写入该 overlay；可参考基线 `guest-overlay.sh`、`prepare-uefi.py` 及 `rebuild/ipados-up1/scripts/finish-build.py` 的 VM 调用方式。重新编译受影响的 EFI 模块和完整 UEFI，导出本轮固件及本机 Android DTB。不要在基线 overlay 原地改写 pin。
6. 复制并改造候选自己的 `pack.py` 后打包；确认启动镜像中的固件、DTB 和 payload 可回读，核验 EFI 实际编入的 payload、内核和 ADT 校验值，核对模块与固件卷；记录镜像及全部输入哈希。`pack.py` 与 build-uefi 记录包含硬编码路径：必须改为本轮路径，不能仅复制旧 JSON 或旧固件。容器校验、编译通过和设备启动是不同证据。

`out/p21/touch-drive/` 有从基线复制的历史文件；只有 `kernel-source-build.json` 及其对应新内核声称是该轮重建结果。其原有 payload、固件、镜像、pack 日志、离线门禁和 boot 日志可能仍是 d2 旧产物，未完成本轮重打包和对应验证前不得用于宣称 touch-drive 可启动。

## 实机与交接

启动前核对镜像哈希、最近 boot ID 与日志、设备序列号 `3b25cc7c`，检查旧监视器和等待中的 Fastboot，确保只有一个 ADB 服务。只在用户授权且设备处于 Recovery 时切入 Fastboot，临时执行：

```sh
.work/p3-tools-35/platform-tools/fastboot -s 3b25cc7c boot <本轮镜像路径>
```

`Sending/Booting OKAY` 仅表示传输和命令接受。记录屏幕结果；返回 Recovery 后使用系统实际端口读取日志并比较 boot ID，重点查看 TOUCH 的 reset、I2C、HID 和 DISPLAY/SWAP。不要自动循环重启或把旧日志当新日志。更新 `HANDOFF.md`，明确实际镜像、源码提交、构建与设备结果、风险和下一步；按项目约定提交源码及文档，不提交镜像、签名材料和设备私有日志。
