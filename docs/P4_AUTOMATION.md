# P4 自动化边界与使用

`tools/device_loop/loop.py` 实现指定序列号/产品校验、项目内镜像 SHA256 与 DTB 身份验证、关键备份重新哈希、ADB 转 Bootloader、一次临时启动、ADB/Fastboot/macOS USB 多信号观察及 JSONL/CSV 索引。原始含设备标识的输出只存私有目录。

```bash
python3 tools/device_loop/run_profile.py config/p4/register-probe-profile.json
python3 tools/device_loop/run_profile.py config/p4/ram-probe-profile.json
```

每份 profile 固定镜像和哈希、最大一次启动。不能将失联简单判为失败或 UEFI 成功；观察超时进入 NO_TRANSPORT_MANUAL_RECOVERY，停止重试。没有 flash、erase、unlock、EDL 路径。Fastboot stdout 出现 FAILED 即使进程返回0也判失败。实际 Android 返回要求 ADB 在线且 sys.boot_completed=1。

自动恢复通过 BootServices 定时器调用冷重启，已实测可回到 Android；ExitBootServices 后停止该逻辑，不能承诺内核挂死恢复。在当前用户“暂不问问题、无人值守”的要求下，失联只记录现场，不反复要求按键、不盲目重新启动。

## 构建组件

- `scripts/p4-build-and-export.sh`：真实基础 XNU 构建及 Mach-O 校验。
- `scripts/p4-build-adt.py`：真实 FDT/寄存器派生 ADT 与锁定 DarwinBoot C 解析器验证。
- `scripts/p4-verify-package.py`：重新验证 P2 已源码编译的 AArch64 EFI 包，不伪称重新编译。
- `scripts/p4-build-payload.py`：绑定已验证 kernel/ADT 构造只读 RAM bundle。
- `scripts/p4-prepare-rampayload.py`：在保留的 guest ext4 工作树应用可回滚适配。
- `scripts/p4-pack-rampayload.py`：真实 FD + 同型号 DTB + bundle 打包与回读。
- `scripts/p4-build-arm-drivers.py`、`p4-build-crypto-bootstrap.py`、`p4-link-platform-kernel.sh`：独立运行组件候选，不覆盖基础内核。
- `tools/device_loop/pipeline.py`：按输入内容和输出哈希缓存分阶段结果，缺依赖/实现明确 BLOCKED。完整端到端 pipeline 尚未验收；不能把单组件成功当成整个 pipeline 成功。
- `config/p4/pipeline.json` 已补齐 payload、UEFI、bootimg 的实际命令和依赖；新增 UEFI 构建封装通过语法检查，尚未单独验收整个封装流程。

所有命令使用 `scripts/p4run.py <唯一label> -- <命令>` 保存 stdout/stderr/退出码。不要复用标签覆盖旧尝试。下载脚本取消代理，工具临时目录与缓存均在项目内。

## 日志回传

A：AutoReturn 易失 ring 含序号、CNTPCT、频率、阶段、CRC，BootServices 分配并发布配置表；尚未验证跨重启寿命，没有异常 PC/ESR/FAR 捕获或自动回传成功证据。

B：实机 SimpleFS3/BlockIO79 存在，但不能据此认定有可写外接 FAT；当前没有指定外接盘。新增 RAM filesystem 是只读，不承载持久日志。

C：SerialIO 未找到；现有 USB Host/MassStorage 驱动不证明 USB Device CDC gadget 可用，尚未实现日志通道。

当前可靠自动回收的是主机传输/状态日志；UEFI/DarwinBoot/XNU 内容仍需后续建立电子通道或一次人工屏幕取证。不得把无通信推断为内核成功。

## 7点唤醒

按用户要求建立北京时间2026-10-22 07:00一次性本线程自动任务 `7-unixnu-p4`。该唤醒不是无限重试或分区写授权。执行后删除任务，续接以 P4_HANDOFF 和真实日志为准。
