# P10 当前接续入口

## 最新现场与首错

最新镜像已真实测试：
`out/p10/uefi/boot-p10-xnu.img`，
SHA256 `5923c72ce73bf8ce3b0eb6c66c9870fbb23baa3b8e339bf1562ec2e4a2b8480e`。
内核 `out/p10/kernel/kernel.macho`，
SHA256 `684e673c783556a629bc5f55f05075770ef4538653331d1974c948e8d42a9ce5`。

run `20261022T171445Z-5f7842cb` 已发送，控制器最终 `NO_TRANSPORT_MANUAL_RECOVERY`。用户报告：
`AMFI: No PMGR? @ConfigurationSettings.cpp:235`，
Caller `0xfffffff004e9c420`，Last bootstrap `bsd_init`。
同一消息重复的文字仅是一次 fatal。证据为用户 OCR，不是电子日志。

设备失联，不得重发相同候选。保留屏幕证据后需人工恢复 Android/Recovery，再考虑新的合格候选。没有新修复镜像，不为了恢复状态而循环启动。

## 已经完成

原始普通 ARM64 安全组件经官方 NativePrelink 真正预链接；实机已确认 AMFI 接口 3、Trust Cache Runtime、两份外部模块加载完成，并进入 BSD。P9 Image4 缺失、ADT 算法和 AMFI 策略属性错误已越过。

当前 prelink-v4 额外包含同源原始 com.apple.kec.pthread，代码、依赖和真实注册绑定检查通过。没有 pthread 直接运行成功证据，不能用新的异步 PMGR panic 替代该证明。

当前安全组件 donor 是固定普通 ARM64 17.7.11 / 21H461，设备保持 OnePlus 5，未来目标是 iPhone iOS。不要混用 XR ARM64e、不同 Trust Cache 或 sheng 硬件配置。

## 下一处必须解决的实际问题

1. 为 MSM8998 设计并实现真实的平台 PMGR 服务与生命周期适配。实际 AMFI 要求可发现、正确类型的 ApplePMGRNub，并通过 provider 槽 320/321 完成读/OR/写调用。需要明确本平台支持的语义、实现和拒绝行为；不能将原始 donor 的常数/空写当作 Snapdragon 硬件能力。
2. 固定上游 ShengPlatformPower 是非功能性后端的接口测试工具，不能直接当作修复；不得假装 VMM、删除 AMFI 分支、伪造注册结构或试探性写寄存器。
3. 单独完成 P8 NativeUfs 对原始 IOStorageFamily 的 ABI/绑定，再恢复真实驱动链接；避免重复 C++ 类。实机目前明确 NativeUfs 类不可用。
4. 其后继续 IOMedia、APFS 根卷、BSD 后续及 launchd；尚无完整用户空间镜像或安装资格。

实际 PMGR 合约审计已运行通过：
`scripts/p10-audit-pmgr.py`；
`out/p10/fatal-analysis/pmgr-contract.json`。
这只是原始二进制/绑定静态证据，不是硬件测试。

## 复现与保留

官方预链接入口 `scripts/p10-official-prelink.py`，明确使用新的 P10_PRELINK_VARIANT 保留每个候选。当前 `prelink-v4` 是已测试版本；重复生成不可覆盖已冻结里程碑。Python 3.13 用于官方驱动的 tomllib。

资格/注册检查、ADT、载荷、UEFI 脚本仍在 scripts/p10-*；config/p10/board.toml 已同步 pthread root。内核编译保持 -j2，大型编译前停止 Linux VM，复用唯一 XNU 工作树。当前没有为 PMGR 错误重新编译 XNU。

冻结版本和原始日志均保留于 out/p10/；P1–P9 未删除。仅维护本阶段三份文档，详细历史由日志、里程碑和 user-report-01 至 04 保存。没有分区写入或持久化安装。
