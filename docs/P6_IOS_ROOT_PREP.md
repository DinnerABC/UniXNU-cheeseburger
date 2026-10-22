# P6 iPhone iOS 根卷准备

用户已明确最终目标为 **iPhone iOS**。配置固定在 `config/p6/target-os.json`；iPhone 产品、build、IPSW URL 均保持未选择。未下载大型 IPSW，没有将 iPadOS 镜像改名，没有复用 sheng 的 iPad 身份、根 UUID 或 Trust Cache。

当前内核是 XNU 10063.141.1 / Darwin 23.6，普通 ARM64；编译时使用 iPhoneOS SDK 不代表能直接启动某个 iPhone 系统根卷。

Apple 的[指针认证说明](https://developer.apple.com/documentation/security/preparing-your-app-to-work-with-pointer-authentication)说明 A12 及后续设备的 arm64e/PAC 环境。MSM8998 与当前普通 ARM64 移植不能直接假定兼容相应 iPhone 内核扩展、dyld cache 和用户态 ABI。另一方面，旧 A11/iPhone 系统也不能仅因 ARM64 架构相同就认定与 Darwin 23.6 内核匹配。

Apple [iOS 16.7.16 公告](https://support.apple.com/en-us/127113)列出 iPhone 8/8 Plus/X，发布时间 2026-05-11；此信息只用于说明旧 iPhone 支线仍有单独构建，**不构成已选目标、可下载/仍签名结论或兼容性证明**。

## 当前输入门禁

| 输入/能力 | 状态 | 可验证的放行条件 |
|---|---|---|
| iPhone BuildManifest | 未选择 | 官方来源、ProductType、版本/build、文件身份可追溯 |
| kernel/root ABI | 未验证 | 检查实际 Mach-O slices、指令集、kext/系统 ABI，与当前内核匹配 |
| UFS Block I/O 到 XNU | 未验证 | XNU 枚举真实设备、容量、只读路径；UEFI Block I/O 存在不等于 XNU 存储驱动可用 |
| APFS/根卷/Root UUID | 未验证 | 容器结构与目标根卷、根 UUID、容量和挂载路径真实核对 |
| Trust Cache | 未提供 | 来源可追溯、格式及构建匹配，不填零哈希或 sheng 哈希 |
| launchd/dyld | 未执行 | MMU、VM、Platform Expert、IOKit、根设备与根卷先通过 |
| userdata 镜像 | 未制作 | 容量和布局以实际设备、目标根卷确定；当前不写分区 |

最小下一步仍是早期内核初始化和可靠日志。现在制作完整 iOS 磁盘镜像无法证明可启动，也不能解除 MMU/存储阻塞。没有任何持久化刷写就绪结论；原分区恢复备份继续保留，不执行 flash、erase、GPT 修改或绕过镜像真实性校验。
