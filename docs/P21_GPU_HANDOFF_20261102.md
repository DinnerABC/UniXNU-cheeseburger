# P21 / OnePlus 5 GPU 交接（2026-11-02）

## 接手后先做：阅读与执行顺序

**不要只读本文或依赖上下文摘要。每次接手、压缩上下文后都重新阅读以下资料，并以本轮源码和证据核实：**

1. 仓库 `AGENTS.md` → `HANDOFF.md` 顶部最新记录 → 本文；检查 `git status --short --branch`、最近提交和实际设备状态。历史章节只是历史，以最新记录为准。
2. `docs/P21_LOCAL_BUILD_GPU_HANDOFF_20261102.md`：本机增量构建、NAS 回写、固件 overlay、门禁及启动流程；再看 `docs/P21_BLACK_SCREEN_NEXT_STEPS.md`：零帧和线程栈的读取与证据边界。
3. `docs/P21_GPU_USERSPACE_INTEGRATION_20261102.md`、`docs/P21_OFFICIAL_MIGRATION_DIFF_20261101.md`：当前插件、用户客户端和官方迁移差异；`docs/P21_OFFICIAL_WORKFLOW_AUDIT.md`、`docs/P21_OFFICIAL_ADAPTATION.md`：固定上游及整体启动链。
4. `.work/p21-official-framework/AGENTS.md` 和 `.work/p21-official-framework/docs/porting/{gpu,display,uefi-firmware,migration}.md`；查看 `.work/p21-official-framework/drivers/AdrenoMetal/NativeAdrenoMetalPlugin.mm` 的初始化与异步测试，再对照本机 `package/Userland/native_adreno_metal_plugin.mm`。硬件细节再读 `docs/upstream/reference/sagit/QcomGPU/AdrenoStartup.h`、`AdrenoSubmit.h`。
5. 实际读取本轮 `out/p21/persistent-logs/20261102T143713Z/retrieval.json`、`decoded/cheeseburger-P21-5856c881/latest.txt`、`msgbuf.txt`、`heartbeat.json`，与能出帧的 `out/p21/persistent-logs/20261101T172951Z/` 及崩溃前轮 `20261102T135923Z/` 比较；不要只依赖自动概览。

**先恢复画面，再并行推进 GPU，不把二者混为一件事：**

- 画面：本轮 backboardd 存活但 `frames=0 / swap=0 / notify72=0 / vsync arm=0`。对比成功轮对应的显示客户端、服务和线程栈，确认实际等待点、插件 nil 返回后的发现路径和 Data `state=3` 的影响；找到有源码依据的差异后进行功能修复。`state=3` 的完成状态要用实际代码定义和下轮记录核实，不把日志时间当作剩余进度。
- GPU：沿同 SoC 案例核对用户 IB 的构造、可见性、SMMU 映射、cache 和 fence；之前的 `TLB=0`、单 NOP 停顿、`irq=ffffffff` 都不能各自证明硬件取指原因。提交必须保留原用户数据和失败资源保护；用最小可复核实验把“地址可达、执行、回写”分开。
- 同一候选批量完成相互关联的修复和必要宿主验证，再进行一次受控启动；不要为了每条日志各编译一次。只有实际需要 GPU 执行才能区分的原因才加有界探针，不在共享系统中重复 kick；失败后不可读取故障 GPU/SMMU MMIO。
- 测试结果分别记录 Data、backboardd、显示 swap/frames、用户态 BO/submit/fence/回读、触摸和自动恢复；用户曾明确要求停止 Recovery 自动监控，继续遵守。每次更新 `HANDOFF.md` 并提交本轮源码、文档，保留别人的未跟踪文件。

## 当前状态与边界

项目：`/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger`；设备 OnePlus 5 `cheeseburger` / MSM8998 / Adreno 540，序列号 `3b25cc7c`；当前主线为 iPadOS 17.7.11（21H461）。接手先读仓库 `AGENTS.md`、`HANDOFF.md`、`docs/P21_LOCAL_BUILD_GPU_HANDOFF_20261102.md` 和本文，再查 Git、进程、设备及最新日志。`main` 当前领先远端，存在其他 agent 遗留的三个未跟踪 launchd cache 文件，不要清理或提交。不要推送、重置工作区、覆盖旧候选。

用户称本轮已重启进入 Recovery；2026-11-02 本轮读取时通过设备 3b25cc7c 的系统 ADB 5037 证实 `ro.bootmode=recovery`、root。用户要求停止 Recovery 自动监控；当前没有启动该监控。返回方式（手动或自动）未由用户明确，本轮日志不能独立证明具体 reset 线程触发。下一次操作前重新确认设备状态，确保只使用一个系统 ADB 服务，不自动循环重启。

## 本轮实机结果（最新）

- 本轮 boot `0x5856c881`，只读原始日志 `out/p21/persistent-logs/20261102T143713Z/`，`retrieval.json` = `BOUNDED_LOG_WINDOW_READ_PASS`，64 rounds、写错误 0，窗口 SHA-256 `9c5b2a2e1345ce0f0dccd69d2275bc77fd14adc51fee1a86111ba7a6b30d036c`。最后 heartbeat `sec=318` 是日志工作线程相对时间，不是整机停机时间。
- 镜像 `out/p21/gpu-plugin-lifecycle/uefi/boot-p21-up1.img` SHA-256 `a6d0ced82788442c0949f8c44016fbd335a25d489ac0cc11dd602c3fa55fae22`；2026-11-02 14:13:47 UTC 临时 `fastboot boot` Sending/Booting OKAY。内核 `98ba240854d2cbd44a7d13f85efa2dc43fcc62e5aded0417bb85218fc7c3d251`，固件 `b467412111db5f333fc9c7226397b2f396c4c01fcfb56f46402ea8137d99a23f`，expanded payload `12099c8edc17189aded2ff58eb00dae937e96411fa81b56066437376b60a7a45`；`gate-20261102T141223Z.json` PASS、pins 已核对、args 644 字节。传输成功不等于进入系统。
- `backboardd fault exits=0`、SpringBoard=0；对比前一轮 `0x4dcc2685` 的 backboardd 14 次 fatal exit，有明显改善但一个样本不证明完全修复。`backboardd` type 1 observer 打开成功；`mediaserverd` type 0x4147 ABI 打开成功并完成 2 BO、2 map，说明修复插件的后台探针路径实机执行。`markers=3 last=4`，但不能仅用 marker 判断全部插件行为。
- `DISPLAY clients=5 frames=0`、`SWAP check=0 reject=0 transfer=0`、VS `start=1 arm=0 sent=0`：未观察到画面提交，不是 swap 被拒。未获得用户确认可见系统界面。Frame 计数是软件显示链，不是 GPU 加速证明。
- GPU 启动/GPMU/ZAP、内核 fence seq=1 有成功记录；用户提交 `GSUB rc=-4 stage=3 seq=2 fenced=0`，`GPSVC bo=2 map=2 sub=1 fence=0 wait=0`；用户 IB/fence 与四值回读未成功。`irq/cpirq/rptr/wptr=ffffffff` 是避免故障 MMIO 读的未采样标记，不是硬件寄存器真值。`MAPD tlb=0` 只说明软件映射/TLB 路径返回，不证明 GPU 可取指。不要称 GPU、像素、Metal 或 GPU 合成已工作。
- `DATA persist state=3 header=3 writes=136728 syncs=19334 err=0`：最后快照显示 Data 种子初始化/持久化仍在进行，不能断言 reseed 完成或跨重启持久化成功。用户曾明确授权本轮使用新 seed 重置 Data；**不要将该一次性授权扩展到后续任意重置**。下轮优先复用本轮种子并检查 Data 状态，避免重复更换种子。
- `TOUCH stage=4 error=0`、控制器检测成功、poll 递增、contacts/HID=0；本轮无输入操作证据，不能仅凭零事件判断触摸失效或成功。

## 已做修改与定位

1. 前轮 `0x49a91db8` 用户提供的 panic，PC 对应当轮内核 `CheeseburgerGpu::Startup::kickTo()` 内 GPU `InterruptStatus` 读取，ESR 为同步外部中止；证据和符号化局限见 `HANDOFF.md`。提交 `c6dd16c` 让用户提交 doorbell 后仅轮询 CPU-owned fence，超时标记 abandoned、不再读写 GPU MMIO、不宣称 DMA 已停；保留资源并禁止再次提交。内核 probe fence 仍用原路径。宿主 7 组模型通过，本轮日志无该 panic 证据但不能排除其他 panic。
2. 前轮 `0x4dcc2685` backboardd 14 次 fatal exit；首个 PC/LR 使用 21H461 SystemOS cache 和 `.symbols` 的匹配 UUID，依据样本 cache slide `0x1ce44000`，定位至 libdispatch `_dispatch_release` 与 Metal `-[_MTLDevice dealloc]`；非 cache 地址 `0x1016ba4c4` 未归属。证据 `out/p21/gpu-plugin-lifecycle/symbolication.txt`。
3. 本轮插件提交 `fbd4e88`，对照 `.work/p21-official-framework/drivers/AdrenoMetal/NativeAdrenoMetalPlugin.mm`：在 `initWithAcceleratorPort:` 先调用 `[super init]`；仅在 `mediaserverd` 以单次原子门异步运行探针，保留/释放 IOService 端口，避免阻塞系统显示发现。插件 CDHash `6ff9b3b19210bc4896900adbe3e823358abaaf0d`，trust bundle SHA `0a7d5a328be859eb3d66349318fe93bd6530c2670250a44fb40211fb6ff8e1a4`。实机 backboardd 致命退出为 0，支持修复方向；尚不能断言零帧原因已找到。
4. 本轮种子从 `out/p21/atos-dev-job/dev-seed.dbr` 的 `5446b3a2...` 副本仅替换 Metal 插件生成 `out/p21/gpu-plugin-lifecycle/dev-seed.dbr`，SHA `1e8b97fbc0a2020c5e359e4a1acc93f9e435a45dcf79bf5f415d7fbd3db81993`，host stage 在本地临时镜像完成。新 payload 同时包含该 seed 和新 trust。避免误用旧的 CDHash `6b740b...` 插件：源码变更不等于已部署。本轮未手动直接写 userdata、boot、recovery、GPT；按授权只改 misc 首 32 字节 Recovery command，首块前后备份及 readback 在候选目录。
5. 已移除不安全的 scratch 实验：原实现要求 `buffer_count>=2`，但插件只提交一个 IB；且静默改写用户 IB 和目标会破坏 ABI，不能用其结果冒充用户写回。不要重新直接覆盖用户命令或伪造 fence 成功。

## 参考上游与下一步

优先读取固定官方框架 `.work/p21-official-framework` 的 `AGENTS.md`、`docs/porting/gpu.md` / `display.md` / `uefi-firmware.md` / `migration.md`，以及仓库 `docs/P21_OFFICIAL_WORKFLOW_AUDIT.md`、`docs/P21_OFFICIAL_ADAPTATION.md`、`docs/P21_OFFICIAL_MIGRATION_DIFF_20261101.md`、`docs/P21_GPU_USERSPACE_INTEGRATION_20261102.md`。官方 UniXNU / sheng：`https://github.com/UniXNU/UniXNU`、`https://github.com/UniXNU/UniXNU-sheng`、`https://github.com/UniXNU/AdrenoMetal`、`https://github.com/UniXNU/Adreno740`；同 SoC A540 参考 `https://github.com/MC-XiaoXiao/ios_sagit`，本地 `docs/upstream/reference/sagit/QcomGPU/AdrenoStartup.h`、`AdrenoSubmit.h`。sheng 的 A740 可参考软件接口，寄存器/固件/电源/命令不可直接套 A540。

1. 先查零帧前置：本轮 backboardd 存活、`sel4/5/6=0/0/0`、`notify72=0`、vsync arm=0，结合同 boot backboardd / SpringBoard 线程栈和显示服务状态，对照此前出画面的 `out/p21/persistent-logs/20261101T172951Z/`（`frames=6`、backboardd fault=0）；排查 Metal 发现后 nil 回退、显示初始化、等待对象及本轮 Data state=3 的影响。别假设 crash 已解决就必然出画面，或把无 swap 归因分辨率。
2. GPU 保持现有经验证的时钟、SMMU、ZAP/PAS、安全策略；对照 A540 vendor/sagit 实际命令和页表映射，查运行时新增 `0x4000000/0x4001000` IOVA 可见性、BO CPU/GPU cache 一致性、ring/IB 内容与等待次序。`0x12=CP_WAIT_MEM_WRITES`、`0x26=CP_WAIT_FOR_IDLE`；软件 TLB 或 NOP 超时不能单独证明 GPU 读到了错误页或唯一根因。需使用隔离、合法、明确 ownership 的最小 GPU 操作区分可取指/可写/用户 BO 映射，不篡改真实用户提交；异常后不读取可能卡住的 GPU/SMMU MMIO，资源保守保留。
3. 同一 boot 验证 `IOServiceOpen → 2 BO/map → submit → fence → CPU 3 值 + 哨兵 → 安全释放`，再推进可读回的 GPU 像素、IOSurface、Metal device 与系统合成。不要把内核 fence、`frames>0` 或 `fastboot OKAY` 直接等同 GPU 图形加速。
4. 不使用 TrustControl 绕过 ZAP、关闭 AMFI、放宽 launchd 签名；不随意修改 CPU、内存、显示参数、种子或时钟。一次候选完成相关修复与必要验证再启动，保留独立记录；不循环测试。

## 编译、打包与日志

NAS 仓库是源码与证据位置；XNU 编译及链接在 `/Users/dinnerabc/unixnu-p21/atos-gate/source-tree`。使用 `P21_CANDIDATE`、`P21_LOCAL_ROOT` 运行 `scripts/p21-compile-atos-gate.py`；隔离候选、检查锁和 symlink、同步源码/补丁/生成输入，哈希验证，不要多个 agent 共写同一树。官方头文件优先复用缓存，缺失时代理 127.0.0.1:7897。上一轮内核 90.6 秒；仅用户态插件/seed/trust 改动可复用对应已验证内核，但必须重建 payload 和 UEFI，不能复用绑定旧 payload 的固件。本轮 VM overlay `/home/dinnerabc.guest/unixnu-up1-gpu-plugin-lifecycle/source` 依赖 `unixnu-up1-panic-cpufence` 及更早 lower layers，不删除。FVMAIN / EFI 的 kernel、ADT、expanded payload pins 核对后执行 `scripts/unattended/preboot-gate-p21.py`；核对本轮新 gate、完整哈希、args。监控实际构建进程而不是固定长 sleep。

设备默认只临时 `fastboot -s 3b25cc7c boot <image>`；不得刷 boot/recovery/GPT/其他分区。用户既有授权 misc 仅 Recovery command，保留其余首块并回读。userdata 仅按本次明确授权范围操作，日志窗口为相对 10 GiB、32 MiB。回 Recovery 后先确认唯一 ADB server 和身份，然后 `CHEESEBURGER_ADB_SERVER=tcp:localhost:5037 /opt/homebrew/bin/python3.13 scripts/logging/read-cheeseburger-log.py`，端口以实际为准；比较 boot ID 和窗口哈希。用户要求停止 Recovery 监控，未经新的指示不要恢复自动监控。

每次修改提交源码与文档，保留他人未提交文件；构建/打包/启动/实际画面分别报告。候选证据 `out/p21/gpu-cp/rounds/20261102T141404Z-plugin-lifecycle/` 是启动前冻结，最新读取在独立日志目录；不要覆盖历史。回收新日志后更新 `HANDOFF.md`、本文件及每轮证据关联。
