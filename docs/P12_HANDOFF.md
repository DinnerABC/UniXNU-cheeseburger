# P12 交接

项目：`/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger/`。

P12 离线根卷技术集成完成：真实参考系统镜像、SHNGROOT、所有者 VerifiedRoot、NativeUfsBlock、原始 IOStorageFamily/APFS/HFS、ADT、最终内核、payload、UEFI 和 boot image 均已实际生成。没有启动、刷写、擦除或修改手机；不能声称根卷在 XNU 实机挂载或 launchd 已运行。

## 直接使用的成果

| 文件（项目相对路径） | 实际大小 bytes | SHA256 |
| --- | ---: | --- |
| out/p12/reference/cheeseburger-p12-reference/userdata/userdata-root.img | 9382658048 | fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093 |
| out/p12/kernel/kernel.macho | 124907520 | d398065b4c658d1f614714aff034548fea4c7d67317a3b21378a9b621a3d7133 |
| out/p12/adt/kernel.macho.adt | 14948 | 733ad0dc2faa3dd23f597fd44cc052dcf602f1e5916a47dce8a072ea994b9843 |
| out/p12/payload/payload.bundle | 124993536 | 21af3f6eb418c3103ab4eed473cf43e148988568e5172fd69c64b3a3df049194 |
| out/p12/uefi/MSM8998PKG_UEFI.fd | 6291456 | a53667e7053260d629a9e84477eb128a18e77e82bce5920229e07859265a5cea |
| out/p12/uefi/boot-p12-reference.img | 131112960 | 26d57d0db06d0d2830a9757e7ec55f382c1b89349045965f5b4b2480005c5f1f |

`out/p12/manifest.json` 是完整交付清单；`root-integration.json` 含嵌入策略逐字节比较、根匹配、容量、P11 冻结产物哈希和关键本地备份复核结果。两份策略及真正的 RSA 验证归档分别在 `policies/`、`verifier/`。所有者私钥在被 Git 忽略的 `out/p12/policies/private/`，不能公开上传。

`boot-cheeseburger.img` 是上游构建顺带生成、仅含固件的中间镜像；完整 P12 RAM payload 封装是 `boot-p12-reference.img`。两者不能混淆。完整封装超过原 boot 分区容量，禁止写 boot，也没有证明其可通过实机临时下载。

## 固定基线与证据

- UniXNU：`b6ae99c17194e6a18fd819d3148e69fe405e91fc`。
- NativePrelink：`2b6ffb50117eee820b98aca9bd51b5c7dfb2970e`。
- NativeUfs：`af785c1777161a62931425c2faaa536ce0bebd5b`。
- VerifiedRoot：`2854c61f6c4dfda870ac3d93e1f03d9181edb041`。
- 保留唯一 `.work/p4-unixnu/.cache/xnu-work` 与原对象缓存；本轮 `-j2` 增量链接。XNU 编译时 Linux VM 停止，UEFI 单独使用原固定 AArch64 Linux/GCC 环境。
- 安全组件仍取自普通 ARM64 的固定 21H461 kernelcache；没有引入 XR ARM64e。`qualification/result.json` 说明源 XNU 10063.141.1 与供体 10063.142.1.703.8 的版本差异及已核查的接口范围。
- 本轮 P11 manifest 所列冻结产物复核未变，P1–P10 保留。PMGR 仍是已说明限制的兼容契约，不是新实现 Qualcomm 硬件电源控制。

全部命令、工具调用、失败原因和真实退出码保存于 `out/p12/logs/*.json`、`*.log`。成功门禁包括：

1. `build-userdata-01`、`build-policies-01`：真正构建镜像及验证全部块。
2. `root-path-host-02`、`backed-layout-reference-01`、`backed-root-os-01`、`backed-root-systemos-01`：官方根路径、真实 GPT/镜像与篡改拒绝测试。
3. `build-verifier-01`、`build-ufs-02`、`archive-policies-01`、`link-native-01`：真实 ARM64 对象、静态归档及内核链接。
4. `prelink-01`、`audit-registration-01`、`qualify-prelink-01`、`integration-01`：官方预链接及真实最终绑定/安全注册路径检查。
5. `check-adt-01`、`loader-01`、`build-payload-01`：实际 DarwinBoot 宿主解析与 payload 哈希门禁。
6. `system-consistency-02`：只读 APFS 挂载、实际版本/UUID、launchd ARM64 和宿主代码签名检查；已卸载参考虚拟磁盘。
7. `root-integration-01`：9.38 GB 镜像重新全量 SHA256，策略真实嵌入、P11 与备份完整性检查。
8. `build-uefi-01`、`export-uefi-01`、`verify-uefi-01`、`pack-bootimg-02`：完整 UEFI 编译、PE/FV 核验及完整 payload 镜像回读。

保留失败日志：最初缺少子模块/警告兼容问题、首次 hfs bundle ID 错误、宿主挂载权限问题等均未伪造成功。`pack-bootimg-01` 在固件导出尚未就绪时失败；等待依赖完成后 `pack-bootimg-02` 真实通过。`fetch-root` 中断以 null 退出状态留存，后续任务使用有效缓存完成，没有重复下载整套固件。

## 恢复开发的方法

先确认 NAS 挂载、已有进程及日志完成状态，再读取上述 manifest。不要因为会话中断重复下载或重编整个 XNU。后台任务的实际完成依据是同名日志 JSON 的 exit_code，而不是 launcher 返回 PID。

代码位于 `drivers/p12/NativeUfs/`，配置 `config/p12/`，适配脚本 `scripts/p12-*.py` / `.sh`。构建顺序是 verifier/policies + UFS → 增量 native link → 官方 prelink → 注册/ABI/集成门禁 → ADT/loader → payload → UEFI 私有覆盖层 → 编译/export/verify → pack → root-integration/finalize。精确参数从每项日志 JSON 的 command 读取。

`p12-prepare-driver.py` 是从固定 P11/官方实现重建适配层的生成器，随后必须运行 `p12-tighten-root-match.py`；直接复用当前已编译源码时不要重新生成。`p12-inspect-baseline.py` 用于最初建立配置，会重建 board.toml，不能在现有最终配置上盲目重跑。日志工具拒绝覆盖已有标签；新尝试使用新标签。

## 下一个真实阻塞

1. **部署输入与目标选择**：手机未部署任何 SHNGROOT。本轮 iPad 参考禁止部署，无法通过再次运行同一内核让原 Android userdata 变成 iOS 根卷。正式 iPhone 固件/安全组件/用户空间兼容方案需独立研究。
2. **目标 IOMedia/APFS 运行时**：目前只有宿主验证与完整链接，不能证明原始 APFS 及其依赖在 MSM8998 上能实际启动。下一轮需设计用户单独授权的非持久化验证或合格正式根卷路径，而不是重复底层 UFS 枚举。
3. **更大 payload**：137,302,016-byte 内核虚拟范围小于保留的 512 MiB loader 窗口；约 119 MiB payload 小于新的 256 MiB 上限。实际地址仍由 FDT/内存检查决定，手机 Fastboot 上限和运行时装载均未测试。
4. **安全与系统闭环**：所有者签名不替代 Apple 根封印/个性化；只读根设备也不提供完整可写 Data 卷、Preboot/系统布局和全部 launchd 服务。没有根挂载、用户空间执行或恢复演练成功证据。

下一阶段应围绕正式系统输入与可验证根设备路径推进。保持禁止刷写/擦除/GPT 修改，不能把参考文件构建成功当成安装资格。
