# UniXNU-cheeseburger · iPadOS 17.7.11 交接

下一步首帧前黑屏排查流程见 `docs/P21_BLACK_SCREEN_NEXT_STEPS.md`；其中返回码勘误覆盖下方旧分析。

## 2026-10-30：fb-diag 诊断候选（selector 事件环）已启动一次

- 依据 `[mfb clients]` 全量行与 `[display state]` 得到的新分界：成功轮 backboardd 的 `sel 6` 完成 `(n,0x80000001,0) → 0` 序列，把显示对象 `[5820]/[5824]` 累加到 `0xe`（d2）/`0x2b`（window2g），`(n,0x80000000,0)` 返回 `0xe00002d5`(Busy)，随后 `sel 4`/`sel 5` 各 14 次成功；失败轮 `sel 6` 只调用一次 `(0,0x80000003,0) → 0`，没有 4/5，`[display state]` 的 `5816/5820/5824/5828` 全 0，`[display]` 的 `Current Display` 为空。剩余未知量是 `sel 6`/`sel 8` 的 scalarOutput。
- 上游依据：sheng `package/MobileDisplayProviderProbe.cpp` 用 `selector 8 + scalarOutputCount=2` 读宽高，证明 selector 8 是尺寸查询；本机 `LegacyDisplayProbe.cpp` 的客户端拦截与 sheng 同源。
- 新增只读诊断（提交 `a6651e8`）：32 槽事件环记录 selector 4/5/6/8/15/50 的时间（cntpct/cntfrq）、pid、客户端代际、至多 4 个 scalarInput/scalarOutput、原始返回与丢弃数；报告新增 `[fb events]` 段（≤2560 字节）。不改变任何返回值，不新增睡眠/分配/锁。
- 候选 `out/p21/fb-diag`（自 d2 复制，ADT `775d411b…`、参数与 d2 逐字相同）：内核编译 `466280ad…` → 预链接 `7b19d6ab…` → panic-screen `29d3a758…`（125,698,048 bytes）；payload `b44f0ad3…`；固件 `d131c854…`；镜像 `out/p21/fb-diag/uefi/boot-p21-up1.img` `bf6d86d7…`（82,567,168 bytes）；pins 三项命中、门禁 PASS（644 字节）。目标 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区；监视器已挂起。
- 观测窗口：历史首帧 307/318/524 s，建议跑满 ≥600 s。回收后比较 `[fb events]` 里 `sel 8` 与首个 `sel 6` 的 out 值；若与成功轮相同，则排除“provider 返回不同”，转向通知/等待路径。

## 2026-10-30：黑屏分析勘误与旧镜像复跑

- 用户回到 Recovery 后回收 `out/p21/persistent-logs/20261030T052133Z/`，boot ID `0x4bd8fea6`、962 秒、192 轮、写入错误 0；最新已记录的启动是 touch-window2g `boot-once2.log`（Fastboot 接受；日志本身不证明镜像哈希）。本轮 `stage=10 clients=5 frames=0`、SWAP/prepare 均 0；backboardd 的 15 次调用中 selector 6/8/88 成功，12 等多个 selector 返回 `e00002c7`，15 返回 `e00002bc`。
- **更正下方及报告的 NotOpen 判断**：同版本 XNU `iokit/IOKit/IOReturn.h` 定义 `0xe00002c7` 为 `kIOReturnUnsupported`，`kIOReturnNotOpen` 实际是 `0xe00002cd`。不能由 `e00002c7` 判断 open 失败或发布时序竞态。
- 更关键：成功的 d2 日志 `20261029T172431Z` 的 backboardd selector 50 也返回 `e00002c7`（2/2 失败），同时 selector 4/5 均成功 14 次并显示 14 帧；所以 selector 50 的 unsupported 不是黑屏特有签名，更不能单独作为根因。上游 `.work/p21-official-framework/scripts/check-system-framebuffer-client.py` 也将其原版 frame-info 查询的 `e00002c7` 作为 unsupported 记录，但未证明本机 selector 50 的语义。
- 新旧黑屏对照仍支持问题发生在观察到的首帧提交之前；`stage=10`、`newUserClient` 成功和显示服务匹配不证明所有显示/用户态状态相同。采样仅最多保留 8 个客户端，不能从 0/0 推断进程从未通过其他对象调用。触摸、身份与时序均未被全面排除。下一步应分析 backboardd 为什么在初始查询后没有进入 selector 4/5，优先符号化已有线程栈、关联客户端调用与用户态依赖状态；不应盲改 open 返回码或屏蔽服务。设备已回到 Recovery，未重新启动或写入分区。

## 2026-10-30：黑屏分支的日志分析（framebuffer 客户端 open 失败）

- 失败轮的共同签名：某个显示客户端（backboardd / `IOMFB_bics_dae` / `IOMFB_FDR_Load`）最后一次外部方法调用是 **selector 50，返回 `0xe00002c7`（kIOReturnNotOpen）**，且 backboardd 的帧调用为 `in/out=0/0` 或极少；成功轮 backboardd 最后是 **selector 5（swap_submit）rc=0**，`in/out=77/77` 或 `194/194`。失败轮 `prepare calls=0`、`SWAP rc=ffffffffffffffff`，即一次帧都没提交。
- 内核侧显示发布两轮一致（`IOMobileGraphicsFamily` 注册时间 74 s vs 66 s、`NativeLegacyDisplay` 注册、`DISPLAY stage=10 meta=0 reg=1`），所以差异不在内核显示初始化，而在用户态客户端连接时序。
- 用户态侧差异：失败轮进程退出 599/618/678 次、SpringBoard 8 线程；成功轮 1088 次、17 线程。失败轮 msgbuf 里 datamigrator/语言服务/bluetoothd 反复重启。首帧历史：307 s、318 s、524 s；失败轮 881/977 s 仍为 0。
- 上游核对：官方 `docs/reference/troubleshooting.md` 明确写“Firmware stalls → Random Silicium stall → Start the same image again”，承认随机卡住需重试；官方 sheng（同为 iPadOS 17.7.11/21H461、iPad7,1）`board.toml` 与本机的差异：`offline-legacy-unit-scale=2`（本机 1）、`offline-mobile-graphics-client=1`、`sheng-disable-jobs=com.apple.identityservicesd,com.apple.locationd,com.apple.CommCenter,com.apple.bluetoothd`、`boot-ui-focus3/4`（仅诊断用途）、`debug=0x14e`、`kt_fixed_zones=120`、`-apfs_shared_datavolume`。上游没有“NotOpen 黑屏”的直接案例，也没有可直接合并的修复。
- 待验证的下一步：给 `wrappedExternal` 增加 selector 50 与 open/close 的一次性记录（pid/时间/rc/当时的 `native_legacy_stage`），确认失败轮是否发生在显示服务发布之前；若成立，再考虑让 provider 更早发布或让 open 路径返回可重试状态，而不是直接失败。

## 2026-10-30：同一张 d2 复现失败——镜像代码不是变量

- 回收 d2 复跑日志 `out/p21/persistent-logs/20261030T050326Z/`（boot ID `0x50e2b156`，176 轮，写入错误 0）。同一张 `1999371c…` 镜像：首次 `20261029T172431Z` 为 `clients=115 frames=14`、`EXITS 1088`、backboardd 打开 FB `77/77`；复跑为 `clients=5 frames=0`、`EXITS 599`、backboardd FB `0/0`，`FB0 IOMFB_FDR_Load 15/15 enter/leave=50/50 rc=e00002c7`（kIOReturnNotOpen）、`FB2 MobileGestaltH rc=e00002f0`。
- 因此“触摸改动导致黑屏”与“手机身份导致 frames=0”两个判断都被证伪：镜像、payload、ADT、参数都相同的 d2 会两种结果都出现。触摸代码只影响触摸。
- 时间线：三次出画面的 run 首帧分别在 t≈307 s（`164350Z`）、t≈318 s（`172431Z`）、t≈524 s（`161111Z`）；两次黑屏 run 等到 881 s / 977 s 仍 `frames=0`、`prepare calls=0`、`SWAP rc=ffffffffffffffff`（从未提交）。failure 端 `IOMobileFramebuffer` 客户端反复拿到 `kIOReturnNotOpen`，表明用户态与 framebuffer 的握手在按启动时序分支，而不是在像素路径上失败。
- 三份 msgbuf 对比：APFS 数据卷都完成挂载（disk0/disk1 命名不同）、kext 加载序列一致、SpringBoard 在两种结果下都以约 1390 次/秒 CPU 唤醒自旋；黑屏 run 只有 8 个线程、正常 run 17 个。
- 按用户要求再往前启动 `out/p21/touch-window2g/uefi/boot-p21-up1.img`（`29029186…`，iPad7,1，上次 39 帧），`out/p21/touch-window2g/boot-once2.log` Sending/Booting OKAY；观察窗口按首帧历史（≥300 s）放宽到 ≥480 s。

## 2026-10-30：touch-fix2 回滚到 d2，全链差异核对

- 用户选回 `out/p21/phone-ui-d2/uefi/boot-p21-up1.img`（`1999371c…`）并已临时启动（`out/p21/phone-ui-d2/boot-once2.log`，Sending/Booting OKAY）。此前 touch-fix2 轮日志由监视器回收：`out/p21/persistent-logs/20261030T044653Z/`，boot ID `0x4bfec417`，216 轮，写入错误 0。
- touch-fix2 触摸确有推进：`error=-58` → `error=-30`、`i2c=0/1`、`hw/clock` 已过；说明 mux 0 + 输出 bit1 的复位修复生效，I2C 已发起第一次传输但在 `cb_discover` 首读失败（`SynapticsRMI4.h` 返回 `-30`）。触摸芯片此时仍未上电（pm8998_l6 1.808 V / l28 3.008 V 在 iOS 运行时未确认开启），下一步应先在板级开启这两路供电再谈 HID。
- 显示仍未出帧：`DISPLAY clients=5 frames=0`、`SWAP check=0`、`prepare calls=0`；用户报告黑屏。
- **全链对比（d2 可用版 vs touch-fix2 黑屏版）**：
  - payload 内 `unixnu.toml`、`kernel.macho.adt`、`kernel.macho.trustcache`、`scratch-data.img` 哈希完全一致；启动参数 644 字节逐字一致；仅 `kernel.macho` 不同（`fe6be784…` vs `031cf674…`）。
  - 板级运行时源码 `package/BoardRuntime` 只差 `CheeseburgerTouch.cpp` 一个文件；显示映射（`kIOMapInhibitCache`）、`BootLog.c`、按键代码完全相同。
  - 构建方式同为“官方 framework kernel.build → `mach.development.bcm2837.unstripped.noctf` 预链接 → panic-screen → payload → 独立 overlay 全量固件 → pins → pack → 门禁”；仅 overlay 名不同（`unixnu-up1-phone-d2` vs `unixnu-up1-touch-fix2`）。内核 `__text` 尺寸 d2 `0x7fb534`、touch-fix2 `0x7fb564`，差 0x30，与只改触摸函数一致。
- 结论：**此前“手机身份导致 frames=0”的判断撤回**——d2 同为 iPhone10,3/D22 且有 14 帧。现有证据只把差异收敛到触摸代码；但黑屏轮的用户态签名与触摸改动之前的 `20261029T165823Z` 相同（clients=5、frames=0、FB2 15/15 rc=e00002c7、FB3 MobileGestaltH），且黑屏轮 msgbuf 显示 datamigrator/语言服务/蓝牙在持续运行与重启，说明用户态在推进但没有提交帧，不能仅凭日志断言触摸驱动直接导致黑屏。
- 下一步建议：显示工作以 d2 为基线；触摸改动以启动参数开关隔离（默认关闭），避免再次影响显示；开启前先补 pm8998 l6/l28 供电。

## 2026-10-30：touch-fix2（GPIO89 mux/输出位）与黑屏记录，回滚待选

- 用户反馈 touch-drive（`db1385a3…`）黑屏、进不了系统。回收 `out/p21/persistent-logs/20261030T040140Z/`：boot ID `0x4f5bb00c`，927 秒 / 185 轮 / 写入错误 0；`DISPLAY clients=5 frames=0`、`prepare calls=0`、`SWAP rc=ffffffffffffffff`、`TOUCH error=-58`。
- 该签名与**触摸改动之前**的手机身份首启 `20261029T165823Z` 逐项一致（clients=5、frames=0、`FB2 backboardd 15/15 enter/leave=50/50 rc=e00002c7`、`FB3 MobileGestaltH`、`FB0` 独占）；因此现有日志不能单独证明黑屏由触摸改动造成，此点保持未定论记录。
- 触摸根因（设备 FDT `ts_reset_active` + Linux pinctrl-msm）：gpio89 的复用表只有 `gpio/tsif0/phase_flag`，复位必须 mux 0，原代码写的是 mux 3（I2C 的 mux 3 只属于 gpio87/88）；IO 寄存器 bit1 才是输出电平、bit0 是输入回读，原代码写 bit0，pad 实际从未拉高，故 `-58`。
- 修复提交 `b42b68a`。候选 `out/p21/touch-fix2`（自 touch-drive 复制，ADT 保持手机身份 `775d411b…`）：内核 `031cf674…`、payload `3ac2df84…`、固件 `66868a03…`、镜像 `out/p21/touch-fix2/uefi/boot-p21-up1.img` `80c8b19e…`（82,583,552 bytes）；pins 三项在 P9Probe/DarwinBoot/P4RamPayload 命中，门禁 PASS（644 字节参数），目标 `3b25cc7c` 临时 boot 一次 Sending/Booting OKAY，未写分区。
- 实机结果：用户报告仍未进入系统界面。该修复源码仅保留作比较，不进入回滚候选。

### 可回滚的“最近一次实机确认能进系统”候选（待用户选择）

1. `out/p21/phone-ui-d2/uefi/boot-p21-up1.img` — SHA-256 `1999371cccd22d6b5b36bea283cf596c0f04de63d91c527806da59cf88d25da3`（01:13）；手机身份 iPhone10,3/D22 + `/buttons`（hold/voldown/volup、home-button-type=2、thin-bezel=1）+ `BootLog.c` DISPLAY SCALE 诊断；用户确认进入系统画面（平板布局、触摸无效），日志 `20261029T172431Z` 有 14 帧。
2. `out/p21/touch-window2g/uefi/boot-p21-up1.img` — SHA-256 `290291868afdf7c325438602b3ed77feabc4ebde8c922fe0839216aebc12f59f`（00:33）；iPad7,1 身份 + P23 高内存窗口（`darwinboot-memory-range=0x100000000+0xbd8c0000`、`PcdSystemMemorySize=0x13D8C0000`）；用户确认显示原始 iPadOS 界面，日志 `20261029T164350Z` 有 39 帧。

## 2026-10-30：iPadOS 切换为 main

- `main` 合并保留原 iOS 分支历史，合并后的代码树以 `ipados-17.7.11` 的 `4dc3167` 为准；原 iOS 工作保留在 `ios-16.7.16`。README 调整主线说明及组件状态，硬件源码、参数与镜像未因此更改。
- 切换前 Git 完整 bundle 和未跟踪文件副本保存在 `out/repository-backups/20261030-before-ipados-main/`，bundle 校验通过；不包含被忽略的系统镜像和生成物，它们仍在原位置。设备和构建状态以其后记录为准。

## 2026-10-30：touch-drive 修正重建（复位脉冲 + 构建链修复）

- 触摸：GPIO89 改为厂商复位序列——mux 0、上拉、8 mA，先写锁存低→置 OE→2 ms→锁存高→50 ms→回读；仍低返回 `-58`。
- 构建根因：resolved 树的 `kits/cheeseburger` 软链接原先指向 **d2 的 package**，所以候选树里的触摸改动从未进入内核；`p21-build-board-runtime.py` 增加 `P21_BOARD_ROOT` 覆盖，驱动把它指向候选的 `source-tree`，并把 `kits/cheeseburger` 重指到候选 `package`。链接阶段的 build-report 源码哈希校验因此通过。
- 重新整链：内核 `6a01d1f2`（编译）→ unstripped 预链接 → panic-screen 后 `bbbb2099`；payload `0d4f0932`；固件 `f4c98f7e`（独立 overlay，增量）；三项 pins 通过；镜像 `db1385a3`，门禁 PASS（644 字节）。
- 尚未启动。

## 2026-10-30：touch-drive 候选（S3320 复位拉高）

- 修复：`package/BoardRuntime/CheeseburgerTouch.cpp` 在输入上拉仍读到低电平时，改为置 OE、驱动 GPIO89 高（ACTIVE_LOW 复位释放）并回读；仍低则回退并返回 `-58`（提交 `5f299f0`）。
- 构建（按本仓库流程）：候选树 `out/p21/touch-drive/source-tree`（自 d2 复制）内跑官方框架 `kernel.build`（`scripts/p21-compile-touch-drive.py`，内核 `db6e18f4…` 编译通过）→ 取 `mach.development.bcm2837.unstripped.noctf` 预链接（`d440a90c…`）→ `patch-xnu-entry-probe.py --panic-screen --no-reset`（最终内核 `9efd466e…`）。
- payload `571e414a…`；固件在**独立新 overlay** `/home/dinnerabc.guest/unixnu-up1-touch-drive`（lower 叠加 d2 及以下历史层）全量重编，FD `9d2fce81…`，三项 pins 在 P9Probe/DarwinBoot/P4RamPayload 中核对通过；镜像 `out/p21/touch-drive/uefi/boot-p21-up1.img` `55c694fb…`（82,595,840 bytes），P21 门禁 PASS（644 字节参数）。
- 尚未启动。下一轮重点：日志 `TOUCH error` 是否不再是 `-54`（期望 0 或 `-58`）、`reset=` 值、I2C/F01/F12/HID 是否开始计数；显示与 scale 应与 d2 相同（frames>0）。

## 2026-10-30：手机布局上游对照与本轮日志

- 设备回到 Recovery 后回收 `out/p21/persistent-logs/20261029T172431Z/`：新 boot ID `0x5a0ea281`，511 秒、102 次记录、写入错误 0；用户报告已进入系统但仍为平板布局、触摸不可用。
- 实际 `DISPLAY SCALE status=0 calls=1 chosen=1`，`frames=14`、`SWAP check=14 reject=0 transfer=14`；日志记录 `1242x2208 -> 1080x1920`。这是帧像素尺寸与运行时发布的属性，不是对 UIKit 实际 idiom 或逻辑尺寸的直接测量。触摸仍 error=-54、I2C=0/0。
- 官方 sheng 配置 `offline-legacy-unit-scale=2`，但为 iPadOS 平板配置；ios_sagit 使用 iPadOS、声明已到桌面，现有文字不足以确认其实际布局和 scale；vphone-cli 的 iPadOS 文档说明设备身份与 display-scale 共同影响呈现，不能把其虚拟设备参数直接当成本机配置。当前 phone 身份搭配 iPadOS 根和 chosen scale=1 均为需要进一步核对的差异，尚无证据证明任一项是唯一原因。
- 下一步优先读取实际 MobileGestalt idiom、有效 UIScreen scale 和逻辑 bounds，并对照同版本可复用的手机布局案例；scale=2 可以作为独立对照实验，不能当成已验证的手机布局修复。未经定位不清除 Data。设备保持 Recovery，本轮未启动或写入分区。

## 2026-10-30：手机身份诊断版画面反馈

- 用户随后报告 `out/p21/phone-ui-d2/uefi/boot-p21-up1.img` 已进入系统画面。用户进一步确认布局仍是平板样式且触摸不可用；按键是否可用尚无反馈，本轮新 boot ID 日志尚未回收。设备的实际连接状态应在下次操作时重新确认。

## 2026-10-30：后续开发交接

- 项目简介按官方设备仓库结构更新，后续 DeepSeek 接手入口为 `docs/DEEPSEEK_HANDOFF.md`，包含当前候选、已确认结果、日志和构建路径、手机界面、触摸、按键及启动限制。
- 交接时系统 ADB 服务为 `tcp:localhost:5037`，目标未在 ADB 或 Fastboot 枚举；手机实际画面未知。`phone-ui-d2` 本轮仍待用户反馈及新日志，不应以 Fastboot 成功推断进入系统。

## 2026-10-30：手机身份按键及显示 scale 诊断

- 基于 phone-ui 的 ADT 补充 `/buttons` 的 hold/voldown/volup、home-button-type=2 和 product thin-bezel=1，保留一加 5 已有电源/音量和双音量模拟 Home 处理；尚未验证物理交互或混合 iPadOS 根是否支持手机布局。
- `BootLog.c` 新增实际 `DISPLAY SCALE status/calls/chosen/rotation` 快照，其他显示、SWAP、线程栈和触摸诊断保留。固定官方设备代码会在 phase4 时按 `offline-legacy-unit-scale=1` 发布 chosen display-scale；静态 ADT 中缺失不代表运行时缺失。
- `out/p21/phone-ui-d2/uefi/boot-p21-up1.img` SHA256 `1999371cccd22d6b5b36bea283cf596c0f04de63d91c527806da59cf88d25da3`；重编内核、载荷、固件，编译时 pins、FV 模块与离线门禁通过，目标设备 3b25cc7c 临时 boot 一次 Sending/Booting OKAY，实际画面待用户观察；未写分区。上一轮日志已回收。
- Data 种子本地只读挂载被 hdiutil 拒绝，未确认缓存是否存在，未清理缓存或手机 Data；用户允许必要时清理，应在定位明确后限定目标操作。用户打算延长等待，返回 Recovery 后回收新 boot ID 日志并检查 DISPLAY SCALE / SWAP / 栈。触摸 error=-54 仍未修复。源码提交 `5a03785`、`7c4dfe6`。

## 2026-10-30：手机身份与官方规范复核

- 固定 UniXNU `16e592abee8a550dec8154ed1f38b5ebc4f16213` 的 `docs/guide/mobilegestalt.md` 要求 iPadOS 根配 pad、iOS 根配 phone；当前手机候选为用户要求的 phone 身份 + iPadOS 根实验，与该推荐不一致，不能据此断言成功或失败的具体原因。
- 实际 `out/p21/phone-ui/adt/kernel.macho.adt`：model iPhone10,3、target D22、subtype D22AP、product iPhone X、idiom phone、artwork subtype 2436、scale 3、fallback iPhone9,2；这些字段与本地 P23 ADT 一致，root compatible 保持 `device-tree\0qcom,msm8998\0`。候选无 `/buttons` 节点、无 `home-button-type` 和 chosen `display-scale`；官方全面屏示例需要无 menu 且 home-button-type=2，实际一加 5 物理按键不能直接套无 Home 机型。P23 有 buttons 但无 home-button-type。手机候选未清 MobileGestalt 缓存，实际系统读取值尚未验证。
- 上游 main 当前 iPadOS profile 增加了 product-id；本机固定版本及两个候选均没有。该值为 J120 特定，不能用作 iPhone 值。实际日志 579s SWAP=0/frames=0，未证明上述任何差异为根因；优先保持既有日志，查实际 MobileGestalt、用户栈和显示提交之前的等待。

## 2026-10-30：手机身份候选日志

- 用户反馈手机身份候选未出现系统画面。Recovery 回收 `out/p21/persistent-logs/20261029T165823Z/`，新 boot ID `0x4dd45577`，579 秒、116 记录、写入错误 0。`SpringBoard p27`、`backboardd p56` 在运行，八核在线，VM managed=2048MiB；显示 clients=5，帧、SWAP 检查及传输均为 0。UI 重点进程无已捕获致命退出。
- 相比之前 iPad 身份实机 39 帧，本轮未观察到任何提交，现有证据不足以归因屏幕缩放或判定 identity 为唯一原因；应分析用户态提交前等待、身份匹配及缓存，并保留日志里的线程栈。触摸仍 -54/reset=0/I2C=0，无新触摸修复。
- 设备当前 Recovery，未进行新启动；保留原先 `out/p21/touch-window2g` 可进入系统的候选。

## 2026-10-30：手机界面身份实验与触摸日志

- 用户确认 `out/p21/touch-window2g` 进入系统但触摸无响应。回收日志 `out/p21/persistent-logs/20261029T164350Z/`，boot ID `0x5c58198d`、94 rounds、0 写入错误；`online=8`，39 帧；触摸 `error=-54 reset=0 i2c=0/0`，F01/F12/HID 均未启动，说明上拉不足，尚未证明原因是电源还是复位线被拉低。Recovery 只读检查显示 pm8998_l6 1.808 V 和 pm8998_l28 3.008 V enabled；这仅证明 Recovery 状态。原厂 Recovery dmesg 保存在 `out/p21/phone-ui/evidence/recovery-touch-dmesg.txt`。
- 用户授权在原 iPadOS 上尝试手机界面：复制 P23 iPhone10,3/D22 的 model 和 product identity，保留 OnePlus compatible、原硬件按键、iPadOS root、8 CPU、2 GiB 和触摸内核；没有修改 Data 或清缓存。官方建议身份与系统系列一致，这次为有风险的界面实验，效果未证实。
- `out/p21/phone-ui/uefi/boot-p21-up1.img` SHA-256 `53b8f0dfd837f3d1903dbde20fe6fd3e6f9186097681194c8676799a0cac452d`；payload/UEFI 重编、pins 和离线门禁通过；目标设备临时 Fastboot boot 一次 Sending/Booting OKAY，运行结果待观察，无分区写入。源码 `rebuild/ipados-up1/phone-ui/` 提交 `e59a5a2`。

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
