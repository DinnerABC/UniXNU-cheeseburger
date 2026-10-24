# P15 续接：已进入用户空间，mount退出66

首先读 `out/p15/evidence/screen-result-01.json`、原始照片 `mount-child-exit66.jpg`、`boot-result.json` 和三份P15文档。已有唯一一次真正的fastboot boot，原errno45未重现；**不要自动再次启动或删除boot门禁重试**。当前首个fatal是mount PID3正常退出66，保留现场。

P14根owner认证继续成功；Apple原始EAUTH80保留。类型3/4处理器status0、实际registration及fetch指针与原始组件精确匹配。SystemOS stage6/status0/auth1。PID1累计747次EL0 syscall，首PC与原始dyld mprotect wrapper强匹配；真实签名缓存已处理201083原始块、2189页面检查、53 ready blobs。具体指针、计数、照片SHA和调用栈均在screen-result中。

## 严格区分已经证明的阶段

用户空间确已执行，且原始mount进入exit syscall。这超出了执行请求或单纯Mach-O激活。PID1首PC0x100a99fb4、PSTATE0、syscall74与原始dyld `_mprotect` 的SVC后PC0x3afb4唯一匹配，推导slide0x100a5f000；尚未独立读取运行时map。

当前实测镜像的stage/header/entry是全局记录，会被子进程exec更新。照片stage3和最后entry不能当作PID1的execve返回或dyld基址；dyld_header=0也不能解释为dyld未运行。下一离线准备已限制这些写入到PID1，尚未发机。

LC_MAIN首指令完成仍缺直接PC/原始字节/EL0 PSTATE/下一PC观察。原始entryoff=85896、首指令ff8305d1可供匹配，不能猜ASLR。mount行为与launchd启动阶段一致，不能据此伪填单步证据或“服务初始化完成”。Sandbox handler和shared_region返回值同样没有独立记录，保留此边界。

## 优先解决新的真实阻塞

已确认的失败边界是mount的exit(66)，signal0，无CS_KILLED。内核栈为exit → exit_with_reason → proc_prepareexit → panic。不是initproc代码签名kill，66不是exec errno。不要关闭critical-process panic来掩盖失败。

上游 `check-mount-phase-checkpoints.py` 对相同原始用户空间实际观察了PID1派生 `mount -P 1`，stderr为缺少boot-manifest-hash；同时记录Data在该环境下不是必需项。这是上游证据，本机尚未捕获mount argv/stderr，不能直接套用为本机根因。

本机离线已确认/chosen没有boot-manifest-hash，最终内核没有publisher。原始mount具备对应查询/失败字符串。因此先核对这一有根据的缺项，不要先重做存储、根认证或把Data缺失定为根因。

`out/p15/next-boot-manifest/` 已准备官方 `offline-owner-boot-manifest.patch`：IOKitBSDInit副本及启用调用点的kern_exec副本使用实际XNU配方编译通过，**未链接、未打包、未第二次启动**，没有覆盖实测P15产物。publisher要求不同的真实OS/SystemOS设备、实际UUID及已认证policy digest；以 `VerifiedRoot.OwnerBootSet.v1` 域及角色派生SHA384，拒绝替换已有firmware identity，不能将其宣称为Apple身份。独立预期摘要和编译命令在report中。核对其接口与来源后再形成新的可区分候选。

后续设备观察应有界记录mount实际argv、stderr写入及首次非零API返回；同时补齐严格LC_MAIN观察。不能只增加阶段文字。若之后真正走到需要Data的路径，再复用官方Data ramdisk和现有安全组件，检查libSystem、服务和initproc退出。不提前开发GPU/GUI，不修改GPT。

## 保留与复现

禁止重刷userdata、erase、GPT或boot等分区写入。当前OS/SystemOS与签名策略仍足够复用；本轮没有第二次实机测试。P14最终内核、boot、payload、安全/存储库及manifest所列文件哈希仍保持。

P15新增源码、脚本、补丁、证据与产物均在out/p15，docs仅本阶段三份文档。固定XNU源未直接改写；副本编译所替换的旧构建对象已备份在baseline。next-boot-manifest对象只存独立目录，不替换当前构建对象。不重新克隆，不重建userdata。

使用 `/opt/homebrew/bin/python3.13`，系统python3缺tomllib。prepare.py生成基线副本，add-el0-observer.py仅追加一次；compile.py使用实际XNU配方，link.sh增量链接，prelink.py调用固定NativePrelink。不要对已追加源码重复运行observer，也不要在没有重编译时把新副本当成已链接内容。成功命令在offline-gate，失败尝试日志也保留，标签不可覆盖。

UEFI使用现有VM的独立P15 overlay，身份头位于out/p15/config；同P13/P14压缩协议。现有VM已停止，离线只读镜像已卸载。实测kernel SHA256 `57e494d75f22d9a2a9a5d7a6640bd887689af53b1fa7f4f806606628e736ca7e`，boot SHA256 `6b452719a182e171ba3004ea932f54fd778c605b34ab541fd9c542373af3565f`。最终manifest/SHA256SUMS已随照片结论更新，旧构建门禁保留其执行时的静态状态。
