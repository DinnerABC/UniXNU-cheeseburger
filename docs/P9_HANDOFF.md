# P9 交接（进行中）

首要目标是捕获 `MACH Reboot` 前的第一条真实 fatal，不重审 P1–P7，也不新增色条阶段。

P8 已冻结在 `out/p9/baseline/`。P9 在唯一 `.work/p4-unixnu/.cache/xnu-work` 中添加 first-fatal 源码接入；具体补丁在 `patches/p9/`，可回滚。`drivers/p9/` 保留官方 renderer 原件及来源说明。诊断 archive、kernel 和 host regression 均已真实构建/验证，UEFI/boot image 已真实构建并回读通过。

自动测试工具为 `tools/p9/device_loop.py`，仍复用冻结 P5 状态机，包含身份、备份、DTB、hash、同 SHA 不重复测试及失联停止门禁。用户已确认恢复 Android；最终镜像与 ADB 身份均验证通过后，已按授权开始 run `20261022T123820Z-b6fabca3` 的一次临时诊断启动。

实测已收到用户转录：`P9 FIRST FATAL / PANIC COMMON FUNNEL`，最后 `trust_cache_init`。OCR Caller `…07ala5b0` 若解释为 `…07a1a5b0`，其 LR-4 的实际 BL 和参数字符串确定对应 `img4if == NULL` → `image4 interface not available`。原始转录、条件化地址解析和真实反汇编分别在 `evidence/p9/first-fatal-user-transcript.*`、`out/p9/fatal-analysis/`。不能把该结论写成电子抓取到了完整正文或回溯。

IOKit 基础 `InitIOKit/ConfigureIOKit` 已按顺序返回，但一般驱动匹配、UFS 块设备、APFS、BSD、launchd 均无成功证据。当前不是先修 UFS：第一处阻塞是安全组件没有注册。实际固定预链接解析器拒绝现有 XR ARM64e 输入；见 `out/p9/security-inputs/result.json`。保留 Image4/AMFI/Trust Cache 校验，未创建空成功接口。

已修复导致正文和栈丢失的诊断代码：Darwin 汇编器注释掉分号后的 ISB/MRS，现改显式换行。真实回归重现旧缺陷，并确认新对象及最终内核所有查询均完整。v2 kernel SHA256 `c27e26827ac646a2fc6adf39ae5e6b2a6cc614c36c67566480a3adaa541d87b6`，位置 `out/p9/kernel-v2/kernel.macho`，仅修正诊断，未修复安全组件缺项、未打包/实测。精确冻结输入 `out/p9/milestones/capture-v2/`。原实测输入仍在 `out/p9/milestones/diagnostic-input/`，`candidate-manifest.json` 未冒换为未测试版本。

日志自动化：已有 `tools/p9/collect_logs.py`，即时/120 秒采集最初退出 2（无连接）。用户已长按恢复 Android，并已自动只读采集；当前原设备 `OnePlus5` 通过 ADB 在线。最终 run `20261022T130934Z-4d76879b` 的 pstore 权限拒绝、last_kmsg 不存在，内核日志 0。需要后续复用时运行：

```sh
python3 tools/p9/collect_logs.py --wait-seconds 30
```

结果在 `out/p9/log-recovery/<run_id>/manifest.json`；Android 日志不自动等于 XNU 日志。实际验证 exec-out 可能隐藏远端失败，工具已改用 shell -T；第一次误判的错误提示已重标为 `.partial` 并保留原元数据，最终采集未产生假有效日志。ramoops 的真实保留地址已从设备 FDT 核实，但对应运行时 ABI/所有权/跨重启保留尚不合格，禁止直接写入；见 `out/p9/log-transports/reserved-memory-audit.json`。不启用写 userdata 的 sheng BootLog。

下一步先取得与普通 ARM64 和选定 XNU ABI 相符的真实 Image4/AMFI 组件，验证预链接及早期初始化入口；或对 iPhone 普通 ARM64 系统连同 XNU 版本做一致迁移。不得把现有 XR ARM64e subtype 改名混入。没有合格安全输入前，重复实机启动不会推进到根卷。

Linux VM 已在 v2 XNU 增量链接前停止。当前手机已恢复 Android；没有未完成的临时启动循环。不要重复已知失败 SHA，不使用 fastboot read、EDL、自动长按替代或未经验证的重启写入。所有产物仍在项目目录，P1–P8 保留。最新状态入口 `out/p9/latest-status.json`。
