# P19 Watchdog 根因与开发态隔离

## 最新归档状态（2026-10-24）

P19 已完成首诊断候选与 R2 开发态 watchdogd 作业隔离候选，两者各执行一次临时 boot，均未刷写分区。首诊断结果已有人工核准；R2 已有三张运行中照片（精确文本与数值未整体人工核准），随后两条 APFS 事务日志由用户直接读回。R2 当前为“仍有部分文件系统活动、用户空间推进状态未知”，不是只有传输结果，也不是启动完成或整机硬冻结已证实。

首诊断的确定根因限定为 PID52 保存的异常：原始 watchdogd 在缺少 IOWatchdog 服务分支执行 BRK #1；最终 fatal 为 watchdogd[104] SIGTRAP，未独立捕获 PID104 的同一异常现场。R2 采用官方开发态作业配置路径隔离 watchdogd，没有实现物理 watchdog 驱动，也没有关闭物理计时器的证据。

R2 画面可见原始 launchd 继续到 `Early boot complete. Continuing system boot.`，未提供新的 fatal 画面。约五分钟间隔后用户读回 disk11 的 xid 从35增至55；对应 flush 为636us和598us，不能将打印间隔当作 APFS 操作耗时。单次事务日志不能确定写入者、服务等待点或启动 ETA。镜像配置 `cpus=1`，未启用次级核心；无实时 CPU 在线数、频率、占用率或线程等待快照，不能把慢输出确定归因于单核。

Data/Root/SystemOS/Manifest 的首诊断人工核准数值不得移植为 R2 的精确实测值。R2 照片支持 Data 挂载、策略成功返回及后续启动任务；Keybag、Sandbox、FIPS、tzinit 退出完成与 launchd LC_MAIN 首指令仍缺独立完整证据。

完整分析入口：`out/p19/P19_COMPLETE_LOG.md`；原始资料包：`out/p19/P19_ANALYSIS_BUNDLE.tar.gz`。资料包包含已保存的主机构建/测试/传输记录、人工核准数据、运行照片、用户直接读回、源码/配置与反汇编，不等于连续串口日志。后续未进行设备轮询、重启、重新构建或镜像改写。当前任务为整理资料，供用户独立分析。

当前候选P19-R2：`out/p19/r2/uefi/boot-p19-r2-watchdog-isolation.img`，81895424字节，SHA256 `ff0d178c3a3c06e6c552ff9dd93e0d55c34bf72010060b6376f54cdbdc250aca`；内核SHA256 `70509b664e284b10a5ff6d53265c8a481c33091a319615c426c9e5b93ef63334`。门禁`P19_OFFLINE_BOOT_GATE_PASS`；传输时原始状态`TRANSPORT_ACCEPTED_SCREEN_PENDING`；当前观察状态`RUNNING_PHOTOS_AND_USER_REPORTED_APFS_ACTIVITY_FINAL_OUTCOME_UNKNOWN`，本候选boot命令1，分区写入0。

P19首诊断镜像和P18已测镜像均保留。P19人工核准原照片与数值见out/p19/evidence/screen-user-confirmed.json；R2已补充运行照片与用户直接读回；照片精确数值尚未整体人工核准，最终启动结果未知。

## 已核准的本机定位

P19首候选最终fatal watchdogd[104]退出，namespace2/subcode5。保存的首次断点属于watchdogd PID52/tid489，不冒充PID104现场。用户核准PC/LR=0x100783b0c、ESR=0xf2000001、CPSR=0x60000000、code0=1/code1=PC，copy返回码均0，frame0 LR=0x10077f830，frame1=0x100781958。PC字节200020d4是BRK #1。

已只读提取同版本原watchdogd，SHA ded4c688e1c2add74acf3e7f9eea5fe2c2c569180ce81492221877944d913a01，UUID04a420f0-c003-380e-8bd0-d9b24150a0e7。静态陷阱0x10000ab0c，对应slide0x779000。frame0去slide为0x100006830，是0x10000682c调用缺服务cold分支的返回点。原字符串connect_to_kext() unexpectedly failed - unable to lookup watchdog KEXT service已人工核准并以原字节核验。PID52已定位IOWatchdog lookup缺服务，PID104仍只有最终SIGTRAP退出被确认；不能直接声称其逐指令现场相同。

原始watchdogd四组分支已实际执行：physical missing BRK、virtual missing exit0、IOServiceOpen error BRK、service connected继续。外部API模拟，报告tests/watchdog-branches.json在首候选目录。没有伪造VMM。用户另核准launchd输出Early boot complete. Continuing system boot.，Data4/0/0 dev16777298、RAM98/121/0、tz errno0、PID1 EL0调用7483；仍非LC_MAIN首指令直接证明。

## 官方现成案例

https://unixnu.com/devices/uke 列出真实硬件watchdogd SIGTRAP仍是partial；这不是已完成驱动修复案例。https://unixnu.com/xnu/boot-arguments 明确sheng-disable-watchdogd=1。现有upstream/UniXNU-sheng/package/BootWatchdogPolicy.c在RAM Data成功后、原launchd exec前发布正规disabled.plist与disabled.migrated。本轮复用其syscall实现和原格式，去掉通用disable-jobs/Setup reset，只保留精确watchdogd标签并加强冲突和marker验证。

## 路线B：development-only watchdogd job isolation

独立开关cheeseburger-disable-watchdogd：代码默认0，只有DEVELOPMENT/DEBUG且显式值1才执行；候选配置单独选1，未更改全局board默认。不开启时不写配置，RELEASE传1也不写。仅/private/var/db/com.apple.xpc.launchd/disabled.plist内com.apple.watchdogd=true；真实零字节disabled.migrated选择原始现代loader。原watchdogd、launchd和签名OS/SystemOS完全保留。

目录为0755；策略和标记均O_EXCL|O_NOFOLLOW，既有冲突拒绝，不覆盖其他配置；fchown0:0、fchmod0644、fsync、fstat检查root UID/GID、regular、mode和准确size；任一步失败返回真实errno并在launchd前panic。所有写入仅本次RAM Data；不修改P17 seed，不启用userdata写回。仍保留普通关键进程退出策略。

调用置于原native_owner_data_prepare成功后。实际继承的是P17-R3合格kern_exec源码与活对象：缓存recipe仍指向早期源码，不能用旧cache源码替换而丢失Root/SystemOS/Data路径。R2只对合格源码加该调用，手工用现有编译recipe编译kern_exec并安装活对象再增量link，命令与原对象备份保留。另外编译BootWatchdogPolicy，Capture显示requested/status/applied并修正字节十六进制零补齐。异常记录器和PGZ修复保留。

## 离线证据与限制

21项策略host用原C函数执行，filesystem/syscall替代；默认缺省/0/其他值不IO，RELEASE值1不IO，成功写入及16项错误拒绝。原始launchd loader/job gate指令级测试：root watchdogd disabled=true，uid502不接受，其他label不禁用，filesystem/libxpc替代。不是实机配置接受证明。

真实Mach-O调用顺序为Manifest→RAM activation→Data seed/mount→policy，原Data函数及安全路径源码不改。最终prelink原Apple执行代码、Trust Cache、Owner策略、Manifest、PGZ及Data绑定继承检查通过。P18/P19已测内核、镜像和Data seed哈希冻结。

## 物理硬件计时器边界

IOWatchdog lookup缺服务是用户态契约缺口，不等于确定物理计时器未运行。此前daemon未成功连接该服务，但无法由此排除bootloader/固件残留timer。本轮未更改MSM8998 watchdog MMIO或喂狗操作，未复制SM8550地址；隔离不是关闭硬件timer，更不是Watchdog支持。只做受控一次RAM boot，观察是否复位；发生复位/失联停止自动尝试。稳定性与长时间运行仍未证明。

## R2运行中观察的结论边界

三张照片显示策略发布日志返回0、原始用户空间继续到Early boot complete及后续索引警告，未显示fatal。用户随后称似乎仍缓慢输出；因此当前不是已证实硬冻结，也不能把com.apple.aggregated最后一行当真正阻塞。索引格式串已在本候选原始AMFI prelink extent定位，不是GPU证据；preoslog打印由普通sysctl读取空预引导日志缓冲即可触发。详见BOOT_RESULT运行中初步分析；精确照片读数待人工核准。
