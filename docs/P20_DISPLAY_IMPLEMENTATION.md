# P20 R1 显示实现与边界

## R6 异常现场诊断；USB日志可行性（2026-10-24）

R5新照片已确认desired17408、总vnode1983/free1467，但usermanagerd仍反复SIGABRT，OPEN/clients/frames仍0。最近退出列表无bics不足以证明bics不再出错。R6保持R5 ADT、CPU1、1GiB、Data及安全输入；仅增加诊断，不猜测修复其他服务依赖。

采用官方sheng251b291 BootFaultProbe：捕获EL0 breakpoint和原始VM处理后仍未解决的user abort，记录PID/name/PC/LR/SP/FP/ESR/FAR/slide。异常路径不读取用户内存、不分配、不遍历进程；kernel sleh仅增加受p20_diag_enabled控制的两个观察调用，原异常分发不变。退出路径用官方formatter关联PID并取当前线程寄存器/最多12个返回地址。此读取可缺页，可能延迟该正在退出的线程；不能声称零开销，但监视器/其他退出观察者不等待它。

新增第四页FAULT：保留usermanagerd与IOMFB_bics_daemon各自整个本次启动的匹配信号退出计数，最多各取前2次详细信息，避免被最新3条退出覆盖。只对usermanagerd首个SIGABRT做一次可写malloc区域原因扫描。官方160MiB预算收紧为4MiB尝试读取（失败也计预算），最多128区域，零长度/地址溢出停止；匹配到完整uncaught exception文本提前停止。显示扫描字节/区域/失败/rank，未找到原因不代表没有原因。细节读取在普通退出上下文、IRQ开启且不持有本探针短锁；缓慢读取不会阻塞屏幕monitor，其trylock只显示busy并保留上次完整值。

字体仍12×24，每15秒轮换4页；VNODES原误导性used标签改为total，DTmax改kglob（原kernel全局值，不冒称ADT属性读回）。第四页重点拍USERMANAGERD PC/LR/ESR/FAR/slide/bt及abort reason，CPU页不是整机CPU采样。

主机ASan/UBSan验证实际fault捕获/退出formatter、PID关联/环覆盖/copy失败/默认状态与SIGABRT无硬件异常时的寄存器回退；实际原因扫描验证4MiB/128区域界限（含全部copy失败）、命中/未命中/零长度/重入/输出边界；命名目标明细次数限制、一次扫描、屏幕wrap与竞争也通过。根授权/manifest/Data/PGZ/watchdog静态复核通过；不是实机原因证明。

用户新增USB实时日志需求，取官方文档列出的NativeDwc336f968d9d39b147de334decd9edf3fe5b0993492确认：SM8150/nabu的控制器初始化参考尚未实现EP0枚举和bulk日志端点。本机USB FDT已保存，主机未枚举串口；接收脚本out/p20/usb-log/receive.py与伪终端测试通过，但手机端gadget未实现/未收实时USB日志。独立ring/低优先级有界消费/溢出计数设计见同目录README；当前R6不加载USB驱动、不写USB寄存器，避免把USB控制器bring-up与用户管理诊断混在一起。

## R5 官方差异同步：启动环境与vnode默认值（2026-10-24）

刷新官方framework到bd127c008cdc584b83cf00eaea269e4f1adf3cc8引用；未改固定checkout/构建缓存。sheng251b291与QEMU97b75b1未变化。框架新变化以Linux构建工具及legacy编译条件为主，不整体合并到MSM8998已验证内核。比较与决策见out/p20/r5/qualification/upstream-refresh.json和upstream-decisions.json。

找到官方check-original-bics-environment.py：固定daemon SHA256为2d1f67b75fb33d091396c70aee627bb6a2c8dcdb03ffa7401378b7a688088b53，执行原始ARM64分支，API由stand-ins替代，区分缺失属性导致NULL_CFData、main正常继续和darwinos-ramdisk有意等待。官方sheng/QEMU ADT设/chosen/osenvironment=main，本机R4缺失该属性。R5同步main；它描述要启动的正常OS环境，不把临时RAM boot误设为recovery/ramdisk。实机SIGSEGV是否因此消失仍待验证。

同时同步官方/defaults/kern.maxvnodes计算方法：当前管理1GiB，1048576KiB/64KiB+1024=17408，不拷贝QEMU4GiB的66560。R4实际目标1024已确认，但free567不能证明耗尽。两项ADT白名单以外完全不变；复用R4 kernel及诊断、CPU1、1GiB、原始TrustCache和Data种子，不追加安全状态/密钥/用户记录。原签名可执行代码不变。

Data目录helper及-apfs_shared_datavolume差异被记录为另一个QEMU实验条件，未据SIGABRT直接推断为本机原因。本轮先验证具体bics环境缺失修复，不同时变更Data或新加陷阱诊断。若修复后仍有原崩溃，再使用官方BootFaultProbe取PC/FAR/ESR/原因。这是按用户“找到修复先修复，否则加诊断”的分支执行。

## R4 上游CPU链路复核与增强诊断（2026-10-24）

R3的1GiB已由新照片确认，但457秒时clients/frames仍0，backboardd本轮缺席。PROC是当前进程数量，pass是采样轮数，没有“采满进程数”目标；进程缓增不等同渲染进展。

上游CPU链路为原始QuartzCore/backboardd绘制→IOSurface→原始IOMobileFramebufferLegacy请求→NativeLegacyDisplay校验/映射→CPU copy/diff到GOP→transfer完成。R3已包含该copy路径；与固定NativeMobileDisplay提交比较仅保留现有60Hz适配及真实surface帧分类观察。此次不重复合并复制后端，不增加Keystore安全状态模拟或原始签名代码补丁，不能声称已证明用户空间发现/实际渲染。详细固定commit与diff见out/p20/r4/qualification/render-review.json、cpu-render-diff.patch。

R4保持R3 ADT字节一致、1GiB/CPU1/相同TrustCache/Data，集成sheng上游BootUiTraceProbe.c/Core/Details原文：B=backboardd，S=SpringBoard，T=chronod，U=liveactivitiesd。记录Mach IPC候选对端、用户PC/LR/32层用户栈、选中线程内核栈、上下文切换速率与recount CPU增量。采样是非原子的，首线程不保证主线程；IPC e5仅候选不是确定对端；CPU字段为单核千分比，不是整机CPU总负载。跨用户页读取可能阻塞独立采样worker，显示监视器不等待它，age/busy/trace phase可见。只选这四类的CPU行不覆盖整机忙线程，不能由无CPU行认定CPU空闲。

新增proc_prepareexit只读hook，记录原始wait status/OS reason namespace/code，全部退出计数及保留最近3个非零记录；短IRQ保护+trylock，竞争时丢弃并计数，不改变退出结果/critical panic。另加显示open尝试、type、rc、caller PID、注册回调，以及purgeable/speculative/free-wanted和numvnodes/freevnodes/desiredvnodes/kern_maxvnodes。配方CONFIG_VNODES=1024值得测量，但实际上限尚待实机读数，未据此擅改配置。

字体8×16→12×24，90列、35行、y1008..1847，长地址换行，三页每15秒轮换；顶部常驻MEM/RECLAIM/DISPLAY/OPEN。未变化行跳过重绘，保留CPU1 IRQ保护与state1/2禁止写屏。字号和边界用实际kernel iso_font/实际line函数生成预览并视觉核对；预览是示例数据，不是实机截图。R4仍只支持单核诊断。

主机验证：实际renderer的放大/首末行/越界/dirty cache/长行/接管中断/IRQ恢复、实际process选择与trace派发、exit ring/竞争/默认关闭，ASan+UBSan通过；上游trace格式/VM与IPC引用锁恢复/阻塞worker隔离/32帧往返/已知500‰计时输入通过。实际API仍由主机替身替代，这不是实机等待链、计时准确性或内存完整性证明。现有root-auth、manifest、RAM Data、watchdog/PGZ静态门禁通过。

实际源码、构建脚本、对象和报告统一保存在 out/p20。尚无本候选运行画面。

原闭包19个可执行组件全部保留，新增11个（真实原始依赖，不是额外硬件驱动启用承诺）：

- `com.apple.driver.AppleAudioClockLibs`
- `com.apple.driver.AppleIISController`
- `com.apple.driver.AppleM2ScalerCSCDriver`
- `com.apple.driver.IODARTFamily`
- `com.apple.iokit.IOAVFamily`
- `com.apple.iokit.IOAudio2Family`
- `com.apple.iokit.IOCECFamily`
- `com.apple.iokit.IOHDCPFamily`
- `com.apple.iokit.IOMobileGraphicsFamily`
- `com.apple.iokit.IOSurface`
- `com.apple.kec.Libm`

完整依赖表见 out/p20/qualification/component-closure.json。最终30组件的kmod入口、依赖、原始可执行段SHA与重定位写入逐项通过 qualification/result.json。原始IOMobileFramebufferLegacy大小0x18a0，358槽、9抽象叶节点；继承/构造链、IOSurface getter与软件vsync sender按固定KernelCache真实指令核验。未知叶节点保留fatal，不用空实现表示支持。

R1活动CPU路径：原始系统进程/QuartzCore → 原始IOSurface、IOMobileGraphicsFamily → NativeLegacyDisplay → 同一驱动内的native_legacy_copy_pixels / native_legacy_copy_diff → 运行时GOP映射。上游NativeGOPPresenter源码已保留供后续接口比较，但独立NativeGOPPresenter服务未链接/发布；没有为了拼接图名添加多余服务，亦不宣称该服务已工作。CPU路径沿用Pagani成功候选的实际实现。

源采用PE_current_console读取宽高/rowBytes/baseAddr，限定32bpp、无旋转，最大16384，真实stride≥width*4并检查乘法溢出。provider内存长度为真实rowBytes*height；kIOMapInhibitCache映射，PA和长度须与PE信息匹配，并与P19诊断记录p6_diag_record[29]/[17]核对。源IOSurface只读映射，检查格式、planes、allocation、descriptor长度、pitch及完整可见帧，禁止多层/裁剪/旋转请求被冒充支持。像素复制后dsb sy，再调用原始swap完成；没有实现物理vblank/触控/GPU。

UEFI静态GOP声明1080×1920、PixelBlueGreenRedReserved8BitPerColor、32bpp，PixelsPerScanLine=1080，rowBytes=4320，长度8294400。地址静态默认0x9d400000；源取自实际构建overlay和上游SimpleFbDxe。该声明不等于本次实测读取。Recovery实时fb0为1080×3840虚拟双缓冲、32bpp、stride=4352，name=mdssfb_90000，不能用它代替UEFI GOP，也不据宽高推断pitch。XNU需实际运行后核对GOP映射；未确认面板额外保活需求，不改写DSI。

P8早期文字保留。out/p20/source/osfmk/console/serial_console.c新增p20_scanout_state：0普通绘字，1真实提交的复制处理中，2成功复制后。所有阶段继续更新原32KiB RAM ring。第一次复制失败恢复0；成功后普通文字不覆盖帧。P19的独立Capture fatal路径及其直接framebuffer写入保持原archive，不受普通console状态限制。tests/handoff.json直接执行本次console和handoff函数（宿主以compiler fence替代ARM dsb），验证早期画字、pending/committed不覆盖、首错回退和ring持续。panic实机恢复仍待验证。

内存：当前Recovery MemTotal=5996429312 bytes，属6GB机型范围；当前ADT仍请求512MiB连续窗口。布局静态上界148897792 bytes < 512MiB，仅证明载入布局能放下，不证明完整系统/IOSurface分配够用。未盲目请求Pagani的4GiB，未改保留区。Data继续原合格1GiB DBRDISK1 seed，UUID D2420479-F58E-44C5-AB79-1815EC32C686，RAM-only；未扩容、未覆盖、未刷userdata。

仅保留原watchdogd开发态作业隔离，不禁用其他服务，不改原始代码签名/CodeDirectory/AMFI策略。KeyStore事件服务尚未集成：QEMU的MKBEventsRegister→liveactivitiesd→chronod→SpringBoard依赖仅为本候选后续候选方向，尚无本机同一等待栈。拒绝将lock_state=0或无硬件Keybag冒称真解锁。

G1显示注册、G2客户端/IOSurface、G3真实物理帧、G4 Hello/Setup/SpringBoard均需新的实机证据；离线报告不替代。

## R2 观测字段

实现：out/p20/r2/source/BootProcessProbe.c、P20Observer.c、P20DisplayProbe.cpp及私有serial_console/bsd_init。process probe源自sheng251b291，保持真实任务/线程取锁及快照机制，未链接BootUiTraceProbe，所以显示的是采样的wait_event、continuation和userPC，尚不能直接声称已经追出完整XPC对端。

页底16行：OBS的t是本诊断启动后秒数，pass是完整采样次数，age是完整结果未更新秒数，busy是采样worker标志；MEM managed来自真实max_mem，free/wired/active/inactive单位为页（本候选4KiB页），近似分项采样而非原子总和；r表示TH_RUN可运行/运行状态计数，不是CPU占用率，w表示TH_WAIT。n是任务线程总数；b是取锁忙的线程数；t表示是否取得选中线程现场；W/C/U依次为等待事件、内核continuation、用户PC。进程第一条线程不保证永远是main，kernel_task选VM_pageout_scan。未被本轮采样选中/暂时不在进程表不等于从未启动。

显示phase为最后记录检查点：1 family-enter、2 family-load返回、3 class-register、4 allocate、5 init-enter、6 initialized、7 IOSurface-enter、8 IOSurface-ready、9 probe、10 start-enter、11 started、12 registered、13 client-enter、14 client返回、15 GOP-map、16 swap拒绝、17 copy失败、18 surface拒绝。value随阶段为返回码/布尔/类型/尺寸等；客户端回调不证明调用者就是backboardd。frames仅由真实surface复制成功后累计，blank独立计数，不是帧率或UI证明。

## R3 内存实验与SMP审查


R3最终离线门禁`P20_R3_OFFLINE_BOOT_GATE_PASS`。镜像`out/p20/r3/uefi/boot-p20-r3-memory-1g.img`，82,464,768字节，SHA256 `f8649dd1629f769777c0a36df38d61ab03890abdb2f76d82cffe579ca5099b95`。已在身份锁定root Recovery确认后执行一次临时boot，bootloader接受，状态`TRANSPORT_ACCEPTED_SCREEN_PENDING`；还不能证明1GiB分配/XNU管理内存或UI成功。未写分区/未自动重试。下一步读取屏幕MEM managed与clients/frames，保留本次运行。冻结P19/R1/R2哈希复核通过。

R3显示实现、原始组件及观察器均复用R2。SMP审查发现本核IRQ关闭不能提供跨核绘屏排斥；因此CPU2尚未发出，必须先增加跨核同步及次核CPU_ON/timer/IPI运行证据。构建配方MAX_CPUS=8和实际Recovery FDT PSCI smc已离线核对。

USB日志需求（2026-10-24）：已取官方文档列出的NativeDwc3 36f968d9d39b147de334decd9edf3fe5b0993492，仍缺EP0枚举和日志端点；目标nabu SM8150，不能复制其硬件值。已保存本机USB FDT，主机CDC接收脚本与伪终端测试完成，但未实现/未收到XNU设备USB日志；当前主机无USB串口。设计与明确阻碍见out/p20/usb-log/README.md。当前未写USB控制器、未装载参考驱动。


## R8 user-deployed progress and R9 compact diagnostics (2026-10-25)

User confirms boot-p20-r8-data.img was deployed using another tool. Agent boot commands/partition writes for R8 are zero; external deployment was not executed or independently read back by this agent. User reports no usermanagerd error and display clients latest7 (transcribed pages4), frames0, phase14, last open rc0, backboardd/lsd now present. This supports progress beyond R7 persona fatal, not complete UI or proof all persona state is healthy. Other daemon names/statuses and times are transcription-ambiguous. Evidence: out/p20/r8/evidence/user-runtime-progress.json.

Source audit: clients counts successful opens cumulatively, not concurrent live clients; phase14 is last display-client callback, not boot completion. frames counts successful transfers; errors0 cannot exclude rejected/no submitted swaps. R9 adds bounded diagnostic counts for checks/accept/reject/prepare/transfer/source/format and bounded reason/register capture for PreBoard/backboardd/SpringBoard/ReportCrash. It keeps R8 Data exactly and original driver/exit policy. Per user request screen shrinks4 to2 pages, each15s: concise overview and UI failures. Raw address grids omitted while existing sampler still runs. No device transition is planned while user R8 experiment is active; R9 is a new untested candidate until explicitly observed.


## P20-R11: complete pinned upstream rebuild

Image `out/p20/r11/uefi/boot-p20-r11-upstream.img` (SHA-256 `c54218839149d1c1e5dcf8419740fa1b6d2679011325ce2a847c9d79ffee96de`) passed the R11 offline gate and one temporary boot was accepted. GUI/screen evidence remains pending. All 58 recorded upstream XNU/NativePrelink patches and native source archives were rebuilt; prior native objects and the custom P20 observer were not imported. Stock Legacy display scale/refresh initialization and notification-only key events are selected. Necessary MSM8998-only adapters remain development code. Native SystemOS union is on, obsolete cache carrier is off, cpus=1, owned memory=1 GiB, R8 Data input unchanged, persistent store off. See `../out/p20/r11/README.md`, `offline-gate.json`, and `boot-result.json` for current evidence. This supersedes older pending build claims without rewriting their historical runtime records.


## P20-R12: compact observer restored

R11's silent loader screen revealed a confirmed diagnostic-path omission, not proof of a kernel-entry failure. R12 restores bounded entry/exception markers, phone text output and a single 12x24 blue overview refreshed every five seconds. It reads stock display counters and stops drawing at the existing upstream scanout handoff. Stack page rotation, per-task BootUiTrace and fallback memory reason scanning are omitted. The stock display archive, R11 root/storage policies and R8 Data input are preserved. `out/p20/r12/offline-gate.json` passed; the initial state check found no Recovery/bootloader. After the user confirmed Recovery, one R12 temporary boot was accepted with no partition writes or retries. Screen/GUI evidence remains pending. See `../out/p20/r12/README.md`.
