# UniXNU-cheeseburger · iPadOS 17.7.11 交接

## 2026-10-30：恢复 P23 高内存窗口

- 用户报告上一触摸/8 核/2 GiB 候选疑似 `NO WINDOW`。Recovery 窗口 `out/p21/persistent-logs/20261029T163303Z/` 与前轮相同，boot ID `0x55da2956`，未出现本轮记录；这不能单独证明内核未执行。
- P23 使用 `darwinboot-memory-range=0x100000000+0xbd8c0000`、`PcdSystemMemorySize=0x13d8c0000` 及 DarwinBoot 范围分配支持；前版 iPadOS 只带了 2 GiB size，未带范围和固件支持。本机 FDT RAM bank 证据见 `docs/P4_ADT_BRINGUP.md`。恢复 P23 局部补丁，并强化 AllocateAnyPages 的范围与 2 MiB 对齐检查；仍只分配 UEFI 报告的可用页。范围 host test 通过，实机成功未证实。
- 候选 `out/p21/touch-window2g/uefi/boot-p21-up1.img` SHA-256 `290291868afdf7c325438602b3ed77feabc4ebde8c922fe0839216aebc12f59f`，沿用触摸及 8 核内核，重新打包 ADT、payload、固件并通过离线门禁。目标 `3b25cc7c` 仅临时 Fastboot boot 一次，Sending/Booting OKAY，未写分区；屏幕及 2 GiB/触摸结果待用户确认。
- 修复源码、host test 与边界说明见 `rebuild/ipados-up1/window-fix/`，提交 `949ce25`。Recovery 返回后回收新日志，不自动重复启动。

## 2026-10-30：UP1 触摸、八核和 2 GiB 候选

- 用户确认上一版 `out/p21/up1-clean-rebuild/uefi/boot-p21-up1.img` 已进入 iPadOS。其新日志 `out/p21/persistent-logs/20261029T161111Z/` boot ID `0x55da2956`，268 rounds、写入错误 0。手机端持久日志已工作；主机 `scripts/unattended/monitor.py` 未运行，读取并非自动执行。
- 按一加 5 上游设备树的 GPIO89 input pull-up，修复 S3320 reset 输入未配置上拉导致的潜在初始化失败；不改变电源或驱动复位脚，低电平仍报告 -54。P22 的 R6 曾记录 -54、未发生 I2C；触摸是否修复尚待本轮实机结果。宿主 RMI4 测试通过。
- 新候选 `out/p21/touch-up1/uefi/boot-p21-up1.img` SHA-256 `61562019dd4c8f523c6c17664ea6f68b99bfca0271eac86f62998cde55b8391d`，基于 UP1，仅导入 P22 触摸与 SMP 修改；ADT 的 CPU/PSCI/内存/vnode 来源为本地 iOS Stage C，8 核、2 GiB、33792 vnode。历史 2 GiB iPadOS 尝试出现过 NO WINDOW，本轮不预设成功。
- 本轮内核、payload、UEFI 已构建，EFI 编译 pins 与 FV 模块匹配，`out/p21/touch-up1/offline-gate.json` 通过。设备 `3b25cc7c` 临时 Fastboot boot 一次，Sending/Booting OKAY，未写分区；设备实际运行状态待用户报告。若 NO WINDOW，在内核之前失败，不能判断触摸；若进入系统请测试点击/滑动，回 Recovery 后读取新的日志。
- 本次源码和流程提交 `166b938`；构建与运行日志在 `out/p21/touch-up1/`，构建脚本 `rebuild/ipados-up1/touch-scripts/`，VM overlay lower 仍需要保留。

## 2026-10-29：iPadOS UP1 独立重建与临时启动

- 用户报告此前 `out/p21/upstream-a/uefi/boot-p21-up1.img` 已进入 iPadOS 界面。该轮 Recovery 持久日志位于 `out/p21/persistent-logs/20261029T153842Z/`，boot ID `0x59359b02`，190 次记录，写入错误为 0；先读回再启动新镜像。
- 新内核从提交 `f89c28c9f83ec171ba7ee43018b0cd20766d9698` 导出的独立源码目录 `out/p21/up1-clean-rebuild/source/` 编译；官方框架和参考源码使用本地固定输入。日志在 `out/p21/up1-clean-rebuild/kernel-build.log` 及其独立构建树的 `logs/`；预链接、panic 屏幕与 payload 构建日志也留在该目录。
- UEFI 使用 Linux VM 的 `/home/dinnerabc.guest/unixnu-up1-clean/source` 和空构建输出；源码 overlay 的 lower 层仍依赖原有 UP1 及此前本地 VM 层，不是独立下载的新上游源码。构建脚本保存在 `rebuild/ipados-up1/clean-scripts/`，VM 中的 overlay 层必须保留。
- `out/p21/up1-clean-rebuild/uefi/boot-p21-up1.img` SHA-256 `7aabe63b44d37972658de89ca934151d847d2e6a8d817b45527f87382a00b772`；载荷、内核、ADT 校验值在编译后的 EFI 模块中匹配，三项 EFI 模块与 `FVMAIN.Fv` 对应节一致；镜像内容往返校验通过。注意 FVMAIN 在 FD 中经封装，本轮未单独反向解包 FD 验证 FV 内容。
- 设备 `3b25cc7c` 已通过 `fastboot boot` 临时启动此新镜像一次，Sending 和 Booting 均 OKAY；实机是否出现画面尚待用户报告。未写任何分区；boot 输出在 `out/p21/up1-clean-rebuild/boot-once.log`。出现问题时先记录画面，返回 Recovery 后回收新 boot ID 日志，勿重复自动启动。

## 2026-10-29：恢复 iPadOS userdata

- 用户要求启动一次：已核对 `out/p21/upstream-a/uefi/boot-p21-up1.img` SHA256 `7cf2fef9dea9103e860db4358697b4d2e67d550928b5deb3afac67c5d4309323`，P21 离线门禁 PASS（`pins_log_checked=false`），设备 `3b25cc7c` 通过 `fastboot boot` 临时启动，Sending/Booting 均 OKAY；未修改分区或预置 misc。本次设备运行画面尚待用户观察，不能以传输成功当作 iPadOS 启动成功。回 Recovery 后读取日志再进行后续启动。

- 当前分支 `ipados-17.7.11`，HEAD `08543c9`；未重置工作区，保留未跟踪的 `package/BoardRuntime/NativeLegacyDisplay.cpp.p23-pre-canvas`。此前 iOS 线和生成物未删除。
- 用户授权恢复 iPadOS userdata。设备 `3b25cc7c`，root Recovery，`/dev/block/bootdevice/by-name/userdata` 指向 `/dev/block/sda14`，容量 47938797568 bytes。将此前 iPadOS 基线 `out/p12/reference/cheeseburger-p12-reference/userdata/userdata-root.img` 的 9382658048 bytes 写入该分区前缀；镜像写前 SHA-256 与设备回读均为 `fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093`。
- 写前回收 `out/p21/persistent-logs/20261029T150245Z`，boot ID `0x3e1b3491`；userdata 10 GiB 日志窗口写前写后 SHA-256 均为 `76445f9fa52bd266f442b1e0070bfb94e14d50002beac8eead2b60f4681cc98a`。未写其他分区，未临时启动；本次恢复尚无 iPadOS 运行验证。

## 2026-10-29：主线切回 ipados-17.7.11，日志系统已移植

- 分支：当前 `ipados-17.7.11` @ `2508a59`；iOS 16.7.16 线保存为分支 `ios-16.7.16` @ `c1508e6`，已推送到 origin（49 个提交）。`main` 仍指向同一 iOS 提交，两个分支都在。
- 已移植：`scripts/unattended/monitor.py`
  - 2 秒轮询，只使用已经监听的 ADB 端点（默认 5037/5038），绝不启动第二个 server；
  - 判定“root Recovery + ro.bootmode=recovery + cheeseburger”后立即调用 `scripts/logging/read-cheeseburger-log.py`；
  - 以窗口 SHA-256 对比 `out/unattended/state.json`，输出 `NEW_LOG` / `NO_NEW_LOG`。
- 已移植门禁：`scripts/unattended/preboot-gate-p21.py`
  - 校验 packaging → 镜像哈希 → P13LZ001 payload → 展开哈希/长度 → payload 内 kernel 与 `kernel/validation.json` 一致 → 固件哈希与打包一致 → 镜像内 gzip(FD)+DTB 与 uefi 文件一致 → 参数含 `sheng-log=1`、`keepsyms=1`；
  - 已验证：R17 D2、R14 通过；R4 因参数没有 `sheng-log=1` 被拒（R4 是唯一出过 UI 的历史镜像，但要诊断仍需带日志的候选）。
- iOS 线留档要点（详见 `ios-16.7.16` 分支的 HANDOFF）：
  - sandbox kext 移除成功，根因是宽松 MAC policy 必须在 `mac_policy_init` 早期注册（晚注册会写只读静态表 panic）；
  - `lsd` 的 `mobilegestalt` 拒绝从 11 次降到 0；
  - 显示仍 `frames=0`；swap 入口是 selector 5 → Legacy vtable 槽 178 `swap_submit`，selector 72（vsync 注册）从未出现。

## 设备与约束

- 目标设备 `3b25cc7c`，只做临时 `fastboot boot`，不写 boot/recovery/固件/GPT。
- userdata 唯一授权写入：相对 10 GiB、32 MiB 的持久日志窗口（`out/p21/r14/log-window.json`）。
- 每次启动前要有离线门禁；启动后必须能从 Recovery 回收日志（或证明窗口未变）。
