# P14 根卷认证

## 已定位的缺项

对 P13 实际预链接内核（SHA256 d398065b4c658d1f614714aff034548fea4c7d67317a3b21378a9b621a3d7133）、BSD 编译记录和源码联合核对：只有 native_ufs_block_authenticates，没有 native_verified_block_root_authorized、native_owner_root_*，BSD 对 Apple FSIOC_KERNEL_ROOTAUTH 的非零返回直接 panic。P13 的准确运行时错误码尚未知，不能套用上游 QEMU 的 EAUTH=80。见 out/p14/static-baseline.json。

P12/13 的 owner 签名策略、真实块校验和卷 UUID 已存在；缺的是实际挂载设备到已验证窗口的授权查询与正式接受路径。本轮不把 Apple sealed-root 认证宣称为 owner 签名认证。

## 实现

固定 UniXNU 对应 NativeVirtioBlock 子模块为98909eb9198a4360d7a43be831887cb8b74d814d；原目录未初始化，本轮直连取回此固定提交，未更新依赖。复用其 verifiedVolumeQuery、IOMedia major/minor 精确匹配、唯一性检查、最多64层 Provider 遍历与 NativeUfs 接口；设备适配层只留下 UFS 分派，没有接入 VirtIO MMIO/队列或新的存储类。

设备桥位于 drivers/p14/RootAuth。根查询要求已签名 OS UUID 04769D26-2CA0-4825-B443-E2E5F88300F2；实际 IOMedia UUID 必须对应真实 NativeUfsBlock 的已验签策略，错误 UUID 的同 Provider 查询必须被拒绝。沿用现有 NativeUfs 在线状态、只读策略副本和每次读取的真实块校验，不修改 UFS 源码/库。重复设备号、错误 Provider、缺少策略或 UUID 不符均拒绝。

BSD 唯一根认证位置保留原始 VNOP_IOCTL(...FSIOC_KERNEL_ROOTAUTH...) 及其返回码；XNU_OWNER_ROOT_AUDIT/TRUST 在该设备的 BSD 单元显式启用。根据官方 offline-owner-root-binding-audit 方式，独立 owner 决定还要求只读挂载、真实设备存在、mounted dev_t 等于 rootdev、唯一匹配和真实 UFS 策略验证。Apple 失败且 owner 不成立仍 panic。没有把 Apple errno 改成0；owner 接受不是 Apple 原厂安全启动认证成功。

## 验证与诊断范围

15项宿主测试通过：共享 BSD 决定谓词、真实 VerifiedRoot C/OpenSSL 签名/块校验、错误签名、未重签 UUID、真实重签但不符的 UUID、错误设备、重复/缺失 media、缺失 VerifiedRoot、可写根卷与真实块篡改拒绝。宿主没有运行真实 IOKit 注册树；此项必须由实机证明。原上游 checkpoint 脚本依赖特定 QEMU gdb-stop 文件和固定候选哈希，不可将其历史测试结果冒称本机执行。

P14 First Fatal 复用已有渲染器，保留首个错误，并显示 Apple errno、owner/accepted、只读、mounted/selected/verified dev_t、匹配数量、Provider 深度、实际/策略 UUID。即使后续发生新 panic，仍可核对根认证决定；异常寄存器有效性限制保留。

最终静态检查以 out/p14/candidate-check.json 为准。必须确认原始 Apple 组件 executable SHA 不变、两份策略/密钥逐字节等于 P12、P13 冻结文件不变后，才允许临时启动。

最终内核 SHA256：`87ce76e9aff82999fc6977a0c70813a1ce0492a23c5efec7284997a9fe28796c`，普通 ARM64，124,907,520 bytes。最终反汇编确认调用顺序为原始 VNOP_IOCTL → 实际设备授权查询 → 记录决定 → 接受检查点；Apple ioctl 未移除。

## 实机照片确认：owner 根认证通过，首个新阻塞为 exec ENOTSUP

照片已保存至 `out/p14/evidence/root-auth-accepted-launchd-enotsup.jpg`，SHA256 `8a2eb77e50fc7abe2807e1e8b8605a1abccf50203f83c08b412491feb9846e16`；逐项记录见 screen-result-01.json。

| 字段 | 实机值 |
| --- | --- |
| Apple FSIOC_KERNEL_ROOTAUTH | 0x50 = 80 = EAUTH，原值保留 |
| owner / accepted | 1 / 1 |
| 只读挂载 | 1 |
| mounted / selected / verified dev_t | 均为0x0100004f |
| IOMedia匹配数 / Provider深度 | 1 / 9 |
| 实际UUID / 策略UUID | 均为04769D26-2CA0-4825-B443-E2E5F88300F2 |
| UUID match | 1 |

这证明本轮设备根授权链实际成立，已越过P13根认证panic；不代表原始Apple根封印认证成功。原始Apple错误没有改为0，当前使用经过真实签名、块验证及实际挂载身份约束的owner路径。

新的首个致命错误：`Process 1 exec of /sbin/launchd failed, errno 45 @kern_exec.c:7197`。照片Caller为0xfffffff00bcbab4c，固定最终内核符号表对应 `_load_init_program + 0x1c0`。本分支errno.h确认45为ENOTSUP，80为EAUTH。源码说明此入口由BSD初始化完成后的首进程AST路径进入，因此BSD后续已运行到首次用户程序exec尝试；launchd自身尚未成功执行，没有SystemOS挂载或dyld运行的新证据。

不能从ENOTSUP单独认定是4KiB代码签名、AMFI或SystemOS某一项错误。当前 plain execve 调用不使用posix_spawn属性，kern_exec.c中两个spawn专用ENOTSUP赋值不能直接套用于本次。exec激活、原始安全策略及文件系统回调均可能传递错误；下一步应定位首个返回ENOTSUP的实际边界。固定上游也记录过owner根授权之后的launchd exec ENOTSUP，但其历史QEMU结果不是本机错误来源的独立证明。

当前停机现场已记录，未执行第二次实机启动、重刷或自动恢复。继续测试前需人工恢复原Recovery。保留当前已通过owner认证的P14候选，后续软件修复使用新变体和独立日志，不覆盖它。
