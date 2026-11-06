# P21 owned objects 提交前准备竞态 · 2026-11-06

## 设备事实

首个真实triangle候选 `p21-sagit-triangle-20261106T062852Z` 的boot373b45df：R540像素/时间戳通过；后续 M540 retirement-pending。GPU统计sub5/fence5/wait5，没有owned第六次提交。三binding BO后新增的内部IB8192使总数bo26/map26，其后shader/scratch没有观察到；不能说已执行compute失败或graphics→compute PM4是根因。

计数不证明CPU-map返回长度、alias检查或准确阻塞时刻。源码确认的race可以解释这些现象，但旧设备记录没有第一错误，仍需修复候选实机验证。

## 上游与最小适配

先核对sagit固定673f850…：AdrenoPipeline.h::dispatch和固定Mesa fd5_compute核心CS restore/VSFSdisable/CSenable顺序与本机相符；QcomMTL.mm分离submit/progress/retirement，不用本机2秒准备总预算。因此不改PM4、不扩GPUtimeout、不忽略错误。

本机原Objects execute在asyncworker里分配IB→shader→scratch、encode、flush，waitUntilCompleted同时从commit返回开始计2秒。若IB进入时lost=false、映射晚于2秒才返回，host设置lost，后两个allocate入口拒绝，sub不发生；worker仍持有context，外层显示retirement-pending。该交错匹配观察，不是已经确定的唯一设备原因。

本机最小修复：

- commit先冻结encoder/验证，解锁condition，再sessionlock下同步prepare内部BO、encode、beforeflush及ISAattestation。
- prepare按IB/shader/scratch逐项fail-fast，保存allocation原始rc和阶段；失败不dispatch、不retry，resources仍交retirement。
- prepare成功才发布Committed并dispatch；worker只submit、原200msGPUwait、afterflush及terminalstatus。原2秒hostwait不放宽，Scheduled仍由真实submit成功产生。
- OwnedRun将prepare/wait分别记录；新增独立ME首错，通过现有Metadata传输、kernel只锁存首条，BootLog打印。后续M540retirement文字不能覆盖它。NativeAdreno wire ABI不变。

此同步prepare是本机适配，不是上游逐字案例；下方新候选已实机恢复完整链。受控caller串行commit后wait，不能据此宣称通用并发Metal语义。

## 宿主验证

证据 `/Users/dinnerabc/unixnu-p21/native540/objects-prepare-20261106T0700/`：

- 实际ObjC实现＋ASan/UBSan模型PASS，原wronghash/非法state/外国资源/flush/submit/fencefailure与retirement回归保持。
- 内部IBmapView延迟2300ms：commit同步准备后返回，6个workloadBO（另1个baseline reserve）、oneSubmit/oneWait正常完成，不消耗后续hostdeadline。
- GPUwait回调延迟2300ms并错误：仍2秒hostTimeout，零expectedwrite，不提前close，worker结束后failedretirement持有全部7owners。模型不是GPU/AIR执行证明。
- arm64 iOS17.7 Objects.mm与OwnedRun.mm编译PASS。
- 首次CLT未指定SDK而缺Foundation，失败输出保留；改用现有Xcode显式MacOSX/iPhoneOSSDK后通过，无安装/系统配置改动。

## 实机修复验证

- 新候选 `p21-sagit-triangle-prepare-20261106T070830Z` 的image SHA `b4a31d04d6b3ff70150369f2c1e8bdbef8ded7c182dd8e04ec946eb9e90359b4`，kernel771ebc9c…/FD43b9183b…/expandedpayload1bc67c61…，gate20261106T071433Z PASS且实际EFI pins命中。签名plugin/数据/固件同轮匹配。
- 单次boot回执logs/boot-once-20261106T071511Z，246.01s被动观测rootRecovery，无hostreboot帮助，BCB预置32B并sync/readback，其他字段保留。新boot `0x3e2b9cf1`，日志 `out/p21/persistent-logs/20261106T071927Z`，windowSHA `39f1baa7c2547e0d8d1de6eef4fa7095138267e4d71f284c486500eb33cc1d5c`，35rounds/0writererrors。
- **R540 s12 e0 f12 pxff0000ff/ff000000 bad0；RP in968 out2744 edge384 ts1/2/3**，triangle再次通过。
- **M540 status4 v256 in780 px3f01b6b5/3f4e5da8**，GPUuser BO28/map28/unmap28/sub6/fence6/wait6；owned第六次真实提交/fence/readback恢复，原P1/constant256/transform/AIR全部通过。owned逻辑length正确，不扩大为其自身tailguard验证。
- 冻结 `20261106T072009Z-sagit-triangle-and-owned-pass`，candidate `logs/device-result.json`绑定rawhash。没有ME错误；不据一个成功轮证明原race的精确延迟来源，但确认本最小修复恢复完整链，不需要PM4或安全门禁修改。
- 第一次payload构建遗漏正确名dev-seed.dbr，未到firmware；补相同已验SHA seed链接后继续，原失败log保留。未改seed内容/identity、未重复patchkernel、未复用旧FD。
- 原trianglePASS/ownedpending、旧完整ownedPASS等所有baseline保留，无commit/push、无boot/recovery/GPT/固件刷写。当前仍没有systemMetaldevice、IOSurfaceraster或systempresentation。
