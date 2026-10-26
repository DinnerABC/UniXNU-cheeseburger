# P20 选择性上游集成

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

日期：2026-10-24，Asia/Shanghai。当前候选 R1 只取得 Fastboot 传输/启动接受，实机画面待提供。

| 来源 | 实际 commit |
| --- | --- |
| local | `c5463363de7ca0ad541b8132f29c7f0a3cef718c` |
| local_unixnu | `b6ae99c17194e6a18fd819d3148e69fe405e91fc` |
| official_current | `1a01c2afa197d975d04da892325c56b6b14aaecb` |
| sheng_local | `0c2046b84dbee545e7e3923eb74d7bb1ce020978` |
| sheng_current | `251b291adf941b9953fe3ae66a808b9b626e164d` |
| pagani_current | `69952a98c9180da1d8e579c9c9bcb13df5b12a4b` |
| qemu_current | `97b75b1f63c6b7e895cd7646029db741d5fe5488` |
| display | `8666a9950c9699b368931d18cac92c0fdb2ce9c0` |
| pagani_run39 | `02c7ccd3286b661bef4513a7de50dba5153f8e6d` |

Pagani 的 `02c7ccd` 已作为真实 Git 对象取得，完整 SHA 见上表；该提交到当前 HEAD 的 scripts/drivers/patches/board.toml 无代码差异，后两次提交为文档与运行记录。已阅读 README、STAGE6（含37/38/39）、board.toml、实际显示构建脚本及补丁。源码位于 out/p20/upstream，未在 P19 工作树 git pull/checkout。

| 来源文件/机制 | 目标 | 选择及适用条件 | 冲突、未合并原因 | 验证 |
| --- | --- | --- | --- | --- |
| Pagani scripts/original_stage7.py、audit-stage7-display.py、build-stage7-display.py | out/p20/scripts 同名 | 采用相同21H461 KernelCache哈希的静态358槽ABI审查 | 去除其绑定在一起的KeyStore/Stage7Probe对象；仅显示构建 | logs/display-build-05.log、build/native-legacy-display/build-report.json |
| 官方 NativeMobileDisplay NativeLegacyDisplay.cpp / NativeLegacyRequest.h | out/p20/drivers/NativeMobileDisplay | 复用原始Legacy生命周期、IOSurface getter、实际CPU像素复制与原始swap完成 | 保留未知接口fatal guard；不引入GPU；板级回调独立 | tests/pixels.json、qualification/result.json |
| Pagani Stage7Probe.cpp | out/p20/source/P20DisplayProbe.cpp | 复用原始类晚注册、IOBSD后IOSurface匹配、GOP provider/device-tree nub | 删除SM8750标签、HID/KeyStore状态模拟；核对P19映射PA及长度 | logs/probe-build-02.log、qualification/display-start.asm |
| Pagani第39版/官方8666a995 work-loop定时器 | NativeLegacyDisplay.cpp | 采用真实时钟驱动软件刷新；移植Pagani允许60Hz发布属性的两处范围判断 | 无物理vblank/帧率证明；不宣称60fps | 原始sender指令与绑定审查通过；实机未验证 |
| 官方 bsd_autoconf / offline-legacy-start 顺序 | source/bsd/kern/bsd_init.c | IOKitBSDInit返回后加载图形类与IOSurface、发布显示 | 独立编译、私有link.filelist替换，不写共享活对象 | qualification/display-start.asm |
| QEMU README、scripts/build-display-abi.py、build-native-legacy-display.py、LegacyDisplayProbe、KeyStoreProbe、HidInputProbe/config | 显示设计参考 | 原始CPU renderer→IOSurface→Legacy→GOP；关注definesDisplayBounds及MKBEventsRegister依赖 | 不运行QEMU、不引入VirtIO/HVF/ramfb/VM身份；静态ABI优先采用Pagani同哈希审查，避免伪造QEMU运行观测 | 当前缓存源码及本地ABI报告 |
| sheng LegacyDisplayProbe / build-native-legacy-display | 原始类生命周期与闭包参考 | 同版原始Legacy/IOSurface接口；GOP软件路径 | 不移植触控、Adreno740、PMGR寄存器、其他板内存布局 | 目标原KernelCache及依赖表验证 |
| 最新官方 scripts/unixnu/kernel.py | 未合并 | 已比较，新增XNU_BUILD_ARCHIVES开关和storage headers准备 | 本轮固定成熟手工增量链接/存储ABI已就绪，无显示依赖，不升级整个构建系统 | upstream/core-differences.json及对应diff |
| Pagani用户态页补丁、8项作业隔离、4GiB窗口 | 未合并 | 不在R1启用 | 本轮禁止原始签名可执行代码页补丁；其他7作业无本机阻塞证据；4GiB连续窗口未验证 | 原始用户态/签名信任链保留 |

源XNU固定10063.141.1。原始21H461 donor内部XNU版本10063.142.1.703.8，沿用P19已记录的版本差异，不宣称完整ABI相等。显示审查只限定选中的358槽与方法指令/类范围，runtime_qualified仍false。

保留 P19 prelink slide `-0xc2d000`，避免破坏既有原始IOStorageFamily绝对绑定。Pagani的审查地址规范化为donor-0x1000000；仅在审查通过后将新显示别名加0x3d3000，绑定到本候选实际slide。未复制另一SoC硬件地址。

## 后续：早期用户空间后无UI/慢输出案例复查（2026-10-24）

范围：官方文档和当前缓存QEMU/Pagani/sheng，加新取得tb128fu、fajita、negroni独立源码快照。完整提交见out/p20/upstream/slow-boot-case-pins.json；本轮只读源码和归档，无构建/设备操作。未在这组公开README/STAGE记录中找到把tx_flush的636us当慢启动根因、仅增加核心即解决的案例。

| 案例 | 直接证据 | 与P20关系/限制 |
| --- | --- | --- |
| QEMU README:106-118 | 原始CPU renderer可显示Hello；SpringBoard→chronod→liveactivitiesd同步等待/崩溃链，MKBEventsRegister缺AppleKeyStore返回NULL；补上NativeKeyStoreEvents后链路完成 | 最明确的服务依赖案例；P20缺此事件服务，但未在一加5取得同等待栈，不直接复制锁状态或假解锁 |
| QEMU内存 | 4GiB DarwinBoot管理窗口的完整用户态报告仅约17MiB余量，Data约260MiB按需页；32MiB Data曾造成backboardd ENOSPC | 当前512MiB窗口值得优先量测；一加5Data已1GiB，不重复扩容；4GiB/17MiB是其一次工作负载，不是通用最低内存需求 |
| Pagani STAGE6:569-617 / run39 02c7ccd | 37/38停Logo；39在cpus=1下整组修改后用户确认进入系统，4GiB窗口/1GiB Data、软件刷新、HID、作业策略及同版用户态兼容 | 证明多核不是出现首帧的必要条件；未隔离各项因果；其签名验证后内存页补丁不符合本轮约束，仍不采用 |
| sheng package/BootWatchdogPolicy.c:17-30 | 源码注释记录identityservicesd/contextstored/locationd同步XPC等待环与部分反复退出；BootProcessProbe/BootUiTraceProbe已有实际实现 | 优先适配其观测/等待链追踪，不整批复制lite_jobs/禁用策略；注释记录低于本机堆栈证据 |
| TB128FU README:18-27 / board.toml | Snapdragon680平台声明21H461八核、CPU呈现桌面70–95s；SGI不达，专门用software-pended SPI作IPI；当前board owned_memory_size=4GiB | 不是单改cpus，显示较弱平台也能实现但有完整平台/中断/内存工作；README部分早期段落仍写2GiB，应以当前board4GiB和实测分开 |
| negroni README / patches0065、0078 | 声明八核SpringBoard，提供PSCI启动/次级入口/八核上限补丁；使用iOS16.7.16、XNU8796，3GiB窗口 | 是另版本多核移植参考，不是当前21H461慢输出的对照修复；未独立验证其硬件声明与每项配置 |
| fajita README:16-18 | 最新公开状态仍在Waiting on IOMedia，缺内核存储驱动，未读根镜像 | 比当前更早，不能作为launchd后无UI的成功修复证据 |

另发现当前P20-R1的ADT配置差异：根model仍OnePlus5，target-type缺省，product未设置pad artwork，chosen没有display-scale/display-rotation。官方guide/mobilegestalt说明这些字段选择用户空间设备类别/布局，并要求真实硬件compatible保持板级值。Pagani35+补了这组用户空间身份。可以在不修改签名OS/可执行文件的情况下研究ADT兼容；这不是已证明的一加5首错，也不是把硬件冒称另一SoC。

下一优先：先复用sheng进程/显示保留观测，核对512MiB窗口的内存余量与ADT用户空间身份，再对KeyStore事件等待取得本机证据；SMP应单独验证PSCI/GIC/IPI/计时器和调度，不作为当前现象的盲试参数。

## R2 实际采纳的诊断复用

采纳sheng BootProcessProbe的独立采样/非阻塞快照、任务与线程try-lock、进程过滤和128线程上限。为缩小本轮范围，固定关注launchd/chronod/SpringBoard/liveactivitiesd/backboardd等十项，未启用可选用户栈trace/follow-peer。原private API原型与实际源码头文件一致，使用当前MSM8998 recipe编译和真实函数体宿主故障注入测试。显示观测线程新写为板级小实现，沿用P19已用的framebuffer VA，不复制sheng BootProgress全套硬件/日志写盘/复位功能。ADT身份和较大窗口仅完成离线提案/物理范围检查，未混入诊断候选。


## R3 内存窗口单变量实验准备（2026-10-24）

用户授权1/2GiB及双核实验，按顺序隔离变量。R3只将`/chosen/darwinboot-memory-size`由512MiB变为1GiB；复用R2内核、cpus=1、硬件身份、原始TrustCache、1GiB逻辑稀疏RAM Data和显示后端。屏幕诊断仍标P20-R2，因为内核字节一致，应以MEM managed读数区分。候选位于`out/p20/r3`；一次临时boot已被接受，实机内存管理与UI结果待屏幕确认；UEFI仍使用已验证的真实EfiConventionalMemory扫描和AllocatePages，分配失败即停，不能把Recovery连续RAM范围当成UEFI分配成功。UFS已有CAP.S64A/物理地址上界校验保留。

上游测试顺序和退出值见`out/p20/host-tests/results.json`。窗口parser、packed RAMdisk、实际R2内核prelink XML lexer、NativeMobileDisplay request-state通过。上游UI layout和UI trace原始host harness存在过期依赖/替身：layout缺bar/BP_BAR_SECTION且断言旧标签；trace缺proc_name、recount_thread_time_mach替身及本机decoder依赖。保留失败日志，隔离host适配后ASan/UBSan通过；没有修改待启动内核或原始测试函数体。trace的CPU计时替身为0，因此不证明计时准确性；捕获/锁/引用/边界/32帧编码解码可测试，实机IPC等待原因仍未知。另增1GiB/2GiB明确尺寸和放置/重复拒绝用例通过。

双核预审：从实际startup编译配方预处理确认MAX_CPUS=8、SMP原子支持；已存本机Recovery FDT CPU affinity 0/1/2/3/0x100..0x103及PSCI smc与ADT相符。仍未证明CPU_ON、次核计时器和IPI成功。ArmCpu次核要求sheng-smp=1，单改cpus=2不够；当前诊断绘屏仅本核IRQ保护，在双核下不保证跨核排斥，需先修复/验证并增加运行读数。未发双核候选。2GiB在1GiB结果后再评估。



## R6 runtime evidence and R7 direct abort reason (2026-10-24)

USB work is paused by the user. R6 photo out/p20/r6/evidence/1792854940477.jpg at t5516s/pass1103 records 121 matching usermanagerd fault exits, zero matching bics exits since probe enable, and zero display clients/frames. The bounded scan attempted 4MiB in 26 regions with no copy failures and no matching text. This does not prove no reason exists.

Original 21H461 usermanagerd was extracted read-only: SHA256 33caaa27384e0f99738dc3f9d81644c75e1b13d2f2f711f1feb83021e3f54e11. Original cache/symbol UUIDs match. Legible return address 0x1d5f60b4c minus photographed slide 0xa7b0000 resolves to _abort_with_payload_wrapper_internal. Uncertain PC/LR transcription and unproved executable ASLR are excluded from conclusions. The volumes have been detached.

R7 adds bounded observation before the original abort_with_payload_internal reason construction: first matching fatal usermanagerd call, diagnostics enabled and interrupts enabled; records namespace/code/flags, at most 384 reason bytes and 32 payload bytes, with read errors. Original reason construction, signal and fatal paths remain. Page 4 prioritizes the ABORT record. No USB driver, userland patch, fabricated keys/personas or Data change. See out/p20/r7/README.md; status.json and boot-result.json distinguish packaging/transport from hardware/UI evidence.


## R7 explicit abort reason; R8 Data skeleton candidate (2026-10-25)

User transcribed R7 page4: usermanagerd p89, ns2/code6, reason bytes39/read err0, message matching original "Daemon failed to load persona manifest." Payload null/size0. This establishes the daemon's explicit fatal reason, not the underlying file errno/parse or kernel-persona failure. Evidence: out/p20/r7/evidence/abort-reason-user-transcription.json.

Official prepare-offline-data.py explicitly creates empty user-management directories for original usermanagerd initialization. R8 adds 20 missing directory names to a fresh clone of the frozen 1GiB Data seed, including keybags and PersonaVolumes. Source provenance confirms fetched and pinned scripts identical; upstream fixture is QEMU-only, so this board adaptation requires real testing. Administrator execution preserved root/mobile ownership. Read-only remount verified all existing paths unchanged except macOS FSEvents; only empty directories added. fsck_apfs -n passed; packed logical hash equals raw hash; UUID/role NONE/unencrypted RAM semantics preserved. No keys, users, persona records or original system files modified.

R8 reuses the exact R7 kernel and diagnostic source; blue header still P20-R7 is expected. Candidate status in out/p20/r8/status.json; prior diagnostics tests are inherited with explicit provenance, not falsely rerun. New Data/payload/decoder/layout and package checks are candidate-specific. Persona initialization and UI remain unverified until fresh hardware evidence. USB work stays paused.


## R10 original service audit and native SystemOS union (2026-10-25)

R9 reports SpringBoard14 fatal exits, ns4/code1 with original libobjc-trampolines dlopen reason, and zero swap checks. Two bounded read-only subagents audited service dependencies and upstream missing-library history. Main-agent compile replay proves old nonunion mode; original target lib exists and hash matches upstream. R10 enables native readonly SystemOS union at /, disables old fallback/carrier experiment, and syncs the two official union-directory patches, preserving original services and all authorization/fatal checks. Two sets of11 actual-function host cases pass with explicitly mocked filesystem APIs. See docs/P20_UPSTREAM_SERVICE_AUDIT.md and out/p20/r10/qualification/upstream-union.json. R10 was accepted for one temporary boot from confirmed root Recovery; no flash/retry. Screen ROOT/loader/service/frame results pending.


## P20-R11: complete pinned upstream rebuild

Image `out/p20/r11/uefi/boot-p20-r11-upstream.img` (SHA-256 `c54218839149d1c1e5dcf8419740fa1b6d2679011325ce2a847c9d79ffee96de`) passed the R11 offline gate and one temporary boot was accepted. GUI/screen evidence remains pending. All 58 recorded upstream XNU/NativePrelink patches and native source archives were rebuilt; prior native objects and the custom P20 observer were not imported. Stock Legacy display scale/refresh initialization and notification-only key events are selected. Necessary MSM8998-only adapters remain development code. Native SystemOS union is on, obsolete cache carrier is off, cpus=1, owned memory=1 GiB, R8 Data input unchanged, persistent store off. See `../out/p20/r11/README.md`, `offline-gate.json`, and `boot-result.json` for current evidence. This supersedes older pending build claims without rewriting their historical runtime records.


## P20-R13: pthread provider conflict corrected

R12 user logs identify `Re-initialization of pthread kext callbacks ... pthread_shims.c:631`, after VM and IOKit/NVRAM initialization. The native source pthread startup and original `com.apple.kec.pthread` were both selected. R13 removes the redundant native library/bootstrap, retains the original component and registration guard, and keeps the R12 compact observer. First-fatal display/park is restored so later errors retain their initial message instead of ending only in `MACH Reboot`. Image `out/p20/r13/uefi/boot-p20-r13-pthread.img`, SHA-256 `a41770ca08942c8149761020b16894e73d46dcbff5665ccc1fd7771df5932bc8`, passed host/static/offline checks. Current transport state: `TRANSPORT_ACCEPTED_SCREEN_PENDING`; screen/GUI evidence pending. No partition/userdata writes or retries. See `../out/p20/r13/README.md`.
