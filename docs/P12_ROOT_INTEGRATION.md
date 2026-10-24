# P12 根卷离线集成

本轮仅在项目内开发和构建，没有查询、启动或写入手机。P11 已测试版本不变。iPhone iOS 仍为最终目标；用户授权的 iPad7,1 / 17.7.11 / 21H461 仅用于离线参考，禁止作为安装候选。

## 根设备缺口

P11 已有六个只读 LUN、READ(10) 成功响应和 BSD 入口实机证据，但其设备版源码没有 RootLayout、NativeUfsBlock 或 VerifiedRoot。没有证据证明已发布目标 IOMedia。`Failed to mount root device @IOKitBSDInit.cpp:921` 位于根设备等待失败分支，不能称为 APFS 已挂载后失败。

P3 的 Recovery 记录把 `/dev/block/sda14` 挂为 ext4；各阶段未部署 iOS 根卷。没有当前 userdata 首块转储，所以当前手机的 `manifestStatus` 是**未观测/null**，不能填写 0。缺少已部署根卷输入、窗口发布链和有效根卷匹配是当前明确缺口，具体最后一个运行时 IORegistry 缺口仍待验证。

`out/p12/geometry.json` 从 P3 原始 GPT 备份核验主表头 CRC、分区数组 CRC 和备份文件哈希：

| 项目 | 实际值 |
| --- | ---: |
| UFS LUN0 逻辑块 | 4096 bytes |
| userdata 首 LBA | 2441472 |
| userdata 末 LBA（包含） | 14145279 |
| userdata 容量 | 47,938,797,568 bytes |

这些数值属于 cheeseburger GPT；参考 IPSW 的 Apple 产品身份只用于选择固件，不替代设备身份。

## 已实现的代码链

`drivers/p12/NativeUfs/` 保留 P11 MSM8998 只读传输、READ(10)、锁及超时隔离，复用固定官方 RootLayout、RootWindowRead、RootPublication 和 NativeUfsBlock 逻辑。没有复制其他 SoC 的 MMIO 后端。

- 实际 GPT → userdata SHNGROOT → OS/SystemOS 窗口，拒绝缺失或损坏输入。
- 原始 IOStorageFamily ABI 的真实 IOBlockStorageDevice 子类；没有重新定义 Apple 存储类。
- 512-byte 窗口访问转换至 4 KiB UFS 读，窗口边界检查与串行命令执行保持有效；不实现写入。
- 每个窗口使用真实 RSA3072 所有者签名策略、SHA256 块表和原始 VerifiedRoot C 验证器。策略进入内核后复制到只读内存；失败不发布窗口、不向调用者拷贝未验证数据。
- 策略整窗哈希必须等于 SHNGROOT 哈希。解析 `/chosen/root-matching`，要求其 UUID 等于签名 OS 策略。
- SystemOS 先发布，等待具有真实 UUID、且祖先属于该窗口的 IOMedia，再发布 OS。不会创建空内容占位 IOMedia。

这是完整的源码及链接路径，不是目标机 IOMedia 已经成功发布的证明。官方 Apple APFS/HFS、IOStorageFamily 及其依赖通过官方 NativePrelink 纳入，运行时匹配与 APFS 初始化尚未测试。

## 实际根卷镜像

通过 `scripts/p12-unixnu.py build userdata` 调用固定官方 `unixnu.cli` 和 userdata 构建实现。适配层仅提供可续传、严格 HTTP Range 的直连读取、参考用途配置和报告；没有另写 SHNGROOT 打包器。先完成 dry-run，再实际生成：

`out/p12/reference/cheeseburger-p12-reference/userdata/userdata-root.img`

大小 **9,382,658,048 bytes**，SHA256：

`fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093`

| 窗口 | 镜像内偏移 bytes | 长度 bytes | 实际 APFS 卷 UUID |
| --- | ---: | ---: | --- |
| OS | 1048576 | 6077580800 | 04769D26-2CA0-4825-B443-E2E5F88300F2 |
| SystemOS | 6079643648 | 3303014400 | 8846FE78-2910-4297-AF97-D59BB6F8FC16 |

OS 保留原始磁盘 GPT/APFS 布局；SystemOS 保留原始 APFS 容器。未重写 UUID。镜像内偏移相对 userdata 镜像起点，不能作为整盘写入位置使用。

OS 原始 DMG SHA256 `3f99b859c9728379110b66657c7b15285bcb1925ad8b5726542e2cdcf8b1028d`；SystemOS `b4908cde41d23e6bc05e6af51a2c9eeb00cf0f863a6a18b1600c96a3fd70414f`。两者 SHA384 均与同一固定 BuildManifest 相符，下载直接连接 Apple CDN，不使用代理。完整来源在 `out/p12/reference/components.json`。

## 真实验证及边界

- 实际 RootLayout 解析 P3 GPT + 新镜像：`manifestStatus=0`、两个窗口。此结果仅属于新文件，不属于手机现有 userdata。
- 原始 VerifiedRoot C 验证器验证所有 OS 1,483,785 块、SystemOS 806,400 块；所有者 RSA 签名验证通过。
- 真实文件后端联合运行 GPT/SHNGROOT/VerifiedRoot/RootWindowRead：非对齐读取、跨块、末尾与越界检查；篡改在输出拷贝前拒绝。官方根路径 ASan/UBSan 测试通过。
- 两个参考卷只读挂载，SystemVersion 均为 17.7.11 / 21H461；launchd 普通 ARM64、宿主 `codesign --verify --strict` 返回 0。
- 所有者签名、BuildManifest 哈希及宿主代码签名检查**不等于**完整 Apple secure boot、APFS 根封印或设备个性化验证。没有伪造 Apple 验证成功。
- 独立 ADT 编解码与锁定 DarwinBoot 解析器宿主测试通过，根匹配 UUID 与实际签名策略一致。
- XNU 增量链接无未定义符号。官方预链接 31,005 个绑定核验通过，原始安全代码哈希保持一致。固定 XNU 为 10063.141.1，组件供体为 10063.142.1.703.8；不是相同补丁版本，不能声称全 ABI 已验证。

所有实际命令、失败尝试和退出码在 `out/p12/logs/`。最终二进制、策略嵌入检查和哈希见 `out/p12/manifest.json`、`out/p12/root-integration.json`。内核、payload、UEFI 编译与镜像回读检查均是离线结果；本轮没有新增手机运行证据。
