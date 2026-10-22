# P6 精确续接：已试一次新镜像，设备通信中断

项目 `/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/`。最终系统目标为 **iPhone iOS**；没有指定/下载 IPSW，不能沿用 sheng 的 iPad 身份。原任务在 `docs/P6_TASK.md`，附件原文在 `evidence/p6/request-original.txt`。

## 首先处理现场

用户已从 P5 停机恢复 Android，P6 控制器确认过连接。随后已执行一次 **P6-v1** 临时启动：

- run_id `20261022T020508Z-20037c90`
- image `out/p6/uefi/boot-p6-xnu.img`
- SHA256 `af72619bc49b1dd7b7942259779ae225639a8dd1facd0fe91b7d3369e941c0c3`
- Fastboot 接受，观察 240 秒后无 USB/ADB/Fastboot；控制器退出 2，结果 `NO_TRANSPORT_MANUAL_RECOVERY`。
- `out/p6/device-loop/20261022T020508Z-20037c90/summary.json`、`events.jsonl`、private 原始命令日志已保存。
- **已向用户请求本轮屏幕照片/色条描述，尚待回复。不要重复提问或重发镜像。** 保留现场后需用户长按电源恢复 Android。

没有 P6 屏幕证据前，不能声称 S01/S03/S05 已执行。P5 已有人工观察级别的 __start 证据；本轮尚未证明 MMU/C 初始化。没有自动日志回收或自动恢复 EBS 后停机的能力。

## 已完成的真实开发

1. 唯一 XNU 工作树 `.work/p4-unixnu/.cache/xnu-work` 修改 start.s，保留 P5 __start 品红条，新增 S01 黄、S03 青、S05 白，三个 MMU 前检查点。
2. 原早期 LowExceptionVectorBase 的 16 个向量转到无栈 fault 记录器，保存 ESR/FAR/ELR/SPSR/SP0/SP1/LR/SCTLR/TCR/TTBR/MAIR/BootArgs；MMU 关闭时显示红条与 ESR/ELR/FAR 三行 hex。覆盖限制见 `P6_MMU_DIAGNOSIS.md`，不能当成全启动期异常捕获。
3. `link-kernel-03` 真实增量链接、HMAC sealing、导出校验通过（约23秒），最终 kernel.macho 19277552 字节，SHA256 `b6c5ce0062a6318d1926ac06cd017d0f2afaf757d125f22d645b55b46ee89baf`。
4. P5 ADT 内容不变，13616 字节，SHA `cec5013d014ea9a5c659a388691ab6446e889a43a4e0111e24a5f0e0075f0369`。P6 重跑实际 DarwinBoot 解析器成功。
5. P6 固件独立 overlay/Build 真实编译通过；RAM bundle 包含新 kernel、ADT、UniXNU 配置，期望 SHA 重新编入 P6Probe/DarwinBoot，回读和 FV/PE 核对通过。
6. `probe-emulation-06` 执行最终 ARM64 指令验证色条、寄存器保留、fault 记录、零 FB 负例和 SCTLR guard。`bootstrap-emulation-03` 执行实际页表构造，检查正例及错位负例；均为主机合成输入，未模拟成功开启 MMU，不能替代实机证据。
7. `execution-map-01` 使用实际目标编译参数提取常量并与最终 Mach-O 核对。TCR `0x22a51a251a`，SCTLR `0x3454593d`，4KiB/2MiB L2、16页池。
8. 原 P5 kernel/ADT/image/补丁及只读副本哈希均再次通过；`out/p6/baseline-check.json`。P1–P5 未删除，P3 原诊断工程未修改。

## 文件与环境

- 交付：`P6_EARLY_BOOT_REPORT.md`、`P6_MMU_DIAGNOSIS.md`、`P6_PLATFORM_PROGRESS.md`、`P6_IOS_ROOT_PREP.md`。
- 源码补丁 `patches/p6/early-diag-v1.patch`，本次精确源码 `evidence/p6/start-linked-v1.s`；来源 commit 见 `out/p6/sources.json`，产物 SHA 见 `out/p6/artifacts.json`。
- 全部编译/测试日志 `out/p6/logs/`；失败日志保留。早期宏错误、模拟器夹具问题和真实零FB绘制缺陷均已修正。完整模拟器MMU夹具 probe-emulation-05 失败，之后只通过分支单测，不宣称 MMU通过。
- macOS arm64 16GiB，Xcode27.1，P4 固定 LLVM/GNU make；XNU -j2。唯一源码树不再克隆。
- Linux VM **已经停止**（stop-vm-01 退出0），没有后台编译或设备轮询。P6 overlay位于项目Lima磁盘内 `/home/dinnerabc.guest/unixnu-p6/source`，独立Build。需UEFI编译才启动VM，XNU编译前停止。
- Pinned platform-tools35，ADB5038；设备序列号在 `out/p3/private/selected-device.json`，不要打印到公开文档。
- Unicorn2.1.4 通过直连隔离安装于 `.work/p6/pythondeps`，需项目Python3.12与授权运行JIT；源码/下载/缓存都在项目内。

## 后续按证据迭代

先收到本轮照片/描述，更新 `out/p6/stages.json` 并把证据存入 `evidence/p6/`，记录对应镜像 SHA，不能仅凭超时推断卡点。

- 只见S00：优先看 OSLAR / BootArgs / Low VBAR 阶段，使用红条数字；没有红条也不能排除向量未执行。
- 到S01但没有S03：定位栈地址准备、页表清空/边界检查及构造。
- 到S03但没有S05：定位 TCR/TTBR/MAIR 与 VBAR 设置。
- 到S05：优先验证 MMU切换窗口；当前正常异常向量KVA在开启MMU前安装，新增Low recorder不覆盖后续全路径。先设计并主机验证可跨该窗口的最小诊断，再增加S06/S09；禁止直接用物理FB做MMU后输出。

从 Android 恢复后只读 detect；相同失败 image 已记账，禁止绕过计数重测。只有实质源码变化、新SHA、门禁通过才考虑下一轮授权范围内的临时启动。

## 复用入口（显式以项目为工作目录）

- `python3 tools/p6/device_loop.py detect`：只读检查，USB需要授权环境。
- `python3 scripts/p6run.py <全新标签> -- <命令>`：保存完整输出、命令及退出码，不覆盖旧标签。
- `bash scripts/p6-link-kernel.sh`：停止VM后真实增量链接，输出只到out/p6；不要为无源码变更重复执行。
- `python3 scripts/p6-check-adt.py out/p6/adt/kernel.macho.adt`
- `python3 scripts/p6-build-payload.py`：kernel变化后重新生成期望哈希，随后必须同步源码并重新编UEFI，不能直接配旧固件。
- `bash scripts/p6-guest-overlay.sh`、`python3 scripts/p6-prepare-uefi.py` 在既有VM内执行；具体实际参数见 logs/prepare-overlay-01.json、prepare-uefi-01.json。
- `python3 scripts/p6-pack-image.py`：已导出固件的RAM镜像打包/回读。

p6-prepare-kernel.py 是从 P5 应用 P6 补丁的一次性入口，当前已应用，重复运行会明确停止；后续改动需同步生成器、补丁和精确源码快照。设备控制器复用冻结P5源码并校验其SHA，仅在内存映射输出/config路径到P6，没有修改P5。

未执行刷写、擦除、解锁、GPT/Bootloader/校准分区修改。UFS根设备、APFS、iOS ABI、Trust Cache及launchd均未验证；不具备持久化刷写条件。
