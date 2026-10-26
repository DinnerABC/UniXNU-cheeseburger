# P20 R1 构建与实机结果

## R6 异常现场诊断；USB日志可行性（2026-10-24）

R6最终门禁P20_R6_OFFLINE_BOOT_GATE_PASS，镜像SHA256 `e7ea31f99c1c4e455d53c17fbc23e64087e6bc03808aa81ca3722809a79bb6ec`，82,452,480字节。确认同一root Recovery后已执行一次临时boot并被bootloader接受；TRANSPORT_ACCEPTED_SCREEN_PENDING，boot_commands1/partition_writes0，不自动重试。等待P20-R6 page4/4的异常明细、原因和SCAN计数；这不是USB日志通道的实机成功。


R5新照片已确认desired17408、总vnode1983/free1467，但usermanagerd仍反复SIGABRT，OPEN/clients/frames仍0。最近退出列表无bics不足以证明bics不再出错。R6保持R5 ADT、CPU1、1GiB、Data及安全输入；仅增加诊断，不猜测修复其他服务依赖。

采用官方sheng251b291 BootFaultProbe：捕获EL0 breakpoint和原始VM处理后仍未解决的user abort，记录PID/name/PC/LR/SP/FP/ESR/FAR/slide。异常路径不读取用户内存、不分配、不遍历进程；kernel sleh仅增加受p20_diag_enabled控制的两个观察调用，原异常分发不变。退出路径用官方formatter关联PID并取当前线程寄存器/最多12个返回地址。此读取可缺页，可能延迟该正在退出的线程；不能声称零开销，但监视器/其他退出观察者不等待它。

新增第四页FAULT：保留usermanagerd与IOMFB_bics_daemon各自整个本次启动的匹配信号退出计数，最多各取前2次详细信息，避免被最新3条退出覆盖。只对usermanagerd首个SIGABRT做一次可写malloc区域原因扫描。官方160MiB预算收紧为4MiB尝试读取（失败也计预算），最多128区域，零长度/地址溢出停止；匹配到完整uncaught exception文本提前停止。显示扫描字节/区域/失败/rank，未找到原因不代表没有原因。细节读取在普通退出上下文、IRQ开启且不持有本探针短锁；缓慢读取不会阻塞屏幕monitor，其trylock只显示busy并保留上次完整值。

字体仍12×24，每15秒轮换4页；VNODES原误导性used标签改为total，DTmax改kglob（原kernel全局值，不冒称ADT属性读回）。第四页重点拍USERMANAGERD PC/LR/ESR/FAR/slide/bt及abort reason，CPU页不是整机CPU采样。

主机ASan/UBSan验证实际fault捕获/退出formatter、PID关联/环覆盖/copy失败/默认状态与SIGABRT无硬件异常时的寄存器回退；实际原因扫描验证4MiB/128区域界限（含全部copy失败）、命中/未命中/零长度/重入/输出边界；命名目标明细次数限制、一次扫描、屏幕wrap与竞争也通过。根授权/manifest/Data/PGZ/watchdog静态复核通过；不是实机原因证明。

用户新增USB实时日志需求，取官方文档列出的NativeDwc336f968d9d39b147de334decd9edf3fe5b0993492确认：SM8150/nabu的控制器初始化参考尚未实现EP0枚举和bulk日志端点。本机USB FDT已保存，主机未枚举串口；接收脚本out/p20/usb-log/receive.py与伪终端测试通过，但手机端gadget未实现/未收实时USB日志。独立ring/低优先级有界消费/溢出计数设计见同目录README；当前R6不加载USB驱动、不写USB寄存器，避免把USB控制器bring-up与用户管理诊断混在一起。

## R5 官方差异同步：启动环境与vnode默认值（2026-10-24）

R5最终结果：原始OS镜像只读提取usr/libexec/IOMFB_bics_daemon，精确SHA匹配官方脚本。原始指令四用例missing=NULL_CFData、main=normal_continue、ramdisk=ramdisk_wait、oversize=normal_continue全部通过（IORegistry/CF/signal API替身，非实机）；第一次沙箱内Unicorn SIGILL日志保留，允许JIT的执行环境重跑通过。门禁P20_R5_OFFLINE_BOOT_GATE_PASS。镜像SHA256 `a3a93c2aa2cbcffc9ccc7bfe8bf514cdbe5076b68f23df0d9361ac23c47692fd`，82,456,576字节。已确认同一台root Recovery并执行一次临时boot，bootloader接受，TRANSPORT_ACCEPTED_SCREEN_PENDING；boot_commands1/partition_writes0/不重试。实机bics是否不再SIGSEGV、usermanagerd是否仍SIGABRT及图形是否推进待照片。复用R4内核，标题仍P20-R4，第一页desired应17408；DTmax是旧诊断的kernel全局读数，不应当作该ADT属性的独立读回。


刷新官方framework到bd127c008cdc584b83cf00eaea269e4f1adf3cc8引用；未改固定checkout/构建缓存。sheng251b291与QEMU97b75b1未变化。框架新变化以Linux构建工具及legacy编译条件为主，不整体合并到MSM8998已验证内核。比较与决策见out/p20/r5/qualification/upstream-refresh.json和upstream-decisions.json。

找到官方check-original-bics-environment.py：固定daemon SHA256为2d1f67b75fb33d091396c70aee627bb6a2c8dcdb03ffa7401378b7a688088b53，执行原始ARM64分支，API由stand-ins替代，区分缺失属性导致NULL_CFData、main正常继续和darwinos-ramdisk有意等待。官方sheng/QEMU ADT设/chosen/osenvironment=main，本机R4缺失该属性。R5同步main；它描述要启动的正常OS环境，不把临时RAM boot误设为recovery/ramdisk。实机SIGSEGV是否因此消失仍待验证。

同时同步官方/defaults/kern.maxvnodes计算方法：当前管理1GiB，1048576KiB/64KiB+1024=17408，不拷贝QEMU4GiB的66560。R4实际目标1024已确认，但free567不能证明耗尽。两项ADT白名单以外完全不变；复用R4 kernel及诊断、CPU1、1GiB、原始TrustCache和Data种子，不追加安全状态/密钥/用户记录。原签名可执行代码不变。

Data目录helper及-apfs_shared_datavolume差异被记录为另一个QEMU实验条件，未据SIGABRT直接推断为本机原因。本轮先验证具体bics环境缺失修复，不同时变更Data或新加陷阱诊断。若修复后仍有原崩溃，再使用官方BootFaultProbe取PC/FAR/ESR/原因。这是按用户“找到修复先修复，否则加诊断”的分支执行。

## R4 上游CPU链路复核与增强诊断（2026-10-24）

R4实机发送更新：用户手动进入rec后，身份锁定root Recovery检查通过，执行一次fastboot boot，结果TRANSPORT_ACCEPTED_SCREEN_PENDING。boot_commands=1，partition_writes=0，无自动重试；等待R4大字三页诊断照片，不能把传输成功当作UI成功。


R4最终门禁`P20_R4_OFFLINE_BOOT_GATE_PASS`。镜像`out/p20/r4/uefi/boot-p20-r4-trace.img`，82,464,768字节，SHA256 `2081346dc43f9b03d438c3a3f360fa6edaab39a5686886ee650264029ce073ba`，kernel SHA256 `a9c32953e503390fe7663c9820f9315e7188e0be018a3c36c662c0569e6e7bc3`。新候选未发送，boot_commands=0，partition_writes=0；只读检查未检测到身份锁定的Recovery/bootloader，等待用户手动进入rec，未强制重启当前实验。全部冻结P19/R1/R3及原始30组件代码复核通过。


R3的1GiB已由新照片确认，但457秒时clients/frames仍0，backboardd本轮缺席。PROC是当前进程数量，pass是采样轮数，没有“采满进程数”目标；进程缓增不等同渲染进展。

上游CPU链路为原始QuartzCore/backboardd绘制→IOSurface→原始IOMobileFramebufferLegacy请求→NativeLegacyDisplay校验/映射→CPU copy/diff到GOP→transfer完成。R3已包含该copy路径；与固定NativeMobileDisplay提交比较仅保留现有60Hz适配及真实surface帧分类观察。此次不重复合并复制后端，不增加Keystore安全状态模拟或原始签名代码补丁，不能声称已证明用户空间发现/实际渲染。详细固定commit与diff见out/p20/r4/qualification/render-review.json、cpu-render-diff.patch。

R4保持R3 ADT字节一致、1GiB/CPU1/相同TrustCache/Data，集成sheng上游BootUiTraceProbe.c/Core/Details原文：B=backboardd，S=SpringBoard，T=chronod，U=liveactivitiesd。记录Mach IPC候选对端、用户PC/LR/32层用户栈、选中线程内核栈、上下文切换速率与recount CPU增量。采样是非原子的，首线程不保证主线程；IPC e5仅候选不是确定对端；CPU字段为单核千分比，不是整机CPU总负载。跨用户页读取可能阻塞独立采样worker，显示监视器不等待它，age/busy/trace phase可见。只选这四类的CPU行不覆盖整机忙线程，不能由无CPU行认定CPU空闲。

新增proc_prepareexit只读hook，记录原始wait status/OS reason namespace/code，全部退出计数及保留最近3个非零记录；短IRQ保护+trylock，竞争时丢弃并计数，不改变退出结果/critical panic。另加显示open尝试、type、rc、caller PID、注册回调，以及purgeable/speculative/free-wanted和numvnodes/freevnodes/desiredvnodes/kern_maxvnodes。配方CONFIG_VNODES=1024值得测量，但实际上限尚待实机读数，未据此擅改配置。

字体8×16→12×24，90列、35行、y1008..1847，长地址换行，三页每15秒轮换；顶部常驻MEM/RECLAIM/DISPLAY/OPEN。未变化行跳过重绘，保留CPU1 IRQ保护与state1/2禁止写屏。字号和边界用实际kernel iso_font/实际line函数生成预览并视觉核对；预览是示例数据，不是实机截图。R4仍只支持单核诊断。

主机验证：实际renderer的放大/首末行/越界/dirty cache/长行/接管中断/IRQ恢复、实际process选择与trace派发、exit ring/竞争/默认关闭，ASan+UBSan通过；上游trace格式/VM与IPC引用锁恢复/阻塞worker隔离/32帧往返/已知500‰计时输入通过。实际API仍由主机替身替代，这不是实机等待链、计时准确性或内存完整性证明。现有root-auth、manifest、RAM Data、watchdog/PGZ静态门禁通过。

## R3 内存窗口单变量实验准备（2026-10-24）

R3最终离线门禁`P20_R3_OFFLINE_BOOT_GATE_PASS`。镜像`out/p20/r3/uefi/boot-p20-r3-memory-1g.img`，82,464,768字节，SHA256 `f8649dd1629f769777c0a36df38d61ab03890abdb2f76d82cffe579ca5099b95`。已在身份锁定root Recovery确认后执行一次临时boot，bootloader接受，状态`TRANSPORT_ACCEPTED_SCREEN_PENDING`；还不能证明1GiB分配/XNU管理内存或UI成功。未写分区/未自动重试。下一步读取屏幕MEM managed与clients/frames，保留本次运行。冻结P19/R1/R2哈希复核通过。


用户授权1/2GiB及双核实验，按顺序隔离变量。R3只将`/chosen/darwinboot-memory-size`由512MiB变为1GiB；复用R2内核、cpus=1、硬件身份、原始TrustCache、1GiB逻辑稀疏RAM Data和显示后端。屏幕诊断仍标P20-R2，因为内核字节一致，应以MEM managed读数区分。候选位于`out/p20/r3`；一次临时boot已被接受，实机内存管理与UI结果待屏幕确认；UEFI仍使用已验证的真实EfiConventionalMemory扫描和AllocatePages，分配失败即停，不能把Recovery连续RAM范围当成UEFI分配成功。UFS已有CAP.S64A/物理地址上界校验保留。

上游测试顺序和退出值见`out/p20/host-tests/results.json`。窗口parser、packed RAMdisk、实际R2内核prelink XML lexer、NativeMobileDisplay request-state通过。上游UI layout和UI trace原始host harness存在过期依赖/替身：layout缺bar/BP_BAR_SECTION且断言旧标签；trace缺proc_name、recount_thread_time_mach替身及本机decoder依赖。保留失败日志，隔离host适配后ASan/UBSan通过；没有修改待启动内核或原始测试函数体。trace的CPU计时替身为0，因此不证明计时准确性；捕获/锁/引用/边界/32帧编码解码可测试，实机IPC等待原因仍未知。另增1GiB/2GiB明确尺寸和放置/重复拒绝用例通过。

双核预审：从实际startup编译配方预处理确认MAX_CPUS=8、SMP原子支持；已存本机Recovery FDT CPU affinity 0/1/2/3/0x100..0x103及PSCI smc与ADT相符。仍未证明CPU_ON、次核计时器和IPI成功。ArmCpu次核要求sheng-smp=1，单改cpus=2不够；当前诊断绘屏仅本核IRQ保护，在双核下不保证跨核排斥，需先修复/验证并增加运行读数。未发双核候选。2GiB在1GiB结果后再评估。


## R2 新实机观察：诊断持续运行，显示后端启动但无客户端（2026-10-24）

照片1792831185958.jpg及用户“数字每5秒更新”报告已保存。初步视觉读数t115s/pass23/age0/busy0/tries23/PROC103；MEM managed512MiB/free1084页/wired75359/active20285/inactive20652。free=4.234375MiB，wired约294.37MiB；inactive约80.67MiB，其中可回收比例未知。不能据free一项宣称OOM，也不能把r0快照换算成CPU空闲率。

DISPLAY phase11/value1：本次NativeLegacyDisplay::start返回true。原始family加载、类初始化、IOSurface ready是该路径前提。GOP实测PA0x9d400000、8294400字节、1080×1920、pitch4320；这次终于取得本候选实际映射读回，不用Recovery的4352替代。clients/frames/blank/errors均0、state0，尚无显示用户客户端或帧复制证据；errors0不代表系统无错误。对象启动不独立证明registry所有身份属性可被原始客户端发现，G1用户空间发现项/G2-G4仍未证明。

SpringBoard p27 n2/r0/w2，采样continuation0xfffffff00b844558精确对应本次kernel的ipc_mqueue_receive_continue。只能确认采样时在Mach消息接收等待，尚未识别对端或完整调用栈，不等同已经证明chronod/KeyStore依赖。当前pass未见chronod/liveactivitiesd/backboardd/lsd/cfprefsd，不能说它们从未启动。launchd continuation初读匹配workq_unpark_continue，kernel_task选中线程匹配vm_pageout_continue，均不是独立panic。

目前可排除“整机一直没有调度进展”的说法，不能排除其他线程CPU繁忙或短时反复唤醒。无必要继续盲等APFS日志；下一验证需要真实IPC对端、用户/内核栈和线程CPU时间，并评估较大内存窗口。未增加核心、未重启设备、未重发R2。

## P20-R2 诊断候选：一次临时启动接受，屏幕待观察（2026-10-24）

候选 `out/p20/r2/uefi/boot-p20-r2-observer.img`，82448384 bytes，SHA256 `2f35f2ffd5bda08de4f55db30155d7f55c8e6edc14418284bf42bd430d250f1f`；最终kernel SHA256 `26dec9a70851768e79ccb949a32f07e7b8a3488316b07939f19d9866fad60a36`。门禁P20_R2_OFFLINE_BOOT_GATE_PASS。用户随后确认已手动进入recovery；重新核对固定序列号、cheeseburger/root recovery、bootloader product/unlocked/下载上限后执行一次临时boot。Sending OKAY 1.857s，Booting OKAY 10.094s，总计11.963s，退出0。R2 boot命令1、分区写入0，当前TRANSPORT_ACCEPTED_SCREEN_PENDING；不再次发送、不强制重启。初次只读查询未发现安全状态的历史记录仍保留。

R2只增加观测：复用sheng BootProcessProbe的proc_iterate/任务和线程try-lock、128线程上限、完整快照发布机制；移除本轮不启用的可选用户栈trace/follow-peer接线。使用当前10063.141.1真实头文件与compile recipe编译，PROC flags与原头文件逐项匹配。独立process worker每5秒采样；独立monitor每5秒显示完整快照，若采样忙则保留上一轮并显示age/busy，不让遍历等待直接卡住画面。

启动约30秒仍无图形接管时，常规文字停止逐像素滚屏，全部console字符继续记入256KiB RAM ring；保留诊断页显示线程心跳、内存页数、显示阶段及客户端/真实帧/blank/错误计数。每条诊断扫描线在单核IRQ保护下重查交接状态，避免复制成功后继续写屏；原P19独立fatal路径不受此状态控制。该保护并非SMP方案。

已通过：真实进程采样函数的ASan/UBSan（存在/缺席、任务忙、线程忙、IRQ恢复、128上限、pageout线程、非阻塞重入/快照）；真实扫描线renderer ASan/UBSan（边界、错误pitch、状态1/2禁止覆盖、扫描中途交接、IRQ恢复）；真实console交接及256KiB环形回绕；原Apple闭包和ABI、Root/Manifest/Data/PGZ/watchdog继承；固定解压器正负测试、布局、UEFI PE及最终封装。见out/p20/r2/tests、qualification、logs。宿主测试替代kernel API/ARM barrier，不是实机结果。

保持cpus=1、512MiB窗口、原1GiB RAM-only Data、原TrustCache和ADT身份；不添加KeyStore状态模拟，不禁其他作业，不改原始签名可执行代码。R1镜像/内核/配置源码及P19冻结哈希复核通过。

ADT身份提案9字段通过严格差异/编码往返检查，保存在adt/identity-proposal.NOT-SHIPPED.adt，未打包。真实compatible、安全字段、种子和内存图未变。Recovery物理RAM图包含可容纳1/2/4GiB的候选范围，加载器parser接受这些size；这没有验证UEFI当时的ConventionalMemory/实际AllocatePages或扩大后的XNU映射，故未提高窗口。

当前最深实机证据仍属于R1的launchd Early boot complete与用户读回APFS活动；R2尚无实机G0-G5等级。

## P20-R1 运行照片与后续APFS读回（2026-10-24）

照片1792828484362.jpg保存于out/p20/evidence，SHA核验通过。可见原始launchd Early boot complete. Continuing system boot.、重复trustcache权限诊断、DYLD unnest和AMFI索引警告，未见fatal或原始系统UI。显示检查点可能已滚出屏幕，不据其缺席判定驱动未运行。

随后用户直接读回disk11 xid35、prep351us、flush636us，原文保存在evidence/tx-flush-user-report.json（保留x_flush/owiat原拼写）。flush0.636ms不是打印前等待时间。支持APFS事务活动，不证明SpringBoard推进、写入者、总启动ETA或整机性能瓶颈。仅一条事务不能判断实际flush触发周期或整个事务最终状态。disk11沿用候选RAM Data语义，不表示重新刷写userdata。

当前保持cpus=1；实际XNU窗口512MiB，Data逻辑1GiB、稀疏按需占页。未测得内存余量或CPU利用率，不能断言OOM/单核饱和。下一项需要低频保留显示注册/客户端/真实帧/错误计数、服务等待和内存余量，避免仅靠滚屏末行推断。未重启、未增加核心、未重发候选或修改信任检查。

## 此前构建与传输记录

日期2026-10-24（Asia/Shanghai）。当时传输状态 `TRANSPORT_ACCEPTED_SCREEN_PENDING`。

- 镜像：`out/p20/uefi/boot-p20-display-r1.img`，82456576 bytes。
- SHA256：`5ee950a1098795d4b3412e244247b173cce9e83d990685e9c1d62236c949c42c`。
- 最终kernel SHA256：`f12bd841e766963e102fdc6845bbd6f7854d09f67827b47fdde41742d201706f`。
- 门禁：P20_OFFLINE_BOOT_GATE_PASS；其范围为静态一致性及首轮物理显示集成候选，非运行成功。
- Fastboot boot次数1；Sending OKAY 1.853s，Booting OKAY 10.094s，总计11.975s，退出0。
- 真实bootloader max-download-size=805306368 bytes（768MiB）。
- 分区写入0，userdata未重刷，未执行flash/erase/-w，已固定序列号及cheeseburger recovery身份。
- 传输原始记录：`out/p20/private/20261024T074533Z-p20-display-r1-once-a489a7`；汇总out/p20/boot-result.json。

本轮在用户确认进入recovery并授权完成后实测后，先只读核对型号/root recovery/内存/fb0，再adb reboot bootloader，核对product msm8998、unlocked yes及下载上限，SHA校验后只执行一次fastboot boot。当前未再次轮询、重启或重发。

| 等级 | 本候选实机结论 |
| --- | --- |
| G0 P19链保持 | 静态继承检查通过；本次根卷/SystemOS/Data/原用户态的实机画面待提供 |
| G1 原始图形组件注册 | 原始代码确实在最终内核，动态加载/发布未观测 |
| G2 实际客户端与IOSurface帧 | 未观测，不能把通用client回调等同backboardd身份 |
| G3 物理系统帧 | 未观测 |
| G4 Hello/Setup/SpringBoard | 无实机照片，不宣布成功 |
| G5 交互 | 未实现/未验证 |

原P19-R2历史成功与本次状态分开记录。P19的Early boot complete照片不能充当P20运行证据。当前P20尚不能授予G0–G5运行等级。

完成检查：原始30组件逐项代码哈希/依赖/kmod、358槽ABI与新别名绑定、Root/Manifest/Data/PGZ/watchdog策略继承、无未定义符号、像素边界ASan/UBSan、console交接ASan/UBSan、固定EDK2解压器正负测试、payload与种子/TrustCache/ADT一致性、UEFI PE与压缩封装往返、冻结P19哈希。报告在qualification、tests、logs和offline-gate.json。

当前未取得新fatal或屏幕证据，不能指定一个已证实的运行故障。下一步只读手机画面，先确认P20 display检查点或第一处fatal。潜在限制包括512MiB窗口、缺KeyStore事件、平台显示身份/服务等待；它们尚非本机已证实首错，不批量修改。

R3用户后续确认：MEM managed已变1GB，诊断时间持续增加，未见panic。这是用户读数证据，尚无本轮新照片/clients/frames/free/PID读数；可确认窗口生效，不能推断UI将自行出现，也未完成全部内存完整性测试。

R3新照片1792843597347.jpg：t457s/pass91/age0/busy0/PROC102，managed1024MiB，free1504页(5.875MiB)、wired77778页(303.82MiB)、active20876页(81.55MiB)、inactive143282页(559.70MiB)。DISPLAY phase11/value1、clients/frames/blank/errors全0；SpringBoard p27两线程均WAIT，采样continuation仍为ipc_mqueue_receive_continue；backboardd/chronod/liveactivitiesd/lsd/cfprefsd本轮缺席。1GiB已生效，但尚未越过显示客户端连接停点；inactive增加不能直接认定为不可用或全部可回收；不能凭两个跨启动样本断言持续死锁。下一优先真实IPC对端、用户栈及退出记录，暂不根据低free直接升级2GiB或认定CPU慢。

R4四张实机照片(t120..156s)确认新诊断正常。DISPLAY phase11/reg1、OPEN tries0、clients/frames0。t136s VNODES used1024/free567/desired1024/DTmax0；1024是numvnodes总数，包含free缓存节点，不能称vnode已耗尽。EXITS total110/nonzero13/dropped0：usermanagerd PID111/113/115/117多次以st6/ns2/code6即SIGABRT终止；IOMFB_bics_daemon(照片视觉转录) PID86记录st b/ns2/code b即SIGSEGV11。非零退出不等同kernel panic；total包含正常退出，不能称110次崩溃。SpringBoard p27两线程WAIT，用户栈读取成功，peer=-1/e5是receive参数候选，未辨认真实IPC对端。free-wanted0及无hot rows不能排除其他时段内存压力/其他线程CPU占用。下一优先抓显示守护进程SIGSEGV的PC/FAR/ESR和usermanagerd SIGABRT原因，再做本机栈符号化；不能仅凭截图把这些崩溃归因到vnode/CPU/GPU。

R5新照片t136..166s：desired17408、numvnodes1983/free1467，配置修正生效；usermanagerd PID113/115/117/119/121/123仍SIGABRT(ns2/code6)。OPEN tries0/clients0/frames0；最近3条退出未见bics不能证明其未崩溃。早期白字shared_region_start_address failed两条不能当作后续已运行进程的终止性dyld失败。下一候选仅增官方异常现场/限定原因扫描，保留R5配置和原种子。


## R6 runtime evidence and R7 direct abort reason (2026-10-24)

USB work is paused by the user. R6 photo out/p20/r6/evidence/1792854940477.jpg at t5516s/pass1103 records 121 matching usermanagerd fault exits, zero matching bics exits since probe enable, and zero display clients/frames. The bounded scan attempted 4MiB in 26 regions with no copy failures and no matching text. This does not prove no reason exists.

Original 21H461 usermanagerd was extracted read-only: SHA256 33caaa27384e0f99738dc3f9d81644c75e1b13d2f2f711f1feb83021e3f54e11. Original cache/symbol UUIDs match. Legible return address 0x1d5f60b4c minus photographed slide 0xa7b0000 resolves to _abort_with_payload_wrapper_internal. Uncertain PC/LR transcription and unproved executable ASLR are excluded from conclusions. The volumes have been detached.

R7 adds bounded observation before the original abort_with_payload_internal reason construction: first matching fatal usermanagerd call, diagnostics enabled and interrupts enabled; records namespace/code/flags, at most 384 reason bytes and 32 payload bytes, with read errors. Original reason construction, signal and fatal paths remain. Page 4 prioritizes the ABORT record. No USB driver, userland patch, fabricated keys/personas or Data change. See out/p20/r7/README.md; status.json and boot-result.json distinguish packaging/transport from hardware/UI evidence.

R7 final: P20_R7_OFFLINE_BOOT_GATE_PASS; image SHA256 6459df6d9e0ecf4099f85d36885820f4b777888bd3c608838a4a9e1d70fe4408; 82460672 bytes. One identity-pinned temporary boot accepted (Sending 1.854s; Booting 10.093s; total 11.962s). State TRANSPORT_ACCEPTED_SCREEN_PENDING; partition writes zero, no retry. Hardware diagnostic page and UI are pending screen evidence.


## R7 explicit abort reason; R8 Data skeleton candidate (2026-10-25)

User transcribed R7 page4: usermanagerd p89, ns2/code6, reason bytes39/read err0, message matching original "Daemon failed to load persona manifest." Payload null/size0. This establishes the daemon's explicit fatal reason, not the underlying file errno/parse or kernel-persona failure. Evidence: out/p20/r7/evidence/abort-reason-user-transcription.json.

Official prepare-offline-data.py explicitly creates empty user-management directories for original usermanagerd initialization. R8 adds 20 missing directory names to a fresh clone of the frozen 1GiB Data seed, including keybags and PersonaVolumes. Source provenance confirms fetched and pinned scripts identical; upstream fixture is QEMU-only, so this board adaptation requires real testing. Administrator execution preserved root/mobile ownership. Read-only remount verified all existing paths unchanged except macOS FSEvents; only empty directories added. fsck_apfs -n passed; packed logical hash equals raw hash; UUID/role NONE/unencrypted RAM semantics preserved. No keys, users, persona records or original system files modified.

R8 reuses the exact R7 kernel and diagnostic source; blue header still P20-R7 is expected. Candidate status in out/p20/r8/status.json; prior diagnostics tests are inherited with explicit provenance, not falsely rerun. New Data/payload/decoder/layout and package checks are candidate-specific. Persona initialization and UI remain unverified until fresh hardware evidence. USB work stays paused.


## R8 user-deployed progress and R9 compact diagnostics (2026-10-25)

User confirms boot-p20-r8-data.img was deployed using another tool. Agent boot commands/partition writes for R8 are zero; external deployment was not executed or independently read back by this agent. User reports no usermanagerd error and display clients latest7 (transcribed pages4), frames0, phase14, last open rc0, backboardd/lsd now present. This supports progress beyond R7 persona fatal, not complete UI or proof all persona state is healthy. Other daemon names/statuses and times are transcription-ambiguous. Evidence: out/p20/r8/evidence/user-runtime-progress.json.

Source audit: clients counts successful opens cumulatively, not concurrent live clients; phase14 is last display-client callback, not boot completion. frames counts successful transfers; errors0 cannot exclude rejected/no submitted swaps. R9 adds bounded diagnostic counts for checks/accept/reject/prepare/transfer/source/format and bounded reason/register capture for PreBoard/backboardd/SpringBoard/ReportCrash. It keeps R8 Data exactly and original driver/exit policy. Per user request screen shrinks4 to2 pages, each15s: concise overview and UI failures. Raw address grids omitted while existing sampler still runs. No device transition is planned while user R8 experiment is active; R9 is a new untested candidate until explicitly observed.

R9 final P20_R9_OFFLINE_BOOT_GATE_PASS. Image out/p20/r9/uefi/boot-p20-r9-compact.img; 82460672 bytes; SHA256 f66cc81384d7fd1e720bd6084c75d1f8b58cb7644000dce2c0a087552ad6265a. Two pages, 15s each, font12x24. No device transition/boot/flash executed for R9; current user-deployed R8 preserved. Await Recovery for a single new-candidate temporary boot.

R9 one temporary boot on 2026-10-25 (Asia/Shanghai): fixed-device root Recovery verified, bootloader product/unlock/download limit checked; one fastboot boot accepted. No partition writes or retry. State TRANSPORT_ACCEPTED_SCREEN_PENDING. Screen should identify P20-R9 and two pages; first frame/UI remain unobserved. Raw transport: out/p20/r9/private/20261024T170158Z-p20-r9-compact-once-80f493.


## R10 original service audit and native SystemOS union (2026-10-25)

R9 reports SpringBoard14 fatal exits, ns4/code1 with original libobjc-trampolines dlopen reason, and zero swap checks. Two bounded read-only subagents audited service dependencies and upstream missing-library history. Main-agent compile replay proves old nonunion mode; original target lib exists and hash matches upstream. R10 enables native readonly SystemOS union at /, disables old fallback/carrier experiment, and syncs the two official union-directory patches, preserving original services and all authorization/fatal checks. Two sets of11 actual-function host cases pass with explicitly mocked filesystem APIs. See docs/P20_UPSTREAM_SERVICE_AUDIT.md and out/p20/r10/qualification/upstream-union.json. R10 was accepted for one temporary boot from confirmed root Recovery; no flash/retry. Screen ROOT/loader/service/frame results pending.


## P20-R11: complete pinned upstream rebuild

Image `out/p20/r11/uefi/boot-p20-r11-upstream.img` (SHA-256 `c54218839149d1c1e5dcf8419740fa1b6d2679011325ce2a847c9d79ffee96de`) passed the R11 offline gate and one temporary boot was accepted. GUI/screen evidence remains pending. All 58 recorded upstream XNU/NativePrelink patches and native source archives were rebuilt; prior native objects and the custom P20 observer were not imported. Stock Legacy display scale/refresh initialization and notification-only key events are selected. Necessary MSM8998-only adapters remain development code. Native SystemOS union is on, obsolete cache carrier is off, cpus=1, owned memory=1 GiB, R8 Data input unchanged, persistent store off. See `../out/p20/r11/README.md`, `offline-gate.json`, and `boot-result.json` for current evidence. This supersedes older pending build claims without rewriting their historical runtime records.


## P20-R12: compact observer restored

R11's silent loader screen revealed a confirmed diagnostic-path omission, not proof of a kernel-entry failure. R12 restores bounded entry/exception markers, phone text output and a single 12x24 blue overview refreshed every five seconds. It reads stock display counters and stops drawing at the existing upstream scanout handoff. Stack page rotation, per-task BootUiTrace and fallback memory reason scanning are omitted. The stock display archive, R11 root/storage policies and R8 Data input are preserved. `out/p20/r12/offline-gate.json` passed; the initial state check found no Recovery/bootloader. After the user confirmed Recovery, one R12 temporary boot was accepted with no partition writes or retries. Screen/GUI evidence remains pending. See `../out/p20/r12/README.md`.


## P20-R13: pthread provider conflict corrected

R12 user logs identify `Re-initialization of pthread kext callbacks ... pthread_shims.c:631`, after VM and IOKit/NVRAM initialization. The native source pthread startup and original `com.apple.kec.pthread` were both selected. R13 removes the redundant native library/bootstrap, retains the original component and registration guard, and keeps the R12 compact observer. First-fatal display/park is restored so later errors retain their initial message instead of ending only in `MACH Reboot`. Image `out/p20/r13/uefi/boot-p20-r13-pthread.img`, SHA-256 `a41770ca08942c8149761020b16894e73d46dcbff5665ccc1fd7771df5932bc8`, passed host/static/offline checks. Current transport state: `TRANSPORT_ACCEPTED_SCREEN_PENDING`; screen/GUI evidence pending. No partition/userdata writes or retries. See `../out/p20/r13/README.md`.
