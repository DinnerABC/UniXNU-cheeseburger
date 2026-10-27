# P23 iOS16 启动停滞调研与加速候选

日期：2026-10-27（Asia/Shanghai）。设备当前仍在运行，未进入 Recovery；本轮只做主机侧调研和打包，没有发送新镜像、没有写 userdata、没有重启。

## 当前证据边界

- 最新已回收日志仍是 `out/p21/persistent-logs/20261027T032021Z`（boot_id `0x3a75632c`）：launchd 已执行 `fsck` 到 `launchd_cache_loader` 的 boot task，随后没有 fatal；屏幕报 `ipc_right_copyout: strange rights` 的是上一版旧 lookup 回复 disposition 19。
- 修 disposition 为 17 的镜像 `407eb481…` 已发送，当前运行超过 900 秒，无 ADB/fastboot、无新持久日志可读；用户观察“没有报错，日志还在正常输出，但没有桌面”。
- 因此当前不能断定卡在 `launchd_cache_loader`、SpringBoard 还是某个等待中的服务；需要屏幕最后几行或回到 Recovery 后的新 boot_id 日志。

## 上游对照

| 参考 | 用户态 | 现象/时间 | 关键启动参数 |
| --- | --- | --- | --- |
| [ios-topaz](https://github.com/turgayasuk-sketch/ios-topaz) | iOS 16.7.16 iPhone X | 约 3 分钟到锁屏；4 个 Gold 核 | `launchd_unsecure_cache=1`、`wdt=-1`、`wdt_test=no_user_checkin`、`sweetfb_noconsole=1` |
| [ios-sailfish](https://github.com/joranv1/ios-sailfish) | iOS 16.7.16 iPhone 8 | 约 20 秒到锁屏 | `launchd_unsecure_cache=1`、`cs_enforcement_disable=1`、`wdt=-1`、`sweetfb_noconsole=1` |
| UniXNU skip-setup | iPadOS 17.7.11 | 首次启动跑到 SpringBoard | `sheng-disable-jobs=com.apple.identityservicesd,com.apple.locationd,com.apple.CommCenter,com.apple.iconservices.iconservicesagent` |

本地参考：

- `.work/p4-unixnu/docs/guide/skip-setup.md`
- `.work/p4-unixnu/docs/guide/debugging.md`（`boot-ui-focus=launchd` / `SpringBoard`）
- `docs/P20_UPSTREAM_SERVICE_AUDIT.md`：历史 SpringBoard -> chronod -> liveactivitiesd 等待链
- `docs/P21_UPSTREAM_BOOT_COMPARISON.md`：UP1 曾确认出现原始可见 UI

## 最可能的停滞点

1. **launchd cache 签名验证/缓存装载**：iOS16 `launchd_cache_loader` 读取
   `/System/Library/xpc/launchd.plist` 与 `.sig`，通过 AMFI/SecStaticCode 验证后把 cache
   传给 launchd。本候选启动参数没有 `launchd_unsecure_cache=1`，而两个 iOS16 参考移植
   都显式使用它；验证/重试可能长时间占用而 daemon 仍在写日志。
2. **boot task/服务等待**：如果 cache loader 已退出，SpringBoard/backboardd 可能等待
   chronod/liveactivitiesd 或其它原始 job；P20 记录过同样的等待链。
3. **日志/帧缓冲开销**：当前参数含 `-v`、`-enable_kprintf_spam`、`kextlog=0xfff`、
   `p21-iokit-trace=1`、`p20-observe=1`，在 1080p 帧缓冲上可能显著拖慢启动。
4. **1 GiB 内存**：P20 曾在 1 GiB 记录 free 约 4 MiB；若新日志显示大量 reclaim/jetsam，
   才考虑内存窗口实验。当前不先改内存。

## 已准备的主机侧候选

只修改 boot args，不修改内核、root 策略、trust、Data seed、userdata：

- 在 `out/p23/ios-16.7.11/config/unixnu.toml` 的 args 中增加
  `launchd_unsecure_cache=1`。
- 原配置备份：`out/p23/ios-16.7.11/private/unixnu.toml.before-cache`
- 镜像：`out/p23/ios-16.7.11/boot-args-cache/boot-p23-cache-arg.img`
- 75,399,168 字节
- SHA256 `90912294aa215cd87c7b7f26ac7788884a6048c8782820612e55709902f303db`
- payload container `80d4beb396e5c158b20695ba7f7e81c8cec078e23e5301b81a0fced25d5885ad`

尚未发送。该参数是上游 iOS16 移植用于“无 AMFI 的 launchd cache”开发路径；它只影响
`launchd_cache_loader` 的 cache 验证，不改变内核 root/策略/Data。若不能接受该开发
开关，则下一步只能先缩小日志开销并等待完整日志确认具体服务。

## 后续最小验证顺序

1. 设备回到 Recovery 后，先不发送，读取当前 900 秒运行的 P23 日志，确认 launchd 是否
   已过 `launchd_cache_loader`、SpringBoard/backboardd 是否出现、是否有 jetsam/等待。
2. 若日志停在 cache loader，发送上面的 cache-arg 候选一次。
3. 若日志已到 SpringBoard/backboardd 但无帧，先查图形客户端/帧计数和 job 等待；再决定
   是否使用 `sheng-disable-jobs`。
4. 若 cache 已过但仍慢，做“降日志”候选：`kextlog=0`、去掉
   `-enable_kprintf_spam`/`p21-iokit-trace`，保留 `sheng-log=1`、可见文字、first-fatal、
   no-reset。只在主机侧打包，发送前仍确认 Recovery。

不自动重启、不写 boot/recovery/固件、不重复发送同一镜像。

## 2026-10-27 实际 900 秒日志回收（更新）

已从 Recovery 读取当前运行日志：

- `out/p21/persistent-logs/20261027T035713Z`
- boot_id `0x3a67d52c`
- 281 次提交，uptime 1756 秒，write_errors=0
- 窗口被覆盖 34 个 slot，最新内容仍可读

关键事实：**不是 launcher/cache loader 卡住**。日志已显示：

- `SpringBoard[1084]`、`backboardd[1006]`、`chronod[1065]`、`liveactivitiesd` 都已启动；
- `memorystatus: System is unhealthy!`
- 多次 `killing_idle_process` / `killing_top_process_elevated1` / `killing_top_process_aggressive1`：
  - `liveactivitiesd` 被 jetsam 杀；
  - `chronod`、`backboardd` 被 memorystatus 杀/重拉；
- `memorystatus_available_pages` 约 13,305–26,645（1 GiB 窗口）；
- 还有大量 `containermanagerd` 容器缓存路径扫描。

因此当前瓶颈主要是 **1 GiB 内存压力 + 首轮缓存/容器初始化**，不是 IPC 或 cache 签名。
SpringBoard 已启动，但服务被反复 jetsam，图形客户端/帧无法稳定推进。

已发送的候选仍带 `launchd_unsecure_cache=1`（低风险 cache 路径），但它不是上述
内存压力的主修复。后续优先级：

1. 观察本次是否因首轮缓存已写入 Data 而显著加快；
2. 若仍 jetsam，考虑 2 GiB 启动窗口（需要单独授权/门禁），或先用上游
   `sheng-disable-jobs` 去掉非桌面必需作业以降低内存；
3. 不把 1 GiB 下的反复 jetsam 当作内核/用户态崩溃。
