# P18 启动结果

P18候选：`out/p18/uefi/boot-p18-pgz-fixed.img`，81895424字节，SHA256 `6cedc421f9d56ea84e28344524848b977c4dc6d1a4f984117c913c6523ee80c7`。内核SHA256 `3abecbedc6580bac06ce4ee5366c66d6d90d4cb2dcf1e5162198be8dd6c899c8`。状态 `HARDWARE_IPC_PANIC_PASSED_WATCHDOGD_SIGTRAP_HUMAN_CONFIRMED`；临时boot一次，分区写入0。用户已报告恢复原Recovery。

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

离线十项源码host回归、实际机器码与完整打包门禁已通过；本轮未执行上游完整HVF PGZ分配回归，不将其记为通过。userdata/GPT/OS/SystemOS和P17 Data seed未修改，所有已测镜像保留。
