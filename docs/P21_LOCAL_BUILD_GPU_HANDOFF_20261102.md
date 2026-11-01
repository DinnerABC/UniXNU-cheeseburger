# P21 本机构建与 GPU 交接（2026-11-02）

## 1. 目标和接手原则

项目为 OnePlus 5（cheeseburger / MSM8998 / Adreno 540）运行 iPadOS 17.7.11（21H461）。当前重点是让用户态通过 GPU 服务提交命令并验证回写，随后推进已知像素、IOSurface、Metal 和系统合成。不要将内核 fence 成功说成 Metal 或 GPU UI 已成功。

开始前阅读仓库根 `AGENTS.md`、`HANDOFF.md`、本文件，再检查 `git status --short`、`git log -5 --oneline`、实际构建进程、设备状态和最近日志。现有未提交的 launchd cache 代码来自另一 agent，已被保留；不要清理、重置或覆盖。`out/` 下有私有输入、构建树和历史证据，不是可随意删除的临时目录。

官方本地固定框架 `.work/p21-official-framework`，固定 revision 见 `out/p21/official-adaptation/source-inventory.json`。先阅读其 `docs/porting/gpu.md`、`display.md`、`uefi-firmware.md`、`migration.md`；小米 6 sagit 仅作为 A540/MSM8998 寄存器和固件顺序参考，sheng A740 只能借鉴接口和集成方式，不能直接照搬硬件配置。

## 2. 重要路径

- 正式仓库和持久证据：`/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger`。
- 本机 P21 独立编译树：`/Users/dinnerabc/unixnu-p21/atos-gate/source-tree`。
- 旧本机缓存：`/Users/dinnerabc/unixnu-p21/fb-qup3/source-tree`，仅作来源参考，不要让并行构建共用其对象目录。
- 本机构建驱动：`scripts/p21-compile-atos-gate.py`；`P21_LOCAL_ROOT` 设本机父目录、`P21_CANDIDATE` 设 NAS 候选目录。默认本机父目录 `/Users/dinnerabc/unixnu-p21/atos-gate`、默认 NAS 候选 `out/p21/atos-gate`。注意：覆盖已有候选前应创建本轮独立输出，避免破坏历史。
- 本轮独立候选：`out/p21/atos-local-round/`；内核 `kernel/kernel.macho`、输入 `native/kernel.macho`、载荷 `payload/`、固件和 boot 镜像 `uefi/`，日志位于同一目录。
- GPU 板级代码：`package/BoardRuntime/CheeseburgerGpu{Cp,Smmu,Service,Start}.*`；用户态探针 `package/Userland/adrenoprobed.cpp`；构建入口 `scripts/p21-build-board-runtime.py`、`scripts/p21-build-adrenoprobed.py`。
- 日志回收：`scripts/logging/read-cheeseburger-log.py`；输出 `out/p21/persistent-logs/<UTC>/`；用户数据日志窗口相对 userdata 10 GiB、长度 32 MiB，不能被其他写入覆盖。
- 固件虚拟机：`scripts/p2-vm.sh`，Lima 实例 `p2`；本轮隔离 overlay `/home/dinnerabc.guest/unixnu-up1-local-round/source`，继承历史 `unixnu-up1-atos-gate/upper` 和 `unixnu-up1-fb-touch/upper` 等，后续不可删除其依赖层。

## 3. 本轮已做与未做

- 已建立本机 `atos-gate/source-tree`，由旧本机 `fb-qup3/source-tree` 独立克隆，修正指向旧树的 `kits/cheeseburger` 与两个 archive 软链接。每轮仍必须再次检查，不假定本机缓存已包含 NAS 的最新源码。
- 已修改 `scripts/p21-compile-atos-gate.py` 使用本地根和候选环境变量，增量同步 BoardRuntime，使用本机锁，并把编译结果回写 NAS。不得使用 `rsync --delete` 覆盖其他 agent 的文件。
- 已修改 `scripts/p21-restore-official-headers.py` 支持目标树；缓存文件使用 Git blob 验证，缺失时用 `127.0.0.1:7897` 代理。不要每次重复下载。
- 已在根 `AGENTS.md` 增加本机编译与 NAS 同步规则；后续若路径或脚本变化，更新该章节及本文件，提交源码和文档，不提交私有头文件、IPSW、镜像或设备日志。
- 其他 agent 的本机编译子进程在其停止后完成：编译约 136.7 秒，主机 stripped 内核 SHA-256 `5b84d06e8475cec30e0d9ad2cd181cddfc6ae81a8e690833533e5fa468ab314a`；unstripped `ec19c052ec040d738c90b3f9373a539239d020f310999519b4230d7c6af60373`。记录 `out/p21/atos-gate/kernel-source-build.json`（注意路径为该次进程实际 NAS 输出，不要覆盖历史镜像）。本轮预链接和 entry patch 后内核 SHA-256 `203c5c9d51d8426445aaeda25f03eaa259f1032b11df7eea5093af50d475f8dd`；`out/p21/atos-local-round/kernel-check.log` 记录 BoardRuntime、可执行/数据段、PAN、prelink 和 Mach-O 检查。
- GPU 宿主模型通过：`out/p21/gpu-cp/host-tests/20261101T171340Z/`。launchd cache 编辑器模型也通过宿主 CoreFoundation 合成输入；这不证明设备成功或签名有效。
- 为避免再次改变 Data identity，已从上次实机镜像 `dbd33465cba031b01b5df624577fcffb1d4feea5c9e460e3fc9e8046f594b523` 中提取原 Data seed，文件 `out/p21/atos-local-round/prior-seed.dbr`，SHA-256 `46d03a5a264af71d0e4851cdafca0ff9f646aeaf42e764eda9aafb9166f366ba`；对应 trust bundle `prior-trust.bundle`，SHA-256 `17a5441b5c88c55d06ac2742777cc2e797cbff161fe484bc7ffa6e1f2d19fc2c`。本轮使用上一轮原启动参数，不增加新 seed 导致 reseed。
- 保留的未提交候选涉及 `sgac=1` 和 `launchd_unsecure_cache=/System/Library/xpc/launchd.plist`。另一个设备项目对 loader 的研究称该参数按数字解析，非零会跳过 launchd 缓存签名及 AMFI 校验；路径字符串可能被解析为 0。这并非本机已验证结论，但足以表明风险。此前用户要求保留安全检查：本轮**不启用**该方案。需要官方或目标 loader 的实际证据和明确授权前，不改变缓存签名检查，也不把它当成正式解决方案。
- 本轮固件编译/打包/启动状态以本节末尾和实际日志为准；用户态 GPU 执行未证明，不能以 `fastboot boot` OKAY 或内核 fence 代替。

## 4. 本机编译操作步骤

1. 查看正在运行的 `p21-compile`、`build-xnu` 和固件构建进程；确认没有其他进程使用相同本机树。用户停止 agent 不代表其已启动的子进程一定结束。脚本的 `.build.lock` 用于单树互斥，但旧脚本可能不遵守。
2. 在 NAS 仓库检查 Git HEAD、`git status --short` 和当前候选文件；记录未提交改动，确认 XNU series 引用本轮补丁（不要盲目覆盖 `0008/0009/0010`）。源码改动需同步 `package/BoardRuntime`、补丁、构建脚本、实际生成头及相关输入；核对复制源与目标的 SHA-256。
3. 检查本机 `.../tree/kits/cheeseburger` 及 `build/native-ram-block/libNativeRamBlock.a`、`build/native-keystore-events/libNativeKeyStoreEvents.a` 的 `readlink` 和实际目标，不能指向旧候选或 NAS。源码树或缓存不完整要先修补，不要自动删除额外文件。
4. 从仓库运行（示例，先确保独立候选和 patch series 已准备）：

```sh
P21_LOCAL_ROOT=/Users/dinnerabc/unixnu-p21/atos-gate \
P21_CANDIDATE="$PWD/out/p21/<本轮候选>" \
/opt/homebrew/bin/python3.13 scripts/p21-compile-atos-gate.py \
  > out/p21/<本轮候选>/kernel-build.log 2>&1
```

脚本必须打印本机解析树路径，并把 stripped/unstripped 内核和对象记录回写 NAS、校验哈希。检查 `kernel-source-build.json` 的 `tree`、`build_on_nas=false`、同步哈希；不能只读注释。重新 prepare 需要的官方头必须复用缓存，网络获取按 7897 代理，检查实际 blob 哈希。
5. 拿**本轮 unstripped**内核做 prelink，并验证实际 prelink 输入与编译输出的 `__TEXT_EXEC`、数据段对应；不能误把 stripped 文件当 unstripped。运行项目原有 panic/entry patch 和 kernel check；记录其范围，不能手写 PASS 代替真实检查。
6. 用相匹配的 ADT、trust cache、Data seed 和配置生成 payload；使用同一轮生成的 `CompressedPayload.h`、`Expected.h` 编译 VM 固件。进入独立 overlay，核对低地址 SMMU pool、GPU clock 等本机固件更改仍继承；不要把 sheng 板级地址或固件套到本机。
7. 在 VM 的实际 `FVMAIN.Fv`/FFS/EFI 中核验本轮 kernel、ADT、expanded payload 的 pin；从 VM 导出 FD 和设备 DTB，打包后运行 `scripts/unattended/preboot-gate-p21.py`。保存 gate 文件及各输入哈希。固件压缩时，直接在 FD 扫原始 hash 不能代替 FV 检查。
8. 构建期间用进程状态和日志检查进度，不设固定 300/420 秒的等待；对长步骤定期向用户汇报。模型测试和真正实机验证要分开报告。

## 5. 启动和回收日志

目标设备序列号 `3b25cc7c`。先确认最新上一轮窗口已回收，且系统只使用一个 ADB 服务；查询实际端口，不自行启动多个 server。检查没有旧监视器或等待中的 fastboot 命令会截获设备。

仅在授权范围内操作：`fastboot -s 3b25cc7c boot <本轮镜像>` 临时启动，不刷写 boot/recovery、固件或 GPT。用户已授权 misc 的 Recovery 启动标记：仅在确需自动回 Recovery 时核对 by-name 对应块，保留首块，修改命令字段、同步并回读比较；不得扩大为其他分区写入。不要因为更换 seed 静默触发新的 Data reseed；本轮优先沿用 `46d03a5a...` 同一输入。

回到 root Recovery 后使用系统当前 ADB server 端口，例如端口为 5037 时：

```sh
CHEESEBURGER_ADB_SERVER=tcp:localhost:5037 \
  /opt/homebrew/bin/python3.13 scripts/logging/read-cheeseburger-log.py
```

检查新 `retrieval.json` 的 boot ID、窗口哈希、rounds、写入错误，再看 `decoded/.../latest.txt`、`msgbuf.txt`、`heartbeat.json`。上一轮 boot ID `0x3f3e3c8f`；不可把旧日志当成本轮。记录真实屏幕及用户输入：Fastboot OKAY 仅代表传输，快照停止不等于整个系统停止，心跳和实际返回位置分别记录。

## 6. GPU 后续工作

已实机观察：A540 CP/GPMU、ZAP/PAS、SMMU、内核 fence 写回；服务可以发布。还没有观察到用户态客户端连接、BO 分配/映射、用户态 IB/fence、像素、Metal 或 GPU 合成。最近 `0x3f3e3c8f` 中官方 `bindfs + MNT_UNION` 的原/新增 plist lookup 均成功、挂载后的 plist uid/gid 为 0，但 `GPROBE exec=0`、`GPSVC open=0`。路径存在不等于 launchd 枚举或 job 接受；也不能仅凭 exec=0 断言具体失败点。

下一位 agent 先核对官方用户态插件/launchd 机制、目标 iPadOS loader/cache 的真实 ABI 和签名策略；不要绕过 AMFI、ZAP/PAS 或缓存签名校验。不要替换 bluetoothd 或系统 plist。围绕首个可确认失败做最小功能修复，同一 boot 推进 `exec → IOServiceOpen → 两个 BO → 映射 → Submit → WaitFence → CPU 四值回读 → 安全清理`。之后才是 GPU 已知像素、IOSurface、Metal 前端和系统合成。观察显示、触摸及 PosterBoard crash，但不要无证据归因 GPU。

本机 BO 失败释放须保守：未知/abandoned 不等于 DMA 已停；只有确认 idle 或确认 halt 且 TLB 同步成功才释放。每次功能修改配必要模型验证，避免为单条日志反复构建半小时。

## 7. 文档与交接维护

实际构建路径或流程改变时，在根 `AGENTS.md` 的 `Local XNU builds and NAS evidence` 节同步更新规则；在 `HANDOFF.md` 顶部记录最近一次实际候选、设备状态、日志位置和下一步；本文件保留可重复的操作步骤。每次提交前检查 `git status`，只提交本轮授权源码和文档，不能夹带其他 agent 的私有文件或覆盖其未提交工作。

## 8. 本轮镜像和临时启动（等待实机结果）

- 镜像 `out/p21/atos-local-round/uefi/boot-p21-up1.img`，SHA-256 `56d9ad0834ad059a262bef672c1db27272a2e3249921461e0aa016a18b0f992f`；内核 `203c5c9d51d8426445aaeda25f03eaa259f1032b11df7eea5093af50d475f8dd`；固件 `6070871b0e53662dda8d8e88921b2b0b64e0311aaf605f7fd7e637e62c287ef5`；payload expanded `e829d961f9cb775156a69438b8a974c1019c746b8280512c1c222aa7417002b9`。
- `gate-20261101T171941Z.json` 为 PREBOOT_GATE_P21_PASS，实际固件 pins 检查通过；仅代表离线一致性。上一轮预启动日志回收 `out/p21/persistent-logs/20261101T172009Z/`，boot `0x3f3e3c8f`。
- 目标设备 `3b25cc7c` Recovery → Fastboot 后执行一次 `fastboot boot`，Sending/Booting 均 OKAY，输出 `out/p21/atos-local-round/boot-once.log`。已按授权修改 misc 首块命令字段并回读一致，前后块在候选目录；没有写 boot/recovery/GPT。当前设备运行结果和新日志尚待回收。
- 恢复旧 seed 不代表本轮 P1 成功；本轮禁用 `sgac`/`launchd_unsecure_cache`，只验证新编译链及安全基线。返回 Recovery 后读取新 boot ID，判断 fence、显示、触摸及 crash，记录真实结果。
