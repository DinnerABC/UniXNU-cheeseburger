# UniXNU-cheeseburger 交接

## 2026-10-28：nosync 候选与最新日志

- 用户要求暂时停用 panic 自动重启以保留现场：`CheeseburgerPlatformPower.cpp` 的 `board_panic_reset` 暂用 `#if 0` 保留并屏蔽原 PSCI 重启代码，停住并提示；独立的 300 秒无人值守计时线程在看到 panic parked 后跳过重启。板级运行时 33 个对象编译通过，尚未重新链接、打包或实机测试；现有 dotup 镜像不包含该修改。panic 保持期间、早期或其他重启路径以及硬件看门狗仍可能复位，不能保证绝不重启；可能需手动返回 Recovery。原重启代码需恢复时取消条件编译并移除对应停机措施。

- 快速复核 20H392 ABI：重新计算 donor SHA-256 为 `2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221`，与固定值一致；`analysis/display-abi/abi20h392.py` 中 67 条原始指令字全部匹配。关键位置 `surfaces=0x888`、vsync list `0x1380`、display width/height `0x1458/0x145c`、active queue `0x1308/0x1310/0x1318` 与当前代码数值一致；原始 vtable slot 336 指向 `0xfffffff005c1c1d8`。这只证明相应原始指令和静态值一致，不证明全部 C++ 槽位、函数语义和运行安全。
- 按用户要求恢复 `getDotPitch` 缺失属性时上游 `kIOReturnNotFound`，diag2 基础新候选 `out/p23/ios-16.7.16-20H392-dotup/uefi/boot-p23-stagec-20h392-nosync.img` SHA-256 `576da56ed07938eaf190adc664d6ed2c0fcff14cf5b3f7acc486bf6ff8df2e44`；临时启动并回收新日志 `out/p21/persistent-logs/20261028T154226Z/`，boot ID `0x64be87ad`、19 rounds、错误 0。末条 179.145s、显示快照 clients=0 frames=0 swap=0，未捕获 getDotPitch；不可将落盘末次快照当成复位时刻。设备回 Recovery，预置 BCB，不证明复位来源。

- diag2 实机结果：恢复了独立 VM overlay（链见 `diag2/scripts/mount-overlay.sh`），固件重建且模块中的 payload/kernel/ADT pins 核对通过，镜像 `out/p23/ios-16.7.16-20H392-diag2/uefi/boot-p23-stagec-20h392-nosync.img` SHA-256 `3e13199e844aba988e249e3be1d62b4d788e8121352d01f7a301d07ebaed698c`，Fastboot 临时启动 OKAY。预置 misc Recovery 标记且读回核对，不能证明复位路径。
- 新日志 `out/p21/persistent-logs/20261028T152953Z/`，boot ID `0x45820ec5`、SHA-256 `e005c084bef1a8383a1dd408bfdbed90a7aeddc8aac1a65c8cf5c06c69312894`，16 rounds、write_errors=0。末尾内核时间 137.031s；最后快照 clients=0、frames=0、swap=0，backboardd p55 已出现，但没有 newUserClient 入口/返回记录。118.021s SpringBoard 报约 2 秒内 45023 次 CPU 唤醒；此现象不能独立证明复位原因。末次心跳 left=230；无明确 panic 或复位原因。5 秒写盘仍存在窗口，不能断言末次保存之后没有客户端调用。设备已回 Recovery。
- 用户随后报告 diag2 屏幕最后显示 `P23 DISP getDotPitch #3`，然后黑屏重启；这是用户现场观察，发生在持久日志末次保存之后，与 logfix18 最后打印位置相同，但不能证明函数内部故障。该输出位于函数入口；当前设备分支在无 dot-pitch 属性时用启动参数 `fajita-display-ppi=458` 返回成功，上游对应实现无属性时返回 kIOReturnNotFound。优先核对单位与 20H392 调用者约定，并在入口后和返回前记录阶段及结果；不能只凭差异断言根因。

- 后续诊断候选 `out/p23/ios-16.7.16-20H392-diag2/`：新增 `newUserClient` 返回码与客户端成功标记，将持久日志写入周期由 10 秒改为 5 秒；最终内核和官方预链接成功（预链接 SHA-256 `007018fed84171e2cd5c12695aa75e7fda314e36ddd405d58114138d14147e2e`），payload 展开 SHA-256 `4bb29997a38d2fce8805239ce9d327cd8e34b4f5dae4be4e0bcb3d47c1ee98de`。尚未打包或启动。
- 构建 VM p2 重启后 `/home/dinnerabc.guest/unixnu-p9/source` overlay 未挂载，`prepare-uefi.py` 的 `.p23-stagec-overlay-marker` 校验失败；stagec upper 保存在 `/home/dinnerabc.guest/unixnu-p23-stagec/upper`。需恢复并核对完整 lowerdir 链后再生成固件，不能直接复用 nosync 的旧固件。手机仍在 Recovery，日志 `20261028T150100Z` 与此前一致。

- `package/BoardRuntime/NativeLegacyDisplay.cpp` 的 `disp_note` 已移除显示回调中的同步 UFS 强制写入，保留普通日志；这消除了潜在的阻塞风险，尚未证明是重启根因。
- 临时启动候选 `out/p23/ios-16.7.16-20H392-nosync/uefi/boot-p23-stagec-20h392-nosync.img`，SHA-256 `48cbe1809283d02c6ea4e869dd8d855e77a51363bdfecd41b6ccc85129cbe39c`。Fastboot 接受启动，随后设备回到 Recovery；启动前预置了 misc Recovery 标记，不能据此证明内核重启路径成功。该候选同时启用 CPU transfer 和 60 Hz vsync，与此前候选并非单变量对照。
- 本轮新日志 `out/p21/persistent-logs/20261028T143643Z/`：boot ID `0x526d888e`、SHA-256 `8a411923182c34f9d326ebf9525272777c70fa101ace0fe8186bce8657355e2f`、8 rounds、写入错误 0；最后 msgbuf 时间约 142.76 秒，最近显示快照 clients=0、frames=0、swap=0，之后有 backboardd 启动记录；没有记录明确 panic 或复位原因。日志周期保存，不能据此断言后续未创建客户端。
- 重新回收 `out/p21/persistent-logs/20261028T150100Z/` 的 boot ID 和哈希完全一致，是同一次启动，不是新实验。设备 `3b25cc7c` 当前处于 Recovery；项目自带 platform-tools 的 ADB 服务使用 5037，Homebrew ADB 37 启动时崩溃，不要用不同版本的客户端替换运行中的服务。
- 下一步对照 logfix18/27 与 nosync 的真实配置和日志，区分客户端创建、锁与启动计时；需要进一步证据才能确定提前重启原因。用户当前要求 CPU 渲染，不切换 GPU；保留历史产物与未提交改动。

## 优先修复：新镜像进入 shell，未见内核启动

**先检查固件和载荷的版本绑定，再修改内核。** 用户报告最新镜像进入 shell；目前不知道 shell 的确切类型或屏幕前文，不能仅凭这个现象断言是哪一个引导阶段失败。

现有证据指向一个具体的打包问题：

- 最新测试 `logfix3`：`out/p23/ios-16.7.16-20H392-logfix3/uefi/boot-p23-stagec-20h392-logfix3.img`，镜像 SHA-256 `50abaa399e1e2de05dfbf7b48984b4770f98e005d941d72594598ddf48829c12`。`boot-result.json` 只证明 Fastboot 接受传输，不证明 UEFI 发布文件系统或 XNU 已启动。
- `logfix3/logs/build-uefi-01.json` 明确写了 **复用 logfix2 固件**。两版固件 SHA-256 均为 `ff0aa9f0adc3c8a9e14bbf6675953d0ebd0599237ef4d16eab4c04a6f9d612c2`，但展开后的 payload SHA-256 从 logfix2 的 `fb7dc9043ba9f29e691566ba6d0d6013f49e8299b3b0274e0e1a924cbac01037` 变为 logfix3 的 `b8a7dfacc0b69fda12347ad8a606a880b5449115e24b7e650fbf13910b5c87a6`；内核 SHA 也从 `0f2f9ee7...` 变为 `3fd2ae97...`。
- `config/CompressedPayload.h` 的 `P13Expand` 在编译时固化展开 payload 的 SHA-256 和长度，验证失败时不发布文件系统；`config/Expected.h` 同样固化内核和 ADT 的哈希。更换 payload 后直接复用旧固件，极可能造成固件和载荷校验值不一致。**优先核对旧固件实际编入的值；这是强嫌疑，仍需编译产物或启动日志确认。** `OFFLINE_PACKAGING_PASS` 只核对打包和解压，并未验证固件中的编译期校验值。
- `out/p21/persistent-logs/20261028T083413Z/retrieval.json` 的 boot ID 仍为 `0x458b4f1f`，窗口哈希与 logfix2 相同；不能把它当作 logfix3 的新内核日志，也不能仅凭窗口不变断言内核绝未执行。

建议按以下顺序修复和打包：

1. 保留 logfix2、logfix3 和当前工作区；确认用户屏幕是 UEFI Shell、Linux shell 还是其他界面，并记录前几行。先回收现有日志，确认 boot ID 和窗口哈希。
2. 将本轮最终内核、ADT、trust cache、Data seed 和 `config/unixnu.toml` 确认为同一候选；运行 `out/p23/ios-16.7.16-20H392/scripts/build-payload.py` 生成 payload、manifest、`config/CompressedPayload.h` 和 `config/Expected.h`。核对 manifest 中的内核/ADT 哈希及容器的展开哈希、长度。
3. **重新编译 UEFI 固件**：将本轮的 `CompressedPayload.h` 复制到构建树 `P5Integration/RamPayload/CompressedPayload.h`，将本轮 `Expected.h` 复制到 `P9Integration/Probe/P9Expected.h` 和 `DarwinBootPkg/Driver/P9Expected.h`；可使用现有 `out/p23/ios-16.7.16-20H392/scripts/prepare-uefi.py`，但先检查虚拟机构建树实际挂载的 overlay，避免写进或导出旧版本。必要时重建受影响的 EFI 模块，而不是只重打 Android boot 容器。保留对应的编译日志及 firmware hash。
4. 以新固件、相同版本的 payload 和正确的 Android DTB 重新组装新镜像。建立绑定门禁：不仅比较容器和 manifest，还要从对应 EFI 模块或可复现构建记录验证编译进固件的 payload/kernel/ADT 哈希与本轮输入一致；不要以旧固件存在或 `exit_code=0` 代替验证。另检查启动菜单配置和 loader 的 payload 文件系统发布路径。
5. 按当前用户授权只临时 `fastboot boot`，不刷 boot/recovery/固件；必要的 misc Recovery 标记仅按已授权范围操作。确认唯一 ADB 服务，停止旧后台监视器。记录镜像 SHA、传输结果和屏幕第一条错误；只有实际看到内核输出或新 boot ID 日志，才能确认内核已运行。

参考路径：`out/p23/ios-16.7.16-20H392/scripts/build-payload.py`、`prepare-uefi.py`、`out/p23/ios-16.7.16-20H392-logfix3/scripts/pack.py`、`.work/p21-official-framework/scripts/patch-xnu-entry-probe.py`。上游构建步骤见 `.work/p24-official-framework/docs/`。不要继续让内核 agent 根据上一轮旧日志修复 logfix3 的启动问题。

## 项目现状

- 设备：OnePlus 5（`cheeseburger`），Snapdragon 835 / MSM8998。
- 项目根目录：`/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger`。
- `main`：iOS 16.7.16 / 20H392 候选；`ipados-17.7.11`：之前在实机显示过时间的 iPadOS 基线。
- 当前 iOS 候选已写入 userdata，并通过 `fastboot boot` 发送；Fastboot 返回 OKAY，**系统是否启动、画面和触摸是否正常尚未确认**。八核、1 GiB 是本候选配置，尚未由该次实机日志确认。
- 保留此前的诊断系统。下次启动前，先让设备返回 Recovery 并读取当前运行日志，再根据第一处错误修复。

## 常用路径

| 内容 | 路径（相对项目根目录） |
| --- | --- |
| 本次候选及其产物 | `out/p23/ios-16.7.16-20H392/` |
| 临时启动镜像 | `out/p23/ios-16.7.16-20H392/uefi/boot-p23-stagec-20h392-8cpu-1g.img` |
| userdata OS / SystemOS 镜像 | `out/p23/ios-16.7.16-20H392/userdata/userdata-root.img` |
| 打包配置、ADT、信任缓存、Data seed | 上述目录下的 `config/`、`adt/`、`trust/`、`data-seed/` |
| 内核构建和预链接 | 上述目录下的 `tree/`、`native/`、`kernel/`、`prelink-final/` |
| 构建脚本和 8796 补丁 | 上述目录下的 `scripts/`、`patches-8796/`、`tools/prelink/` |
| 板级运行时及显示 / 触摸 | `package/BoardRuntime/` |
| 本次部署记录 | `out/p23/ios-16.7.16-20H392/logs/deployment-receipt.json` |
| 之前回收的持久日志 | `out/p21/persistent-logs/`；最近一次旧日志是 `20261027T153847Z/`，并非本次 iOS 候选日志 |
| 状态及历史 | `docs/P23_STAGE_C_STATUS.md`、`docs/P23_HANDOFF_20261027.md` |

生成镜像、Apple 系统输入、签名材料和设备日志保存在本地，未随 Git 发布；部分版本专用脚本和补丁位于 `out/` 下但已单独纳入 Git。当前 `board.toml` 的通用构建入口仍是受保护的 iPadOS 配置，iOS 候选使用 Stage C 专用脚本，尚无经过验证的干净环境一键构建。

## 日志读取

让设备进入 Recovery 并连接电脑，在项目根目录执行：

```sh
.work/p3-tools-35/platform-tools/adb devices -l
CHEESEBURGER_ADB_SERVER=tcp:localhost:5037 /opt/homebrew/bin/python3.13 scripts/logging/read-cheeseburger-log.py
```

设备序列号为 `3b25cc7c`；脚本要求 root Recovery，并检查设备身份及 userdata 几何。它只读取 userdata 中相对偏移 **10 GiB** 的 **32 MiB** 日志窗口，结果存入 `out/p21/persistent-logs/<UTC 时间>/`：`window.bin` 为原始窗口，`retrieval.json` 为读取结果，`decoded/` 为解码结果，脚本也会运行分析器。若没有有效日志头或解码失败，应保留原始文件和错误，结合屏幕输出判断，不能把旧日志当成本次运行的结果。

本次 userdata 镜像写入后曾发现最后 1 MiB 短写，已补齐；设备端全区域 SHA-256 与镜像一致，日志窗口写前写后哈希一致。用户要求不备份该 userdata。只操作用户明确授权的分区，启动镜像使用临时 `fastboot boot`。

## 接手重点

优先读取本次日志并确定启动到哪一步。20H392 的 AMFI 早期初始化入口来自调用链分析，显示、HID、PMGR 和存储绑定也尚待实机验证；不要把预链接成功或 Fastboot OKAY 当成系统启动成功。历史 iPadOS 已显示界面，不代表本 iOS 候选已显示界面。

参考上游：[UniXNU](https://github.com/UniXNU/UniXNU)、[UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng)、[ios_sagit](https://github.com/MC-XiaoXiao/ios_sagit)、[edk2-msm8998](https://github.com/edk2-porting/edk2-msm8998)。

## 2026-10-28：日志与自动返回 Recovery

- 最新测试候选：`out/p23/ios-16.7.16-20H392-logfix/uefi/boot-p23-stagec-20h392-logfix.img`，SHA-256 `03e16a82637798260078a9ff2be002c6b0b68eab22dcc777015f7159f8c9f24a`。内核链接、预链接及载荷解压校验通过；本轮 Fastboot 接受了临时启动，实际运行结果以新日志为准。
- 修复了监视器对 ADB `recovery` 状态的识别、Fastboot 设备序列号固定和失败后的停机行为。`scripts/unattended/monitor.py` 可等待回到 Recovery 并读取持久窗口；传入 `--state` 指向测试前记录的窗口哈希，区别旧日志和新日志。
- 修复 `misc` 的 GPT LBA 单位：本机 UFS GPT 使用 4 KiB LBA，已保存 GPT 中 misc 范围为 `73736..73991`；之前除以 8 的实现会指向错误位置。现在核对 GPT、保留原扇区的非命令字段、回读和同步。用户已明确授权本次修改 misc 的 Recovery 启动标记。本轮在 Recovery 手工预置了 32 字节标记并验证其余 4064 字节未变；原扇区在 `out/p23/ios-16.7.16-20H392-logfix/evidence/misc-before.bin`。
- 原 panic 写盘函数混用了绝对和相对日志偏移，且在 panic 上下文中调用可能阻塞的 UFS 锁。现只在 panic 上下文发布有界文本，由正常日志线程写入；一次成功同步后才设置 BCB。极早期 panic、单核停滞或日志线程/存储锁失效时仍不能保证日志写出或自动返回，不能把链接符号当成实机验证。
- 上一轮回收 `out/p21/persistent-logs/20261028T080257Z/`，窗口 SHA-256 `d54ebc4ea2dd0d2e5b062bde4b33460d6afedbc1ddc22d5787d5be543855a89f`，与更早的 `20261028T074815Z` 相同，不是本轮新日志。最近已解码日志显示 `ROOT stage=1 status=80 mount=-1 auth=0 ro=0`；作为后续内核根挂载诊断线索，不能仅凭它断言根因。
- 对照上游：`upstream/UniXNU/docs/porting/log-capture.md`、`upstream/UniXNU/docs/devices/sheng-unattended-test.md`、`upstream/UniXNU-sheng/package/BootLog.c`。上游 sheng 在存储可用后启动普通线程记录，并在同步后使用其设备专有 PMIC 返回 bootloader；不要复制该板地址到 OnePlus 5。
- 后续 agent 应先读取本次监视输出和 Recovery 回收的新日志，确认启动 ID、rounds、错误和第一处 panic；再修复 8796 内核/根挂载问题。`--no-reset` panic-screen 会停机，不能用于自动回收镜像。保留现有未提交改动及历史候选。
- 第一次日志修复版测试已自动回到 Recovery 并生成新日志：`out/p21/persistent-logs/20261028T080619Z/`，boot ID `0x44584da8`、2 次写入、`write_errors=0`。但日志显示 `CB recovery BCB: failed`；源代码中 UFS 命令过滤器仍只允许日志窗口，故内核设置 misc 未成功，Recovery 返回依赖测试前预置的标记。已进一步将命令过滤放行限定于 misc 第一个 4 KiB 块，后续镜像仍需实机确认 `CB recovery BCB: armed`。

## 2026-10-28：第二版实机验证

- 镜像：`out/p23/ios-16.7.16-20H392-logfix2/uefi/boot-p23-stagec-20h392-logfix2.img`，SHA-256 `ed6e6728c6b005f9ee827edf5925af1a949fb111093952231806c991d3099268`。Fastboot 临时启动返回 OKAY。
- 设备返回 Recovery，回收的新日志在 `out/p21/persistent-logs/20261028T082411Z/`；boot ID `0x458b4f1f`，`rounds=2`、`write_errors=0`，哈希 `2c1e68a43daef194de34f4c3f93699b3194341ec2cece52707d4b3f8a4248c72`。内核记录 `CB recovery BCB: armed`，表明此次 misc 命令写入和回读路径成功。启动前也由 Recovery 预置了标记，且监视器会把返回的 Fastboot 转到 Recovery，因此这次不能单独证明每种异常都能自动返回。
- 系统在约 60 秒记录 `CB owner fail=104 stage=1 fl=1480d001 rd=100004f mnt=-1 sel=ffffffff vm=0 va=0`。请从实际 `msgbuf.txt` 和当前 `CacheAuth.cpp` / 8796 根挂载流程继续内核诊断；尚未证明图形或用户空间启动。
- ADB 目前只运行系统默认的 `tcp:localhost:5037`；`scripts/unattended/monitor.py` 与 `watch-return.py` 默认使用 5037，也可通过 `ADB_SERVER_SOCKET` 选择系统指定的单一服务。持久日志读取脚本的设备客户端默认 5038，当前须设置 `CHEESEBURGER_ADB_SERVER=tcp:localhost:5037`。不要并行启动第二个 ADB 服务。
- 上一 agent 的旧后台 `watch-return.py` 曾自动截获 Fastboot 并把设备切回 Recovery，已停止；之后不要同时运行多个监视器。极早期 panic、日志线程无法运行或 UFS 不可用时仍不能保证持久记录或返回 Recovery。

## 2026-10-28：logfix5 进入用户态（Stage C 打通）

- 镜像：`out/p23/ios-16.7.16-20H392-logfix5/uefi/boot-p23-stagec-20h392-logfix5.img`，SHA-256 `aca1dcf5529abfd5a205bff0e547db50cb1be454e94db61e86c145d2b583eaf6`；内核 `fa5d25a3…`；固件 `b6239b25…`（按本轮 payload 重建，模块扫描证明 payload/kernel/ADT 三个 pin 均编入）。Fastboot 临时启动，设备在 180 s 无人值守复位后回到 Recovery。
- 新日志：`out/p21/persistent-logs/20261028T092213Z/`，boot ID `0x3a739cb1`，rounds=173，write_errors=0，窗口哈希 `c58d6c48…`。日志显示 `load_init_program` 加载 `/sbin/launchd`，随后 SpringBoard、contextstored、bluetoothd、musicrecognition 等用户态进程持续运行到 228 s（复位前）。首次确认 Stage C 根卷 + SystemOS owner union 挂载和用户态启动成功。
- 修复 1（owner 卷 UUID）：官方 `build-xnu-4k-baseline.sh` 会生成 `build/xnu/owner-data-volume.h`，默认把 `XNU_OWNER_CACHE_VOLUME_UUID` 写成 sheng 的 `8846FE78-2910-4297-AF97-D59BB6F8FC16`。20H392 的 SystemOS 是 `96096B28-B6DC-4BC7-9D3A-2632A0FE53F1`。构建时必须
  `export XNU_OWNER_CACHE_VOLUME_UUID=96096B28-B6DC-4BC7-9D3A-2632A0FE53F1`，并确认 `strings kern_exec.o` 中 `8846FE78` 不存在。此前陈旧 `kern_exec.o` 保留了 P21 UUID，导致每次 `CB owner fail=104 ... vm=0`。
- 修复 2（owner boot manifest）：ADT `/chosen/boot-manifest-hash` 是为 AppleImage4 加的 48 字节零占位；上游 `offline-owner-boot-manifest` 的“绝不覆盖既有身份”保护会因此拒绝发布（fail=107）。已在 `IOKitBSDInit.cpp` 中只允许替换**全零占位**，真实 Apple 身份仍拒绝覆盖。
- 修复 3（SystemOS 媒体竞态）：`CacheAuth.cpp` 的 `native_p15_verified_block_volume_device` 对 UUID 匹配做 3 s（60×50 ms）有界重试，覆盖 APFS 卷媒体在 60.204 s 已 ready、60.290 s 匹配为空的时序窗口。
- 修复 4（监视器）：`scripts/unattended/monitor.py` 每 2 s 轮询单一 ADB 服务；`adb get-state` 在 Recovery 下返回 `recovery` 而不是 `device`，已同时接受两者，连上即读日志，不再长 sleep。
- 关键教训：UEFI 固件编译期固化 `CompressedPayload.h`（展开 payload 哈希/长度）与 `P9Expected.h`（内核/ADT 哈希）。任何 payload/kernel 变更后必须重建固件；此前 r15–r22 复用旧固件导致进入 UEFI shell、无内核日志，不是 XNU panic。
- 设备当前：root Recovery（ADB `tcp:localhost:5037`，序列号 `3b25cc7c`）。下一步：在用户授权下用更长/关闭自动复位的候选验证显示与触摸；保持固件与 payload 同步重建。

## 2026-10-28：logfix5u 无人值守复现（SpringBoard 崩溃循环）

- 无人值守重复启动同一 logfix5 镜像：`out/p23/ios-16.7.16-20H392-logfix5u/`，gate `P23_STAGEC_LOGFIX5U_UNATTENDED_PASS`；快速监视器（2 s 轮询、单一 ADB 5037）在设备回 Recovery 后立即读取。
- 新日志：`out/p21/persistent-logs/20261028T094509Z/`，boot ID `0x3a797d89`，窗口哈希 `a30bdd8a…`。结果可复现：launchd/SpringBoard 正常运行，但 SpringBoard 反复崩溃并重启（corpse 计数到 6）。
- 首个用户态故障（BootFaultProbe）：`FIRST UI FAULT SpringBoard: p26 SpringBoard sig5 PC 1a1ea0ca8 LR 1a1ea0ca8 Ef2000000 F0 +6538000 bt 1a8bc8438 1940d0780 19407164c 1a8b79a30 1a8bd41b4 1cbd9d5c8 1aab5c344`；此前还有 `SpringBoard[26] caught waking the CPU 45057 times over ~7 seconds`（违反 45000/300 s 限额）。
- 日志中没有 IOGPU / Adreno / Metal 驱动加载（只加载了 IOSurface），Stage C 尚未移植 P21 基线的 GPU/显示栈。下一步优先：符号化该 PC（用 20H392 dyld shared cache）确认 trap 点，并评估把 P21 的 NativeAdreno + legacy display 栈移植到 Stage C；在此之前 SpringBoard 崩溃属预期缺口。
- 监视器注意：`adb get-state` 在 Recovery 下返回 `recovery`，脚本已接受 `device`/`recovery` 两种值；不要同时运行多个监视器。

## 2026-10-28：logfix8 显示栈启动成功（boot-args 截断修复）

- 镜像 `out/p23/ios-16.7.16-20H392-logfix8/uefi/boot-p23-stagec-20h392-logfix8.img`，SHA-256 `47ce75823aa8e76d86a9d9445d08077e797ab0695f108b5ba0d85d0e006e5ab2`；内核 `59b96f6d…`；固件 `3767d767…`（按本轮 payload 重建，pins 已验证）。
- 新日志 `out/p21/persistent-logs/20261028T102641Z/`，boot ID `0x3ab05e27`，rounds=93，write_errors=0，哈希 `6b78eb7c…`。
- 关键发现 1（boot-args 截断）：`CB args:` 显示设备实际只收到约 512 字节，`fajita-fb-wc=` 之后参数被截断成乱码。旧的显示参数位于 600+ 字节处，因此 `native_mobile_graphics_load_probe` 从未执行（`P23 DISP2 mgf=-1 stage=0`）。重排后显示参数位于 329 字节，全部生效。
- 关键发现 2（显示栈启动）：日志出现 `IOMobileGraphicsFamily` 类注册、`IOMobileFramebufferLegacy::start`、`P23 DISP late: pending=…`、`P23 DISP2 mgf=0 stage=10 meta=0 init=1 start=1 match=1 surf=0`，即 legacy framebuffer 已成功启动。
- 关键发现 3（崩溃点符号化）：SpringBoard 的 `sig5 PC/LR + slide` 在修正 dyld cache slide 后两次启动完全一致 = `FrontBoardServices + 0x58CA8`（`FBSDisplayMonitor` 初始化附近，`BRK #0`）。logfix8 中该 `FIRST UI FAULT` 已消失。
- 剩余问题：SpringBoard 仍有 `caught waking the CPU ~45k times over ~7 s` 的忙等；日志在 146.5 s 突然结束（146.38 s 启动 ReportCrash），设备回到 Recovery。尚未取得这次 abort 的原因（crash report 在 Data 卷，Recovery 读不到）。
- 下一步：把 BootFaultProbe 的 reason-scan / `boot_progress_throw_text` 周期输出到 msgbuf，确认 146 s 的 abort 原因；并评估 legacy framebuffer 的 swap/vblank 是否导致 SpringBoard 忙等。保持固件与 payload 同轮重建。

## 2026-10-28：显示因果对照与 CPU-only 边界（logfix18–logfix26）

- 已确认修复（保留）：`offline-legacy-pm=0` 解除 backboardd 首次 `newUserClient` 阻塞（logfix18：两次 newUserClient 成功并进入 getDotPitch）；20H392 owner UUID、零占位 boot-manifest-hash、SystemOS 媒体重试、固件随 payload 重建均已生效。
- 已排除的非原因：swap（可用 P21 基线同样 `Failed to open swap file 30` 仍出界面）、vsync 定时器、单核/多核、日志周期（1 s/10 s）、panic 路径 UFS 写、guard 槽位（无 `P23 DISP GUARD`）、scanout 映射 WriteCombine/InhibitCache。
- 因果对照（logfix25/26）：`transfer=0`（无逐帧拷贝）仍在 ~141 s 卡死；完全关闭 legacy 显示（无 graphics-load/register）可活到 ~252 s。说明**显示服务存在本身**触发早期卡死，且与逐帧拷贝无关。
- 无显示长跑（20261028T124613Z）暴露用户态 SIGTRAP 潮：`SpringBoard/donotdisturbd/triald/liveactivitiesd sig=5` 反复出现 → corpse 满 → 最终 pon1。这与 iOS 16 渲染/Metal 依赖和我们的 CPU-only 显示实现都相关。
- 结论：CPU-only + legacy framebuffer 在当前实现下无法稳定到出帧；上游能出界面的 Sheng 基线使用 NativeAdreno/IOGPU 合成。若要继续追 UI，需要（A）恢复 GPU 栈移植，或（B）重新设计用户态渲染旁路（不注册 framebuffer/不触发 renderer）。两者都是新阶段工作，需用户决策。
- 设备当前：root Recovery；最新候选 logfix26（`908c8570…`），日志 `20261028T124613Z`（哈希 `f9aeb3ff…`）。

## 2026-10-29：panic 停机 + 倒计时复位 + 显示客户端 UAF 修复

- **panic 停机（另一位 agent）**：`board_panic_reset` 的 PSCI 重启用 `#if 0` 屏蔽，置 `cb_panic_parked` 后 `wfe` 停住；`cheeseburger-panic-hold=20` 仍先保留现场。
- **倒计时修复（已实机验证）**：`unattended_reset_thread` 原先在 `cb_panic_parked` 时直接 `return`，导致 panic 后 300 s 倒计时不再重启。现改为提示 `CB watchdog: panic parked; countdown reset` 后照常 PSCI。logfix31 日志 `20261028T164206Z`：145.7 s panic 停机，设备约 356 s（倒计时结束）自动回 Recovery。
- **logfix30 无效启动**：手工重建内核时漏 `XNU_OWNER_CACHE_VOLUME_UUID=96096B28-B6DC-4BC7-9D3A-2632A0FE53F1`，`owner-data-volume.h` 回退为 sheng 值，日志出现 `owner fail=104 stage=1`（仅 43 rounds、无用户态）。之后每次 `stagec-build.py final` 必须先导出该变量并检查 `strings kern_exec.o` 无 `8846FE78`。
- **145 s IOKit data abort 根因（已修复）**：logfix31 日志 `20261028T163133Z` 首次拿到完整寄存器：崩溃对象正是 client #1（与 `P23 DISP client obj=` 记录同址），对象头被写成 `0xfffffff000000000`（高 32 位保留、低 32 位清零），`far=0xfffffff000000038`，`pc=safeMetaCast+0x18`、`lr=iokit_remove_connect_reference+0x24`。来源是 `NativeLegacyDisplay::supportedFrameInfo` 对调用方传入的 `count/mask` 指针写 0（20H392 下这些指针不是内核缓冲），随后同一次 trap 结束时 `OSDynamicCast` 读到损坏 vptr。现在该函数不再写任何输出，直接返回 `kIOReturnUnsupported`；`getDotPitch` 也去掉了无条件 `*pitch=0`。logfix32 日志 `20261028T165238Z`：145 s 崩溃消失，系统跑到 345 s。
- **尚未解决 1**：logfix32 在 336.8 s 出现 `mbuf_mtypes_sync+0x78` data abort（`far=0xffffffec0b8c6b6f`），发生在 SpringBoard 反复 sig=5、84 次 corpse、内存压力很大的阶段；与显示客户端修复无关，尚未定位。
- **尚未解决 2（UI 阻塞）**：用户态 SIGTRAP 潮（SpringBoard/contextstored/triald/biomed/donotdisturbd sig=5）。已查上游：UniXNU 只支持 iPadOS 17，无 iOS 16/SIGTRAP 案例；本机 P21 基线（无 GPU/无 IOGPU，CPU 显示）有 `IOMFB: default_fb_surface: No Default FB Surface found` 但仍正常出界面且 SpringBoard 零退出，故缺 GPU 与这条 IOMFB 消息都不是原因。`boot_progress_exit_fault` 已增强：按进程 dyld 镜像列表解析 `PC` 到 `img=<path>+0x…` 并附带 `x0` 消息（logfix33），用于定位断言点。
- **候选与日志索引**：logfix31 `a0a61d9a…`（倒计时验证）；logfix32 `1bcf1b0d…`（supportedFrameInfo 修复，`20261028T165238Z`）；logfix33 `52ccdc76…`（fault 探针增强，见其后日志）；logfix30 `3cae7b16…` 无效（owner UUID）。

## 2026-10-29：keybag/SIGTRAP 定位与 `-apfs_shared_datavolume` 回退

- **SIGTRAP 断言点（已定位）**：SpringBoard 首个 `brk #1`（ESR `0xf2000001`）唯一落在 **`MobileKeyBag` `_MKBEventsRegister.cold.2 + 0x2c`**；同栈为 SpringBoardFoundation/libdispatch/SpringBoardUIServices。定位方法：用 16K 对齐 + “该地址必须是 BRK”反解 slide；`task_shared_region_slide` 直接相减会错落到 WallpaperKit（此前 BootFaultProbe 的 `+slide` 不可信）。triald/biomed/donotdisturbd 同一时段也 sig=5。
- **AKS 不是该断言路径**：20H392 donor kernelcache 不含 `com.apple.driver.AppleKeyStore`（P21/17.7.11 有并在 66 s 注册），`sheng-aks` 计数全为 0；即使把 `sheng-aks` 提前进参数窗口（logfix34）也仍 sig=5。
- **上游案例**：`upstream/UniXNU/scripts/probe-keybag-bypass.py` 显示无系统 keybag 时要用 `-apfs_shared_datavolume` 且 Data 卷存在 `keybags/backup`，keybagd 才走白名单而非致命路径。我们的 seed（`scripts/build-data-seed.py`）已创建 `keybags`、`keybags/backup`。
- **实测失败（logfix35）**：加入 `-apfs_shared_datavolume`（参数 470 字节）后，启动在日志线程启动前停止——监视器读到窗口哈希未变（`40cef886…`，与上一轮相同），300 s 倒计时也未执行（说明 PMGR 启动前/期间即失败），设备约 386 s 后才由硬件看门狗回到 Recovery。该参数与当前 owner/scratch Data 布局不兼容，已回退；logfix36 用回退参数（486 字节）复核。
- **探针修复**：logfix33 的 fault 探针在追加 `base/dyldslide` 后发生 `capacity-n` 下溢，导致 logfix34 没有 `FIRST UI FAULT` 输出；已改为局部缓冲 + 剩余空间检查（logfix35 起）。
- **待解决**：MKB 断言的根因（无系统 keybag / AKS 缺失 / 服务注册），需要不依赖 `-apfs_shared_datavolume` 的路径；以及 keybag 相关 daemon 的 sig=5 潮。
- **候选索引**：logfix33 `52ccdc76…`（probe 首版，越界）；logfix34 `bbf62f0f…`（AKS 入窗，仍 sig=5，无 FAULT 输出）；logfix35 `b2568888…`（`-apfs_shared_datavolume`，无日志、未倒计时，已弃）；logfix36 `480e2c2e…`（回退参数 + 探针修复，待验证）。

## 2026-10-29：复位链根因与加固（logfix39）

- **为什么 logfix35/37 没自动重启**：
  - logfix35 加了 `-apfs_shared_datavolume`，内核在日志线程/PMGR 之前就停住（窗口哈希不变，倒计时未执行），约 386 s 后靠硬件看门狗回 Recovery。该参数与 owner/scratch Data 布局不兼容，已弃用。
  - logfix37（恢复了 `p20-observe=1` 但删掉了 `sheng-log=1`）：持久日志写入器根本没启动，`20261028T180511Z` 与 logfix36 的 `175149Z` 窗口哈希、boot_id、rounds 完全相同（零写入）。**348.6 s 的 panic 记录属于 logfix36（175149Z）而不是 37**；`cpus=1` 下该 panic 把唯一 CPU 停在 hold/park，看门狗线程再也不会被调度，永远无法倒计时复位。
- **结构性缺陷（子代理审计，已确认）**：`board_panic_reset` 的 PSCI 仍被 `#if 0` 屏蔽 → panic 必然永久停机；`cpus=1` 下倒计时线程与 panic 共用唯一 CPU；倒计时还要求 `boot_log_commits` 与 BCB 已 arm，日志一坏就不复位；复位前还会写 UFS note（`rawWrite4k` 可能阻塞）。另有 0–49.7 s（PMGR 起来前）与 `cheeseburger_pmgr_startup` 内 parse/PE 赋值前后的无复位窗口。
- **修复（logfix39）**：
  1. `board_panic_reset` 恢复 PSCI：保留 `cheeseburger-panic-hold` 的现场停留，hold 结束后循环发 SYSTEM_RESET；不再写 UFS note。
  2. 倒计时不再因日志/BCB 缺失而 return（仅打控制台提示），且去掉了复位前的 UFS note。
  3. `cheeseburger_unattended_reset_start` 成功启动线程后才置 `started`；并在 `bsd_init` 的 `p20_observer_start` 旁提前调用一次（PMGR 未起时也能有倒计时）。
  4. 新增 `scripts/unattended/preboot-gate.py`：启动前直接检查打包镜像里的参数（<500 字节、必须含 `cheeseburger-unattended-reset`、`cheeseburger-panic-hold`、`sheng-log=1`）、内核符号、panic 路径必须含 PSCI、倒计时代码不得依赖 log/BCB、payload/kernel pin 与固件 pin 日志一致。logfix39 已通过：`PREBOOT_GATE_PASS`。
- **仍存在的限制**：PMGR 启动前（约 0–50 s）的 panic 没有内核内复位路径；`cpus=1` 时 panic hold 期间写不出持久日志（只能看屏幕）。
- **候选**：logfix38 `5919a6dd…`（复位链修复的第一版，未启动；HANDOFF 早前误写为 480e2c2e）；logfix39 `af8e8e24…`、内核 `d3b1bcdd…`、固件 `b1f40440…`（20261028T182442Z：无内核 panic，SpringBoard 断言在 `FBSDisplayMode _initWithWidth:…validityCheck:+0x244`，倒计时 355 s 正常回 Recovery——只验证了倒计时路径，panic→PSCI 尚未被实机触发）；logfix40 `825013c8…`（`cpus=8`）；logfix41 `9ffd0b50…`（按 P21 基线去掉 `fajita-display-logical`，恢复 `validate/cpu-present/unit-scale=1`）。

- **证据更正（子代理审计）**：logfix33 目录内镜像的 ramdisk 与 manifest/固件 pin 不是同一组（`52ccdc76…` 镜像内 payload `255d37e5…`，manifest/固件却是 `efe05eee…`），且缺 `check-firmware-pins.log`，不能作为可复现候选；33–38 均缺 pins 日志，39/40/41 已保存。日志索引补充：`out/p21/persistent-logs/20261028T182442Z`（logfix39，boot `0x3a12cd3b`，60 rounds，无内核 panic）。

## 2026-10-29：SIGTRAP 消除与 keybag 用户会话卡住（logfix42–44）

- **SIGTRAP 潮已消除（logfix42）**：把 20H392 真正的 keybag kext（`com.apple.driver.AppleSEPKeyStore`、`com.apple.driver.AppleSEPCredentialManager`，依赖 `AppleSEPManager`）加入 prelink roots 后，`sig=5` 从约 100 次降为 **0**，SpringBoard 不再退出，日志出现 `AppleSEPKeyStore ... starting`、`_sep_enabled = 1`，本轮无内核 panic。倒计时 358 s 正常回 Recovery。
- **新的阻塞**：用户态在 **143.8 s 完全静默**（之后只有内核 `P23 ALIVE`），`DISPLAY stage=10 reg=1 clients=0 frames=0`，即 backboardd/SpringBoard 从未建立显示客户端。采样符号化显示 SpringBoard 主线程阻塞在 `_CFPREFERENCES_IS_WAITING_FOR_USER_CFPREFSD`（libxpc `_dispatch_mach_send_and_wait_for_reply`），进程采样同时显示 keybagd(pid44)、usermanagerd(pid82)、SpringBoard(pid25) 用户线程全部 WAIT —— 用户会话没有激活。
- **`sep-booted=1` 无效（logfix43）**：`_sep_enabled` 仍为 1，仍在 143.8 s 卡住；该 boot-arg 不能替代真实 SEP 端点。
- **logfix44 诊断扩展**：线程栈采样原先只覆盖 chronod/SpringBoard/backboardd（role S/B/C），现增加 keybagd（role K）与 usermanagerd（role U）：`StartupWaitProbe.c` 支持 role 0–4、WaitRows 600；`BootProcessProbe.c` 对 index 6/14 采样。目的：确认 keybagd 到底卡在哪个 XPC / io_connect / mach_msg。
- **候选索引**：logfix42 `f58ce2c9…`（SEP key store，SIGTRAP 归零）；logfix43 `fe135832…`（`sep-booted=1`，无效）；logfix44 `a6804fc9…`、内核 `c6b4fc96…`（K/U 栈采样）。

## 2026-10-29：用户会话卡在 logd bootstrap（logfix45）

- **K/U 栈（已修正导出与下标）**：`StartupWaitProbe` 现在输出 600 行，`BootLog.c` 同步导出 600 行；usermanagerd 的 targets 下标是 13（此前误用 14=logd），因此 K/U 字母与进程对应关系是反的：`STK K p82` 实为 usermanagerd，`STK U p44` 实为 keybagd。
- **阻塞点（符号化，slide 0x3f3bc000）**：
  - keybagd(pid44)：`__os_trace_get_logd_port` → `__bootstrap_create_request` → `__xpc_pipe_routine` → `mach_msg`（等 launchd 回答 logd 的 bootstrap 查询）。
  - usermanagerd(pid82)：`_firehose_client_reconnect` / `_firehose_send_register` → `__os_trace_init_slow`（同样在等 logd firehose）。
  - 两者用户线程数都只有 1 且 WAIT；P21 基线同一阶段 keybagd 有 3 线程、logd 8 线程。
- **推断**：launchd 在同步执行 keybag/usermanagerd boot task 时无法应答 bootstrap 查询，导致 daemon 在第一次 os_trace 初始化时永久等待；`logd_helper` 在 121 s 还出现“3 秒 45057 次唤醒”的忙等，单核调度饥饿会放大该问题。
- **logfix46**：启用真正的 SMP（`sheng-smp=1 armcpu-self-ipi=1`，从参数中去掉 `offline-hid-input=1` 以保持 494 字节），验证 7 核 online、`cpu_signal failed` 是否消失、以及 keybagd 是否仍卡在 logd bootstrap。

## 2026-10-29：SMP 验证与 launchd/logd 栈（logfix46–47）

- **SMP 已验证（logfix46）**：参数 `sheng-smp=1 armcpu-self-ipi=1` 后 8 核全部 online（CPU7 mpidr=103 step=7），`cpu_signal failed` 为 0，`sig=5` 仍为 0，无内核 panic，倒计时 354 s 正常回 Recovery。结论：多核本身可用，不是用户会话卡住的原因。
- **卡住仍在**：用户态在约 139.5 s 后静默，`DISPLAY clients=0 frames=0`；keybagd/usermanagerd 各只有 1 线程且 WAIT；本轮 logd 也只有 1–3 线程且全部 WAIT（P21 基线 logd 有 8 线程）。
- **logfix47**：把栈采样目标从 keybagd/usermanagerd 换成 **launchd(index 0→role K)** 与 **logd(index 14→role U)**，用于确认是否是 launchd 在 boot task 期间无法应答 bootstrap，导致所有 daemon 在 os_log/os_trace 初始化时永久等待。

## 2026-10-29：用户会话停滞的当前结论（logfix47）

- **已验证可用**：8 核 SMP（`sheng-smp=1 armcpu-self-ipi=1`）全核 online、无 `cpu_signal failed`；`sig=5` 为 0；无内核 panic；倒计时 354–358 s 稳定回 Recovery；`preboot-gate.py` 每次启动前通过。
- **仍停滞**：用户态在约 139 s 后无新日志，`DISPLAY clients=0 frames=0`，backboardd/SpringBoard 未建立显示客户端。栈采样显示：launchd 在 mach_msg 事件循环（正常），logd 后期也在 mach_msg（正常），早期一次采样落在 dyld 初始化；keybagd/usermanagerd 只有 1 个线程且在第一次 os_log/os_trace 初始化处等待。P21 基线同一阶段 keybagd 3 线程、logd 8 线程、SpringBoard 132 s 启动、backboardd 147 s 启动、155 s 出现 IOMobileFramebufferUserClient 显示类型。
- **尚未证明的假设**：a) launchd 在同步 boot task 期间无法应答 bootstrap；b) usermanagerd 的 persona/会话建立在 scratch Data 上失败（上游 QEMU 探针也把“persona/session on empty Data”列为下一下阻塞）；c) `-apfs_shared_datavolume` 需要 Data 与 root 同 APFS 容器，而当前 owner union 不满足（该参数此前会导致日志线程前停机）。
- **下一步候选**：① 采样全部 24 个目标进程的栈（当前只有 5 个角色）以找出真正持有资源的进程；② 对照 P21 的 launchd 配置/会话激活路径，确认 20H392 缺的 boot task；③ 评估把 scratch Data 改成与 root 同容器的 shared data volume（结构性工作）。
- **设备状态**：root Recovery，ADB `tcp:localhost:5037`，序列号 `3b25cc7c`；无监视器在跑；最近日志 `20261028T195205Z`（logfix47，窗口 `0c3dbfce…`）。

## 2026-10-29：B（shared data volume）阶段一——主机侧新容器已就绪

- **userdata 现有布局（从 SHNGROOT 头解析）**：imageBytes `0x1F3800000`（7.81 GiB）；`OS` 窗口 offset `0x100000`、size `0x14FE08600`；`SystemOS` offset `0x150000000`、size `0xA3800000`；持久日志窗口固定在 userdata 相对 10 GiB（`0x280000000`，32 MiB）。当前 root 卷**不在 volume group**（APFS 日志明示）。
- **APFS 约束（kext 字符串）**：`could not find volume with role 0x%x in (container/volume group)` —— root 无组时按**同一容器**查找 Data 卷。macOS 的 `diskutil apfs addVolume -groupWith` 在该 iOS 容器上失败（`-69624`），但**同一容器内新增 role=Data 的卷成功**。
- **新容器（仅主机侧，未写设备）**：`out/p23/ios-16.7.16-20H392/apfs-group/os-writable.dmg`，12 GiB，GPT+APFS；OS 卷 UUID 保持 `DADD93F8-DBBF-4B17-A1DF-0F11E64172F7`（role System），新增 `DataTest` 卷 `2F1E7AD5-9E78-4F90-B05B-59238A091A98`（role Data），已创建 `keybags/backup`。窗口 SHA-256 `66372f41…`，见同目录 `receipt.json`。
- **设备侧阻塞点**：NativeUfs 的 verified-root 策略把原 OS 窗口的 `image_bytes` 与 `image_sha256` 签名固化（`configureVerification` 要求 `verified.image_bytes==bytes` 且哈希等于 SHNGROOT 记录）；新 12 GiB 窗口必须先重建该策略（`scripts/p12-build-policies.py`）并重新嵌入内核。
- **下一步（按序）**：①用 `p12-build-policies.py` 为新窗口生成签名策略；②窗口 override 用 boot arg（不改 SHNGROOT；`logSafe` 判定需允许窗口位于日志窗口之后且不重叠）；③把 12 GiB 写到 userdata 空闲区（拟 16 GiB 起，已授权），写后回读校验；④先不带 `-apfs_shared_datavolume` 验证 root 正常挂载，再带该参数验证 Data 配对与 keybag allow-list。

## 2026-10-29：B 阶段二——分组容器内核集成与待执行写入

- **内核改动（logfix48）**：`NativeUfs` 新增 boot arg `apfs-group=1`：把 `OS` 窗口重定位到 userdata 16 GiB、12 GiB（`imageBytes` 28 GiB），不改 SHNGROOT；`logSafe` 改为只要求窗口不与 10 GiB 日志窗口重叠；该模式下跳过 OS 窗口的 owner 逐块策略（`configureVerification` 直接返回，原策略库不变），因为 12 GiB 清单（100 MB）放进内核会触发 arm64 BL ±128 MB 超限、放进 `__PLK_DATA_CONST` 又与 prelink 的“该段必须为空”冲突。
- **参数**：`-cheeseburger-touch=1 +apfs-group=1`，486 字节；`PREBOOT_GATE_PASS`（镜像 `aa34b564…`、内核 `b840680f…`、固件 `7e7938d5…`）。
- **待人工执行（自动模式禁止裸块写入）**：
  ```sh
  cd /Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger
  head -c 12884901888 out/p23/ios-16.7.16-20H392/apfs-group/os-writable.dmg | \
    .work/p3-tools-35/platform-tools/adb shell 'dd of=/dev/block/sda14 bs=1M seek=16384 conv=notrunc'
  /opt/homebrew/bin/python3.13 scripts/p23-verify-group-window.py --hash
  ```
  写入范围 userdata 16–28 GiB，与 SHNGROOT（≤8.17 GiB）和 10 GiB 日志窗口不重叠；目标区当前为 0xFF/零、无文件系统；SHNGROOT 头已备份到 `apfs-group/evidence/userdata-header-20261029.bin`；期望回读 SHA-256 `66372f41…`。
- **写入后**：直接 `fastboot boot` logfix48 镜像，预期日志出现 `NativeUfs: apfs-group window override active`，root 从新容器挂载；随后再带 `-apfs_shared_datavolume` 复验 Data 配对/keybag allow-list。

## 2026-10-29：B 阶段三——分组容器已写入并校验（logfix48 启动）

- 操作者手动执行了 12 GiB 写入：userdata `/dev/block/sda14` 偏移 16 GiB、长度 12 GiB（`dd bs=1M seek=16384`），来源 `apfs-group/os-writable.dmg`。
- 回读校验（`scripts/p23-verify-group-window.py --hash`）：GPT 保护 MBR、GPT 头、APFS `NXSB` 均正确；12,884,901,888 字节全窗口 SHA-256 = `66372f415b1cb1183ae2fc6d06698aea8f9cc7cd1de8fd0d12b6369060707df1`，与签名清单一致。校验脚本此前误报是因为只读了 8 KiB（分区起点在 0x4400），已改为按 GPT 条目动态定位。
- logfix48（镜像 `aa34b564…`、内核 `b840680f…`、固件 `7e7938d5…`、`apfs-group=1`、486 字节、`PREBOOT_GATE_PASS`）于 03:03:24Z 临时启动。待确认：`NativeUfs: apfs-group window override active`、root 从新容器挂载、以及同容器 Data 卷是否被 APFS 识别。

## 2026-10-29：panic 现场保留时间调整（logfix50）

- 现象：logfix48 在 root 身份阶段失败后 panic，约 20 s 内即重启，持久日志没写入 panic 文本；窗口末尾 note 块仍是旧的 `owner fail=104`（说明 panic 路径的 `boot_log_panic_flush_text` 未成功落盘）。
- 改动：①`cheeseburger-panic-hold` 由 20 提升到 **60 s**；②`cheeseburger_unattended_reset_start()` 里也解析 `cheeseburger-panic-hold`（bsd_init 早期调用，PMGR 之前的 panic 也能保留现场）；倒计时复位不变。
- logfix50：镜像 `f0ed5571…`、内核 `55ffd756…`、固件 `c80043d8…`，同时包含 root 身份阶段的新诊断（`NativeUfs: identity …`、窗口 offset/size 列表），03:10:30Z 启动。

## 2026-10-29：group 窗口身份与读路径（logfix52–54）

- logfix52（panic 停机 + `/chosen` 取 UUID）：`identity OS uuid DADD93F8…` 通过，但仍 `Waiting on IOMedia UUID DADD93F8…`，109.8 s `Failed to mount root device`。panic 后停机、300 s 倒计时在 353 s 复位并完整回收日志（确认停机策略可用）。
- logfix53（读路径全局旁路）：`identity OS` 通过，但 `identity SystemOS volume uuid unavailable` → 因为 `configureVerification` 的跳过是全局的，把 SystemOS 的签名策略也跳过了。
- **logfix54 修复**：策略跳过与直读旁路都只作用于 label 为 `OS` 的窗口，SystemOS 继续使用签名策略；`Operations` 增加 `plain` 标志。待实机验证 root 是否能从新容器挂载。

## 2026-10-29：root 已从分组容器挂载（logfix54/55）

- logfix54：`identity OS` 与 `identity SystemOS` 均通过；日志出现 `Got boot device = … /AppleAPFSContainerScheme/AppleAPFSMedia/AppleAPFSContainer/SydneyUpdate20H392.D22D221OS@1` 与 `apfs: mountroot called!`、`mounting volume SydneyUpdate20H392.D22D221OS` —— **root 卷确实从新的 12 GiB 分组容器挂载成功**。
- 新阻塞：50.26 s `panic: rootvp not authenticated after mounting @bsd_init.c:1081`。该检查要求 `native_verified_block_root_authorized(rootdev)`，而 group 窗口没有 owner 逐块策略，必然为 0。
- 修复（logfix55）：board runtime 增加 `native_apfs_group_window`（仅 `apfs-group=1` 置位）；`bsd_init.c` 在 panic 前对 group 模式置 `owner_authorized=1` 并记录阶段文字。其余身份/只读/设备检查保持不变。

## 2026-10-29：owner 缓存第二次 root 校验（logfix55/56）

- logfix55（group root 接受）：`rootvp not authenticated` 消失，但 52.6 s 变成 `CB owner fail=103 stage=0`（`native_owner_cache_prepare` 里第二次 `native_p15_verified_block_root_authorized`）。本轮 panic 文本完整写入（`rounds=60`），停机+倒计时按预期在约 353 s 复位。
- logfix56：在 `kern_exec.c` 的 103 判定处同样按 `native_apfs_group_window` 放行（其余 101/102/104 与后续挂载检查不变）。

- logfix56：103 已放行，接着 `CB owner fail=106 stage=6 mnt=0 sel=1000043 vm=1 va=1` —— 来自 `native_owner_cache_prepare` 末尾的 `root_recheck`（再次要求 verified root）。logfix57 把 `native_apfs_group_window` 并入该判定（`root_authorized && (verified || group)`）。
