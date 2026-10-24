# P18 交接

P18以P17 R4实测为基线。P17已人工确认owner1、SystemOS0/auth1、Manifest0、Data stage4/status0/readonly0，/dev/disk11s1 dev16777298，RAM89/5/0，tz symlink errno0、exit-request0，PID1 EL0调用3879。tzinit退出完成及launchd LC_MAIN首指令仍无独立证明。

新候选：`out/p18/uefi/boot-p18-pgz-fixed.img`，81895424字节，SHA256 `6cedc421f9d56ea84e28344524848b977c4dc6d1a4f984117c913c6523ee80c7`。内核SHA256 `3abecbedc6580bac06ce4ee5366c66d6d90d4cb2dcf1e5162198be8dd6c899c8`。门禁 `P18_OFFLINE_BOOT_GATE_PASS`；当前启动状态 `HARDWARE_IPC_PANIC_PASSED_WATCHDOGD_SIGTRAP_HUMAN_CONFIRMED`，boot命令数1，分区写入0。

先读本轮IPC_ROOT_CAUSE、pgz-kernel-check.json、tests/report.json、offline-gate.json、boot-result.json（如有）及logs；保留所有失败日志。当前两行官方修复真实落入唯一缓存工作树与最终机器码，refer输入保持原样。不要再次添加补丁或禁用PGZ。

新内核只重编zalloc.o并重新链接/预链接，Capture标题更新；P17 R4 Data seed与ADT通过只读引用继承，原始RAM/UFS/PMGR/安全组件未改。禁止通过输出目录symlink写回P17；生成的kernel-check等报告必须写P18自己的普通文件。冻结pins在baseline/p17-pins.json。

两个轻量子代理只读完成源码和二进制分析，不再派生代理；回归、工程修改、构建及验收由主代理完成。修订要求最多2名，主代理不可为回归额外创建第三名。

本次人工核准已确认越过旧IPC panic且P17节点保持，支持官方修复解决该阻塞；新首错为watchdogd SIGTRAP。旧对象的运行时PGZ元数据仍未捕获，不追溯性补造证据。

用户已恢复原Recovery。只有离线门禁通过才发送新候选一次，需固定序列号/cheeseburger/MSM8998/unlocked/max-download/SHA门禁。不删除boot状态绕过一次限制。失败或失联停止。新照片仍需人工确认，不猜测metadata缺少的指针位数。

P18保留AMFI、VerifiedRoot、Trust Cache、Sandbox和Keybag/FIPS真实验证。未执行上游完整HVF PGZ分配回归，只有十项提取源码host执行及实际机器码核验；不得改写成完整虚拟化实测成功。后续如要重复官方完整检查，需对应候选SHA与真实HVF/GDB实录，不能替换哈希或合成日志。

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
