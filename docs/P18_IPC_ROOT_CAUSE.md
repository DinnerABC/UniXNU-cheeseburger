# P18 IPC / PGZ 根因证据

P18以P17 R4实测为基线。P17已人工确认owner1、SystemOS0/auth1、Manifest0、Data stage4/status0/readonly0，/dev/disk11s1 dev16777298，RAM89/5/0，tz symlink errno0、exit-request0，PID1 EL0调用3879。tzinit退出完成及launchd LC_MAIN首指令仍无独立证明。

新候选：`out/p18/uefi/boot-p18-pgz-fixed.img`，81895424字节，SHA256 `6cedc421f9d56ea84e28344524848b977c4dc6d1a4f984117c913c6523ee80c7`。内核SHA256 `3abecbedc6580bac06ce4ee5366c66d6d90d4cb2dcf1e5162198be8dd6c899c8`。门禁 `P18_OFFLINE_BOOT_GATE_PASS`；当前启动状态 `HARDWARE_IPC_PANIC_PASSED_WATCHDOGD_SIGTRAP_HUMAN_CONFIRMED`，boot命令数1，分区写入0。

## 精确定位与证据等级

P17人工核准块位于out/p17/r4/evidence。照片fatal主体匹配固定源码zone_invalid_element_panic()在osfmk/kern/zalloc.c:1175-1179的原格式串：metadata %p corresponding to addr %p being freed to zone %s%s%s, has chunk len greater than max。zone文本可辨识为ipc ports。该文案不是已执行free的证明：zone_id_require_aligned可由IPC对象锁前校验调用，失败后进入此诊断函数，检查本身不释放对象。不能从being freed字样直接推断double-free或内存损坏。

用户新提供地址原文：metadata `0xfffffffbe4f350`，对应addr `.0xfffffff0a98b81a0`。前者仅14个十六进制数位，原样保存，不补成完整内核指针；后者前导点按原文保留。未取得运行时页元数据值、PGZ范围、backing指针或完整调用栈。旧画面Caller属于panic记录点，不能当作完整IPC调用链；没有有效保存异常上下文时不把EL1寄存器认作故障PC/LR。

冻结R4内核SHA e88abc279ba751f4c400e930f429eca592b32138961475924e66f209336902e1。_zone_id_require_aligned=0xfffffff00b8cd000；_zone_invalid_element_panic=0xfffffff00bf7f558；__pgz_decode=0xfffffff00b8ce11c。原chunk超限分支在0xfffffff00bf7f67c比较0x9000，最终0xfffffff00bf7f6b8调用panic；编译行号1179。冻结反汇编在out/p18/baseline/p17-aligned.asm，源码原件在baseline/zalloc.c。

源码及冻结机器码均证实旧zone_id_require_aligned直接用addr做from_zone_map与对齐；缺少官方pgz_decode。ZM_CHUNK_LEN_MAX=8，合法ZM_PGZ_ALLOCATED=10。上游check-pgz-legacy-checkpoint已记录正确zone的活PGZ secondary-page对象被旧校验误拒绝，panic文案与本次相同。因此本次高度符合已知故障特征，但尚无本次手机对象属于PGZ alias的直接记录，不能将“符合”写成“已实机证明唯一根因”。

## 最新授权与最小修复

用户进一步明确“先应用官方修复，然后再找问题”，因此先复用官方pgz-aligned-validation两行语义，不同时增加诊断逻辑：elem=pgz_decode((vm_offset_t)addr,1)，from_zone_map(elem,1)。唯一工作树.work/p4-unixnu/.cache/xnu-work/osfmk/kern/zalloc.c已改；refer原始源码未改；官方补丁及实际diff在out/p18/patches。未改IPC引用计数、free路径、zone_invalid_element_panic或zone_element_resolve，不关闭PGZ或放宽检查。

实际CC日志仅zalloc.o；Capture另改标题为P18 PGZ FIX。新机器码内联PGZ区间检查、allocated标记0xa、backing页加载与地址重建，之后才进入原校验；保留UAF panic及invalid-element拒绝。正式候选不含native_zone_pgz_probe。见pgz-kernel-check.json、baseline/p18-aligned.asm及logs/link-native-01.log。

## 回归及边界

主代理阅读官方check-pgz-legacy-checkpoint、check-pgz-alignment-checkpoints、check-owner-union-checkpoints。它们依赖上游特定SHA候选和HVF/GDB实录；本项目未拥有对应完整实录，本轮没有运行官方完整HVF回归，不能用这些脚本的存在冒充通过。

主代理从固定实际源码提取旧/新zone_id_require_aligned、__pgz_decode及Z_FAST_ALIGNED，执行ASan/UBSan十例：旧合法secondary alias拒绝、新合法alias通过、alias错位拒绝、错误zone拒绝、freed PGZ触发UAF、普通对象通过、普通错位/错误zone拒绝、普通secondary页通过、非zone地址拒绝。地址映射和元数据由host夹具模拟，没有执行真实内核PGZ分配。早期夹具类型错误与secondary布局错误日志保留，最终pgz-source-tests-03通过；测试不是实机修复证明。

原Apple模块代码哈希、Trust Cache、签名路径、Owner策略和Manifest检查均保留。4KiB配置/ADT和用户ABI未修改；正式Mach-O实际decode与拒绝指令核对通过。实际Data seed、UFS、PMGR、RAM归档与R4冻结哈希核对；无userdata/GPT/OS/SystemOS写入。

上游参考：https://unixnu.com/xnu/four-k-page-tables 与 https://unixnu.com/xnu/patch-reference 。

## P18 人工核准实机结果

用户已确认以下fatal及状态。原照片、逐字文本、结构化数据及SHA256位于out/p18/evidence。

```text
watchdogd[106] exited -- exit reason namespace 2 subcode 0x5 description: none

owner=1 SystemOS=0 auth=1 Manifest=0
RAM ready=1 bytes=1073741824 resident=4575232
RAM reads=96 writes=117 failures=0
Data stage=4 status=0 readonly=0 dev=16777298
tz calls=1 errno=0 exit-request=0
PID1 EL0 traps=8949
Data path=/dev/disk11s1 act=0 ms=2045 t0=148840 t1=150885
```

本次启动已越过P17原IPC/Zone panic，进入watchdogd因SIGTRAP终止的后续故障；这支持官方PGZ修复解决当前启动阻塞。仍不声称已经取得旧对象的PGZ归属/元数据或排除所有其他内存错误。P17所有已确认核心节点在本次保持：真实可写APFS Data，RAM读写无失败，原tzinit symlink返回0。RAM写入5→117，PID1 EL0调用3879→8949。

新首fatal的namespace2为OS_REASON_SIGNAL，subcode5为SIGTRAP；不是已证明的硬件watchdog超时。description none未提供断言内容，当前无watchdogd有效用户态PC/异常栈，不能认定触发SIGTRAP的具体指令或服务。下方UserEventAgent类重复警告与此fatal未建立因果关系，且不在本次人工核准选择块内。

尚未独立确认tzinit退出完成、launchd LC_MAIN首指令、Keybag/Data Protection、Sandbox及FIPS成功。退出请求0不等于进程退出完成，PID1 syscall计数不等于LC_MAIN直接断点证据。

下一处真正阻塞：watchdogd[106] SIGTRAP。下一轮应先定位其用户态异常PC/调用栈及退出前日志，核实断言/依赖；不禁用watchdogd、不移除关键进程退出策略、不关闭安全验证。设备已回Recovery，本轮只归档日志，不构建或启动新候选。
