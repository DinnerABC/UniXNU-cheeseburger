# P16 原始用户空间与 Owner Boot Manifest

本轮以 P15 已实机成功的19组件内核为唯一基线。P15 的原始压缩处理器、UFS/APFS、Owner Root Trust、AMFI/Image4/Trust Cache、Sandbox、SystemOS及原始dyld均保留；P15冻结文件哈希经过核对。未重新克隆、未重建userdata，未添加GPU等外围模块。

## P15 mount 66 的证据与修复选择

P15照片确认mount PID3以signal0、exit status66退出，无CS_KILLED；没有捕获mount内部最早失败API、argv或stderr。固定上游check-mount-phase-checkpoints.py实际捕获过相同原始用户空间的mount -P 1及缺少boot-manifest-hash的stderr。当前P15 ADT确实没有该属性，最终内核也没有publisher，因此复用官方现成修复有明确依据。**P15本机退出66的内部根因仍不能只凭同一状态码定论。** P16若正确发布并越过该点，将补充因果证据，仍须区别上游和本机结果。

已阅读固定上游的mount-phase、owner-boot-manifest、native-ram-data检查脚本，offline-owner-boot-manifest及systemos-mount补丁和patch-reference。包含固定QEMU哈希的历史检查没有冒称本机通过。

## 真实策略生成与发布

复用P15已编译的IOKitBSDInit和kern_exec对象，未重复编译它们；最终内核的真实publisher及首次exec前调用链均经机器码核对，不仅检查符号或字符串。XNU_OWNER_BOOT_MANIFEST=1，保留DEVELOPMENT/DEBUG限定。

运行顺序：实际OS根与SystemOS的只读APFS挂载/授权成功并root重查后，传递实际设备号、固定角色UUID到publisher；publisher要求设备不同，通过真实NativeUfs provider查询已验签策略和卷UUID，然后对每份实际Manifest完整有效长度取SHA384。依照官方编码，组合域`VerifiedRoot.OwnerBootSet.v1\0`、角色byte1+OS摘要、角色byte2+SystemOS摘要，再取SHA384。没有把其他设备的固定hash作为身份输入。

| 角色 | 真实签名策略 UUID | Manifest字节 | SHA384 |
| --- | --- | ---: | --- |
| OS | 04769D26-2CA0-4825-B443-E2E5F88300F2 | 47481664 | 67a9405b9f1c9e1e707665f7738e2b707486042d74e9b1a218e4e7cae06dc988db40157267a12652c281b24c67927a18 |
| SystemOS | 8846FE78-2910-4297-AF97-D59BB6F8FC16 | 25805344 | 6418d1a30715babb42abc91413f2d8af8b5359a5d5cf37787d65381af208b60d480c36944293d3b87c1741150bd2fdfb |

独立宿主预期48字节摘要：`f13cd57d18fad6f2786e2d2616edfa34e7b1aa5b6e30b707543fbb3b8122dac8f5d3ec347d497f4352bf2c44fcd8a308`。屏幕中的host-match仅额外对照预期；真正授权与hash生成来自运行时策略，不来自该常量。

publisher拒绝既有boot-manifest-hash或owner-domain属性，发布owner-domain和48字节hash后调用**实际IOGetBootManifestHash**，要求bool成功、返回长度48且全部字节与本次计算结果相同，才保存actual并status=0。失败不继续用户空间，保留状态1/2/3/4；未运行初值为-1。发布器actual保存缓冲区在初始内核中为零，不预填host常量。明确这是开发Owner身份，不是Apple固件个性化身份。

实际函数体11项宿主测试通过，包括正确摘要、错误设备/角色/UUID、缺认证、缺chosen、既有冲突、分配/发布失败、读回失败/长度错误/内容错误和回滚。宿主IOKit/device API使用替身，不能代替实机发布证据。AMFI、Trust Cache、VerifiedRoot、真实代码签名均未绕过。

## 一次测试的观察范围

屏幕优先显示原始首个panic、owner/SystemOS状态、Manifest状态/完整actual摘要/host-match，以及最新原始用户输出采样。stage mask标签明确为output-token mask；某任务字样仅证明输出被观察，不能单独证明该任务执行成功。FIPS需要对应进程原始输出/结果结合判断，Keybag启动与成功分别报告。

新增监测只采PID不超过128、fd1/2的成功write/write_nocancel返回后缓冲区；使用固定ARM64的64位uu_rval ABI。每次最多32KiB、分块copyin；日志保留最新32767字节，原子try-lock避免互相覆盖，明确报告丢弃/忙碌/截断/copy错误。没有采writev或其他fd控制台，返回后用户缓冲也可能被其他线程修改，所以不是完整、不可变的stderr记录。最近相关errno仅覆盖symlink/mount/execve，不能冒称所有调用的首错。原始panic保持first-fatal语义。

修正P15全局exec stage/header被子进程覆盖的问题，只保留PID1写入。没有接入硬件单步或修改用户空间代码；**LC_MAIN首指令完成仍必须由直接EL0 PC、原始字节及完成后PC证明，不能由阶段文字或spawn行为替代。**

## Data及后续依赖的真实准备状态

现有启动包没有Data seed。已按官方var复制方案另建32MiB APFS seed，真实UUID为8294E8C6-1230-4CDB-80D1-714B2CC02D33。原始NXSB/APSB checksum与UUID一致，宿主写/读回及符号链接曾成功；**raw inode审计发现183个inode是宿主501:20，仅1个为0:0，不能直接激活为iOS Data。** 该seed未加入当前boot，未冒称签名或生产存储已合格。报告见data-seed/report.json。

NativeRamBlock已准备且静态审查190槽位、24覆盖、11原始IOStorage导入通过，未加入当前内核。后续须保留原始var的guest uid/gid/权限/xattr修正seed，再核对late constructor、matching category、runtime metaclass、RAMDisk内存范围、卷UUID和真实APFS写入；只用RAM scratch，不启用UFS持久化写入。

上游Manifest修复后曾观察到mount-phase-1、data-protection、mount-phase-2、FIPS和Keybag任务，随后timezone symlink EROFS。这是后续排查顺序，不是本机结果。不提前绕过FIPS或Keybag，不修改已签名OS文件。若真实失败涉及/var/db/timezone/zoneinfo，应先解决实际可写Data及目录归属。


## P16照片更新：Manifest成功，下一阻塞为Data/timezone

用户提供的第一张就是P16 FIRST FATAL，无需为补拍重启。完整actual Owner SHA384与上文预期逐字节一致，status0/host-match1；owner仍accepted1、SystemOS status0/auth1。mount PID3退出0，已越过原来mount66；PID1 exec stage4及后续用户服务行为有实机证据。

新fatal是tzinit PID17正常退出30。syscall57 symlink对/var/db/timezone/zoneinfo返回EROFS30，原始stderr明确找不到Data、退回system partition、force_system_dataset失败及Read-only file system。这已将真正阻塞推进到可写Data及目录归属，下一步应修正独立RAM seed元数据后集成官方NativeRamBlock，不能启用当前501:20污染seed或修改已签名OS。

还存在wearlyboot Failed to enter sandbox，以及0x3ff输出位图中的FATAL KEYBAG ERROR；不宣称Keybag或Sandbox服务成功。FIPS及测试成功字样出现，但缺少完整早期归属上下文，不能把bit当完成证明。第二张shared-region提示是更早的检查失败，之后已执行更深任务，不是最后停止原因。LC_MAIN单步证据仍未取得。
