# P19 R2 交接

## 最新归档状态（2026-10-24）

P19 已完成首诊断候选与 R2 开发态 watchdogd 作业隔离候选，两者各执行一次临时 boot，均未刷写分区。首诊断结果已有人工核准；R2 已有三张运行中照片（精确文本与数值未整体人工核准），随后两条 APFS 事务日志由用户直接读回。R2 当前为“仍有部分文件系统活动、用户空间推进状态未知”，不是只有传输结果，也不是启动完成或整机硬冻结已证实。

首诊断的确定根因限定为 PID52 保存的异常：原始 watchdogd 在缺少 IOWatchdog 服务分支执行 BRK #1；最终 fatal 为 watchdogd[104] SIGTRAP，未独立捕获 PID104 的同一异常现场。R2 采用官方开发态作业配置路径隔离 watchdogd，没有实现物理 watchdog 驱动，也没有关闭物理计时器的证据。

R2 画面可见原始 launchd 继续到 `Early boot complete. Continuing system boot.`，未提供新的 fatal 画面。约五分钟间隔后用户读回 disk11 的 xid 从35增至55；对应 flush 为636us和598us，不能将打印间隔当作 APFS 操作耗时。单次事务日志不能确定写入者、服务等待点或启动 ETA。镜像配置 `cpus=1`，未启用次级核心；无实时 CPU 在线数、频率、占用率或线程等待快照，不能把慢输出确定归因于单核。

Data/Root/SystemOS/Manifest 的首诊断人工核准数值不得移植为 R2 的精确实测值。R2 照片支持 Data 挂载、策略成功返回及后续启动任务；Keybag、Sandbox、FIPS、tzinit 退出完成与 launchd LC_MAIN 首指令仍缺独立完整证据。

完整分析入口：`out/p19/P19_COMPLETE_LOG.md`；原始资料包：`out/p19/P19_ANALYSIS_BUNDLE.tar.gz`。资料包包含已保存的主机构建/测试/传输记录、人工核准数据、运行照片、用户直接读回、源码/配置与反汇编，不等于连续串口日志。后续未进行设备轮询、重启、重新构建或镜像改写。当前任务为整理资料，供用户独立分析。

当前候选P19-R2：`out/p19/r2/uefi/boot-p19-r2-watchdog-isolation.img`，81895424字节，SHA256 `ff0d178c3a3c06e6c552ff9dd93e0d55c34bf72010060b6376f54cdbdc250aca`；内核SHA256 `70509b664e284b10a5ff6d53265c8a481c33091a319615c426c9e5b93ef63334`。门禁`P19_OFFLINE_BOOT_GATE_PASS`；传输时原始状态`TRANSPORT_ACCEPTED_SCREEN_PENDING`；当前观察状态`RUNNING_PHOTOS_AND_USER_REPORTED_APFS_ACTIVITY_FINAL_OUTCOME_UNKNOWN`，本候选boot命令1，分区写入0。

P19首诊断镜像和P18已测镜像均保留。P19人工核准原照片与数值见out/p19/evidence/screen-user-confirmed.json；R2已补充运行照片与用户直接读回；照片精确数值尚未整体人工核准，最终启动结果未知。

首候选evidence/screen-user-confirmed.json保存四段人工核准，不要混淆PID52与104。R2位于out/p19/r2，先读policy-kernel-check.json、tests/policy-host-report.json、tests/original-launchd-policy.json、offline-gate.json与boot-result.json。R2 已执行一次固定设备临时 boot；不得把此处历史流程当成再次启动授权。当前仅归档，后续设备动作需依据新的任务要求。

代码默认关闭；本候选config/unixnu.toml显式cheeseburger-disable-watchdogd=1。撤销为后续候选去掉参数或值0，原seed未变，下次RAM重建自然无disabled文件。不得加其他作业名或伪造VMM，不能改为全局跳过critical panic。

源BootWatchdogPolicy.c派生官方sheng、仅watchdogd；.source/bsd/kern/kern_exec.c是保留Root/Data扩展的合格源码，不能从缓存原始kern_exec重建替换。.objects/kern_exec-command.json记录真正编译输入。政策archive通过force_load链接。原异常记录器指向out/p19/source/WatchdogException.inc，保留该源；PGZ源和对象不动。

本P19任务此前已创建两名轻量只读代理并结束，本R2不创建第三名。所有实际改动、回归和构建由主代理执行，一次最多一个XNU任务。policy-host-01的SDK typedef错误和缺少policy输入导致的original-launchd-policy-01失败保留；正式通过标签为policy-host-02与original-launchd-policy-02。不要重复已通过阶段。

物理timer状态未知，本隔离不提供喂狗/关闭timer能力。受控一次启动，若失联/复位停止；不以禁用daemon宣称硬件计时器已停或系统稳定。保留新首错继续真实用户空间，只改实际阻塞，不启动外围图形等大工程。

## P19-R2 运行中照片初步分析（非 fatal，精确转录待核准）

用户提供三张运行中照片，并补充“似乎还在缓慢输出内容中，我将继续等待查看还有无fatal”。当前应记录为仍可能有运行进展的慢输出，不能定为整机硬冻结。原照片和SHA256在out/p19/r2/evidence；下述照片文本属于初步视觉观察，不升级为人工核准的精确数值。

照片按阶段应读为：3057（APFS Data及作业策略发布）→3040（原始launchd缓存/启动任务）→3018（后续日志及当前停留画面），不是按文件名给出的顺序。3057可见Data卷mount-complete及cheeseburger development-only watchdogd job isolation: 0；3040可见AMFI cdhash trusted、validated cache发送给launchd，随后finish-restore、finish-demo-restore、systatuscheck、prng_seedctl和launchd_cache_loader；3018可见workload-properties-init、init-exclavekit、Early boot complete. Continuing system boot.，以及com.apple.aggregated索引警告。没有照片显示P19-R2 FIRST FATAL标题或捕获完成字段。

**已取得进展的证据与边界**：画面支持Data和策略发布已进入成功返回路径，原始launchd继续执行多个boot task，且目前未出现以往watchdogd fatal。这支持隔离起效的方向；但没有完整进程列表、launchd解析配置的直接读回、WATCHDOG实例计数或最终fatal记录，不能只凭“没看到watchdogd”认定从未启动或系统稳定。尚无新LC_MAIN首指令、Keybag/Sandbox/FIPS完整成功及tzinit退出完成证据。

**索引警告**：本机冻结kernel中格式串Couldn't build index for %s: %s位于0xfffffff0046b1037，Not eligible for acceleration位于0xfffffff0046b77ba，均在真实com.apple.driver.AppleMobileFileIntegrity的prelink extent内。不能因acceleration一词转去GPU驱动；具体加速索引算法的因果仍未完整反汇编。本组照片显示同类init_exclavekit索引警告后launchd继续打印其他boot task，故该消息至少没有在这些更早出现点立即阻止所有启动。最后一行aggregated索引警告不等于最后执行指令/当前阻塞函数，不能据此patch返回值或跳过AMFI。

**显示停顿/重叠**：原P8 framebuffer控制台在drivers/p8/FramebufferConsole.inc及out/p8/kernel-source/serial_console.c。67列/60行；写满后以约207万次32-bit volatile写清空1080x1920（约8.29MB），每字符画512个像素存储并dsb sy。只对总字符计数做原子操作，行列/清屏/绘制没有整体串行化；console_write和console_write_unbuffered均在原控制台buffer/锁逻辑前转到此路径。即使cpus=1，中断/重入也可能交错；CR只复位列不清余行。它解释了可见暂停、交错或残留的可能机制，并可能放大大量日志的耗时；未做实机耗时量测，不能认定为唯一慢启动原因。不要把旧屏幕内容当实时唯一日志。

**preoslog**：osfmk/kern/debug.c:2150的sysctl_debug_get_preoslog在loader未提供preoslog时打印Couldn't obtain preoslog region并返回NULL；bsd/kern/sys_generic.c:2931调用它处理kern.preoslog，空缓冲返回0。该打印不是独立panic证据，也不应伪造preoslog地址来消除日志。

**其余日志分类**：prng_seedctl缺初始seed、virtual random失败应保留为熵/PRNG初始化待核实项，不直接归咎于索引或擅自填常量随机种子。shared_region_start_address失败在早期屏幕后仍出现用户空间运行，单条失败不是最终挂死证明。DYLD shared region unnest是映射诊断，文案提示内存占用增加，不能当成签名终止。AMFI Launch Constraint Violation (not enforcing)是具体启动约束诊断，既不能说已阻断该进程，也不能推导整个AMFI/CodeDirectory已关闭；本候选没有添加关闭AMFI参数，原始组件代码保持不变。当前照片只证明部分trust/cache检查出现成功输出，不是所有安全阶段完整成功。

**当前处置**：按用户最新安排继续等待，保留首fatal，不发命令重启、不构建新镜像。若仍输出，可按实际等待时长拍前后两张，比较新的task/process/日志而非屏幕位置；出现fatal则完整保存标题、首错和状态。若长时间不再输出且无fatal，下一轮最小诊断应先加入低频运行心跳/最后调度或syscall进度快照，并处理现有控制台写入串行化和日志限流；不借最后一行推断阻塞、不关闭其他daemon。现有p16 syscall观察只记录PID<=128且RAM日志32KiB滚动，后续高PID/被覆盖日志不可视为进程未运行。

## 后续APFS事务统计审计（用户直接读回）

用户最新读回：

```text
tx_flush:1338: disk11 xid 35 tx stats: #20 owiat 0 0us finish 20 bar2 20 f 19 enter 19 fq 3 9 111us close 0us prep 351us flush 636us
```

与冻结APFS的真实格式串匹配，属于com.apple.filesystems.apfs，字符串VA0xfffffff0049c508f；真实拼写为owait。disk11按本次启动照片对应承载/private/var的RAM Data容器，disk11s1为卷。xid35是APFS事务标识，不是第35秒、35%或文件数；官方格式说明xid为单调增加的事务标识，但内部统计字段不能据此推导总工作量。

flush636us=0.636ms，prep351us=0.351ms，close0us为该打印的量测值，不表示所有close从不耗时。fq后111us可读为相应统计时间，但未确认其内部精确语义。各阶段可能嵌套或不同统计口径，不能简单相加当总启动时间。#、finish、bar2、f、enter、fq为内部计数/状态，未有公开源码和实参逐一确认；f19不能解释为19次失败，finish20不能解释为20秒或完成20%。

该日志支持APFS仍运行到事务提交/flush路径，且RAM Data产生事务更新；具体可能由日志、配置、数据库或后台服务写入引起，单行不能归因某个文件/daemon。它没有显示I/O error/ENOSPC/只读错误，但不因此宣称全系统存储/服务无故障。记录含各阶段统计并不独立证明整个事务最终无错误。

当前Data seed和payload均为RAM-only、persistent_store_enabled=false；这里的flush不是重刷9.38GB userdata，也不是修改签名OS/SystemOS镜像。RAM内容的事务提交不意味着写回手机持久化分区。

无法从本行估算启动完成时间：单次flush在亚毫秒量级，而后续服务可能等待缺失设备、熵源或IPC依赖；APFS后台周期提交也可能在服务等待时继续出现。即使xid递增也不能推出UI必将出现或所有boot任务推进。没有已定义的阶段完成标志，不能承诺再等几分钟即可启动完成。

建议观察2–5分钟窗口仅用于收集前后差异，不是启动ETA：比较xid、进程名、boot任务或新错误是否变化。若仅周期tx_flush且业务日志长时间不前进，下一轮应捕获服务等待/最后调度/syscall进展，而非以本条日志修APFS。若无新输出，也须区分控制台显示暂停和CPU/内核停止。此处询问已获后续回答：约五分钟后xid35增至55，详见后续审计；未自动轮询设备或重启。

参考Apple File System Reference：https://developer.apple.com/support/downloads/Apple-File-System-Reference.pdf 。本轮仅审计和记录，没有构建或设备写入。

## Follow-up: xid 35 -> 55 after approximately five minutes

Source: direct user readback; see out/p19/r2/evidence/tx-flush-followup-user-report.json. Latest flush=598us (0.598ms), prep=343us (0.343ms). The five-minute screen interval is not the duration of this flush. Logging cadence/trigger has not been established. The xid increase supports ongoing APFS transaction activity, not a boot percentage or a measured throughput. Exact writer process/files remain unknown. disk11 remains RAM Data, with no userdata writeback.

Frozen config out/p19/r2/config/unixnu.toml:14 contains cpus=1 and no sheng-smp opt-in. drivers/p8/ArmCpu/ArmCpu.cpp requires explicit opt-in for secondary startup. This candidate is configured for one CPU; there is no live CPU online-count, frequency, utilization or scheduler measurement. Heavy framebuffer logging can add single-core overhead, but its contribution is unmeasured. Single-core configuration is not proof of the current delay's cause. Changing to cpus=8 alone is not a validated SMP implementation.

Partial filesystem liveness does not establish userland boot progress. No reliable boot ETA is available. Next useful evidence is bounded process/thread wait state, service/syscall progress and scheduler counters, distinguishing service dependency waits/restarts from CPU saturation. No device command, reboot, build or image mutation was performed for this audit.
