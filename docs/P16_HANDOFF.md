# P16 续接

先读out/p16/evidence/screen-result-01.json及两张原始照片、boot-result.json和本轮报告。P16 Manifest已真实发布读回成功，mount PID3退出0，最终tzinit PID17对/var/db/timezone/zoneinfo的symlink返回EROFS30并退出30。当前已有唯一一次boot_commands=1，**无需为补拍重启，不自动重发，不删除门禁**。用户第一张已是有效P16 FIRST FATAL。

同一实测镜像SHA256仍为3badf4a9b7ba3ce7d26c6f48a01cad71db9277cc7e2e92046f5208a41b27f0e5，未更改。

P15冻结产物仍保持；禁止刷写userdata、GPT、boot或其他分区，不重做已成功XNU/UFS/APFS/根认证。全部P16新增产物在out/p16，docs只有本阶段三份报告。固定XNU源未直接改写，使用副本和实际编译配方；旧构建对象备份baseline，离线旧变体为-v1。

## 本次现场字段含义

先保留原始fatal、Caller、进程名和signal/exit status；它们与kernel errno不同。核对owner accepted及SystemOS status/auth。Manifest status=-1表示未运行；0表示官方publisher已实际发布，并通过IOGetBootManifestHash bool成功、48字节及完整比较。host-match应为1，actual应等于 `f13cd57d18fad6f2786e2d2616edfa34e7b1aa5b6e30b707543fbb3b8122dac8f5d3ec347d497f4352bf2c44fcd8a308`，但该host值只是观测对照，不能作为身份来源。

output-token mask按bit0到10依次为mount-phase-1、data-protection、mount-phase-2、init-with-data-volume、fips、all tests PASSED、result = true、keybag、FATAL KEYBAG ERROR、usermanagerd、tzinit。字样只表示输出采样，不独立证明成功。结合日志进程前缀和下一任务/退出结果判断。write采集范围、截断/并发丢失量须一同报告，缺输出不证明未运行。

屏幕只打印最近相关symlink/mount/execve错误，不是完整首错链。LC_MAIN未接硬件单步，严格保持直接PC/原始字节/第一条完成后PC要求；PID1 syscall和用户服务行为不替代它。

## 继续原始用户空间

若hash成功并越过mount66，优先按实际新fatal处理Data、timezone、Keybag、Data Protection、Sandbox或服务。不要把上游FIPS成功或Keybag任务日志冒充本机结果，不关闭critical-process panic或伪造安全成功。

现有data-seed为32MiB APFS，UUID8294E8C6-1230-4CDB-80D1-714B2CC02D33，SHA256178a805a6f37edbe1510b3d18be4e3a49c89c2aa03c1ba5a50acbae8a9ed690b。**183个inode为501:20，尚不能激活。** data-seed/report.json与audit-raw.py保留证据；先保留原var的guest uid/gid/xattrs修正，不能用宿主可写取代iOS权限正确。

NativeRamBlock的data-driver/report.json已有190槽位/24覆盖/11导入静态通过，但未链接本次候选。后续仍需final prelink地址、late C++初始化、matching category/runtime metaclass、RAMDisk范围、实际UUID与真实读写；坚持RAM scratch、不启用UFS持久化store、不写已有只读镜像。

## 构建来源

使用/opt/homebrew/bin/python3.13。两个Manifest对象复用P15 next-boot-manifest已编译结果；compile.py只编译新增观察/capture，link.sh仅增量链接；finish-kernel.sh顺序调用固定NativePrelink及本轮必要门禁，不要重复已经有成功标签的命令。prepare-monitor.py是初始版本，refine-monitor.py及后续Capture可见行预算修正已反映在最终source；复现应使用保存的最终source和命令，不盲目重跑初始生成器。

UEFI使用既有VM的P16独立overlay，只更新身份头；保留压缩协议与原地址。build-payload、test-decoder、pack及final-gate的哈希必须对应最终内核。NAS掉线先恢复卷并读实际退出码，不创建空项目、不重新克隆。设备测试只用身份固定的boot-once.py；一次测试之外的后续候选应独立记录并遵守用户设备授权。


## 下一实际任务：可写Data与真实服务错误

停止把Manifest或早期shared-region提示作为当前fatal。P16照片已确认owner/SystemOS保持成功，Manifest完整actual匹配，mount退出0。应先提供真正可写且guest元数据正确的RAM Data，解决timezone路径；当前seed的501:20所有权污染必须修复，再做matching-category/late init/最终prelink/真实写入验证。不写userdata或OS/SystemOS。

原始stderr：Unable to locate data; resetting to system partition；No such file or directory；Last resort force_system_dataset() failed；Read-only file system。errno0x1e=30、PID0x11=17、syscall0x39=57与symlink路径相互印证。

同时保留wearlyboot Sandbox失败和早期FATAL KEYBAG ERROR字样。0x3ff只记录输出token；它含bit8错误，不能误报Keybag成功。约0x8de字节早期日志滚出尾部，FIPS成功字样缺来源归属；后续新候选可按具体任务保留有界输出/状态，不能重启旧镜像假装获得更多现有证据。LC_MAIN仍无直接首指令完成PC，不把服务行为升级为该证明。
