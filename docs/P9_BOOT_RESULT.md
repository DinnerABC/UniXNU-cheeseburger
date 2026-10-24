# P9 构建与实机结果

P8 成功执行过的版本、哈希和精确修改源码状态已冻结于 `out/p9/baseline/`；没有修改 P8 二进制。

P9 实际 first-fatal archive 已编译；唯一 XNU 工作树以 `-j2` 增量重链接通过，无未解析符号，panic/halt 捕获分支已通过真实反汇编核对。格式化及 Darwin wrapper 宿主测试通过。相关命令、完整输出和退出码在 `out/p9/logs/`。

P9 UEFI/boot image 已真实构建并回读通过。用户确认恢复 Android，`detect-device-01` 核实 ANDROID_ADB 后，仅执行一次临时诊断启动，run `20261022T123820Z-b6fabca3`。Fastboot 接受镜像；用户随后确认屏幕进入 `P9 FIRST FATAL / PANIC COMMON FUNNEL`，最后阶段 `trust_cache_init`。完整记录在 `evidence/p9/first-fatal-user-transcript.*`。

控制器等待 240 秒后以 `NO_TRANSPORT_MANUAL_RECOVERY` 停止，退出码 2，没有重复启动。没有刷写、擦除、解锁或持久化修改手机分区。

## 已验证的诊断版输入

| 文件 | 字节 | SHA256 |
|---|---:|---|
| `out/p9/kernel/kernel.macho` | 19460352 | `4f2837307c2237b2d0b54abe31fbf629acf7c2cf3feeee4f1e60e597b82ed58a` |
| `out/p9/adt/kernel.macho.adt` | 14600 | `883aa5a8a554c687fe6ffe38c4e6ef683f595c424ac912f746c7f80b2b6b74e0` |
| `out/p9/uefi/boot-p9-xnu.img` | 25608192 | `7e07986fed99e6b4ed1283a97df693cb84e38bb055beaf6058571e7566a8b509` |

## 最深阶段与证据限度

- 已实测运行到 kernel bootstrap 的 `trust_cache_init` 并进入 panic funnel。
- 按固定构建中该标记的顺序，`PE_init_iokit()` 已返回；该函数执行 `InitIOKit()` / `ConfigureIOKit()`。因此 IOKit 基础环境、Registry/Catalogue 与 root nub 默认配置已有顺序证据。
- 一般驱动匹配由稍后的 `PE_lockdown_iokit()` → `StartIOKitMatching()` 启动；不能据此宣称完整 IOKit 匹配完成、ArmCpu/NativeUfs 硬件初始化成功或块设备已发布。
- UFS/IOMedia、APFS 根挂载、BSD、launchd 均无成功证据，没有生成可启动 iOS 用户空间的完整新镜像。
- Caller 的 OCR 候选对应 Image4 未注册的 panic；正文未被电子捕获，精确证据等级见 `P9_ROOT_CAUSE.md`。

## 后续真实构建

修复 panic renderer 的 Darwin 汇编分隔错误后，使用原缓存、停止 Linux VM、`MAKEJOBS=-j2`，新 archive 和 `out/p9/kernel-v2/kernel.macho` 增量链接退出 0，无未解析符号。该文件为 19460360 字节，SHA256 `c27e26827ac646a2fc6adf39ae5e6b2a6cc614c36c67566480a3adaa541d87b6`。

真实对象及最终内核的 AT/ISB/PAR 检查通过；Darwin wrapper/formatter 宿主测试与日志采集身份/权限拒绝测试通过。失败过的分析脚本断言和反汇编窗口检查亦保留原日志，修正后重跑通过，未忽略编译退出码。精确命令、编译器参数、完整输出及退出码在 `out/p9/logs/`、`out/p9/capture-v2/build.json`。冻结输入见 `out/p9/milestones/capture-v2/manifest.json`。

v2 尚未生成新 boot image 或再次实测：没有兼容 Image4/AMFI 输入时，再次启动只会重复同一安全依赖阻塞。原版 P9 的内核、镜像和 `candidate-manifest.json` 继续指向实际测试过的版本。

## 自动日志采集尝试

实际检测 ADB、Fastboot、USB 和串口，没有可用手机日志传输接口。`tools/p9/collect_logs.py` 已实现选择序列号/型号验证、只读流式采集 pstore/last_kmsg、SHA256 和退出码记录；禁止 mount、su、重启及分区写入。失败读取只保存 `.partial`，不会冒充有效日志。

已实际执行即时采集及有界等待 120 秒的采集，均退出 2（无 ADB），没有获取 XNU 电子日志。现有 fatal 只存于普通内核 DATA，不能假定长按重启后仍可回读。

P3 实机 FDT 中确有 2 MiB ramoops 保留区，但固定历史 OnePlus pstore 驱动的配置 ABI 与采集的 Recovery FDT 不一致，且 UEFI/XNU 占用及跨重启保留未验证。审查见 `out/p9/log-transports/reserved-memory-audit.json`；未向该区域写数据，未启用上游向 userdata 尾部写 BootLog 的方案。

用户随后确认恢复 Android，已核验原序列号对应的 `OnePlus5` 在线（这是 P3 实测和冻结 P5 控制器已接受的 Android 别名，Recovery 可报告 cheeseburger）。采集器初版只接受 cheeseburger，按既有真实别名修正后重测。

最终只读采集 `20261022T130934Z-4d76879b` 完成，宿主采集任务退出 0，逐项结果：pstore 列表读取退出 1（Permission denied），last_kmsg 读取退出 1（文件不存在）；可用内核日志 0，没有 XNU 日志。当前 Android 为 Linux 4.4.302-perf+，也说明不能把历史厂商源代码直接当作当前运行时 pstore ABI。手机仍处于 Android，没有执行第二次临时启动。

实测还发现 `adb exec-out` 对失败的 cat 返回宿主退出码 0，并把错误提示放入 stdout。已改用 `adb shell -T` 核验远端退出状态，并验证真实失败返回 1；初次候选记录 `20261022T130823Z-dd0f2a71` 已重标为无有效日志，原元数据保留 `manifest.original.json`，错误输出保留 `.partial`。没有把 48 字节错误提示算作 last_kmsg。
