# P15 exec 根因与修复

基线仅为 P14 实机 owner root accepted 的最终内核。P14 的内部首个 errno 来源没有被实机跟踪，不能将本轮静态证据冒称 P14 的运行时返回记录。P15 已补齐实际类型4文件读取路径上的缺项，唯一一次临时启动后的照片确认原始处理器真实注册、进入后续用户空间，errno45没有重现。证据见 `out/p15/evidence/screen-result-01.json`。

## 证据

- P14 kernel SHA256：`87ce76e9aff82999fc6977a0c70813a1ce0492a23c5efec7284997a9fe28796c`。
- 实际构建配置 `fs_compression.h` 为 `FS_COMPRESSION=1`；最终 P14 预链接报告只有16个 Apple 组件，无 `com.apple.AppleFSCompression.AppleFSCompressionTypeZlib`，无其 kmod、构造入口或注册表。不能由字符串存在推断它已加载。
- 只读连接已有 OS 镜像，原始 `/sbin/launchd` 和 `/usr/lib/dyld` 的隐藏 `com.apple.decmpfs` xattr 均为类型4，压缩标志为32。解压后的 SHA256 分别为 `8cec9a3c44a443b08e977ccc5d577850316ad428c575dc01f09b018dbd5b6a25`、`f8df3c58fae6e409db740df22c98aa699aee110b73523541a63f1bd5e23cf705`，与固定上游样本一致。见 `out/p15/file-compression.json`。
- 固定 XNU `bsd/kern/decmpfs.c` 的 `_decmpfs_read_compressed` 在 `compression_type_valid` 失败时返回 ENOTSUP；pagein 等边界亦保留该拒绝。首次 exec 激活通过 `vn_rdwr` 读取实际 vnode。
- 上游 `check-owner-root-compression-checkpoints.py` 记录过同版本 donor 的真实 decmpfs 返回45及补齐后进入原始 dyld。该脚本含固定 QEMU 运行哈希，不能直接当作本机测试结果。

结论：**缺失原始类型4解压处理器这一阻塞已在P15实机排除，errno45未重现。文件格式、缺项、固定XNU拒绝路径及修复后执行结果构成强因果证据，指向decmpfs。P14内部首次返回45的函数仍未被直接捕获，不能伪称已有该次调用栈。** P14当时唯一直接可归属边界是 `_load_init_program + 0x1c0` 的exec失败；P15新停止点为mount进程退出66。

## 原始组件与 ABI

沿用固定普通 ARM64 donor，SHA256 `f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7`。通过已有官方 NativePrelink 将 Zlib、Sandbox、AppleMatch 加入 roots，由工具解析实际依赖；最终共19个组件。未自制 decompressor，未将 ENOTSUP 改为0，未关闭 FS_COMPRESSION。

`out/p15/compression-check.json` 独立复用官方可分离验证逻辑，检查：

- 所有原始 `__TEXT_EXEC` extent 与 donor 逐字节一致；全部导入指针等于真实重绑定目标。
- 数学常量池、原始 ADR 引用及重定位保留；kmod start/stop 和模块构造函数指向原始可执行区域。
- 原始类型3表：`0xfffffff00638fee0`；类型4表：`0xfffffff00638ff10`。两者 version=3，fetch 位于原始代码 `0xfffffff005efca54`，注册结构大小48，fetch偏移24；全部回调指针与 donor 加 slide 后一致。
- 原始组件的 register 导入确实绑定到新内核 `_register_decmpfs_decompressor`。IOResources/IOBSD 匹配 personality 保留。

以上证明可用的静态注册路径，**不等于实机已注册**。真实 `register_decmpfs_decompressor` 仅在原函数执行后记录类型3/4的实际返回码、registration 和 fetch 指针；没有替它填成功。P15照片确认两者status=0，两个registration及fetch均与上述原始表精确一致，因而已真实注册。

## 最小定位与安全边界

仅对 PID1 的 `/sbin/launchd` 实际尝试记录首个正错误码，保存函数、vnode、路径。开发版 launchd 缺失的正常回退不抢占记录。覆盖 decmpfs 缺处理器、exec权限/文件读取、Mach-O激活及 load_machfile 返回、原始16K签名到4K缓存预验证、最终 execve 边界。未对每个内部 Mach-O/AMFI/VM 返回点做全覆盖；若停在外层，仍需沿记录向内定位，不能推断所有内部调用成功。

exec stage：1=请求发起；2=已进入 Mach-O loader；3=load_machfile 返回 LOAD_SUCCESS；4=execve 返回0。均不单独证明 EL0 或 LC_MAIN。实机暴露了记录范围限制：stage2/3及header/entry写入未限制PID1，会被mount等子进程exec更新。照片末态stage3不能解释为PID1 execve未返回，也不能用末态entry推断PID1 dyld基址。该记录范围修正已在next-boot-manifest的离线源码中编译，未修改实测内核。

在真实 ARM64 unix syscall 入口保留最多64条 PID1 saved PC/PSTATE/syscall号，只有 saved PSTATE 低4位为0时计数。该观察不改寄存器或用户代码。它可证明 EL0 发起 syscall；PC仍须映射到原始 dyld/launchd，不能据计数宣称 LC_MAIN 第一条指令完成。原始 LC_MAIN entryoff=85896，首指令字节见 `systemos-dyld-check.json`。

## 提前集成后续原始路径

P14 源码已有 `native-4k-signature-hash-cache.patch`，最终 P14 未启用；P15 在该单元启用真实实现，先验证整个原始16K签名块，再从同一已验缓冲区派生4K哈希，发布为只读；失败仍拒绝。没有假签名成功。

复用官方 SystemOS mount/cache carrier 补丁，UFS适配置于独立 CacheAuth 单元，P14 OS root verifier和屏幕字段不变。OS UUID `04769D26-2CA0-4825-B443-E2E5F88300F2`；SystemOS UUID `8846FE78-2910-4297-AF97-D59BB6F8FC16`。真实 IOMedia UUID/dev_t唯一匹配、UFS签名策略验证、实际只读APFS挂载及root重查均须通过；缺失或不符仍拒绝。

采用官方独立 `/private/preboot` 只读挂载，设置原始 dyld 的 shared-cache目录和 `/private/preboot/usr/lib` fallback；未启用union。owner双卷验证后才设置官方startup bit3；已核对原始dyld中对应skipIgnition分支。AMFI、Image4、VerifiedRoot和原始CodeDirectory验证保留，不修改Apple系统文件。

离线已完整验证 launchd、dyld及53个shared cache文件的原始CodeDirectory代码块，真实首块篡改负例全部拒绝（`signature-preflight.json`）。这是宿主字节验证，不是设备AMFI运行证明。commpage原生4K分支及最终反汇编保留。Sandbox/AppleMatch及闭包已预链接、kmod/导入检查通过，运行时policy注册没有独立返回记录；实机已推进到原始mount运行，与越过上游缺Sandbox时的libSystem拒绝阶段一致，但不能替代handler请求结果。Data ramdisk尚未集成；不提前开发GPU。


## 实机用户空间证据与新阻塞

原始照片 `out/p15/evidence/mount-child-exit66.jpg` 已保留并记录SHA256。root owner仍accepted=1，Apple EAUTH80原值不变；SystemOS stage6/status0/mount0/auth1；类型3/4 status0且指针匹配原始组件。签名缓存计数为201083个原始块、2189次native4K页面检查、53个ready blobs，证明真实签名预验证和页面检查路径运行，支持原始shared cache已实际读取；没有独立shared_region返回0截图。

PID1有747次真实EL0 syscall，首PC=`0x100a99fb4`，PSTATE=0，syscall74。对同哈希原始dyld检查，唯一具有相同4K页内偏移和syscall号的原始指令位置是 `_mprotect` 的SVC后PC `0x3afb4`；前两条原始指令为 `mov x16,#74; svc #0x80`。推导slide=`0x100a5f000`。这是原始dyld运行的强匹配证据；slide由PC推导，尚无独立运行时map读取。见 `evidence/dyld-first-syscall.asm`。

新panic为 `mount[3] exited -- no exit reason available -- (signal 0, exit status 66 )`。返回地址按LR-4定位后，实际链为 `_exit → _exit_with_reason → _proc_prepareexit → _proc_prepareexit.cold.3 → _panic_with_options`。这是PID3正常退出的非零状态码，不是exec errno66，也不是initproc遭代码签名kill。照片没有CS_KILLED标记。不能由该照片推断launchd全部服务初始化完成。

上游 `check-mount-phase-checkpoints.py` 对相同原始用户空间捕获过真实 `mount -P 1`，其stderr指出缺少boot-manifest-hash，且明确Data缺失在当时环境下不是必需错误。当前P15的/chosen静态ADT确无该属性，最终内核未编入owner publisher；原始mount包含对应查询及失败字符串。因此缺失boot-manifest-hash是有匹配静态证据的首要下一假设，**本机mount argv/stderr/内部返回尚未采集，不能宣布它就是已实证的退出66根因**。

已将官方 `offline-owner-boot-manifest.patch` 应用到独立副本，并连同启用调用点和PID1范围修正编译为 `out/p15/next-boot-manifest/` 对象。该路径对真实OS/SystemOS已认证manifest分别取SHA384，使用 `VerifiedRoot.OwnerBootSet.v1` 域与角色组合；拒绝替换已有firmware identity，不将owner元数据冒称Apple身份。报告包含从现有签名策略导出的独立预期值。该准备未链接、未生成第二镜像、未第二次启动，实测P15产物保持不变。
